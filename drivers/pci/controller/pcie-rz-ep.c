// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe endpoint driver for Renesas RZ Series SoCs
 *  Copyright (c) 2025 Renesas Electronics Europe GmbH
 */

#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include <linux/arm-smccc.h>
#include <uapi/linux/psci.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/container_of.h>

#include "../pci.h"
#include "pcie-rzg3s.h"

#define RZ_EPC_MAX_FUNCTIONS		2

/* Structure representing the PCIe interface */
struct rz_pcie {
	void __iomem		*base_cfg;
	void __iomem		*base;
	struct device		*dev;
};

struct rz_pcie_endpoint {
	struct rz_pcie			pcie;
	phys_addr_t			*ob_mapped_addr;
	struct pci_epc_mem_window	*ob_window;
	u8				max_functions;
	unsigned int			bar_to_atu[PCI_STD_NUM_BARS];
	unsigned long			*ib_window_map;
	u32				num_ib_windows;
	u32				num_ob_windows;
	int				channel;
	struct reset_control		*rst;
	struct regmap			*syscon;
	u32				vendor_id;
	u32				device_id;
};

/* Static Function */
static void rz_write_conf_ep(struct rz_pcie *pcie, u32 data, int where)
{
	writel_relaxed(RZG3S_PCI_PERM_CFG_HWINIT_EN,
		       pcie->base + RZG3S_PCI_PERM);
	writel_relaxed(data, pcie->base_cfg + where);
	writel_relaxed(0, pcie->base + RZG3S_PCI_PERM);
}

static u32 rz_read_conf_ep(struct rz_pcie *pcie, int where, u8 fn)
{
	return readl_relaxed(pcie->base_cfg + where);
}

static void rz_pcie_set_outbound_ep(struct rz_pcie *pcie, int win,
				phys_addr_t cpu_addr, u64 pci_addr, size_t size)
{
	u64 mask;

	mask = roundup_pow_of_two(size) - 1;
	cpu_addr = (cpu_addr - 0x30000000) | 0x30000000UL;
	/* PW base */
	writel_relaxed((u32)(cpu_addr >> 32),
		       pcie->base + RZG3S_PCI_PWBASEU(win));
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_PWBASEL(win), 0xFFFFF000,
			       (u32)(cpu_addr & 0xFFFFF000));

	/* PW mask */
	writel_relaxed((u64)(mask >> 32),
		       pcie->base + RZG3S_PCI_PWMASKU(win));
	writel_relaxed((u64)(mask & 0xFFFFF000),
		       pcie->base + RZG3S_PCI_PWMASKL(win));

	/* PW dest */
	writel_relaxed((u32)(pci_addr >> 32),
		       pcie->base + RZG3S_PCI_PDESTU(win));
	writel_relaxed((u32)(pci_addr & 0xFFFFFFFF),
		       pcie->base + RZG3S_PCI_PDESTL(win));

	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_PWBASEL(win),
			       RZG3S_PCI_PWBASEL_ENA, RZG3S_PCI_PWBASEL_ENA);
}

static void rz_pcie_set_inbound_ep(struct rz_pcie *pcie, u64 cpu_addr,
			   u64 pci_addr, u64 flags, int idx, u8 fn)
{

	/* AW base */
	writel_relaxed(lower_32_bits(pci_addr),
		       pcie->base + RZG3S_PCI_AWBASEL(idx));
	writel_relaxed(upper_32_bits(pci_addr),
		       pcie->base + RZG3S_PCI_AWBASEU(idx));

	/* AW dest */
	writel_relaxed(lower_32_bits(cpu_addr),
		       pcie->base + RZG3S_PCI_ADESTL(idx));
	writel_relaxed(upper_32_bits(cpu_addr),
		       pcie->base + RZG3S_PCI_ADESTU(idx));

	/* AW base */
	writel_relaxed(lower_32_bits(flags),
		       pcie->base + RZG3S_PCI_AWMASKL(idx));
	writel_relaxed(upper_32_bits(flags),
		       pcie->base + RZG3S_PCI_AWMASKU(idx));
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_AWBASEL(idx),
			       RZG3S_PCI_AWBASEL_WIN_ENA,
			       RZG3S_PCI_AWBASEL_WIN_ENA);
}


/*-----------------------------------------------------
  PCIe Setting Function
-------------------------------------------------------*/
static void rz_pcie_setting_config_ep(struct rz_pcie *pcie, u8 func_no)
{
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_PCMSET1,
			       RZG3S_PCI_PCMSET1_MODE_PORT, 0);

	/* Clear BAR mask register */
	for (int i = 0; i < PCI_STD_NUM_BARS; i++)
		writel_relaxed(0, pcie->base_cfg + RZ_PCI_EP_CFG_BARMSK(i));

	/* Setting following hardware manual */
	writel_relaxed(0, pcie->base_cfg + RZG3S_PCI_CFG_BSIZE00_01);
	writel_relaxed(0, pcie->base_cfg + RZ_PCI_EP_CFG_BSIZE02_03);
	writel_relaxed(0, pcie->base_cfg + RZ_PCI_EP_CFG_BSIZE04_05);
	writel_relaxed(0, pcie->base_cfg + RZ_PCI_EP_CFG_BSIZE06);

	writel_relaxed(PCI_EXP_DEVCTL_PAYLOAD_256B |
		       PCI_EXP_DEVCTL_READRQ_512B |
		       PCI_EXP_DEVCTL_RELAX_EN,
		       pcie->base_cfg + RZG3S_PCI_CFG_DEVCS);
}

static int PCIE_EP_IRQ_Initialize(struct rz_pcie *pcie)
{
	/* Clear Event Interrupt Status 0 */
	writel_relaxed(RZG3S_PCI_PEIS0_DL_UPDOWN |
		       RZG3S_PCI_PEIS0_RX_DLLP_PM_ENTER, pcie->base + RZG3S_PCI_PEIS0);

	/* Disable all interrupts */
	writel_relaxed(0, pcie->base + RZG3S_PCI_PEIE0);

	/* Clear all parity and ecc error interrupts */
	writel_relaxed(~0U, pcie->base + RZG3S_PCI_PEIS1);

	/* Disable all parity and ecc error interrupts */
	writel_relaxed(0, pcie->base + RZG3S_PCI_PEIE1);

	/* Clear AXI Master Error Interrupt Status */
	writel_relaxed(~0U, pcie->base + RZG3S_PCI_AMEIS);

	/* Set AXI Master Error Interrupt Enable */
	writel_relaxed(~0U, pcie->base + RZG3S_PCI_AMEIE);

	/* Clear AXI Slave Error Interrupt Status */
	writel_relaxed(~0U, pcie->base + RZG3S_PCI_ASEIS1);

	/* Set AXI Slave Error Interrupt Enable */
	writel_relaxed(~0U, pcie->base + RZG3S_PCI_ASEIE1);

	/* Clear Message Receive Interrupt Status */
	writel_relaxed(~0U, pcie->base + RZG3S_PCI_MSGRCVIS);

	/* Set Message Receive Interrupt Enable */
	writel_relaxed(RZG3S_PCI_MSGRCVIE_PME_TURN_OFF_RCV |
		       RZG3S_PCI_MSGRCVIE_PM_STATE_NAK_RCV |
		       RZG3S_PCI_MSGRCVIE_MSG_RCV,
		       pcie->base + RZG3S_PCI_MSGRCVIE);

	return 0;
}

static int rz_pcie_hw_init_ep(struct rz_pcie *pcie, int channel)
{
	struct rz_pcie_endpoint *ep = container_of(pcie, struct rz_pcie_endpoint, pcie);
	u32 mask = RZG3S_PCI_RESET_RST_LOAD_B | RZG3S_PCI_RESET_RST_CFG_B;

	/* SYS setting mode port */
	regmap_write(ep->syscon, SYS_PCIE_MODE_CH(channel), MODE_PORT_SYS_EP);

	/* Set to the PCIe reset state */
	writel_relaxed(0, pcie->base + RZG3S_PCI_RESET);

	/* De-assert LOAD_B, CFG_B */
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_RESET, mask, mask);

	regmap_write(ep->syscon, SYS_PCIE_LANE_MODE, LINK_MASTER_4_LANE_MODE);

	/* Enable access control to the CFGU */
	writel_relaxed(RZG3S_PCI_PERM_CFG_HWINIT_EN,
		       pcie->base + RZG3S_PCI_PERM);

	/* Update vendor ID and device ID */
	writew_relaxed(ep->vendor_id, pcie->base_cfg + PCI_VENDOR_ID);
	writew_relaxed(ep->device_id, pcie->base_cfg + PCI_DEVICE_ID);

	/* set BAR */
	rz_pcie_setting_config_ep(pcie, 0);

	/* Disable access control to the CFGU */
	writel_relaxed(0, pcie->base_cfg + RZG3S_PCI_PERM);

	/* SYS setting allow_enter_l1  */
	regmap_write(ep->syscon, SYS_PCIE_MISC_CH(channel), ALLOW_ENTER_L1);

	/* Set Interrupt settings */
	PCIE_EP_IRQ_Initialize(pcie);

	/* De-assert PS_B, GP_B, RST_B  */
	mask = RZG3S_PCI_RESET_RST_PS_B | RZG3S_PCI_RESET_RST_GP_B |
	       RZG3S_PCI_RESET_RST_B;
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_RESET, mask, mask);

	/* Wait for 500us or more */
	fsleep(500);

	/* De-assert OUT_B and RSM_B */
	mask = RZG3S_PCI_RESET_RST_OUT_B | RZG3S_PCI_RESET_RST_RSM_B;
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_RESET, mask, mask);

	return 0;
}

static int rz_pcie_ep_get_window(struct rz_pcie_endpoint *ep,
				   phys_addr_t addr)
{
	int i;

	for (i = 0; i < ep->num_ob_windows; i++)
		if (ep->ob_window[i].phys_base == addr)
			return i;

	return -EINVAL;
}

static int rz_pcie_parse_outbound_ranges(struct rz_pcie_endpoint *ep,
					   struct platform_device *pdev)
{
	struct rz_pcie *pcie = &ep->pcie;
	char outbound_name[10];
	struct resource *res;
	unsigned int i = 0;

	ep->num_ob_windows = 0;
	for (i = 0; i < RZ_PCI_EP_MAX_RESOURCES; i++) {
		sprintf(outbound_name, "memory%u", i);
		res = platform_get_resource_byname(pdev,
						   IORESOURCE_MEM,
						   outbound_name);
		if (!res) {
			dev_err(pcie->dev, "missing outbound window %u\n", i);
			return -EINVAL;
		}
		if (!devm_request_mem_region(&pdev->dev, res->start,
					     resource_size(res),
					     outbound_name)) {
			dev_err(pcie->dev, "Cannot request memory region %s.\n",
				outbound_name);
			return -EIO;
		}

		ep->ob_window[i].phys_base = res->start;
		ep->ob_window[i].size = resource_size(res);
		/* controller doesn't support multiple allocation
		 * from same window, so set page_size to window size
		 */
		ep->ob_window[i].page_size = resource_size(res);
	}
	ep->num_ob_windows = i;

	return 0;
}

static int rz_pcie_ep_get_pdata(struct rz_pcie_endpoint *ep,
				  struct platform_device *pdev)
{
	struct rz_pcie *pcie = &ep->pcie;
	struct pci_epc_mem_window *window;
	struct device *dev = pcie->dev;
	struct resource res;
	int err;

	err = of_address_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;

	pcie->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);
	pcie->base_cfg = pcie->base + RZG3S_PCI_CFG_BASE;

	ep->ob_window = devm_kcalloc(dev, RZ_PCI_EP_MAX_RESOURCES,
				     sizeof(*window), GFP_KERNEL);
	if (!ep->ob_window)
		return -ENOMEM;

	rz_pcie_parse_outbound_ranges(ep, pdev);

	err = of_property_read_u8(dev->of_node, "max-functions",
				  &ep->max_functions);
	if (err < 0 || ep->max_functions > RZ_EPC_MAX_FUNCTIONS)
		ep->max_functions = RZ_EPC_MAX_FUNCTIONS;

	return 0;
}

static int rz_pcie_ep_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				   struct pci_epf_header *hdr)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rz_pcie *pcie = &ep->pcie;
	u32 val;

	if (!func_no)
		val = hdr->vendorid;
	else
		val = rz_read_conf_ep(pcie, RZG3S_PCI_CFG_VID, func_no);

	val |= hdr->deviceid << 16;
	rz_write_conf_ep(pcie, val, RZG3S_PCI_CFG_VID);

	val = hdr->revid;
	val |= hdr->progif_code << 8;
	val |= hdr->subclass_code << 16;
	val |= hdr->baseclass_code << 24;
	rz_write_conf_ep(pcie, val, PCI_CLASS_REVISION);

	if (!func_no)
		val = hdr->subsys_vendor_id;
	else
		val = rz_read_conf_ep(pcie, PCI_SUBSYSTEM_VENDOR_ID, func_no);
	val |= hdr->subsys_id << 16;
	rz_write_conf_ep(pcie, val, PCI_SUBSYSTEM_VENDOR_ID);

	if (hdr->interrupt_pin > PCI_INTERRUPT_INTA)
		return -EINVAL;

	val = rz_read_conf_ep(pcie, RZ_PCI_EP_CFG_INT, func_no);
	val |= (hdr->interrupt_pin << 8);
	rz_write_conf_ep(pcie, val, RZ_PCI_EP_CFG_INT);

	rz_write_conf_ep(pcie, PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY,
			 PCI_COMMAND);

	return 0;
}

static int rz_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				struct pci_epf_bar *epf_bar)
{
	int flags = epf_bar->flags;
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	u64 size = 1ULL << fls64(epf_bar->size - 1);
	dma_addr_t cpu_addr = epf_bar->phys_addr;
	enum pci_barno bar = epf_bar->barno;
	struct rz_pcie *pcie = &ep->pcie;
	u32 mask;
	int idx;

	idx = find_first_zero_bit(ep->ib_window_map, ep->num_ib_windows);
	if (idx >= ep->num_ib_windows) {
		dev_err(pcie->dev, "no free inbound window\n");
		return -EINVAL;
	}

	ep->bar_to_atu[bar] = idx;
	/* use 64-bit BARs */
	set_bit(idx, ep->ib_window_map);
	set_bit(idx + 1, ep->ib_window_map);

	if (cpu_addr > 0) {
		unsigned long nr_zeros = __ffs64(cpu_addr);
		u64 alignment = 1ULL << nr_zeros;

		size = min(size, alignment);
	}

	size = min(size, 1ULL << 32);
	mask = roundup_pow_of_two(size) - 1;

	/* setup BAR */
	rz_write_conf_ep(pcie, mask, RZ_PCI_EP_CFG_BARMSK(idx));

	mask &= ~0xf;
	rz_pcie_set_inbound_ep(pcie, cpu_addr,
			      0x0, mask | flags, idx, func_no);

	return 0;
}

static void rz_pcie_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				   struct pci_epf_bar *epf_bar)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

	rz_pcie_set_inbound_ep(&ep->pcie, 0x0, 0x0, 0x0, bar, func_no);

	clear_bit(atu_index, ep->ib_window_map);
	clear_bit(atu_index + 1, ep->ib_window_map);
}

static int rz_pcie_ep_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 u8 interrupts)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rz_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rz_read_conf_ep(pcie, RZ_PCI_EP_CFG_MSICAP, func_no);
	flags &= ~RZ_PCI_EP_CFG_MSICAP_MMESCAP;
	flags |= FIELD_PREP(RZ_PCI_EP_CFG_MSICAP_MMESCAP, interrupts);
	rz_write_conf_ep(pcie, flags, RZ_PCI_EP_CFG_MSICAP);

	return 0;
}

static int rz_pcie_ep_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rz_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rz_read_conf_ep(pcie, RZ_PCI_EP_CFG_MSICAP, func_no);

	if (!(flags & RZ_PCI_EP_CFG_MSICAP_MSIE))
		return -EINVAL;

	return FIELD_GET(RZ_PCI_EP_CFG_MSICAP_MMESE, flags);
}

static int rz_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 phys_addr_t addr, u64 pci_addr, size_t size)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rz_pcie *pcie = &ep->pcie;
	struct resource_entry win;
	struct resource res;
	int window;
	u32 status;

	/* check if we have a link. */
	status = readl_relaxed(pcie->base + RZG3S_PCI_PCSTAT2);
	status = FIELD_GET(RZG3S_PCI_PCSTAT2_STATE_RX_DETECT, status);
	if (status) {
		dev_err(pcie->dev, "PCIe have Link up\n");
		/*- Detect Lane 0 or Lane 1 -*/
	} else {
		dev_err(pcie->dev, "PCIe x%d: Link not up\n", status);
		return -EPERM;
	}

	window = rz_pcie_ep_get_window(ep, addr);
	if (window < 0) {
		dev_err(pcie->dev, "failed to get corresponding window\n");
		return -EINVAL;
	}

	memset(&win, 0x0, sizeof(win));
	memset(&res, 0x0, sizeof(res));
	res.start  = addr;
	res.end    = addr + size - 1;
	res.flags  = IORESOURCE_MEM;
	win.res    = &res;
	win.offset = res.start - pci_addr;

	rz_pcie_set_outbound_ep(pcie, window, addr, pci_addr, size);

	ep->ob_mapped_addr[window] = addr;

	return 0;
}

static void rz_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				    phys_addr_t addr)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rz_pcie *pcie = &ep->pcie;
	struct resource_entry win;
	struct resource res;
	int idx;

	for (idx = 0; idx < ep->num_ob_windows; idx++)
		if (ep->ob_mapped_addr[idx] == addr)
			break;

	if (idx >= ep->num_ob_windows)
		return;

	memset(&win, 0x0, sizeof(win));
	memset(&res, 0x0, sizeof(res));
	win.res = &res;

	rz_pcie_set_outbound_ep(pcie, idx, 0x0, 0x0, 0x0);

	ep->ob_mapped_addr[idx] = 0;
}

static int rz_pcie_ep_assert_intx(struct rz_pcie_endpoint *ep,
				    u8 func_no)
{
	struct rz_pcie *pcie = &ep->pcie;
	u32 val;

	/* Check MSI enable bit */
	val = rz_read_conf_ep(pcie, RZ_PCI_EP_CFG_MSICAP, func_no);
	if ((val & RZ_PCI_EP_CFG_MSICAP_MSIE)) {
		dev_err(pcie->dev, "MSI is enabled, cannot assert INTx\n");
		return -EINVAL;
	}

	/* SYS assert INTX interrupt */
	regmap_write(ep->syscon, SYS_PCIE_INTX_CH(ep->channel), 0x1);

	return 0;
}

static int rz_pcie_ep_assert_msi(struct rz_pcie_endpoint *ep,
				   u8 func_no, u8 interrupt_num)
{
	struct rz_pcie *pcie = &ep->pcie;
	u16 msi_count;
	u32 val;

	/* Check MSI enable bit */
	val = rz_read_conf_ep(pcie, RZ_PCI_EP_CFG_MSICAP, func_no);
	if (!(val & RZ_PCI_EP_CFG_MSICAP_MSIE)) {
		dev_err(pcie->dev, "MSI is not enabled, cannot assert MSI\n");
		return -EINVAL;
	}

	/* Get MSI numbers from MME */
	msi_count = FIELD_GET(RZ_PCI_EP_CFG_MSICAP_MMESE, val);
	msi_count = 1 << msi_count;

	if (!interrupt_num || interrupt_num > msi_count)
		return -EINVAL;

	/* SYS assert MSI interrupt */
	regmap_write(ep->syscon, SYS_PCIE_MSI1_CH(ep->channel), 0x0);
	regmap_write(ep->syscon, SYS_PCIE_MSI1_CH(ep->channel), 0x1);

	return 0;
}

static int rz_pcie_ep_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				unsigned int type, u16 interrupt_num)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);

	switch (type) {
	case PCI_IRQ_INTX:
		return rz_pcie_ep_assert_intx(ep, func_no);

	case PCI_IRQ_MSI:
		return rz_pcie_ep_assert_msi(ep, func_no, interrupt_num);

	default:
		return -EINVAL;
	}

}

static const struct pci_epc_features rz_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	/* use 64-bit BARs so mark BAR[1,3,5] as reserved */
	.bar[BAR_0] = { .type = BAR_FIXED, .fixed_size = SZ_1M,
			.only_64bit = true, },
	.bar[BAR_1] = { .type = BAR_RESERVED, },
	.bar[BAR_2] = { .type = BAR_FIXED, .fixed_size = SZ_4K,
			.only_64bit = true, },
	.bar[BAR_3] = { .type = BAR_RESERVED, },
	.bar[BAR_4] = { .type = BAR_FIXED, .fixed_size = SZ_256,
			.only_64bit = true, },
	.bar[BAR_5] = { .type = BAR_RESERVED, },
};

static const struct pci_epc_features *rz_pcie_ep_get_features(struct pci_epc *epc,
								u8 func_no, u8 vfunc_no)
{
	return &rz_pcie_epc_features;
}

static const struct pci_epc_ops rz_pcie_epc_ops = {
	.write_header	= rz_pcie_ep_write_header,
	.set_bar	= rz_pcie_ep_set_bar,
	.clear_bar	= rz_pcie_ep_clear_bar,
	.set_msi	= rz_pcie_ep_set_msi,
	.get_msi	= rz_pcie_ep_get_msi,
	.map_addr	= rz_pcie_ep_map_addr,
	.unmap_addr	= rz_pcie_ep_unmap_addr,
	.raise_irq	= rz_pcie_ep_raise_irq,
	.get_features	= rz_pcie_ep_get_features,
};

static const struct of_device_id rz_pcie_ep_of_match[] = {
	{ .compatible = "renesas,rzv2h-pcie-ep", },
	{}, /* sentinel */
};

static int rz_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rz_pcie_endpoint *ep;
	struct device_node *np = dev->of_node;
	struct rz_pcie *pcie;
	struct pci_epc *epc;
	int err, channel;

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	pcie = &ep->pcie;
	pcie->dev = dev;

	pm_runtime_enable(dev);
	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err(dev, "pm_runtime_resume_and_get failed\n");
		goto err_pm_disable;
	}

	err = of_property_read_u32(np, "vendor-id", &ep->vendor_id);
	if (err)
		return err;

	err = of_property_read_u32(np, "device-id", &ep->device_id);
	if (err)
		return err;

	err = of_property_read_u32(dev->of_node, "pcie,channel", &channel);
	if (err) {
		dev_err(pcie->dev, "%pOF: No pcie,channel property found\n",
				dev->of_node);
		return -EINVAL;
	}

	ep->channel = channel;
	if (ep->channel >= PCIE_MAX_CHANNEL) {
		dev_err(pcie->dev, "%pOF: Invalid pcie,channel '%u'\n",
				dev->of_node, ep->channel);
		return -EINVAL;
	}

	err = rz_pcie_ep_get_pdata(ep, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		goto err_pm_put;
	}

	ep->syscon = syscon_regmap_lookup_by_phandle(dev->of_node, "renesas,pcie-sys");
	if (IS_ERR(ep->syscon))
		return dev_err_probe(dev, PTR_ERR(ep->syscon),
				"Failed to get pcie syscon");

	ep->num_ib_windows = RZG3S_MAX_WINDOWS;
	ep->ib_window_map =
			devm_kcalloc(dev, BITS_TO_LONGS(ep->num_ib_windows),
				     sizeof(long), GFP_KERNEL);
	if (!ep->ib_window_map) {
		err = -ENOMEM;
		dev_err(dev, "failed to allocate memory for inbound map\n");
		goto err_pm_put;
	}

	ep->ob_mapped_addr = devm_kcalloc(dev, ep->num_ob_windows,
					  sizeof(*ep->ob_mapped_addr),
					  GFP_KERNEL);
	if (!ep->ob_mapped_addr) {
		err = -ENOMEM;
		dev_err(dev, "failed to allocate memory for outbound memory pointers\n");
		goto err_pm_put;
	}

	epc = devm_pci_epc_create(dev, &rz_pcie_epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "failed to create epc device\n");
		err = PTR_ERR(epc);
		goto err_pm_put;
	}

	epc->max_functions = ep->max_functions;
	epc_set_drvdata(epc, ep);

	ep->rst = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(ep->rst)) {
		dev_err(dev, "PCIE cannot get reset\n");
		return PTR_ERR(ep->rst);
	}

	err = reset_control_deassert(ep->rst);
	if (err) {
		dev_err(dev, "PCIE failed to deassert reset %d\n", err);
		return err;
	}

	udelay(200);

	rz_pcie_hw_init_ep(pcie, ep->channel);

	err = pci_epc_multi_mem_init(epc, ep->ob_window, ep->num_ob_windows);

	if (err < 0) {
		dev_err(dev, "failed to initialize the epc memory space\n");
		goto err_pm_put;
	}

	pci_epc_init_notify(epc);

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);

	return err;
}

static struct platform_driver rz_pcie_ep_driver = {
	.driver = {
		.name = "rz-pcie-ep",
		.of_match_table = rz_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rz_pcie_ep_probe,
};
builtin_platform_driver(rz_pcie_ep_driver);

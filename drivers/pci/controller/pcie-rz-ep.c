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
#include <linux/dma/pcie-rzg3s-dma.h>

#include "../pci.h"
#include "pcie-rzg3s.h"

#define RZ_EPC_MAX_FUNCTIONS		2

/* Structure representing the PCIe interface */
struct rz_pcie {
	void __iomem		*base_cfg;
	void __iomem		*base;
	struct device		*dev;
};

struct rz_pcie_endpoint;

/**
 * struct rz_pcie_window_config - BAR to window mapping configuration
 * @start: Starting window index
 * @num: Number of windows available
 *
 * Defines which AXI windows can be used for a specific BAR.
 */
struct rz_pcie_window_config {
	u8 start;
	u8 num;
};

/**
 * struct rz_pcie_func_config - Per-function configuration
 * @bar_win: Window configuration for each BAR
 *
 * Defines the window mapping for all BARs of a single function.
 */

struct rz_pcie_func_config {
	struct rz_pcie_window_config bar_win[PCI_STD_NUM_BARS];
};

struct rz_pcie_soc_data {
	const struct rz_pcie_func_config *func_cfg;
	struct rzg3s_sysc_info sysc_info;
	u8 max_channels;
	int (*init_phy)(struct rz_pcie_endpoint *ep);
};

struct rz_pci_saved_regs {
	u32 cfg_regs[PCI_EP_CFG_REG_NUM];

	struct {
		u32 base_L[RZG3S_MAX_WINDOWS];
		u32 base_U[RZG3S_MAX_WINDOWS];
		u32 mask_L[RZG3S_MAX_WINDOWS];
		u32 mask_U[RZG3S_MAX_WINDOWS];
		u32 dest_L[RZG3S_MAX_WINDOWS];
		u32 dest_U[RZG3S_MAX_WINDOWS];
	} axi_win_regs;

	struct {
		u32 base_L[RZ_PCI_EP_MAX_RESOURCES];
		u32 base_U[RZ_PCI_EP_MAX_RESOURCES];
		u32 mask_L[RZ_PCI_EP_MAX_RESOURCES];
		u32 mask_U[RZ_PCI_EP_MAX_RESOURCES];
		u32 dest_L[RZ_PCI_EP_MAX_RESOURCES];
		u32 dest_U[RZ_PCI_EP_MAX_RESOURCES];
	} pci_win_regs;
};

struct rz_pcie_endpoint {
	struct rz_pcie			pcie;
	struct rz_pci_saved_regs	*saved_regs;
	struct rz_pcie_dma		dmac;
	phys_addr_t			*ob_mapped_addr;
	struct pci_epc_mem_window	*ob_window;
	u8				max_functions;
	unsigned long			*ib_window_map;
	u32				num_ib_windows;
	u32				num_ob_windows;
	int				channel;
	struct reset_control		*rst;
	const struct rz_pcie_soc_data	*data;
	struct rzg3s_sysc		*sysc;
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
				    phys_addr_t cpu_addr, u64 pci_addr,
				    size_t size)
{
	u64 mask;

	/*
	 * According to the RZ/V2H HW Manual (Rev.1.30 section 6.6.4.2.3
	 * (73) PCIe Window Mask m (Lower) Register
	 * Minimum window size is 4KB.
	 */
	size = max(size, SZ_4K);
	mask = roundup_pow_of_two(size) - 1;

	/* Set PCIe window mask */
	writel_relaxed(lower_32_bits(mask), pcie->base + RZG3S_PCI_PWMASKL(win));
	writel_relaxed(upper_32_bits(mask), pcie->base + RZG3S_PCI_PWMASKU(win));

	/* Set PCIe destination */
	writel_relaxed(lower_32_bits(pci_addr), pcie->base + RZG3S_PCI_PDESTL(win));
	writel_relaxed(upper_32_bits(pci_addr), pcie->base + RZG3S_PCI_PDESTU(win));

	/* Set PCIe window base */
	writel_relaxed(lower_32_bits(cpu_addr) | RZG3S_PCI_PWBASEL_ENA,
		       pcie->base + RZG3S_PCI_PWBASEL(win));
	writel_relaxed(upper_32_bits(cpu_addr), pcie->base + RZG3S_PCI_PWBASEU(win));
}

static void rz_pcie_set_inbound_ep(struct rz_pcie *pcie, u64 cpu_addr,
				   u64 pci_addr, u64 mask, u8 idx, u8 fn)
{
	/* Set CPU window base address */
	writel_relaxed(lower_32_bits(cpu_addr),
		       pcie->base + RZG3S_PCI_ADESTL(idx));
	writel_relaxed(upper_32_bits(cpu_addr),
		       pcie->base + RZG3S_PCI_ADESTU(idx));

	/* Set window size */
	writel_relaxed(lower_32_bits(mask),
		       pcie->base + RZG3S_PCI_AWMASKL(idx));
	writel_relaxed(upper_32_bits(mask),
		       pcie->base + RZG3S_PCI_AWMASKU(idx));

	/* Set PCIe window base address and enable the window */
	writel_relaxed(lower_32_bits(pci_addr) | RZG3S_PCI_AWBASEL_WIN_ENA,
		       pcie->base + RZG3S_PCI_AWBASEL(idx));
	writel_relaxed(upper_32_bits(pci_addr),
		       pcie->base + RZG3S_PCI_AWBASEU(idx));
}


/* PCIe Setting Function */
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
	int ret;

	/* Put controller in EP mode */
	ret = rzg3s_sysc_config_func(ep->sysc, RZG3S_SYSC_FUNC_ID_MODE, 0,
				     ep->channel);
	if (ret)
		return ret;

	/* Set to the PCIe reset state */
	writel_relaxed(0, pcie->base + RZG3S_PCI_RESET);

	/* De-assert LOAD_B, CFG_B */
	rzg3s_pcie_update_bits(pcie->base, RZG3S_PCI_RESET, mask, mask);

	/*
	 * Put controller in:
	 * - 4 lanes mode x 1 channel (RZ/V2H)
	 * - 2 lanes mode x 1 channel (RZ/T2H)
	 */
	ret = rzg3s_sysc_config_func(ep->sysc, RZG3S_SYSC_FUNC_LANES_MODE, 1,
				     ep->channel);
	if (ret)
		return ret;

	/* Set the PHY, if any */
	if (ep->data->init_phy) {
		ret = ep->data->init_phy(ep);
		if (ret) {
			dev_err_probe(pcie->dev, ret, "Failed to set the PHY!\n");
			return ret;
		}
	}

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
	ret = rzg3s_sysc_config_func(ep->sysc, RZG3S_SYSC_FUNC_ID_L1_ALLOW, 1,
				     ep->channel);
	if (ret)
		return ret;

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
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	const struct rz_pcie_soc_data *data = ep->data;
	const struct rz_pcie_window_config *win_cfg;
	u64 cpu_end, mask, align_limit, pci_addr = 0;
	u64 size = 1ULL << fls64(epf_bar->size - 1);
	dma_addr_t cpu_addr = epf_bar->phys_addr;
	enum pci_barno bar = epf_bar->barno;
	struct rz_pcie *pcie = &ep->pcie;
	int flags = epf_bar->flags;
	int idx, idx_base, idx_end;
	int i;

	if (flags & PCI_BASE_ADDRESS_SPACE_IO)
		return -EINVAL;

	if (!cpu_addr)
		return -EINVAL;

	/* Do not configure BAR1, 3 and 5 as they are used to form 64-bit BAR */
	if ((flags & PCI_BASE_ADDRESS_MEM_TYPE_64) && (bar & 1))
		return -EINVAL;

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		mask = size - 1;
		rz_write_conf_ep(pcie, lower_32_bits(mask),
				 RZ_PCI_EP_CFG_BARMSK(bar));
		rz_write_conf_ep(pcie, upper_32_bits(mask),
				 RZ_PCI_EP_CFG_BARMSK(bar + 1));
	}

	win_cfg = &data->func_cfg[func_no].bar_win[bar];
	if (!win_cfg->num)
		return 0;

	idx_base = win_cfg->start;
	idx_end  = win_cfg->start + win_cfg->num;

	/*
	 * The "no bit carry" rule requires base addresses to be
	 * aligned to the window size. Find the maximum window size
	 * that both addresses can support based on their natural
	 * alignment (lowest set bit).
	 */
	align_limit = 1ULL << __ffs64(cpu_addr);
	align_limit = min(align_limit, size);

	/*
	 * Minimum window size is 4KB.
	 * See RZ/V2H HW manual (Rev.1.30, section 6.6.4.2.3.67)
	 */
	align_limit = max(align_limit, SZ_4K);
	mask = roundup_pow_of_two(align_limit) - 1;

	cpu_end = cpu_addr + size - 1;
	idx = idx_base;
	while (cpu_addr < cpu_end) {
		if (idx >= idx_end) {
			dev_err(pcie->dev,
				"BAR%d: exceeded inbound window range [%d~%d]\n",
				bar, idx_base, idx_end - 1);
			goto err_set_ib;
		}

		if (test_bit(idx, ep->ib_window_map)) {
			dev_err(pcie->dev,
				"BAR%d: inbound window %d already in use\n",
				bar, idx);
			goto err_set_ib;
		}

		rz_pcie_set_inbound_ep(pcie, cpu_addr, pci_addr, mask,
				       idx, func_no);
		set_bit(idx, ep->ib_window_map);
		cpu_addr += align_limit;
		pci_addr += align_limit;
		idx++;
	}

	return 0;

err_set_ib:
	for (i = idx_base; i < idx; i++)
		clear_bit(i, ep->ib_window_map);

	return -EINVAL;
}

static void rz_pcie_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 struct pci_epf_bar *epf_bar)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	const struct rz_pcie_window_config *win_cfg;
	struct rz_pcie *pcie = &ep->pcie;
	enum pci_barno bar = epf_bar->barno;
	int flags = epf_bar->flags;
	int idx, idx_base, idx_end;

	if ((flags & PCI_BASE_ADDRESS_MEM_TYPE_64) && (bar & 1))
		return;

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		rz_write_conf_ep(pcie, 0, RZ_PCI_EP_CFG_BARMSK(bar));
		rz_write_conf_ep(pcie, 0, RZ_PCI_EP_CFG_BARMSK(bar + 1));
	}

	win_cfg = &ep->data->func_cfg[func_no].bar_win[bar];
	if (!win_cfg->num)
		return;

	idx_base = win_cfg->start;
	idx_end  = win_cfg->start + win_cfg->num;

	for (idx = idx_base; idx < idx_end; idx++) {
		if (!test_bit(idx, ep->ib_window_map))
			continue;

		rz_pcie_set_inbound_ep(pcie, 0, 0, 0, idx, func_no);
		clear_bit(idx, ep->ib_window_map);
	}
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
	int window;
	u32 status;

	/* check if we have a link. */
	status = readl_relaxed(pcie->base + RZG3S_PCI_PCSTAT2);
	status = FIELD_GET(RZG3S_PCI_PCSTAT2_STATE_RX_DETECT, status);
	if (status) {
		dev_dbg(pcie->dev, "PCIe have Link up\n");
	} else {
		dev_err(pcie->dev, "PCIe x%d: Link not up\n", status);
		return -EPERM;
	}

	window = rz_pcie_ep_get_window(ep, addr);
	if (window < 0) {
		dev_err(pcie->dev, "failed to get corresponding window\n");
		return -EINVAL;
	}

	/*
	 * According to the RZ/V2H HW Manual (Rev.1.30 section 6.6.4.2.3
	 * (71) PCIe Window Base m (Lower) Register and
	 * (75) PCIe Destination m (Lower) Register
	 * need to be 4K aligned.
	 */
	if ((!IS_ALIGNED(pci_addr, SZ_4K) || (!IS_ALIGNED(addr, SZ_4K))))
		return -EINVAL;

	rz_pcie_set_outbound_ep(pcie, window, addr, pci_addr, size);

	ep->ob_mapped_addr[window] = addr;

	return 0;
}

static void rz_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				    phys_addr_t addr)
{
	struct rz_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rz_pcie *pcie = &ep->pcie;
	int idx;

	for (idx = 0; idx < ep->num_ob_windows; idx++)
		if (ep->ob_mapped_addr[idx] == addr)
			break;

	if (idx >= ep->num_ob_windows)
		return;

	rz_pcie_set_outbound_ep(pcie, idx, 0x0, 0x0, 0x0);

	ep->ob_mapped_addr[idx] = 0;
}

static int rz_pcie_ep_assert_intx(struct rz_pcie_endpoint *ep,
				    u8 func_no)
{
	struct rz_pcie *pcie = &ep->pcie;
	u32 val;
	int ret;

	/* Check MSI enable bit */
	val = rz_read_conf_ep(pcie, RZ_PCI_EP_CFG_MSICAP, func_no);
	if ((val & RZ_PCI_EP_CFG_MSICAP_MSIE)) {
		dev_err(pcie->dev, "MSI is enabled, cannot assert INTx\n");
		return -EINVAL;
	}

	/* SYS assert INTX interrupt */
	ret = rzg3s_sysc_config_func(ep->sysc, RZG3S_SYSC_FUNC_ID_INTX, 1,
				     ep->channel);
	if (ret) {
		dev_err(pcie->dev, "Cannot assert INTx interrupt\n");
		return ret;
	}

	return 0;
}

static int rz_pcie_ep_assert_msi(struct rz_pcie_endpoint *ep,
				   u8 func_no, u8 interrupt_num)
{
	struct rz_pcie *pcie = &ep->pcie;
	u16 msi_count;
	u32 val;
	int ret;

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

	/* SYS deassert MSI interrupt */
	ret = rzg3s_sysc_config_func(ep->sysc, RZG3S_SYSC_FUNC_ID_MSI1, 0,
				  ep->channel);
	if (ret) {
		dev_err(pcie->dev, "Cannot deassert MSI interrupt\n");
		return ret;
	}

	/* SYS assert MSI interrupt */
	ret = rzg3s_sysc_config_func(ep->sysc, RZG3S_SYSC_FUNC_ID_MSI1, 1,
				  ep->channel);
	if (ret) {
		dev_err(pcie->dev, "Cannot assert MSI interrupt\n");
		return ret;
	}

	return 0;
}

static int rzt2h_soc_pcie_init_phy(struct rz_pcie_endpoint *ep)
{
	static const u32 xcfgd_settings[RZG3S_PCI_PHY_XCFGD_NUM] = {
		0x00002000, 0x00C00090, 0x000001E0, 0x02000000, 0x00000000,
		0x00520154, 0x00000000, 0x00000000, 0x44440000, 0x00000000,
		0x5DBB8000, 0x1E000000,
	};
	static const u32 xcfga_cmn_settings[RZG3S_PCI_PHY_XCFGA_CMN_NUM] = {
		0x00000080, 0x60940060, 0x00C12000, 0x60000BF8, 0x80834238,
		0x00001118, 0x32040400, 0x21914064, 0x52948A03, 0x219CE008,
		0x0C867F02, 0x40643228, 0x010A9291, 0xE044039C, 0x08800807,
		0x00041002,
	};
	static const u32 xcfga_l0_l1_settings[RZT2H_PCI_PHY_XCFGA_L0_L1_NUM] = {
		0x08000000, 0x00050400, 0xE0003300, 0xC0400FBF, 0x960902A0,
		0x00020860, 0x08000000, 0x00050400, 0xE0003300, 0xC0400FBF,
		0x960902A0, 0x00020860,
	};

	unsigned int i;

	/*
	 * Enable access permission for physical layer control and status
	 * registers.
	 */
	writel_relaxed(RZG3S_PCI_PERM_PIPE_PHY_REG_EN,
		       ep->pcie.base + RZG3S_PCI_PERM);

	for (i = 0; i < RZG3S_PCI_PHY_XCFGD_NUM; i++) {
		writel_relaxed(xcfgd_settings[i],
			       ep->pcie.base + RZG3S_PCI_PHY_XCFGD(i));
	}

	for (i = 0; i < RZG3S_PCI_PHY_XCFGA_CMN_NUM; i++) {
		writel_relaxed(xcfga_cmn_settings[i],
			       ep->pcie.base + RZG3S_PCI_PHY_XCFGA_CMN(i));
	}

	for (i = 0; i < RZT2H_PCI_PHY_XCFGA_L0_L1_NUM; i++) {
		writel_relaxed(xcfga_l0_l1_settings[i],
			       ep->pcie.base + RZG3S_PCI_PHY_XCFGA_RX(i));
	}

	/* Select PHY settings values */
	writel_relaxed(RZG3S_PCI_PHY_XCFG_CTRL_PHYREG_SEL,
		       ep->pcie.base + RZG3S_PCI_PHY_XCFG_CTRL);

	/*
	 * Disable access permission for physical layer control and status
	 * registers.
	 */
	writel_relaxed(0, ep->pcie.base + RZG3S_PCI_PERM);

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
	.bar[BAR_0] = { .type = BAR_FIXED, .fixed_size = SZ_4M,
			.only_64bit = true, },
	.bar[BAR_1] = { .type = BAR_RESERVED, },
	.bar[BAR_2] = { .type = BAR_FIXED, .fixed_size = SZ_4M,
			.only_64bit = true, },
	.bar[BAR_3] = { .type = BAR_RESERVED, },
	.bar[BAR_4] = { .type = BAR_FIXED, .fixed_size = SZ_8K,
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

static const struct rz_pcie_func_config rzv2h_func_configs[] = {
	/* Function 0 */
	{
		.bar_win = {
			[BAR_0] = { .start = 0, .num = 4 },
			[BAR_2] = { .start = 4, .num = 4 },
		},
	},
};

static const struct rz_pcie_soc_data rzv2h_soc_data = {
	.func_cfg = rzv2h_func_configs,
	.max_channels = 2,
	.sysc_info = {
		.functions = {
			[RZG3S_SYSC_FUNC_ID_INTX] = {
				.offset = 0x1000,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_MSI1] = {
				.offset = 0x1004,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_L1_ALLOW] = {
				.offset = 0x1020,
				.stride = 0x30,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_MODE] = {
				.offset = 0x1024,
				.stride = 0x30,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_LANES_MODE] = {
				.offset = 0x1060,
				.mask = GENMASK(9, 8),
			},
		},
	},
};

static const struct rz_pcie_soc_data rzv2n_soc_data = {
	.func_cfg = rzv2h_func_configs,
	.max_channels = 1,
	.sysc_info = {
		.functions = {
			[RZG3S_SYSC_FUNC_ID_INTX] = {
				.offset = 0x1000,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_MSI1] = {
				.offset = 0x1004,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_L1_ALLOW] = {
				.offset = 0x1020,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_MODE] = {
				.offset = 0x1024,
				.mask = BIT(0),
			},
		},
	},
};

static const struct rz_pcie_func_config rzt2h_func_configs[] = {
	/* Function 0 */
	{
		.bar_win = {
			[BAR_0] = { .start = 0, .num = 4 },
			[BAR_2] = { .start = 4, .num = 4 },
		},
	},
};

static const struct rz_pcie_soc_data rzt2h_soc_data = {
	.func_cfg = rzt2h_func_configs,
	.max_channels = 2,
	.init_phy = rzt2h_soc_pcie_init_phy,
	.sysc_info = {
		.functions = {
			[RZG3S_SYSC_FUNC_ID_INTX] = {
				.offset = 0x2000,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_MSI1] = {
				.offset = 0x2004,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_L1_ALLOW] = {
				.offset = 0x2020,
				.stride = 0x30,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_ID_MODE] = {
				.offset = 0x2024,
				.stride = 0x30,
				.mask = BIT(0),
			},
			[RZG3S_SYSC_FUNC_LANES_MODE] = {
				.offset = 0x2060,
				.mask = GENMASK(9, 8),
			},
		},
	},
};

static const struct of_device_id rz_pcie_ep_of_match[] = {
	{
		.compatible = "renesas,rzv2h-pcie-ep",
		.data = &rzv2h_soc_data,
	},
	{
		.compatible = "renesas,rzv2n-pcie-ep",
		.data = &rzv2n_soc_data,
	},
	{
		.compatible = "renesas,rzt2h-pcie-ep",
		.data = &rzt2h_soc_data,
	},
	{}, /* sentinel */
};

static int rz_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rz_pcie_endpoint *ep;
	struct rz_pcie_dma *dmac;
	struct device_node *np = dev->of_node;
	struct device_node *sysc_np __free(device_node) =
		of_parse_phandle(np, "renesas,sysc", 0);
	struct rzg3s_sysc *sysc;
	struct rz_pcie *pcie;
	struct pci_epc *epc;
	int err, channel;

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (err)
		return err;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	ep->saved_regs = devm_kzalloc(dev, sizeof(struct rz_pci_saved_regs), GFP_KERNEL);
	if (!ep->saved_regs)
		return -ENOMEM;

	platform_set_drvdata(pdev, ep);
	pcie = &ep->pcie;
	pcie->dev = dev;
	ep->data = device_get_match_data(dev);
	platform_set_drvdata(pdev, ep);

	ep->sysc = devm_kzalloc(dev, sizeof(*ep->sysc), GFP_KERNEL);
	if (!ep->sysc)
		return -ENOMEM;

	sysc = ep->sysc;
	sysc->info = &ep->data->sysc_info;

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

	if (ep->data->max_channels > 1) {
		err = of_property_read_u32(dev->of_node, "pcie,channel", &channel);
		if (err) {
			dev_err(pcie->dev, "%pOF: No pcie,channel property found\n",
				dev->of_node);
			return -EINVAL;
		}

		ep->channel = channel;
		if (ep->channel >= ep->data->max_channels) {
			dev_err(pcie->dev, "%pOF: Invalid pcie,channel '%u'\n",
				dev->of_node, ep->channel);
			return -EINVAL;
		}
	}

	sysc->regmap = syscon_node_to_regmap(sysc_np);
	if (IS_ERR(sysc->regmap)) {
		err = PTR_ERR(sysc->regmap);
		goto err_pm_put;
	}

	err = rz_pcie_ep_get_pdata(ep, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		goto err_pm_put;
	}

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

	err = rz_pcie_hw_init_ep(pcie, ep->channel);
	if (err < 0) {
		dev_err(dev, "failed to initialize endpoint\n");
		goto err_pm_put;
	}

	err = pci_epc_multi_mem_init(epc, ep->ob_window, ep->num_ob_windows);
	if (err < 0) {
		dev_err(dev, "failed to initialize the epc memory space\n");
		goto err_pm_put;
	}

	/* Don't failed if PCIe DMA not enabled */
	dmac = &ep->dmac;
	dmac->dev = dev;
	dmac->base = pcie->base;

	err = rzg3s_pcie_dma_probe(dmac, false);
	if (err && err != -ENODEV)
		goto err_pm_put;

	pci_epc_init_notify(epc);

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);

	return err;
}

static int rz_pcie_ep_suspend_noirq(struct device *dev)
{
	struct rz_pcie_endpoint *ep = dev_get_drvdata(dev);
	struct rz_pci_saved_regs *saved_regs = ep->saved_regs;
	struct rz_pcie *pcie = &ep->pcie;
	int i, ret;

	for (i = 0; i < PCI_EP_CFG_REG_NUM; i++)
		saved_regs->cfg_regs[i] = rz_read_conf_ep(pcie, i * 4, 0);

	for (i = 0; i < RZG3S_MAX_WINDOWS; i++) {
		saved_regs->axi_win_regs.base_L[i] = readl_relaxed(pcie->base + RZG3S_PCI_AWBASEL(i));
		saved_regs->axi_win_regs.base_U[i] = readl_relaxed(pcie->base + RZG3S_PCI_AWBASEU(i));
		saved_regs->axi_win_regs.mask_L[i] = readl_relaxed(pcie->base + RZG3S_PCI_AWMASKL(i));
		saved_regs->axi_win_regs.mask_U[i] = readl_relaxed(pcie->base + RZG3S_PCI_AWMASKU(i));
		saved_regs->axi_win_regs.dest_L[i] = readl_relaxed(pcie->base + RZG3S_PCI_ADESTL(i));
		saved_regs->axi_win_regs.dest_U[i] = readl_relaxed(pcie->base + RZG3S_PCI_ADESTU(i));
	}

	for (i = 0; i < RZ_PCI_EP_MAX_RESOURCES; i++) {
		saved_regs->pci_win_regs.base_L[i] = readl_relaxed(pcie->base + RZG3S_PCI_PWBASEL(i));
		saved_regs->pci_win_regs.base_U[i] = readl_relaxed(pcie->base + RZG3S_PCI_PWBASEU(i));
		saved_regs->pci_win_regs.mask_L[i] = readl_relaxed(pcie->base + RZG3S_PCI_PWMASKL(i));
		saved_regs->pci_win_regs.mask_U[i] = readl_relaxed(pcie->base + RZG3S_PCI_PWMASKU(i));
		saved_regs->pci_win_regs.dest_L[i] = readl_relaxed(pcie->base + RZG3S_PCI_PDESTL(i));
		saved_regs->pci_win_regs.dest_U[i] = readl_relaxed(pcie->base + RZG3S_PCI_PDESTU(i));
	}

	ret = pm_runtime_put_sync(dev);
	if (ret)
		return ret;

	ret = reset_control_assert(ep->rst);
	if (ret)
		goto rpm_restore;

	return 0;

rpm_restore:
	pm_runtime_resume_and_get(dev);
	return ret;
}

static int rz_pcie_ep_resume_noirq(struct device *dev)
{
	struct rz_pcie_endpoint *ep = dev_get_drvdata(dev);
	struct rz_pci_saved_regs *saved_regs = ep->saved_regs;
	struct rz_pcie *pcie = &ep->pcie;
	int i, ret;

	ret = reset_control_deassert(ep->rst);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		goto assert_resets;

	ret = rz_pcie_hw_init_ep(pcie, ep->channel);
	if (ret)
		goto rpm_put;

	for (i = PCI_EP_CFG_REG_NUM - 1; i >= 0; i--) {
		rz_write_conf_ep(pcie, saved_regs->cfg_regs[i], i * 4);
	}

	for (i = 0; i < RZG3S_MAX_WINDOWS; i++) {
		writel_relaxed(saved_regs->axi_win_regs.base_L[i], pcie->base + RZG3S_PCI_AWBASEL(i));
		writel_relaxed(saved_regs->axi_win_regs.base_U[i], pcie->base + RZG3S_PCI_AWBASEU(i));
		writel_relaxed(saved_regs->axi_win_regs.mask_L[i], pcie->base + RZG3S_PCI_AWMASKL(i));
		writel_relaxed(saved_regs->axi_win_regs.mask_U[i], pcie->base + RZG3S_PCI_AWMASKU(i));
		writel_relaxed(saved_regs->axi_win_regs.dest_L[i], pcie->base + RZG3S_PCI_ADESTL(i));
		writel_relaxed(saved_regs->axi_win_regs.dest_U[i], pcie->base + RZG3S_PCI_ADESTU(i));
	}

	for (i = 0; i < RZ_PCI_EP_MAX_RESOURCES; i++) {
		writel_relaxed(saved_regs->pci_win_regs.base_L[i], pcie->base + RZG3S_PCI_PWBASEL(i));
		writel_relaxed(saved_regs->pci_win_regs.base_U[i], pcie->base + RZG3S_PCI_PWBASEU(i));
		writel_relaxed(saved_regs->pci_win_regs.mask_L[i], pcie->base + RZG3S_PCI_PWMASKL(i));
		writel_relaxed(saved_regs->pci_win_regs.mask_U[i], pcie->base + RZG3S_PCI_PWMASKU(i));
		writel_relaxed(saved_regs->pci_win_regs.dest_L[i], pcie->base + RZG3S_PCI_PDESTL(i));
		writel_relaxed(saved_regs->pci_win_regs.dest_U[i], pcie->base + RZG3S_PCI_PDESTU(i));
	}

	return 0;
rpm_put:
	pm_runtime_put_sync(dev);
assert_resets:
	reset_control_assert(ep->rst);
	return ret;
}

static const struct dev_pm_ops rz_pcie_ep_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(rz_pcie_ep_suspend_noirq,
				  rz_pcie_ep_resume_noirq)
};

static struct platform_driver rz_pcie_ep_driver = {
	.driver = {
		.name = "rz-pcie-ep",
		.of_match_table = rz_pcie_ep_of_match,
		.pm = pm_ptr(&rz_pcie_ep_pm_ops),
		.suppress_bind_attrs = true,
	},
	.probe = rz_pcie_ep_probe,
};
builtin_platform_driver(rz_pcie_ep_driver);

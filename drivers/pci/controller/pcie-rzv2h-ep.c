// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe endpoint driver for Renesas RZ/V2H Series SoCs
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

#include "pcie-rzv2h.h"

/* Structure representing the PCIe interface */

struct pcie_soc_config {
	u32 device_id;
};

struct rzv2h_pcie_endpoint {
	struct rzv2h_pcie		pcie;
	phys_addr_t			*ob_mapped_addr;
	struct pci_epc_mem_window	*ob_window;
	u8				max_functions;
	unsigned int			bar_to_atu[MAX_NR_INBOUND_MAPS_EP];
	unsigned long			*ib_window_map;
	u32				num_ib_windows;
	u32				num_ob_windows;
	int				channel;
	struct reset_control		*rst;
	struct regmap			*syscon;
	const struct pcie_soc_config	*soc_cfg;
};

/*-----------------------------------------------------
  PCIe Setting Function
-------------------------------------------------------*/
static void rzv2h_pcie_setting_config_ep(struct rzv2h_pcie *pcie, u8 func_no)
{
	rzv2h_pci_write_reg(pcie, rzv2h_pci_read_reg(pcie, PCIE_CORE_MODE_SET_1_REG) & ~MODE_PORT,
			    PCIE_CORE_MODE_SET_1_REG);

	rzv2h_pci_write_reg(pcie, RESET_CONFIG_DEASSERT, PCI_RESET_REG);

	/* Clear BAR mask register */
	for (int i = 0; i < MAX_NR_INBOUND_MAPS_EP; i++)
		rzv2h_write_conf_ep(pcie, 0, PCI_EP_BAR_MASK_ADR(i), func_no);

	/* Setting following hardware manual */
	rzv2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0001_EP_F0, PCI_EP_BSIZE00_0001_ADR, func_no);
	rzv2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0203_EP_F0, PCI_EP_BSIZE00_0203_ADR, func_no);
	rzv2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0405_EP_F0, PCI_EP_BSIZE00_0405_ADR, func_no);
	rzv2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0006_EP_F0, PCI_EP_BSIZE00_0006_ADR, func_no);

	rzv2h_write_conf_ep(pcie, DEVICE_CONTROL_INIT, PCI_EP_DEVICE_CONTROL_ADDR, func_no);
}

static int PCIE_EP_INT_Initialize(struct rzv2h_pcie *pcie)
{
	/* Clear Event Interrupt Status 0 */
	rzv2h_pci_write_reg(pcie, INT_ST0_CLR, PCI_RC_PEIS0_REG);	/* Set PCI_RC 0204h */

	/* Set Event Interrupt Enable 0 */
	rzv2h_pci_write_reg(pcie, INT_EN0_SET, PCI_RC_PEIE0_REG);	/* Set PCI_RC 0200h */

	/* Clear  Event Interrupt Status 1 */
	rzv2h_pci_write_reg(pcie, INT_ST1_CLR, PCI_RC_PEIS1_REG);	/* Set PCI_RC 020ch */

	/* Set Event Interrupt Enable 1 */
	rzv2h_pci_write_reg(pcie, INT_EN1_SET, PCI_RC_PEIE1_REG);	/* Set PCI_RC 0208h */

	/* Clear AXI Master Error Interrupt Status */
	rzv2h_pci_write_reg(pcie, INT_ST_AXIM_CLR, PCI_RC_AMEIS_REG);	/* Set PCI_RC 0214h */

	/* Set AXI Master Error Interrupt Enable */
	rzv2h_pci_write_reg(pcie, INT_EN_AXIM_SET, PCI_RC_AMEIE_REG);	/* Set PCI_RC 0210h */

	/* Clear AXI Slave Error Interrupt Status */
	rzv2h_pci_write_reg(pcie, INT_ST_AXIS_CLR, PCI_RC_ASEIS1_REG);	/* Set PCI_RC 0224h */

	/* Set AXI Slave Error Interrupt Enable */
	rzv2h_pci_write_reg(pcie, INT_EN_AXIS_SET, PCI_RC_ASEIE1_REG);	/* Set PCI_RC 0220h */

	/* Clear Message Receive Interrupt Status */
	rzv2h_pci_write_reg(pcie, INT_MR_CLR, PCI_RC_MSGRCVIS_REG);	/* Set PCI_RC 0124h */

	/* Set Message Receive Interrupt Enable */
	rzv2h_pci_write_reg(pcie, INT_MR_SET_EP, PCI_RC_MSGRCVIE_REG);	/* Set PCI_RC 0120h */

	return 0;
}

static int rzv2h_pcie_hw_init_ep(struct rzv2h_pcie *pcie, int channel)
{
	struct rzv2h_pcie_endpoint *ep = container_of(pcie, struct rzv2h_pcie_endpoint, pcie);

	/* SYS setting mode port */
	regmap_write(ep->syscon, SYS_PCIE_MODE_CH(channel), MODE_PORT_SYS_EP);

	/* Set to the PCIe reset state */
	rzv2h_pci_write_reg(pcie, RESET_ALL_ASSERT, PCI_RESET_REG);

	/* Release the PCIe reset */
	rzv2h_pci_write_reg(pcie, RESET_LOAD_CFG_RELEASE, PCI_RESET_REG);

	/* SYS set lane mode - only valid RZ/V2H IP */
	if (ep->soc_cfg->device_id == 0x003a)
		regmap_write(ep->syscon, SYS_PCIE_LANE_MODE, LINK_MASTER_4_LANE_MODE);

	/* config Device ID for PCIE EP here */
	rzv2h_write_conf(pcie, ep->soc_cfg->device_id << 16
				| PCIE_CONF_VENDOR_ID, PCI_RC_VID_ADR);

	/* set BAR */
	rzv2h_pcie_setting_config_ep(pcie, 0);

	/* SYS setting allow_enter_l1  */
	regmap_write(ep->syscon, SYS_PCIE_MISC_CH(channel), ALLOW_ENTER_L1);

	/* Set Interrupt settings             : step14  */
	PCIE_EP_INT_Initialize(pcie);

	/* Release the PCIe reset : step14 : RST_PS_B, RST_GP_B, RST_B */
	rzv2h_pci_write_reg(pcie, RESET_PS_GP_RELEASE, PCI_RESET_REG);	/* Set PCI_RC 310h */

	/* Wait 500us over */
	msleep(20);

	/* Release the PCIe reset : step17 : RST_OUT_B, RST_RSM_B) */
	rzv2h_pci_write_reg(pcie, RESET_ALL_DEASSERT,  PCI_RESET_REG);	/* Set PCI_RC 310h */

	rzv2h_pci_write_reg(pcie, 0x3ff2,  MODE_SET_1_REG);		/* Set PCI_RC 318h */

	return 0;
}

static int rzv2h_pcie_ep_get_window(struct rzv2h_pcie_endpoint *ep,
				   phys_addr_t addr)
{
	int i;

	for (i = 0; i < ep->num_ob_windows; i++)
		if (ep->ob_window[i].phys_base == addr)
			return i;

	return -EINVAL;
}

static int rzv2h_pcie_parse_outbound_ranges(struct rzv2h_pcie_endpoint *ep,
					   struct platform_device *pdev)
{
	struct rzv2h_pcie *pcie = &ep->pcie;
	char outbound_name[10];
	struct resource *res;
	unsigned int i = 0;

	ep->num_ob_windows = 0;
	for (i = 0; i < RZV2H_PCI_MAX_RESOURCES_EP; i++) {
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

static int rzv2h_pcie_ep_get_pdata(struct rzv2h_pcie_endpoint *ep,
				  struct platform_device *pdev)
{
	struct rzv2h_pcie *pcie = &ep->pcie;
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

	ep->ob_window = devm_kcalloc(dev, RZV2H_PCI_MAX_RESOURCES_EP,
				     sizeof(*window), GFP_KERNEL);
	if (!ep->ob_window)
		return -ENOMEM;

	rzv2h_pcie_parse_outbound_ranges(ep, pdev);

	err = of_property_read_u8(dev->of_node, "max-functions",
				  &ep->max_functions);
	if (err < 0 || ep->max_functions > RZV2H_EPC_MAX_FUNCTIONS)
		ep->max_functions = RZV2H_EPC_MAX_FUNCTIONS;

	return 0;
}

static int rzv2h_pcie_ep_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				     struct pci_epf_header *hdr)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzv2h_pcie *pcie = &ep->pcie;
	u32 val;

	if (!func_no)
		val = hdr->vendorid;
	else
		val = rzv2h_read_conf_ep(pcie, PCI_EP_VID_ADR, func_no);

	val |= hdr->deviceid << 16;
	rzv2h_write_conf_ep(pcie, val, PCI_EP_VID_ADR, func_no);

	val = hdr->revid;
	val |= hdr->progif_code << 8;
	val |= hdr->subclass_code << 16;
	val |= hdr->baseclass_code << 24;
	rzv2h_write_conf_ep(pcie, val, PCI_EP_RID_CC_ADR, func_no);

	if (!func_no)
		val = hdr->subsys_vendor_id;
	else
		val = rzv2h_read_conf_ep(pcie, PCI_EP_SUBSYS_ID_ADR, func_no);
	val |= hdr->subsys_id << 16;
	rzv2h_write_conf_ep(pcie, val, PCI_EP_SUBSYS_ID_ADR, func_no);

	if (hdr->interrupt_pin > PCI_INTERRUPT_INTA)
		return -EINVAL;

	val = rzv2h_read_conf_ep(pcie, PCI_EP_INTERRUPT_ADR, func_no);
	val |= (hdr->interrupt_pin << 8);
	rzv2h_write_conf_ep(pcie, val, PCI_EP_INTERRUPT_ADR, func_no);

	rzv2h_write_conf_ep(pcie, PCI_EP_BUS_MASTER_ENABLE | PCI_EP_MEMORY_SPACE_ENABLE,
							PCI_EP_COMMAND_AND_STATUS, func_no);

	return 0;
}

static int rzv2h_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				struct pci_epf_bar *epf_bar)
{
	int flags = epf_bar->flags | LAR_ENABLE | LAM_64BIT;
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	u64 size = 1ULL << fls64(epf_bar->size - 1);
	dma_addr_t cpu_addr = epf_bar->phys_addr;
	enum pci_barno bar = epf_bar->barno;
	struct rzv2h_pcie *pcie = &ep->pcie;
	u32 mask;
	int idx;

	idx = find_first_zero_bit(ep->ib_window_map, ep->num_ib_windows);
	if (idx >= ep->num_ib_windows) {
		dev_err(pcie->dev, "no free inbound window\n");
		return -EINVAL;
	}

	if ((flags & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
		flags |= IO_SPACE;

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
	rzv2h_write_conf_ep(pcie, mask, PCI_EP_BAR_MASK_ADR(idx), func_no);

	mask &= ~0xf;
	rzv2h_pcie_set_inbound_ep(pcie, cpu_addr,
			      0x0, mask | flags, idx, func_no);

	return 0;
}

static void rzv2h_pcie_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				   struct pci_epf_bar *epf_bar)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

	rzv2h_pcie_set_inbound_ep(&ep->pcie, 0x0, 0x0, 0x0, bar, func_no);

	clear_bit(atu_index, ep->ib_window_map);
	clear_bit(atu_index + 1, ep->ib_window_map);
}

static int rzv2h_pcie_ep_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 u8 interrupts)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzv2h_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rzv2h_read_conf(pcie, PCI_EP_MSICAP(0));
	flags |= interrupts << MSICAP0_MMESCAP_OFFSET;
	rzv2h_write_conf(pcie, flags, PCI_EP_MSICAP(0));

	return 0;
}

static int rzv2h_pcie_ep_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzv2h_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rzv2h_read_conf(pcie, PCI_EP_MSICAP(0));

	if (!(flags & MSICAP0_MSIE))
		return -EINVAL;

	return ((flags & MSICAP0_MMESE_MASK) >> MSICAP0_MMESE_OFFSET);
}

static int rzv2h_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 phys_addr_t addr, u64 pci_addr, size_t size)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzv2h_pcie *pcie = &ep->pcie;
	struct resource_entry win;
	struct resource res;
	int window;
	u32 status;

	/* check if we have a link. */
	status = (rzv2h_pci_read_reg(pcie, PCIE_CORE_STATUS_2_REG) >> 8) & 0x3;
	if (status) {
		dev_err(pcie->dev, "PCIe have Link up\n");
		/*- Detect Lane 0 or Lane 1 -*/
	} else {
		dev_err(pcie->dev, "PCIe x%d: Link not up\n", status);
		return -EPERM;
	}

	window = rzv2h_pcie_ep_get_window(ep, addr);
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

	rzv2h_pcie_set_outbound_ep(pcie, window, addr, pci_addr, size);

	ep->ob_mapped_addr[window] = addr;

	return 0;
}

static void rzv2h_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				    phys_addr_t addr)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzv2h_pcie *pcie = &ep->pcie;
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

	rzv2h_pcie_set_outbound_ep(pcie, idx, 0x0, 0x0, 0x0);

	ep->ob_mapped_addr[idx] = 0;
}

static int rzv2h_pcie_ep_assert_intx(struct rzv2h_pcie_endpoint *ep,
				    u8 func_no)
{
	struct rzv2h_pcie *pcie = &ep->pcie;
	u32 val;

	/* Check MSI enable bit */
	val = rzv2h_read_conf_ep(pcie, PCI_EP_MSICAP(0), func_no);
	if ((val & MSICAP0_MSIE)) {
		dev_err(pcie->dev, "MSI is enabled, cannot assert INTx\n");
		return -EINVAL;
	}

	/* SYS assert INTX interrupt */
	regmap_write(ep->syscon, SYS_PCIE_INTX_CH(ep->channel), 0x1);

	return 0;
}

static int rzv2h_pcie_ep_assert_msi(struct rzv2h_pcie_endpoint *ep,
				   u8 func_no, u8 interrupt_num)
{
	struct rzv2h_pcie *pcie = &ep->pcie;
	u16 msi_count;
	u32 val;

	/* Check MSI enable bit */
	val = rzv2h_read_conf_ep(pcie, PCI_EP_MSICAP(0), func_no);
	if (!(val & MSICAP0_MSIE)) {
		dev_err(pcie->dev, "MSI is not enabled, cannot assert MSI\n");
		return -EINVAL;
	}

	/* Get MSI numbers from MME */
	msi_count = ((val & MSICAP0_MMESE_MASK) >> MSICAP0_MMESE_OFFSET);
	msi_count = 1 << msi_count;

	if (!interrupt_num || interrupt_num > msi_count)
		return -EINVAL;

	/* SYS assert MSI interrupt */
	regmap_write(ep->syscon, SYS_PCIE_MSI1_CH(ep->channel), 0x0);
	regmap_write(ep->syscon, SYS_PCIE_MSI1_CH(ep->channel), 0x1);

	return 0;
}

static int rzv2h_pcie_ep_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				  enum pci_epc_irq_type type,
				  u16 interrupt_num)
{
	struct rzv2h_pcie_endpoint *ep = epc_get_drvdata(epc);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return rzv2h_pcie_ep_assert_intx(ep, func_no);

	case PCI_EPC_IRQ_MSI:
		return rzv2h_pcie_ep_assert_msi(ep, func_no, interrupt_num);

	default:
		return -EINVAL;
	}

}

static const struct pci_epc_features rzv2h_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	/* use 64-bit BARs so mark BAR[1,3,5] as reserved */
	.reserved_bar = 1 << BAR_1 | 1 << BAR_3 | 1 << BAR_5,
	.bar_fixed_64bit = 1 << BAR_0 | 1 << BAR_2 | 1 << BAR_4,
	.bar_fixed_size[0] = SZ_1M,
	.bar_fixed_size[2] = SZ_4K,
	.bar_fixed_size[4] = SZ_256,
};

static const struct pci_epc_features *rzv2h_pcie_ep_get_features(struct pci_epc *epc,
								u8 func_no, u8 vfunc_no)
{
	return &rzv2h_pcie_epc_features;
}

static const struct pci_epc_ops rzv2h_pcie_epc_ops = {
	.write_header	= rzv2h_pcie_ep_write_header,
	.set_bar	= rzv2h_pcie_ep_set_bar,
	.clear_bar	= rzv2h_pcie_ep_clear_bar,
	.set_msi	= rzv2h_pcie_ep_set_msi,
	.get_msi	= rzv2h_pcie_ep_get_msi,
	.map_addr	= rzv2h_pcie_ep_map_addr,
	.unmap_addr	= rzv2h_pcie_ep_unmap_addr,
	.raise_irq	= rzv2h_pcie_ep_raise_irq,
	.get_features	= rzv2h_pcie_ep_get_features,
};

static const struct pcie_soc_config rzv2h_pcie_ep_cfg = {
	.device_id = 0x003a,
};

static const struct pcie_soc_config rzg3e_pcie_ep_cfg = {
	.device_id = 0x0038,
};

static const struct of_device_id rzv2h_pcie_ep_of_match[] = {
	{ .compatible = "renesas,rzv2h-pcie-ep", .data = &rzv2h_pcie_ep_cfg },
	{ .compatible = "renesas,rzg3e-pcie-ep", .data = &rzg3e_pcie_ep_cfg },
	{}, /* sentinel */
};

static int rzv2h_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzv2h_pcie_endpoint *ep;
	struct rzv2h_pcie *pcie;
	struct pci_epc *epc;
	const struct pcie_soc_config *soc_cfg;
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

	err = rzv2h_pcie_ep_get_pdata(ep, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		goto err_pm_put;
	}

	soc_cfg = of_device_get_match_data(dev);
	if (!soc_cfg)
		return -ENODEV;

	ep->soc_cfg = soc_cfg;

	ep->syscon = syscon_regmap_lookup_by_phandle(dev->of_node, "renesas,pcie-sys");
	if (IS_ERR(ep->syscon))
		return dev_err_probe(dev, PTR_ERR(ep->syscon),
				"Failed to get pcie syscon");

	ep->num_ib_windows = MAX_NR_INBOUND_MAPS_EP;
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

	epc = devm_pci_epc_create(dev, &rzv2h_pcie_epc_ops);
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

	rzv2h_pcie_hw_init_ep(pcie, ep->channel);

	err = pci_epc_multi_mem_init(epc, ep->ob_window, ep->num_ob_windows);

	if (err < 0) {
		dev_err(dev, "failed to initialize the epc memory space\n");
		goto err_pm_put;
	}

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);

	return err;
}

static struct platform_driver rzv2h_pcie_ep_driver = {
	.driver = {
		.name = "rzv2h-pcie-ep",
		.of_match_table = rzv2h_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rzv2h_pcie_ep_probe,
};
builtin_platform_driver(rzv2h_pcie_ep_driver);

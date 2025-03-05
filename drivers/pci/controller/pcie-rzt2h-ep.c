// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe endpoint driver for Renesas RZ/T2H Series SoCs
 * Copyright (c) 2023 Renesas Electronics Europe GmbH
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include <linux/arm-smccc.h>
#include <uapi/linux/psci.h>

#include "pcie-rzt2h.h"

#define RZT2H_EPC_MAX_FUNCTIONS		1

/* Structure representing the PCIe interface */
struct rzt2h_pcie_endpoint {
	struct rzt2h_pcie		pcie;
	phys_addr_t			*ob_mapped_addr;
	struct pci_epc_mem_window	*ob_window;
	u8				max_functions;
	unsigned int			bar_to_atu[MAX_NR_INBOUND_MAPS];
	unsigned long			*ib_window_map;
	u32				num_ib_windows;
	u32				num_ob_windows;
	int				channel;
	struct reset_control    *rst;
};

static void __iomem	*supplemental;

static void rzt2h_pcie_setting_phy(struct rzt2h_pcie *pcie)
{
	rzt2h_pci_write_reg(pcie, CFG_PHYINIT_EN, PERMISSION_REG);

	rzt2h_pci_write_reg(pcie, 0x00002000, 0x2000);
	rzt2h_pci_write_reg(pcie, 0x00C00090, 0x2010);
	rzt2h_pci_write_reg(pcie, 0x000001E0, 0x2020);
	rzt2h_pci_write_reg(pcie, 0x02000000, 0x2030);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2040);
	rzt2h_pci_write_reg(pcie, 0x00520154, 0x2050);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2060);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2070);
	rzt2h_pci_write_reg(pcie, 0x44440000, 0x2080);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2090);
	rzt2h_pci_write_reg(pcie, 0x5DBB8000, 0x20A0);
	rzt2h_pci_write_reg(pcie, 0x1E000000, 0x20B0);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x20C0);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x20D0);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x20E0);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x20F0);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2100);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2110);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2120);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2130);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2140);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2150);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2160);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2170);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2180);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x2190);
	rzt2h_pci_write_reg(pcie, 0x00000000, 0x21A0);

	rzt2h_pci_write_reg(pcie, 0x00000080, 0x2400);
	rzt2h_pci_write_reg(pcie, 0x60940060, 0x2410);
	rzt2h_pci_write_reg(pcie, 0x00C12000, 0x2420);
	rzt2h_pci_write_reg(pcie, 0x60000BF8, 0x2430);
	rzt2h_pci_write_reg(pcie, 0x80834238, 0x2440);
	rzt2h_pci_write_reg(pcie, 0x00001118, 0x2450);
	rzt2h_pci_write_reg(pcie, 0x32040400, 0x2460);
	rzt2h_pci_write_reg(pcie, 0x21914064, 0x2470);
	rzt2h_pci_write_reg(pcie, 0x52948A03, 0x2480);
	rzt2h_pci_write_reg(pcie, 0x219CE008, 0x2490);
	rzt2h_pci_write_reg(pcie, 0x0C867F02, 0x24A0);
	rzt2h_pci_write_reg(pcie, 0x40643228, 0x24B0);
	rzt2h_pci_write_reg(pcie, 0x010A9291, 0x24C0);
	rzt2h_pci_write_reg(pcie, 0xE044039C, 0x24D0);
	rzt2h_pci_write_reg(pcie, 0x08800807, 0x24E0);
	rzt2h_pci_write_reg(pcie, 0x00041002, 0x24F0);
	rzt2h_pci_write_reg(pcie, 0x08000000, 0x2500);
	rzt2h_pci_write_reg(pcie, 0x00050400, 0x2510);
	rzt2h_pci_write_reg(pcie, 0xE0003300, 0x2520);
	rzt2h_pci_write_reg(pcie, 0xC0400FBF, 0x2530);
	rzt2h_pci_write_reg(pcie, 0x960902A0, 0x2540);
	rzt2h_pci_write_reg(pcie, 0x00020860, 0x2550);

	rzt2h_pci_write_reg(pcie, 0x08000000, 0x2560);
	rzt2h_pci_write_reg(pcie, 0x00050400, 0x2570);
	rzt2h_pci_write_reg(pcie, 0xE0003300, 0x2580);
	rzt2h_pci_write_reg(pcie, 0xC0400FBF, 0x2590);
	rzt2h_pci_write_reg(pcie, 0x960902A0, 0x25A0);
	rzt2h_pci_write_reg(pcie, 0x00020860, 0x25B0);

	rzt2h_pci_write_reg(pcie, 0, PERMISSION_REG);
}

static void rzt2h_pcie_setting_config_ep(struct rzt2h_pcie *pcie, u8 fn)
{
	/* Configuration space (Root complex) setting */
	/* Vendor and Device ID: PCI Express Configuration Registers Adr 6000h */
	rzt2h_write_conf_ep(pcie,
			((PCIE_CONF_DEVICE_ID << 16) |
			 (PCIE_CONF_VENDOR_ID)),
			  PCI_EP_VID_F, fn);

	/* Revision ID and Class Code: PCI Express Configuration Registers Adr 6008h */
	rzt2h_write_conf_ep(pcie,
			((PCIE_CONF_BASE_CLASS << 24) |
			 (PCIE_CONF_SUB_CLASS << 16) |
			 (PCIE_CONF_PROGRAMING_IF << 8) |
			 (PCIE_CONF_REVISION_ID)),
			  PCI_EP_RID_CC_F, fn);

	rzt2h_write_conf_ep(pcie, PCIE_CFG_BAR_MASK0_L_EP_F0, PCI_EP_BARMSK00L_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BAR_MASK0_H_EP_F0, PCI_EP_BARMSK00U_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BAR_MASK1_L_EP_F0, PCI_EP_BARMSK01L_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BAR_MASK1_H_EP_F0, PCI_EP_BARMSK01U_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BAR_MASK2_L_EP_F0, PCI_EP_BARMSK02L_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BAR_MASK2_H_EP_F0, PCI_EP_BARMSK02U_F, fn);

	rzt2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0001_EP_F0, PCI_EP_BSIZE00_01_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0203_EP_F0, PCI_EP_BSIZE02_03_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_0405_EP_F0, PCI_EP_BSIZE04_05_F, fn);
	rzt2h_write_conf_ep(pcie, PCIE_CFG_BASE_SIZE_06_EP_F0, PCI_EP_BSIZE06_F, fn);
}

static int PCIE_CFG_Initialize_ep(struct rzt2h_pcie *pcie)
{
	/* Bus Number : PCI Express Configuration Registers Adr 6018h */
	rzt2h_write_conf_ep(pcie,
			   ((PCIE_CONF_SUBORDINATE_BUS << 16) |
			    (PCIE_CONF_SECOUNDARY_BUS  <<  8) |
			    (PCIE_CONF_PRIMARY_BUS)),
			     PCI_PRIMARY_BUS, 0);

	rzt2h_write_conf_ep(pcie,
			   ((PCIE_CONF_MEMORY_LIMIT << 16) |
			    (PCIE_CONF_MEMORY_BASE)),
			     PCI_MEMORY_BASE, 0);

	/*
	 * Base Address Register Mask00 (Lower) (Function #1):
	 * PCIE Configuration Registers Adr 60A0h
	 */
	rzt2h_write_conf_ep(pcie, BASEADR_MKL_ALLM, PCI_EP_BARMSK00L_F, 0);

	/*
	 * Base Address Register Mask00 (Upper) (Function #1):
	 * PCIE Configuration Registers Adr 60A4h
	 */
	rzt2h_write_conf_ep(pcie, BASEADR_MKU_ALLM, PCI_EP_BARMSK00U_F, 0);

	/* Base Size 00/01 : PCI Express Configuration Registers Adr 60C8h */
	rzt2h_write_conf_ep(pcie, BASESZ_INIT, PCI_EP_BSIZE00_01_F, 0);

	rzt2h_write_conf_ep(pcie, PM_CAPABILITIES_INIT, PCI_PM_CAPABILITIES, 0);

	return 0;
}

static void PCIE_EP_INT_Initialize(struct rzt2h_pcie *pcie)
{
	/* Clear Event Interrupt Status 0 */
	rzt2h_pci_write_reg(pcie, INT_ST0_CLR, PCI_EP_PEIS0_REG);	/* Set PCI_EP 0204h */

	/* Set Event Interrupt Enable 0 */
	rzt2h_pci_write_reg(pcie, INT_EN0_SET, PCI_EP_PEIE0_REG);	/* Set PCI_EP 0200h */

	/* Clear  Event Interrupt Status 1 */
	rzt2h_pci_write_reg(pcie, INT_ST1_CLR, PCI_EP_PEIS1_REG);	/* Set PCI_EP 020ch */

	/* Set Event Interrupt Enable 1 */
	rzt2h_pci_write_reg(pcie, INT_EN1_SET, PCI_EP_PEIE1_REG);	/* Set PCI_EP 0208h */

	/* Clear AXI Master Error Interrupt Status */
	rzt2h_pci_write_reg(pcie, INT_ST_AXIM_CLR, PCI_EP_AMEIS_REG);	/* Set PCI_EP 0214h */

	/* Set AXI Master Error Interrupt Enable */
	rzt2h_pci_write_reg(pcie, INT_EN_AXIM_SET, PCI_EP_AMEIE_REG);	/* Set PCI_EP 0210h */

	/* Clear AXI Slave Error Interrupt Status */
	rzt2h_pci_write_reg(pcie, INT_ST_AXIS_CLR, PCI_EP_ASEIS1_REG);	/* Set PCI_EP 0224h */

	/* Set AXI Slave Error Interrupt Enable */
	rzt2h_pci_write_reg(pcie, INT_EN_AXIS_SET, PCI_EP_ASEIE1_REG);	/* Set PCI_EP 0220h */

	/* Clear Message Receive Interrupt Status */
	rzt2h_pci_write_reg(pcie, INT_MR_CLR,
				PCI_EP_MSGRCVIS_REG);		/* Set PCI_RC 0124h */

	/* Set Message Receive Interrupt Enable */
	rzt2h_pci_write_reg(pcie, PCI_EP_MSGRCVIE,
				PCI_EP_MSGRCVIE_REG);		/* Set PCI_RC 0120h */
}

static int rzt2h_pcie_hw_init_ep(struct rzt2h_pcie *pcie)
{
	unsigned int timeout = 50;
	int tmp;
	u32 value;

	/* Set to the PCIe reset state: step6 */
	rzt2h_pci_write_reg(pcie, 0, PCI_RC_RESET_REG);
	rzt2h_pci_write_reg(pcie, RST_CFG_B | RST_LOAD_B, PCI_EP_RESET_REG);

	/* Command and Status: Enable Memory Space and Bus Master */
	tmp = rzt2h_read_conf(pcie, PCI_EP_COM_STA_F);
	rzt2h_write_conf(pcie, tmp | PCI_EP_COM_STA_F_MSE | PCI_EP_COM_STA_F_BME, PCI_EP_COM_STA_F);

	/* Setting of the PHY: step7 */
	rzt2h_pcie_setting_phy(pcie);

	/* Setting of HWINT related registers: step8 */
	PCIE_CFG_Initialize_ep(pcie);

	/* Permit ASPM L1 State Transition: step9 */
	writel(ALLOW_ENTER_L1, supplemental + PCIE_MISC);

	/* Set Interrupt settings: step10 */
	PCIE_EP_INT_Initialize(pcie);

	/* Release the PCIe reset: step11: RST_PS_B, RST_GP_B, RST_B, RST_OUT_B */
	value = rzt2h_pci_read_reg(pcie, PCI_RC_RESET_REG);
	rzt2h_pci_write_reg(pcie, value | RST_PS_B | RST_GP_B |
				RST_B | RST_OUT_B,  PCI_EP_RESET_REG);

	/* Wait for 500 µs or more: step12 */
	usleep_range(1000, 1250);

	/* Release the PCIe reset: step13: RST_RSM_B */
	value = rzt2h_pci_read_reg(pcie, PCI_RC_RESET_REG);
	rzt2h_pci_write_reg(pcie, value | RST_RSM_B,  PCI_EP_RESET_REG);

	/* This will timeout if we don't have a link. */
	while (timeout--) {
		if (!(rzt2h_pci_read_reg(pcie, PCIE_CORE_STATUS_1_REG) & DL_DOWN_STATUS))
			return 0;

		usleep_range(5000, 6250);
	}

	return -ETIMEDOUT;
}

static int rzt2h_pcie_ep_get_window(struct rzt2h_pcie_endpoint *ep,
				   phys_addr_t addr)
{
	int i;

	for (i = 0; i < ep->num_ob_windows; i++)
		if (ep->ob_window[i].phys_base == addr)
			return i;

	return -EINVAL;
}

static int rzt2h_pcie_parse_outbound_ranges(struct rzt2h_pcie_endpoint *ep,
					   struct platform_device *pdev)
{
	struct rzt2h_pcie *pcie = &ep->pcie;
	char outbound_name[10];
	struct resource *res;
	unsigned int i = 0;

	ep->num_ob_windows = 0;

	for (i = 0; i < RZT2H_PCI_MAX_RESOURCES; i++) {
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

static int rzt2h_pcie_ep_get_pdata(struct rzt2h_pcie_endpoint *ep,
				  struct platform_device *pdev)
{
	struct rzt2h_pcie *pcie = &ep->pcie;
	struct pci_epc_mem_window *window;
	struct device *dev = pcie->dev;
	struct resource res0, res1;
	int err, i;

	err = of_address_to_resource(dev->of_node, 0, &res0);
	if (err)
		return err;

	pcie->base = devm_ioremap_resource(dev, &res0);

	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	err = of_address_to_resource(dev->of_node, 1, &res1);
	if (err)
		return err;

	supplemental = devm_ioremap_resource(dev, &res1);

	if (IS_ERR(supplemental))
		return PTR_ERR(supplemental);

	ep->ob_window = devm_kcalloc(dev, RZT2H_PCI_MAX_RESOURCES,
				     sizeof(*window), GFP_KERNEL);

	if (!ep->ob_window)
		return -ENOMEM;

	rzt2h_pcie_parse_outbound_ranges(ep, pdev);


	err = of_property_read_u8(dev->of_node, "max-functions",
				  &ep->max_functions);

	if (err < 0 || ep->max_functions > RZT2H_EPC_MAX_FUNCTIONS)
		ep->max_functions = RZT2H_EPC_MAX_FUNCTIONS;

	i = irq_of_parse_and_map(dev->of_node, 0);
	if (!i) {
		dev_err(dev, "cannot get platform resources for msi interrupt\n");
		err = -ENOENT;
	}

	return 0;
}

static int rzt2h_pcie_ep_write_header(struct pci_epc *epc, u8 fn, u8 vfn,
				     struct pci_epf_header *hdr)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzt2h_pcie *pcie = &ep->pcie;
	u32 val;

	fn = 1;

	val = hdr->vendorid;
	val |= hdr->deviceid << 16;
	rzt2h_write_conf_ep(pcie, val, PCI_EP_VID_F, fn);


	val = hdr->revid;
	val |= hdr->progif_code << 8;
	val |= hdr->subclass_code << 16;
	val |= hdr->baseclass_code << 24;

	rzt2h_write_conf_ep(pcie, val, PCI_EP_RID_CC_F, fn);

	if (!fn)
		val = hdr->subsys_vendor_id;
	else
		val = rzt2h_read_conf_ep(pcie, PCI_EP_SUBSID_F, fn);
	val |= hdr->subsys_id << 16;
	rzt2h_write_conf_ep(pcie, val, PCI_EP_SUBSID_F, fn);

	if (hdr->interrupt_pin > PCI_INTERRUPT_INTA)
		return -EINVAL;
	val = rzt2h_read_conf_ep(pcie, PCI_EP_INTERRUPT_F, fn);
	val |= (hdr->interrupt_pin << 8);
	rzt2h_write_conf_ep(pcie, val, PCI_EP_INTERRUPT_F, fn);

	return 0;
}

static int rzt2h_pcie_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				struct pci_epf_bar *epf_bar)
{
	int flags = epf_bar->flags | LAR_ENABLE | LAM_64BIT;
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	u64 size = 1ULL << fls64(epf_bar->size - 1);
	dma_addr_t cpu_addr = epf_bar->phys_addr;
	enum pci_barno bar = epf_bar->barno;
	struct rzt2h_pcie *pcie = &ep->pcie;
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
	mask &= ~0xf;

	rzt2h_pcie_ep_set_inbound(pcie, cpu_addr,
			      0x0, mask | flags, idx, false);

	return 0;
}

static void rzt2h_pcie_ep_clear_bar(struct pci_epc *epc, u8 fn, u8 vfn,
				   struct pci_epf_bar *epf_bar)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

	rzt2h_pcie_ep_set_inbound(&ep->pcie, 0x0, 0x0, 0x0, bar, false);

	clear_bit(atu_index, ep->ib_window_map);
	clear_bit(atu_index + 1, ep->ib_window_map);
}

static int rzt2h_pcie_ep_set_msi(struct pci_epc *epc, u8 fn, u8 vfn, u8 interrupts)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzt2h_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rzt2h_read_conf(pcie, PCI_EP_MSICAP(fn));
	flags |= interrupts << MSICAP0_MMESCAP_OFFSET;
	rzt2h_write_conf(pcie, flags, PCI_EP_MSICAP(fn));

	return 0;
}

static int rzt2h_pcie_ep_get_msi(struct pci_epc *epc, u8 fn, u8 vfn)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzt2h_pcie *pcie = &ep->pcie;
	u32 flags;

	flags = rzt2h_read_conf(pcie, PCI_EP_MSICAP(fn));
	if (!(flags & MSICAP0_MSIE))
		return -EINVAL;

	return ((flags & MSICAP0_MMESE_MASK) >> MSICAP0_MMESE_OFFSET);
}

static int rzt2h_pcie_ep_map_addr(struct pci_epc *epc, u8 fn, u8 vfn,
				 phys_addr_t addr, u64 pci_addr, size_t size)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
	struct rzt2h_pcie *pcie = &ep->pcie;
	struct resource_entry win;
	struct resource res;
	int window;

	window = rzt2h_pcie_ep_get_window(ep, addr);
	if (window < 0) {
		dev_err(pcie->dev, "failed to get corresponding window\n");
		return -EINVAL;
	}

	memset(&win, 0x0, sizeof(win));
	memset(&res, 0x0, sizeof(res));
	res.start = pci_addr;
	res.end = pci_addr + size - 1;
	res.flags = IORESOURCE_MEM;
	win.res = &res;

	rzt2h_pcie_ep_set_outbound(pcie, window, &win, addr, pci_addr, size);

	ep->ob_mapped_addr[window] = addr;

	return 0;
}

static void rzt2h_pcie_ep_unmap_addr(struct pci_epc *epc, u8 fn, u8 vfn,
				    phys_addr_t addr)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);
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
	rzt2h_pcie_ep_set_outbound(&ep->pcie, idx, &win, 0x0, 0x0, 0x0);

	ep->ob_mapped_addr[idx] = 0;
}

static int rzt2h_pcie_ep_assert_intx(struct rzt2h_pcie_endpoint *ep,
				    u8 fn, u8 intx)
{
	struct rzt2h_pcie *pcie = &ep->pcie;
	u32 val;

	/* Check MSI enable bit */
	val = rzt2h_read_conf_ep(pcie, PCI_EP_MSICAP(0), fn);
	if ((val & MSICAP0_MSIE)) {
		dev_err(pcie->dev, "MSI is enabled, cannot assert INTx\n");
		return -EINVAL;
	}

	val = rzt2h_pci_read_reg(pcie, PCI_INTX_RCV_INTERRUPT_ENABLE_REG);
	if ((val & INTX_RECEIVE_INTERRUPT_ENABLE)) {
		dev_err(pcie->dev, "INTx is already asserted\n");
		return -EINVAL;
	}

	rzt2h_rmw(pcie, PCI_INTX_RCV_INTERRUPT_ENABLE_REG,
					INTX_RECEIVE_INTERRUPT_ENABLE,
					INTX_RECEIVE_INTERRUPT_ENABLE);
	writel(0x01, supplemental + 0x0);
	writel(0x00, supplemental + 0x0);

	return 0;
}

static int rzt2h_pcie_ep_assert_msi(struct rzt2h_pcie *pcie,
				   u8 fn, u8 interrupt_num)
{
	u32 val, ret;

	fn = 1;

	writel(0x0, supplemental + 0x04);
	writel(0x0, supplemental + 0x00);

	/* Check MSI enable bit */
	val = rzt2h_read_conf_ep(pcie, PCI_EP_MSICAP(0), fn);
	if (!(val & MSICAP0_MSIE)) {
		dev_err(pcie->dev, "MSI is not enabled, cannot assert MSI\n");
		return -EINVAL;
	}

	writel(0xF, supplemental + 0x10);
	writel(0xF, supplemental + 0x08);
	writel(0xF, supplemental + 0x04);

	ret = rzt2h_read_conf_ep(pcie, PCI_EP_MSICAP(0), 1);
	ret |= MSICAP0_MSIE;
	rzt2h_write_conf_ep(pcie, ret, PCI_EP_MSICAP(0), 1);

	val = rzt2h_pci_read_reg(pcie, PCI_INTX_RCV_INTERRUPT_ENABLE_REG);
	if ((val & MSI_RECEIVE_INTERRUPT_ENABLE)) {
		dev_err(pcie->dev, "MSI is already asserted\n");
		return -EINVAL;
	}

	rzt2h_rmw(pcie, PCI_INTX_RCV_INTERRUPT_ENABLE_REG,
					MSI_RECEIVE_INTERRUPT_ENABLE,
					MSI_RECEIVE_INTERRUPT_ENABLE);
	return 0;
}

static int rzt2h_pcie_ep_raise_irq(struct pci_epc *epc, u8 fn, u8 vfn,
				  unsigned int type, u16 interrupt_num)
{
	struct rzt2h_pcie_endpoint *ep = epc_get_drvdata(epc);

	switch (type) {
	case PCI_IRQ_INTX:
		return rzt2h_pcie_ep_assert_intx(ep, fn, 0);

	case PCI_IRQ_MSI:
		return rzt2h_pcie_ep_assert_msi(&ep->pcie, fn, interrupt_num);

	default:
		return -EINVAL;
	}
}

static int rzt2h_pcie_ep_start(struct pci_epc *epc)
{
	return 0;
}

static const struct pci_epc_features rzt2h_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	/* use 64-bit BARs so mark BAR[1,3,5] as reserved */
	.bar[BAR_0] = { .type = BAR_FIXED, .fixed_size = 128,
			.only_64bit = true, },
	.bar[BAR_1] = { .type = BAR_RESERVED, },
	.bar[BAR_2] = { .type = BAR_FIXED, .fixed_size = 256,
			.only_64bit = true, },
	.bar[BAR_3] = { .type = BAR_RESERVED, },
	.bar[BAR_4] = { .type = BAR_FIXED, .fixed_size = 256,
			.only_64bit = true, },
	.bar[BAR_5] = { .type = BAR_RESERVED, },
};

static const struct pci_epc_features*
rzt2h_pcie_ep_get_features(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	return &rzt2h_pcie_epc_features;
}

static const struct pci_epc_ops rzt2h_pcie_epc_ops = {
	.write_header	= rzt2h_pcie_ep_write_header,
	.set_bar	= rzt2h_pcie_ep_set_bar,
	.clear_bar	= rzt2h_pcie_ep_clear_bar,
	.set_msi	= rzt2h_pcie_ep_set_msi,
	.get_msi	= rzt2h_pcie_ep_get_msi,
	.map_addr	= rzt2h_pcie_ep_map_addr,
	.unmap_addr	= rzt2h_pcie_ep_unmap_addr,
	.raise_irq	= rzt2h_pcie_ep_raise_irq,
	.start		= rzt2h_pcie_ep_start,
	.get_features	= rzt2h_pcie_ep_get_features,
};

static const struct of_device_id rzt2h_pcie_ep_of_match[] = {
	{ .compatible = "renesas,rzt2h-pcie-ep", },
	{ .compatible = "renesas,rzn2h-pcie-ep", },
	{},
	{ },
};

static int rzt2h_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzt2h_pcie_endpoint *ep;
	struct rzt2h_pcie *pcie;
	struct pci_epc *epc;
	int err;

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

	err = rzt2h_pcie_ep_get_pdata(ep, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		goto err_pm_put;
	}

	writel(0xfa00f0, pcie->base + PCI_EP_PCMSET1);
	writel(MODE_PORT_EP, supplemental + PCIE_MODE);

	ep->num_ib_windows = MAX_NR_INBOUND_MAPS;
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

	epc = devm_pci_epc_create(dev, &rzt2h_pcie_epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "failed to create epc device\n");
		err = PTR_ERR(epc);
		goto err_pm_put;
	}

	epc->max_functions = ep->max_functions;
	epc_set_drvdata(epc, ep);

	rzt2h_pcie_hw_init_ep(pcie);

	rzt2h_pcie_setting_config_ep(pcie, 1);

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

static struct platform_driver rzt2h_pcie_ep_driver = {
	.driver = {
		.name = "rzt2h-pcie-ep",
		.of_match_table = rzt2h_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rzt2h_pcie_ep_probe,
};
builtin_platform_driver(rzt2h_pcie_ep_driver);

// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas RZ/G3S Series SoCs
 * Copyright (C) 2023 Renesas Electronics Corp.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include <uapi/linux/psci.h>
#include <linux/arm-smccc.h>

#include "pcie-rzg3s.h"

struct rzg3s_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	struct msi_controller chip;
	unsigned long pages;
	unsigned long virt_pages;
	struct mutex lock;
	int irq;
};

static u32 r_configuration_space[] = {
	0x00000004,
	0x00000000,
	0xfff0fff0,
	0x48035001
};

static u32 r_msi_capability[] = {
	0x01807005,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000
};

static u32 r_msi_and_msix_capability[] = {
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000
};

static u32 r_virtual_channel_enhanced_capability_header[] = {
	0x00010002,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x800000FF,
	0x00020000,
	0x00020000
};

static u32 r_device_serial_number_capability[] = {
	0x1B010003,
	0x00000000,
	0x00000000
};


static inline struct rzg3s_msi *to_rzg3s_msi(struct msi_controller *chip)
{
	return container_of(chip, struct rzg3s_msi, chip);
}

/* Structure representing the PCIe interface */
struct rzg3s_pcie_host {
	struct rzg3s_pcie	pcie;
	struct device		*dev;
	struct phy		*phy;
	void __iomem		*base;
	struct clk		*bus_clk;
	struct			rzg3s_msi msi;
	int			(*phy_init_fn)(struct rzg3s_pcie_host *host);
	struct irq_domain	*intx_domain;
	struct reset_control    *rst;
	int			channel;
};

static int rzg3s_pcie_hw_init(struct rzg3s_pcie *pcie, int channel);

static int rzg3s_pcie_request_issue(struct rzg3s_pcie *pcie, struct pci_bus *bus)
{
	int i;
	u32 sts;

	rzg3s_rmw(pcie, REQUEST_ISSUE_REG, REQ_ISSUE, REQ_ISSUE);
	for (i = 0; i < STS_CHECK_LOOP; i++) {
		sts = rzg3s_pci_read_reg(pcie, REQUEST_ISSUE_REG);
		if (!(sts & REQ_ISSUE))
			break;

		udelay(5);
	}

	if (sts & MOR_STATUS) {
		dev_info(&bus->dev, "rzg3s_pcie_conf_access: Request failed(%d)\n", ((sts & MOR_STATUS)>>16));

		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int rzg3s_pcie_read_config_access(struct rzg3s_pcie_host *host,
		struct pci_bus *bus, unsigned int devfn, int where, u32 *data)
{
	struct rzg3s_pcie *pcie = &host->pcie;
	unsigned int dev, func, reg, ret;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);
	reg = where & ~3;


	/*
	 * While each channel has its own memory-mapped extended config
	 * space, it's generally only accessible when in endpoint mode.
	 * When in root complex mode, the controller is unable to target
	 * itself with either type 0 or type 1 accesses, and indeed, any
	 * controller initiated target transfer to its own config space
	 * result in a completer abort.
	 *
	 * Each channel effectively only supports a single device, but as
	 * the same channel <-> device access works for any PCI_SLOT()
	 * value, we cheat a bit here and bind the controller's config
	 * space to devfn 0 in order to enable self-enumeration. In this
	 * case the regular ECAR/ECDR path is sidelined and the mangled
	 * config access itself is initiated as an internal bus transaction.
	 */
	if (pci_is_root_bus(bus) && (devfn == 0)) {
		if (dev != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;

		if (reg == 0x10) {
			*data = r_configuration_space[0];
		} else if (reg == 0x14) {
			*data = r_configuration_space[1];
		} else if (reg == 0x20) {
			*data = r_configuration_space[2];
		} else if (reg == 0x40) {
			*data = r_configuration_space[3];
		} else if ((reg >= 0x50) && (reg <= 0x64)) {
			//MSI Capability register
			*data = r_msi_capability[(reg-0x50)/4];
		} else if ((reg >= 0x70) && (reg <= 0xA8)) {
			//PCI Express Capability register
			*data = rzg3s_read_conf(pcie, reg - 0x10);
		} else if ((reg >= 0xE0) && (reg <= 0xFC)) {
			//MSI-X Capability register
			*data = r_msi_and_msix_capability[(reg - 0xE0)/4];
		} else if ((reg >= 0x100) && (reg <= 0x118)) {
			*data = r_virtual_channel_enhanced_capability_header[(reg - 0x100) / 4];
		} else if ((reg >= 0x1B0) && (reg <= 0x1B8)) {
			*data = r_device_serial_number_capability[(reg - 0x1B0) / 4];
		} else {
			*data = rzg3s_read_conf(pcie, reg);
		}

		return PCIBIOS_SUCCESSFUL;
	}

	reg &= 0x0FFC;

	if (bus->number == 1) {
		/* Type 0 */
		rzg3s_pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_FUNC(func) | reg, REQUEST_ADDR_1_REG);
		rzg3s_pci_write_reg(pcie, TR_TYPE_CFREAD_TP0, REQUEST_ISSUE_REG);
	} else {
		/* Type 1 */
		rzg3s_pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_DEV(dev) | PCIE_CONF_FUNC(func) | reg, REQUEST_ADDR_1_REG);
		rzg3s_pci_write_reg(pcie, TR_TYPE_CFREAD_TP1, REQUEST_ISSUE_REG);
	}

	ret = rzg3s_pcie_request_issue(pcie, bus);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	*data = rzg3s_pci_read_reg(pcie, REQUEST_RCV_DATA_REG);

	return PCIBIOS_SUCCESSFUL;
}

static int rzg3s_pcie_write_config_access(struct rzg3s_pcie_host *host,
		struct pci_bus *bus, unsigned int devfn, int where, u32 data)
{
	struct rzg3s_pcie *pcie = &host->pcie;
	unsigned int dev, func, reg;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);
	reg = where & ~3;
	/*
	 * While each channel has its own memory-mapped extended config
	 * space, it's generally only accessible when in endpoint mode.
	 * When in root complex mode, the controller is unable to target
	 * itself with either type 0 or type 1 accesses, and indeed, any
	 * controller initiated target transfer to its own config space
	 * result in a completer abort.
	 *
	 * Each channel effectively only supports a single device, but as
	 * the same channel <-> device access works for any PCI_SLOT()
	 * value, we cheat a bit here and bind the controller's config
	 * space to devfn 0 in order to enable self-enumeration. In this
	 * case the regular ECAR/ECDR path is sidelined and the mangled
	 * config access itself is initiated as an internal bus transaction.
	 */
	if (pci_is_root_bus(bus) && (devfn == 0)) {
		if (dev != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;

		if (reg == 0x20) {
			r_configuration_space[2] = data;
		} else if (reg == 0x40) {
			r_configuration_space[3] = data;
		} else if ((reg >= 0x50) && (reg <= 0x64)) {
			//MSI capability register
			r_msi_capability[(reg-0x50)/4] = data;
		} else if ((reg >= 0x70) && (reg <= 0xA8)) {
			//PCI Express Capability register
			rzg3s_write_conf(pcie, data, reg-0x10);
		} else if ((reg >= 0xE0) && (reg <= 0xFC)) {
			//MSI-X capability register
			r_msi_and_msix_capability[(reg - 0xE0) / 4] = data;
		} else if ((reg >= 0x100) && (reg <= 0x118)) {
			r_virtual_channel_enhanced_capability_header[(reg - 0x100) / 4] = data;
		} else if ((reg >= 0x1B0) && (reg <= 0x1B8)) {
			r_device_serial_number_capability[(reg - 0x1B0) / 4] = data;
		} else {
			rzg3s_write_conf(pcie, data, reg);
		}

		return PCIBIOS_SUCCESSFUL;
	}

	reg &= 0x0FFC;

	rzg3s_pci_write_reg(pcie, 0, REQUEST_DATA_REG(0));
	rzg3s_pci_write_reg(pcie, 0, REQUEST_DATA_REG(1));
	rzg3s_pci_write_reg(pcie, data, REQUEST_DATA_REG(2));

	if (bus->number == 1) {
		/* Type 0 */
		rzg3s_pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_FUNC(func) | reg, REQUEST_ADDR_1_REG);
		rzg3s_pci_write_reg(pcie, TR_TYPE_CFWRITE_TP0, REQUEST_ISSUE_REG);
	} else {
		/* Type 1 */
		rzg3s_pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) |
			PCIE_CONF_DEV(dev) | PCIE_CONF_FUNC(func) | reg, REQUEST_ADDR_1_REG);
		rzg3s_pci_write_reg(pcie, TR_TYPE_CFWRITE_TP1, REQUEST_ISSUE_REG);
	}

	return rzg3s_pcie_request_issue(pcie, bus);
}

static int rzg3s_pcie_read_conf(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct rzg3s_pcie_host *host = bus->sysdata;
	int ret;

	if ((bus->number == 0) && (devfn >= 0x08) && (where == 0x0))
		return PCIBIOS_DEVICE_NOT_FOUND;

	ret = rzg3s_pcie_read_config_access(host, bus, devfn, where, val);
	if (ret != PCIBIOS_SUCCESSFUL) {
		*val = 0xffffffff;
		return ret;
	}

	if (size == 1)
		*val = (*val >> (BITS_PER_BYTE * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (BITS_PER_BYTE * (where & 2))) & 0xffff;

	dev_dbg(&bus->dev, "pcie-config-read: bus=%3d devfn=0x%04x where=0x%04x size=%d val=0x%08x\n",
		bus->number, devfn, where, size, *val);

	return ret;
}

static int rzg3s_pcie_write_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	struct rzg3s_pcie_host *host = bus->sysdata;
	unsigned int shift;
	u32 data;
	int ret;

	ret = rzg3s_pcie_read_config_access(host, bus, devfn, where, &data);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	dev_dbg(&bus->dev, "pcie-config-write: bus=%3d devfn=0x%04x where=0x%04x size=%d val=0x%08x\n",
		bus->number, devfn, where, size, val);

	if (size == 1) {
		shift = BITS_PER_BYTE * (where & 3);
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
	} else if (size == 2) {
		shift = BITS_PER_BYTE * (where & 2);
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
	} else
		data = val;

	ret = rzg3s_pcie_write_config_access(host, bus, devfn, where, data);

	return ret;
}

static struct pci_ops rzg3s_pcie_ops = {
	.read	= rzg3s_pcie_read_conf,
	.write	= rzg3s_pcie_write_conf,
};

static int rzg3s_pcie_set_max_link_speed(struct rzg3s_pcie *pcie)
{
	unsigned int timeout = 1000;
	u32 lcs_reg, lcs2_reg, cs2_reg, ctr2_reg;

	lcs_reg = rzg3s_pci_read_reg(pcie, PCIE_LINK_CTRL_STATUS_REG);
	cs2_reg = rzg3s_pci_read_reg(pcie, PCIE_CORE_STATUS_2_REG);

	/*
	 * just return if link speed has reached the maximum (5.0 GT/s) or
	 * the opposing devices don't support it
	 */
	if ((lcs_reg & CURRENT_LINK_SPEED_5_0_GTS) ||
	    !(cs2_reg & LINK_SPEED_SUPPORT_5_0_GTS))
		return 0;

	/* set target Link speed to 5.0 GT/s */
	lcs2_reg = rzg3s_pci_read_reg(pcie, PCIE_LINK_CTRL_STATUS_2_REG);
	rzg3s_pci_write_reg(pcie, (lcs2_reg & (~LINKCS2_TARGET_LINK_SPEED_MASK)) |
			    LINKCS2_TARGET_LINK_SPEED_5_0_GTS,
			    PCIE_LINK_CTRL_STATUS_2_REG);

	/* request link speed change */
	while (timeout--) {
		ctr2_reg = rzg3s_pci_read_reg(pcie, PCIE_CORE_CONTROL_2_REG);
		rzg3s_pci_write_reg(pcie, ctr2_reg | UI_LINK_SPEED_CHANGE_REQ |
				    UI_LINK_SPEED_CHANGE_5_0_GTS,
				    PCIE_CORE_CONTROL_2_REG);
		udelay(100);
		/* Check completion of speeding up */
		cs2_reg = rzg3s_pci_read_reg(pcie, PCIE_CORE_STATUS_2_REG);
		if (cs2_reg & UI_LINK_SPEED_CHANGE_DONE)
			return 0;
	}

	/* timed out */
	return -1;
}

static void rzg3s_pcie_hw_enable(struct rzg3s_pcie_host *host)
{
	struct rzg3s_pcie *pcie = &host->pcie;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(host);
	struct resource_entry *win;
	LIST_HEAD(res);
	int i = 0;

	/* Try to set maximum supported link speed (5.0 GT/s) */
	if (rzg3s_pcie_set_max_link_speed(pcie) != 0)
		dev_err(pcie->dev, "fail to set link speed to 5.0 GT/s\n");

	/* Setup PCI resources */
	resource_list_for_each_entry(win, &bridge->windows) {
		struct resource *res = win->res;

		if (!res->flags)
			continue;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
		case IORESOURCE_MEM:
			rzg3s_pcie_set_outbound(pcie, i, win);
			i++;
			break;
		}
	}
}

static int rzg3s_pcie_enable(struct rzg3s_pcie_host *host)
{
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(host);

	rzg3s_pcie_hw_enable(host);

	pci_add_flags(PCI_REASSIGN_ALL_BUS);

	bridge->sysdata = host;
	bridge->ops = &rzg3s_pcie_ops;
	if (IS_ENABLED(CONFIG_PCI_MSI))
		bridge->msi = &host->msi.chip;

	return pci_host_probe(bridge);
}

static void rzg3s_pcie_setting_config(struct rzg3s_pcie *pcie)
{
	rzg3s_pci_write_reg(pcie, RESET_CONFIG_DEASSERT, PCI_RC_RESET_REG);

	// Configuration space(Root complex) setting
	// Vendor and Device ID      : PCI Express Configuration Registers Adr 6000h
	rzg3s_write_conf(pcie,
			((PCIE_CONF_DEVICE_ID << 16) |
			  (PCIE_CONF_VENDOR_ID)),
			PCI_RC_VID_ADR);

	// Revision ID and Class Code : PCI Express Configuration Registers Adr 6008h
	rzg3s_write_conf(pcie,
			((PCIE_CONF_BASE_CLASS << 24) |
			  (PCIE_CONF_SUB_CLASS << 16) |
			  (PCIE_CONF_PROGRAMING_IF << 8) |
			  (PCIE_CONF_REVISION_ID)),
			PCI_RC_RID_CC_ADR);

	rzg3s_write_conf(pcie,
			((PCIE_CONF_SUBORDINATE_BUS << 16) |
			  (PCIE_CONF_SECOUNDARY_BUS << 8) |
			  (PCIE_CONF_PRIMARY_BUS)),
			PCI_PRIMARY_BUS);

	rzg3s_write_conf(pcie,
			((PCIE_CONF_MEMORY_LIMIT << 16) |
			  (PCIE_CONF_MEMORY_BASE)),
			PCI_MEMORY_BASE);

	rzg3s_write_conf(pcie, PCIE_CONF_BAR0_MASK_LO, PCIE_CONF_OFFSET_BAR0_MASK_LO);
	rzg3s_write_conf(pcie, PCIE_CONF_BAR0_MASK_UP, PCIE_CONF_OFFSET_BAR0_MASK_UP);
}

static int PCIE_CFG_Initialize(struct rzg3s_pcie *pcie)
{
	// Vendor and Device ID: PCI Express Configuration Registers Adr 6000h
	rzg3s_write_conf(pcie,
			((PCIE_CONF_DEVICE_ID << 16) |
			  (PCIE_CONF_VENDOR_ID)),
			PCI_RC_VID_ADR);

	// Revision ID and Class Code: PCI Express Configuration Registers Adr 6008h
	rzg3s_write_conf(pcie,
			((PCIE_CONF_BASE_CLASS << 24) |
			  (PCIE_CONF_SUB_CLASS << 16) |
			  (PCIE_CONF_PROGRAMING_IF << 8) |
			  (PCIE_CONF_REVISION_ID)),
			PCI_RC_RID_CC_ADR);

	// Base Address Register Mask00 (Lower) (Function #1) : PCI Express Configuration Registers Adr 60A0h
	rzg3s_write_conf(pcie, BASEADR_MKL_ALLM, PCI_RC_BARMSK00L_ADR);

	// Base Address Register Mask00 (upper) (Function #1) : PCI Express Configuration Registers Adr 60A4h
	rzg3s_write_conf(pcie, BASEADR_MKU_ALLM, PCI_RC_BARMSK00U_ADR);

	// Base Size 00/01 : PCI Express Configuration Registers Adr 60C8h
	rzg3s_write_conf(pcie, BASESZ_INIT, PCI_RC_BSIZE00_01_ADR);

	// Bus Number : PCI Express Configuration Registers Adr 6018h
	rzg3s_write_conf(pcie,
			((PCIE_CONF_SUBORDINATE_BUS << 16) |
			  (PCIE_CONF_SECOUNDARY_BUS << 8) |
			  (PCIE_CONF_PRIMARY_BUS)),
			PCI_PRIMARY_BUS);

	rzg3s_write_conf(pcie,
			((PCIE_CONF_MEMORY_LIMIT << 16) |
			  (PCIE_CONF_MEMORY_BASE)),
			PCI_MEMORY_BASE);

	rzg3s_write_conf(pcie, PM_CAPABILITIES_INIT, PCI_PM_CAPABILITIES);

	return 0;
}

static int PCIE_INT_Initialize(struct rzg3s_pcie *pcie)
{
	/* Clear Event Interrupt Status 0 */
	rzg3s_pci_write_reg(pcie, INT_ST0_CLR, PCI_RC_PEIS0_REG);		/* Set PCI_RC 0204h */

	/* Set Event Interrupt Enable 0 */
	rzg3s_pci_write_reg(pcie, INT_EN0_SET, PCI_RC_PEIE0_REG);		/* Set PCI_RC 0200h */

	/* Clear  Event Interrupt Status 1 */
	rzg3s_pci_write_reg(pcie, INT_ST1_CLR, PCI_RC_PEIS1_REG);		/* Set PCI_RC 020ch */

	/* Set Event Interrupt Enable 1 */
	rzg3s_pci_write_reg(pcie, INT_EN1_SET, PCI_RC_PEIE1_REG);		/* Set PCI_RC 0208h */

	/* Clear AXI Master Error Interrupt Status */
	rzg3s_pci_write_reg(pcie, INT_ST_AXIM_CLR, PCI_RC_AMEIS_REG);	/* Set PCI_RC 0214h */

	/* Set AXI Master Error Interrupt Enable */
	rzg3s_pci_write_reg(pcie, INT_EN_AXIM_SET, PCI_RC_AMEIE_REG);	/* Set PCI_RC 0210h */

	/* Clear AXI Slave Error Interrupt Status */
	rzg3s_pci_write_reg(pcie, INT_ST_AXIS_CLR, PCI_RC_ASEIS1_REG);	/* Set PCI_RC 0224h */

	/* Set AXI Slave Error Interrupt Enable */
	rzg3s_pci_write_reg(pcie, INT_EN_AXIS_SET, PCI_RC_ASEIE1_REG);	/* Set PCI_RC 0220h */

	/* Clear Message Receive Interrupt Status */
	rzg3s_pci_write_reg(pcie, INT_MR_CLR, PCI_RC_MSGRCVIS_REG);		/* Set PCI_RC 0124h */

	/* Set Message Receive Interrupt Enable */
	rzg3s_pci_write_reg(pcie, INT_MR_SET, PCI_RC_MSGRCVIE_REG);		/* Set PCI_RC 0120h */

	return 0;
}

static int rzg3s_pcie_hw_init(struct rzg3s_pcie *pcie, int channel)
{
	unsigned int timeout = 50;
	struct arm_smccc_res local_res;

	/* Set RST_RSM_B after PCIe power is applied according to HW manual */
	arm_smccc_smc(RZ_SIP_SVC_SET_PCIE_RST_RSMB, 0xd74, 0x1, 0, 0, 0, 0, 0, &local_res);

	/* Clear all PCIe reset bits */
	rzg3s_pci_write_reg(pcie, RESET_ALL_ASSERT, PCI_RC_RESET_REG);
	udelay(100);

	/* Release RST_CFG_B, RST_LOAD_B, RST_OUT_B */
	rzg3s_pci_write_reg(pcie, RST_CFG_B | RST_LOAD_B | RST_OUT_B, PCI_RC_RESET_REG);
	udelay(100);

	/* Setting of HWINT related registers */
	PCIE_CFG_Initialize(pcie);

	/* Set Interrupt settings */
	PCIE_INT_Initialize(pcie);

	/* Release RST_PS_B, RST_GP_B, RST_B */
	rzg3s_pci_write_reg(pcie, RST_OUT_B | RST_PS_B | RST_LOAD_B | RST_CFG_B | RST_GP_B | RST_B, PCI_RC_RESET_REG);
	udelay(100);

	/* Release all */
	rzg3s_pci_write_reg(pcie, RESET_ALL_DEASSERT, PCI_RC_RESET_REG);

	/* This will timeout if we don't have a link. */
	while (timeout--) {
		if (!(rzg3s_pci_read_reg(pcie, PCIE_CORE_STATUS_1_REG) & DL_DOWN_STATUS))
			return 0;

		msleep(5);
	}

	return -ETIMEDOUT;
}

/* INTx Functions */

/**
 * rzg3s_pcie_intx_map - Set the handler for the INTx and mark IRQ as valid
 * @domain: IRQ domain
 * @irq: Virtual IRQ number
 * @hwirq: HW interrupt number
 *
 * Return: Always returns 0.
 */

static int rzg3s_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

/* INTx IRQ Domain operations */
static const struct irq_domain_ops intx_domain_ops = {
	.map = rzg3s_pcie_intx_map,
};

static int rzg3s_msi_alloc(struct rzg3s_msi *chip)
{
	int msi;

	mutex_lock(&chip->lock);

	msi = find_first_zero_bit(chip->used, INT_PCI_MSI_NR);
	if (msi < INT_PCI_MSI_NR)
		set_bit(msi, chip->used);
	else
		msi = -ENOSPC;

	mutex_unlock(&chip->lock);

	return msi;
}

static int rzg3s_msi_alloc_region(struct rzg3s_msi *chip, int no_irqs)
{
	int msi;

	mutex_lock(&chip->lock);
	msi = bitmap_find_free_region(chip->used, INT_PCI_MSI_NR,
				      order_base_2(no_irqs));
	mutex_unlock(&chip->lock);

	return msi;
}

static void rzg3s_msi_free(struct rzg3s_msi *chip, unsigned long irq)
{
	mutex_lock(&chip->lock);
	clear_bit(irq, chip->used);
	mutex_unlock(&chip->lock);
}

static irqreturn_t rzg3s_pcie_msi_irq(int irq, void *data)
{
	struct rzg3s_pcie_host *host = data;
	struct rzg3s_pcie *pcie = &host->pcie;
	struct rzg3s_msi *msi = &host->msi;
	unsigned long reg, msi_stat;

	reg = rzg3s_pci_read_reg(pcie, PCI_INTX_RCV_INTERRUPT_STATUS_REG);

	/* Clear INTx/MSI Interrupts Status */
	rzg3s_pci_write_reg(pcie, ALL_RECEIVE_INTERRUPT_STATUS, PCI_INTX_RCV_INTERRUPT_STATUS_REG);
	/* Clear Message Receive Interrupt Status */
	rzg3s_pci_write_reg(pcie, INT_MR_CLR, PCI_RC_MSGRCVIS_REG);

	// MSI Only
	if (!(reg & MSI_RECEIVE_INTERRUPT_STATUS))
		return IRQ_NONE;

	msi_stat = rzg3s_pci_read_reg(pcie, PCI_RC_MSIRCVSTAT(0));

	while (msi_stat) {
		unsigned int index = find_first_bit(&msi_stat, 32);
		unsigned int msi_irq;

		rzg3s_pci_write_reg(pcie, 1 << index, PCI_RC_MSIRCVSTAT(0));
		msi_irq = irq_find_mapping(msi->domain, index);
		if (msi_irq) {
			if (test_bit(index, msi->used))
				generic_handle_irq(msi_irq);
			else
				dev_info(pcie->dev, "unhandled MSI\n");
		} else {
			/* Unknown MSI, just clear it */
			dev_dbg(pcie->dev, "unexpected MSI\n");
		}

		/* see if there's any more pending in this vector */
		msi_stat = rzg3s_pci_read_reg(pcie, PCI_RC_MSIRCVSTAT(0));
        }

        return IRQ_HANDLED;
}

static int rzg3s_msi_setup_irq(struct msi_controller *chip, struct pci_dev *pdev,
			      struct msi_desc *desc)
{
	struct rzg3s_msi *msi = to_rzg3s_msi(chip);
	struct rzg3s_pcie_host *host = container_of(chip, struct rzg3s_pcie_host,
						   msi.chip);
	struct rzg3s_pcie *pcie = &host->pcie;
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;

	hwirq = rzg3s_msi_alloc(msi);
	if (hwirq < 0)
		return hwirq;

	irq = irq_find_mapping(msi->domain, hwirq);
	if (!irq) {
		rzg3s_msi_free(msi, hwirq);
		return -EINVAL;
	}

	irq_set_msi_desc(irq, desc);

	msg.address_lo = rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_ADDRL_REG) & (~PCIE_WINDOW_ENABLE);
	msg.address_hi = rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_ADDRU_REG);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static int rzg3s_msi_setup_irqs(struct msi_controller *chip,
			       struct pci_dev *pdev, int nvec, int type)
{
	struct rzg3s_msi *msi = to_rzg3s_msi(chip);
	struct rzg3s_pcie_host *host = container_of(chip, struct rzg3s_pcie_host,
						   msi.chip);
	struct rzg3s_pcie *pcie = &host->pcie;
	struct msi_desc *desc;
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;
	int i;

	/* MSI-X interrupts are not supported */
	if (type == PCI_CAP_ID_MSIX)
		return -EINVAL;

	WARN_ON(!list_is_singular(&pdev->dev.msi_list));
	desc = list_entry(pdev->dev.msi_list.next, struct msi_desc, list);

	hwirq = rzg3s_msi_alloc_region(msi, nvec);
	if (hwirq < 0)
		return -ENOSPC;

	irq = irq_find_mapping(msi->domain, hwirq);
	if (!irq)
		return -ENOSPC;

	for (i = 0; i < nvec; i++) {
		/*
		 * irq_create_mapping() called from rzg3s_pcie_probe() pre-
		 * allocates descs,  so there is no need to allocate descs here.
		 * We can therefore assume that if irq_find_mapping() above
		 * returns non-zero, then the descs are also successfully
		 * allocated.
		 */
		if (irq_set_msi_desc_off(irq, i, desc)) {
			/* TODO: clear */
			return -EINVAL;
		}
	}

	desc->nvec_used = nvec;
	desc->msi_attrib.multiple = order_base_2(nvec);

	msg.address_lo = rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_ADDRL_REG) & (~PCIE_WINDOW_ENABLE);
	msg.address_hi = rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_ADDRU_REG);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static void rzg3s_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct rzg3s_msi *msi = to_rzg3s_msi(chip);
	struct irq_data *d = irq_get_irq_data(irq);

	rzg3s_msi_free(msi, d->hwirq);
}

static struct irq_chip rzg3s_msi_irq_chip = {
	.name = "RZG3S PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static int rzg3s_msi_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &rzg3s_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = rzg3s_msi_map,
};

static void rzg3s_pcie_unmap_msi(struct rzg3s_pcie_host *host)
{
	struct rzg3s_msi *msi = &host->msi;
	int i, irq;

	for (i = 0; i < INT_PCI_MSI_NR; i++) {
		irq = irq_find_mapping(msi->domain, i);
		if (irq > 0)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(msi->domain);
}

static void rzg3s_pcie_hw_enable_msi(struct rzg3s_pcie_host *host)
{
	struct rzg3s_pcie *pcie = &host->pcie;
	struct device *dev = pcie->dev;
	struct rzg3s_msi *msi = &host->msi;
	unsigned long base;
	unsigned long pci_base;
	unsigned long msi_base;
	unsigned long msi_base_mask;
	int idx;

	msi->pages = __get_free_pages(GFP_KERNEL | GFP_DMA, 0);
	base = dma_map_single(pcie->dev, (void *)msi->pages, (MSI_RCV_WINDOW_MASK_MIN+1), DMA_BIDIRECTIONAL);

	msi_base = 0;
	for (idx = 0; idx < MAX_NR_INBOUND_MAPS; idx++) {
		if (!(rzg3s_pci_read_reg(pcie, AXI_WINDOW_BASEL_REG(idx)) & AXI_WINDOW_ENABLE)) {
			continue;
		}
		pci_base = rzg3s_pci_read_reg(pcie, AXI_DESTINATIONL_REG(idx));
		pci_base |= (((unsigned long)rzg3s_pci_read_reg(pcie,
								AXI_DESTINATIONU_REG(idx))) << 32);
		msi_base_mask = rzg3s_pci_read_reg(pcie, AXI_WINDOW_MASKL_REG(idx));
		if ((pci_base <= base) && (pci_base + msi_base_mask >= base)) {
			msi_base  = base & msi_base_mask;
			msi_base |= rzg3s_pci_read_reg(pcie, AXI_WINDOW_BASEL_REG(idx));
			msi->virt_pages = msi_base & ~AXI_WINDOW_ENABLE;
			break;
		}
	}
	if (!msi_base) {
		dev_err(dev, "MSI Address setting failed (Address:0x%lx)\n", base);
		goto err;
	}

	/* open MSI window */
	rzg3s_pci_write_reg(pcie, lower_32_bits(base), MSI_RCV_WINDOW_ADDRL_REG);
	rzg3s_pci_write_reg(pcie, upper_32_bits(base), MSI_RCV_WINDOW_ADDRU_REG);
	rzg3s_pci_write_reg(pcie, MSI_RCV_WINDOW_MASK_MIN, MSI_RCV_WINDOW_MASKL_REG);
	rzg3s_rmw(pcie, MSI_RCV_WINDOW_ADDRL_REG, MSI_RCV_WINDOW_ENABLE, MSI_RCV_WINDOW_ENABLE);

	/* enable all MSI interrupts */
	rzg3s_rmw(pcie, PCI_INTX_RCV_INTERRUPT_ENABLE_REG,
					 MSI_RECEIVE_INTERRUPT_ENABLE,
					 MSI_RECEIVE_INTERRUPT_ENABLE);

	rzg3s_pci_write_reg(pcie, PCI_RC_MSIRCVE_EN, PCI_RC_MSIRCVE(0));
	rzg3s_pci_write_reg(pcie, ~PCI_RC_MSIRCVMSK_MSI_MASK, PCI_RC_MSIRCVMSK(0));

	return;

err:
	rzg3s_pcie_unmap_msi(host);
}

static int rzg3s_pcie_enable_msi(struct rzg3s_pcie_host *host)
{
	struct rzg3s_pcie *pcie = &host->pcie;
	struct device *dev = pcie->dev;
	struct rzg3s_msi *msi = &host->msi;
	int err, i;

	mutex_init(&msi->lock);

	host->intx_domain = irq_domain_add_linear(dev->of_node, PCI_NUM_INTX,
						  &intx_domain_ops,
						  pcie);

	if (!host->intx_domain)
		dev_err(dev, "failed to create INTx IRQ domain\n");

	for (i = 0; i < PCI_NUM_INTX; i++)
		irq_create_mapping(host->intx_domain, i);

	msi->chip.dev = dev;
	msi->chip.setup_irq = rzg3s_msi_setup_irq;
	msi->chip.setup_irqs = rzg3s_msi_setup_irqs;
	msi->chip.teardown_irq = rzg3s_msi_teardown_irq;

	msi->domain = irq_domain_add_linear(dev->of_node, INT_PCI_MSI_NR,
					    &msi_domain_ops, &msi->chip);
	if (!msi->domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < INT_PCI_MSI_NR; i++)
		irq_create_mapping(msi->domain, i);

	/* request a shared IRQ for all interrupts */
	err = devm_request_irq(dev, msi->irq, rzg3s_pcie_msi_irq,
	/* Temporarily set only shared IRQ flag */
			       IRQF_SHARED,
			       rzg3s_msi_irq_chip.name, host);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	/* setup MSI data target */
	rzg3s_pcie_hw_enable_msi(host);

	return 0;

err:
	rzg3s_pcie_unmap_msi(host);
	return err;
}

static int rzg3s_pcie_get_resources(struct rzg3s_pcie_host *host)
{
	struct rzg3s_pcie *pcie = &host->pcie;
	struct device *dev = pcie->dev;
	struct resource res;
	int err, i;

	host->phy = devm_phy_optional_get(dev, "pcie");
	if (IS_ERR(host->phy))
		return PTR_ERR(host->phy);

	err = of_address_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;

	pcie->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	i = irq_of_parse_and_map(dev->of_node, 0);
	if (!i) {
		dev_err(dev, "cannot get platform resources for msi interrupt\n");
		err = -ENOENT;
		goto err_irq;
	}
	host->msi.irq = i;

err_irq:
	return err;
}

static int rzg3s_pcie_inbound_ranges(struct rzg3s_pcie *pcie,
				    struct resource_entry *entry,
				    int *index)
{
	u64 cpu_addr = entry->res->start;
	u64 cpu_end = entry->res->end;
	u64 pci_addr = entry->res->start - entry->offset;
	u64 mask;
	u64 size = resource_size(entry->res);
	u64 size_idx = 0;
	int idx = *index;

	while (cpu_addr < cpu_end) {
		if (idx >= MAX_NR_INBOUND_MAPS - 1) {
			dev_err(pcie->dev, "Failed to map inbound regions!\n");
			return -EINVAL;
		}

		size = resource_size(entry->res) - size_idx;

		/*
		 * If the size of the range is larger than the alignment of
		 * the start address, we have to use multiple entries to
		 * perform the mapping.
		 */
		if (cpu_addr > 0) {
			unsigned long nr_zeros = __ffs64(cpu_addr);
			u64 alignment = 1ULL << nr_zeros;

			size = min(size, alignment);
		}

		/* Supports max 4GB inbound region for each window */
		size = min(size, 1ULL << 32);
		mask = roundup_pow_of_two(size) - 1;
		mask |= 0xfff;

		rzg3s_pcie_set_inbound(pcie, cpu_addr, pci_addr,
					mask, idx, true);

		pci_addr += size;
		cpu_addr += size;
		size_idx = size;
		idx++;
	}
	*index = idx;

	return 0;
}

static int rzg3s_pcie_parse_map_dma_ranges(struct rzg3s_pcie_host *host)
{
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(host);
	struct resource_entry *entry;
	int index = 0, err = 0;

	resource_list_for_each_entry(entry, &bridge->dma_ranges) {
		err = rzg3s_pcie_inbound_ranges(&host->pcie, entry, &index);
		if (err)
			break;
	}

	return err;
}

static int rzg3s_pcie_phy_init(struct rzg3s_pcie_host *host)
{
	struct rzg3s_pcie *pcie = &host->pcie;

	rzg3s_pci_write_reg(pcie, PIPE_PHY_REG_EN, PERMISSION_REG);

	/* PHY Control Pin (XCFGD) registers setting: 0x2000 - 0x2260 */
	rzg3s_pci_write_reg(pcie, 0, 0x2000);
	rzg3s_pci_write_reg(pcie, 0, 0x2010);
	rzg3s_pci_write_reg(pcie, 0, 0x2020);
	rzg3s_pci_write_reg(pcie, 0, 0x2030);
	rzg3s_pci_write_reg(pcie, 0, 0x2040);
	rzg3s_pci_write_reg(pcie, 0, 0x2050);
	rzg3s_pci_write_reg(pcie, 0, 0x2060);
	rzg3s_pci_write_reg(pcie, 0, 0x2070);
	rzg3s_pci_write_reg(pcie, 0xE0006801, 0x2080);
	rzg3s_pci_write_reg(pcie, 0x007F7E30, 0x2090);
	rzg3s_pci_write_reg(pcie, 0x183E0000, 0x20A0);
	rzg3s_pci_write_reg(pcie, 0x978FF500, 0x20B0);
	rzg3s_pci_write_reg(pcie, 0xEC000000, 0x20C0);
	rzg3s_pci_write_reg(pcie, 0x009F1400, 0x20D0);
	rzg3s_pci_write_reg(pcie, 0x0000D009, 0x20E0);
	rzg3s_pci_write_reg(pcie, 0, 0x20F0);
	rzg3s_pci_write_reg(pcie, 0, 0x2100);
	rzg3s_pci_write_reg(pcie, 0x78000000, 0x2110);
	rzg3s_pci_write_reg(pcie, 0, 0x2120);
	rzg3s_pci_write_reg(pcie, 0x00880000, 0x2130);
	rzg3s_pci_write_reg(pcie, 0x000005C0, 0x2140);
	rzg3s_pci_write_reg(pcie, 0x07000000, 0x2150);
	rzg3s_pci_write_reg(pcie, 0x00780920, 0x2160);
	rzg3s_pci_write_reg(pcie, 0xC9400CE2, 0x2170);
	rzg3s_pci_write_reg(pcie, 0x90000C0C, 0x2180);
	rzg3s_pci_write_reg(pcie, 0x000C1414, 0x2190);
	rzg3s_pci_write_reg(pcie, 0x00005034, 0x21A0);
	rzg3s_pci_write_reg(pcie, 0x00006000, 0x21B0);
	rzg3s_pci_write_reg(pcie, 0x00000001, 0x21C0);
	rzg3s_pci_write_reg(pcie, 0, 0x21D0);
	rzg3s_pci_write_reg(pcie, 0, 0x21E0);
	rzg3s_pci_write_reg(pcie, 0, 0x21F0);
	rzg3s_pci_write_reg(pcie, 0, 0x2220);
	rzg3s_pci_write_reg(pcie, 0, 0x2210);
	rzg3s_pci_write_reg(pcie, 0, 0x2220);
	rzg3s_pci_write_reg(pcie, 0, 0x2230);
	rzg3s_pci_write_reg(pcie, 0, 0x2240);
	rzg3s_pci_write_reg(pcie, 0, 0x2250);
	rzg3s_pci_write_reg(pcie, 0, 0x2260);

	/* PHY Control Pin (XCFGA CMN) registers setting: 0x2400 - 0x24F0*/
	rzg3s_pci_write_reg(pcie, 0x00000D10, 0x2400);
	rzg3s_pci_write_reg(pcie, 0x08310100, 0x2410);
	rzg3s_pci_write_reg(pcie, 0x00C21404, 0x2420);
	rzg3s_pci_write_reg(pcie, 0x013C0010, 0x2430);
	rzg3s_pci_write_reg(pcie, 0x01874440, 0x2440);
	rzg3s_pci_write_reg(pcie, 0x1A216082, 0x2450);
	rzg3s_pci_write_reg(pcie, 0x00103440, 0x2460);
	rzg3s_pci_write_reg(pcie, 0x00000080, 0x2470);
	rzg3s_pci_write_reg(pcie, 0x00000010, 0x2480);
	rzg3s_pci_write_reg(pcie, 0x0C1000C1, 0x2490);
	rzg3s_pci_write_reg(pcie, 0x1000C100, 0x24A0);
	rzg3s_pci_write_reg(pcie, 0x0222000C, 0x24B0);
	rzg3s_pci_write_reg(pcie, 0x00640019, 0x24C0);
	rzg3s_pci_write_reg(pcie, 0x00A00028, 0x24D0);
	rzg3s_pci_write_reg(pcie, 0x01D11228, 0x24E0);
	rzg3s_pci_write_reg(pcie, 0x0201001D, 0x24F0);

	/* PHY Control Pin (XCFGA RX) registers setting: 0x2500 - 0x25C0 */
	rzg3s_pci_write_reg(pcie, 0x07D55000, 0x2500);
	rzg3s_pci_write_reg(pcie, 0x030E3F00, 0x2510);
	rzg3s_pci_write_reg(pcie, 0x00000288, 0x2520);
	rzg3s_pci_write_reg(pcie, 0x102C5880, 0x2530);
	rzg3s_pci_write_reg(pcie, 0x0000000B, 0x2540);
	rzg3s_pci_write_reg(pcie, 0x04141441, 0x2550);
	rzg3s_pci_write_reg(pcie, 0x00641641, 0x2560);
	rzg3s_pci_write_reg(pcie, 0x00D63D63, 0x2570);
	rzg3s_pci_write_reg(pcie, 0x00641641, 0x2580);
	rzg3s_pci_write_reg(pcie, 0x01970377, 0x2590);
	rzg3s_pci_write_reg(pcie, 0x00190287, 0x25A0);
	rzg3s_pci_write_reg(pcie, 0x00190028, 0x25B0);
	rzg3s_pci_write_reg(pcie, 0x00000028, 0x25C0);

	/* PHY Control Pin (XCFGA TX) registers setting: 0x25D0 */
	rzg3s_pci_write_reg(pcie, 0x00000107, 0x25D0);

	/* PHY Control Pin (XCFG*) select registers setting: 0x2A20 */
	rzg3s_pci_write_reg(pcie, 0x00000001, 0x2A20);

	rzg3s_pci_write_reg(pcie, 0, PERMISSION_REG);

	return 0;
}

static const struct of_device_id rzg3s_pcie_of_match[] = {
	{ .compatible = "renesas,rzg3s-pcie",
	  .data = rzg3s_pcie_phy_init },
	{},
};

static int rzg3s_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg3s_pcie_host *host;
	struct rzg3s_pcie *pcie;
	u32 data;
	int err, channel;
	struct pci_host_bridge *bridge;

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*host));
	if (!bridge)
		return -ENOMEM;

	host = pci_host_bridge_priv(bridge);
	pcie = &host->pcie;
	pcie->dev = dev;
	platform_set_drvdata(pdev, host);

	err = of_property_read_u32(dev->of_node, "pcie,channel", &channel);
	if (err) {
		dev_err(pcie->dev, "%pOF: No pcie,channel property found\n",
			dev->of_node);
		return -EINVAL;
	}
	host->channel = channel;

	if (host->channel >= PCIE_MAX_CHANNEL) {
		dev_err(pcie->dev, "%pOF: Invalid pcie,channel '%u'\n",
				dev->of_node, host->channel);
		return -EINVAL;
	}

	pm_runtime_enable(pcie->dev);
	err = pm_runtime_get_sync(pcie->dev);
	if (err < 0) {
		dev_err(pcie->dev, "pm_runtime_get_sync failed\n");
		return err;
	}

	err = rzg3s_pcie_get_resources(host);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		return err;
	}

	host->rst = devm_reset_control_array_get(dev, 0, 0);
	if (IS_ERR(host->rst)) {
		dev_err(dev, "PCIE cannot get reset\n");
		return PTR_ERR(host->rst);
	}

	err = reset_control_deassert(host->rst);
	if (err) {
		dev_err(dev, "PCIE failed to deassert reset %d\n", err);
		return err;
	}

	udelay(200);

	err = rzg3s_pcie_parse_map_dma_ranges(host);
	if (err)
		return err;

	host->phy_init_fn = of_device_get_match_data(dev);
	err = host->phy_init_fn(host);
	if (err) {
		dev_err(dev, "failed to init PCIe PHY\n");
		return err;
	}

	err = rzg3s_pcie_hw_init(pcie, host->channel);
	if (err) {
		dev_info(&pdev->dev, "PCIe link down\n");
		return 0;
	}

	data = rzg3s_pci_read_reg(pcie, PCIE_CORE_STATUS_2_REG);
	dev_info(&pdev->dev, "PCIe Link status [0x%x]\n", data);

	switch ((data >> 8) & 0xFF) {
	case 0x01:
	case 0x02:
		/*- Detect Lane 0 or Lane 1 -*/
		data = 0x01;
		break;
	case 0x03:
		/*- Detect Lane 0 and Lane 1 -*/
		data = 0x02;
		break;
	default:
		/*- unknown -*/
		data = 0xff;
		break;
	}
	dev_info(&pdev->dev, "PCIe x%d: link up Lane number\n", data);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		err = rzg3s_pcie_enable_msi(host);
		if (err < 0) {
			dev_err(dev,
				"failed to enable MSI support: %d\n",
				err);
			return err;
		}
	}

	return rzg3s_pcie_enable(host);
}

static int rzg3s_pcie_suspend(struct device *dev)
{
	struct rzg3s_pcie_host *host = dev_get_drvdata(dev);
	struct rzg3s_pcie *pcie = &host->pcie;
	struct arm_smccc_res local_res;
	int idx;

	for (idx = 0; idx < RZG3S_PCI_MAX_RESOURCES; idx++) {
		/* Save AXI window setting	*/
		pcie->save_reg.axi_window.base_l[idx] = rzg3s_pci_read_reg(pcie, AXI_WINDOW_BASEL_REG(idx));
		pcie->save_reg.axi_window.base_u[idx] = rzg3s_pci_read_reg(pcie, AXI_WINDOW_BASEU_REG(idx));
		pcie->save_reg.axi_window.mask_l[idx] = rzg3s_pci_read_reg(pcie, AXI_WINDOW_MASKL_REG(idx));
		pcie->save_reg.axi_window.mask_u[idx] = rzg3s_pci_read_reg(pcie, AXI_WINDOW_MASKU_REG(idx));
		pcie->save_reg.axi_window.dest_l[idx] = rzg3s_pci_read_reg(pcie, AXI_DESTINATIONL_REG(idx));
		pcie->save_reg.axi_window.dest_u[idx] = rzg3s_pci_read_reg(pcie, AXI_DESTINATIONU_REG(idx));

		/* Save PCIe window setting	*/
		pcie->save_reg.pci_window.base_l[idx]   = rzg3s_pci_read_reg(pcie, PCIE_WINDOW_BASEL_REG(idx));
		pcie->save_reg.pci_window.base_u[idx]   = rzg3s_pci_read_reg(pcie, PCIE_WINDOW_BASEU_REG(idx));
		pcie->save_reg.pci_window.mask_l[idx]   = rzg3s_pci_read_reg(pcie, PCIE_WINDOW_MASKL_REG(idx));
		pcie->save_reg.pci_window.mask_u[idx]   = rzg3s_pci_read_reg(pcie, PCIE_WINDOW_MASKU_REG(idx));
		pcie->save_reg.pci_window.dest_u[idx] = rzg3s_pci_read_reg(pcie, PCIE_DESTINATION_HI_REG(idx));
		pcie->save_reg.pci_window.dest_l[idx] = rzg3s_pci_read_reg(pcie, PCIE_DESTINATION_LO_REG(idx));
	}
	/* Save MSI setting*/
	pcie->save_reg.interrupt.msi_win_addrl	= rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_ADDRL_REG);
	pcie->save_reg.interrupt.msi_win_addru	= rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_ADDRU_REG);
	pcie->save_reg.interrupt.msi_win_maskl	= rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_MASKL_REG);
	pcie->save_reg.interrupt.msi_win_masku	= rzg3s_pci_read_reg(pcie, MSI_RCV_WINDOW_MASKU_REG);
	pcie->save_reg.interrupt.intx_ena	= rzg3s_pci_read_reg(pcie, PCI_INTX_RCV_INTERRUPT_ENABLE_REG);
	pcie->save_reg.interrupt.msi_ena	= rzg3s_pci_read_reg(pcie, PCI_RC_MSIRCVE(0));
	pcie->save_reg.interrupt.msi_mask	= rzg3s_pci_read_reg(pcie, PCI_RC_MSIRCVMSK(0));
	pcie->save_reg.interrupt.msi_data	= rzg3s_pci_read_reg(pcie, PCI_RC_MSIRMD(0));

	/* Clear RST_RSM_B before PCIe power is turned off according to HW manual */
        arm_smccc_smc(RZ_SIP_SVC_SET_PCIE_RST_RSMB, 0xd74, 0x0, 0, 0, 0, 0, 0, &local_res);

	reset_control_assert(host->rst);

	return 0;
}

static int rzg3s_pcie_resume(struct device *dev)
{
	struct rzg3s_pcie_host *host = dev_get_drvdata(dev);
	struct rzg3s_pcie *pcie = &host->pcie;
	int idx, err;

	err = reset_control_deassert(host->rst);
	if (err) {
		dev_err(dev, "PCIE failed to deassert reset %d\n", err);
		return err;
	}
	udelay(200);

	rzg3s_pcie_setting_config(pcie);

	err = rzg3s_pcie_phy_init(host);
	if (err) {
		dev_err(dev, "failed to init PCIe PHY\n");
		return err;
	}

	err = rzg3s_pcie_hw_init(pcie, host->channel);
	if (err)
		dev_warn(pcie->dev, "PCIe link down\n");

	for (idx = 0; idx < RZG3S_PCI_MAX_RESOURCES; idx++) {
		/* Restores AXI window setting	*/
		rzg3s_pci_write_reg(pcie, pcie->save_reg.axi_window.mask_l[idx], AXI_WINDOW_MASKL_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.axi_window.mask_u[idx], AXI_WINDOW_MASKU_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.axi_window.dest_l[idx], AXI_DESTINATIONL_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.axi_window.dest_u[idx], AXI_DESTINATIONU_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.axi_window.base_l[idx], AXI_WINDOW_BASEL_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.axi_window.base_u[idx], AXI_WINDOW_BASEU_REG(idx));

		/* Restores PCIe window setting	*/
		rzg3s_pci_write_reg(pcie, pcie->save_reg.pci_window.mask_l[idx], PCIE_WINDOW_MASKL_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.pci_window.mask_u[idx], PCIE_WINDOW_MASKU_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.pci_window.dest_u[idx], PCIE_DESTINATION_HI_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.pci_window.dest_l[idx], PCIE_DESTINATION_LO_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.pci_window.base_l[idx], PCIE_WINDOW_BASEL_REG(idx));
		rzg3s_pci_write_reg(pcie, pcie->save_reg.pci_window.base_u[idx], PCIE_WINDOW_BASEU_REG(idx));

	}
	/* Restores MSI setting*/
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_win_maskl, MSI_RCV_WINDOW_MASKL_REG);
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_win_masku, MSI_RCV_WINDOW_MASKU_REG);
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_win_addrl, MSI_RCV_WINDOW_ADDRL_REG);
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_win_addru, MSI_RCV_WINDOW_ADDRU_REG);
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.intx_ena, PCI_INTX_RCV_INTERRUPT_ENABLE_REG);
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_ena, PCI_RC_MSIRCVE(0));
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_mask, PCI_RC_MSIRCVMSK(0));
	rzg3s_pci_write_reg(pcie, pcie->save_reg.interrupt.msi_data, PCI_RC_MSIRMD(0));

	return 0;
}

static struct dev_pm_ops rzg3s_pcie_pm_ops = {
	.suspend_noirq =	rzg3s_pcie_suspend,
	.resume_noirq =		rzg3s_pcie_resume,
};

static struct platform_driver rzg3s_pcie_driver = {
	.driver = {
		.name = "rzg3s-pcie",
		.of_match_table = rzg3s_pcie_of_match,
		.pm = &rzg3s_pcie_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = rzg3s_pcie_probe,
};
builtin_platform_driver(rzg3s_pcie_driver);

MODULE_DESCRIPTION("Renesas RZ/G3S PCIe driver");
MODULE_LICENSE("GPL v2");

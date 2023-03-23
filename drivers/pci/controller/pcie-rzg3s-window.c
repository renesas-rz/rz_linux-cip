// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas RZ/G3S Series SoCs
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include "pcie-rzg3s.h"

void rzg3s_pci_write_reg(struct rzg3s_pcie *pcie, u32 val, unsigned long reg)
{
	writel(val, pcie->base + reg);
}

u32 rzg3s_pci_read_reg(struct rzg3s_pcie *pcie, unsigned long reg)
{
	return readl(pcie->base + reg);
}

void rzg3s_rmw(struct rzg3s_pcie *pcie, int where, u32 mask, u32 data)
{
	u32 val = rzg3s_pci_read_reg(pcie, where);

	val &= ~(mask);
	val |= (data);
	rzg3s_pci_write_reg(pcie, val, where);
}

u32 rzg3s_read_conf(struct rzg3s_pcie *pcie, int where)
{
	int shift = 8 * (where & 3);
	u32 val = rzg3s_pci_read_reg(pcie, PCIE_CONFIGURATION_REG + (where & ~3));

	return val >> shift;
}

void rzg3s_write_conf(struct rzg3s_pcie *pcie, u32 data, int where)
{
	rzg3s_pci_write_reg(pcie, CFG_HWINIT_EN, PERMISSION_REG);
	rzg3s_pci_write_reg(pcie, data, PCIE_CONFIGURATION_REG + where);
	rzg3s_pci_write_reg(pcie, 0, PERMISSION_REG);
}

void rzg3s_pcie_set_outbound(struct rzg3s_pcie *pcie, int win,
			    struct resource_entry *window)
{
	/* Setup PCIe address space mappings for each resource */
	struct resource *res = window->res;
	resource_size_t res_start;
	resource_size_t size;
	u32 mask;

	/*
	 * The PAMR mask is calculated in units of 128Bytes, which
	 * keeps things pretty simple.
	 */
	size = resource_size(res);
	mask = size - 1;

	if (res->flags & IORESOURCE_IO)
		res_start = pci_pio_to_address(res->start) - window->offset;
	else
		res_start = res->start - window->offset;

	rzg3s_pci_write_reg(pcie, res_start, PCIE_WINDOW_BASEL_REG(win));
	rzg3s_pci_write_reg(pcie, upper_32_bits(res_start), PCIE_DESTINATION_HI_REG(win));
	rzg3s_pci_write_reg(pcie, lower_32_bits(res_start), PCIE_DESTINATION_LO_REG(win));

	rzg3s_pci_write_reg(pcie, mask, PCIE_WINDOW_MASKL_REG(win));

	rzg3s_rmw(pcie, PCIE_WINDOW_BASEL_REG(win), PCIE_WINDOW_ENABLE, PCIE_WINDOW_ENABLE);
}

void rzg3s_pcie_set_inbound(struct rzg3s_pcie *pcie, u64 cpu_addr,
			   u64 pci_addr, u64 flags, int idx, bool host)
{
	/*
	 * Set up 64-bit inbound regions as the range parser doesn't
	 * distinguish between 32 and 64-bit types.
	 */
	rzg3s_pci_write_reg(pcie, lower_32_bits(pci_addr), AXI_WINDOW_BASEL_REG(idx));
	pcie->save_reg.axi_window.base_u[idx] = upper_32_bits(pci_addr);
	rzg3s_pci_write_reg(pcie, lower_32_bits(cpu_addr), AXI_DESTINATIONL_REG(idx));
	pcie->save_reg.axi_window.dest_u[idx] = upper_32_bits(cpu_addr);
	rzg3s_pci_write_reg(pcie, flags, AXI_WINDOW_MASKL_REG(idx));
	rzg3s_rmw(pcie, AXI_WINDOW_BASEL_REG(idx), AXI_WINDOW_ENABLE, AXI_WINDOW_ENABLE);
}

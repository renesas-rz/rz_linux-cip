// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas RZ/T2H Series SoCs
 *  Copyright (C) 2022 Renesas Electronics Europe Ltd
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include "pcie-rzt2h.h"

void rzt2h_pci_write_reg(struct rzt2h_pcie *pcie, u32 val, unsigned long reg)
{
	writel(val, pcie->base + reg);
}

u32 rzt2h_pci_read_reg(struct rzt2h_pcie *pcie, unsigned long reg)
{
	return readl(pcie->base + reg);
}

void rzt2h_rmw(struct rzt2h_pcie *pcie, int where, u32 mask, u32 data)
{
	u32 val = rzt2h_pci_read_reg(pcie, where);

	val &= ~(mask);
	val |= (data);
	rzt2h_pci_write_reg(pcie, val, where);
}

u32 rzt2h_read_conf(struct rzt2h_pcie *pcie, int where)
{
	int shift = 8 * (where & 3);
	u32 val = rzt2h_pci_read_reg(pcie, PCIE_CONFIGURATION_REG + (where & ~3));

	return val >> shift;
}

void rzt2h_write_conf(struct rzt2h_pcie *pcie, u32 data, int where)
{
	rzt2h_pci_write_reg(pcie, CFG_HWINIT_EN, PERMISSION_REG);
	rzt2h_pci_write_reg(pcie, data, PCIE_CONFIGURATION_REG + where);
	rzt2h_pci_write_reg(pcie, 0, PERMISSION_REG);
}

u32 rzt2h_read_conf_ep(struct rzt2h_pcie *pcie, int where, u8 fn)
{
	int shift = 8 * (where & 3);
	u32 val = rzt2h_pci_read_reg(pcie, PCIE_CONFIGURATION_REG_EP(fn) + (where & ~3));

	return val >> shift;
}

void rzt2h_write_conf_ep(struct rzt2h_pcie *pcie, u32 data, int where, u8 fn)
{
	rzt2h_pci_write_reg(pcie, CFG_HWINIT_EN, PERMISSION_REG);
	rzt2h_pci_write_reg(pcie, data, PCIE_CONFIGURATION_REG_EP(fn) + where);
	rzt2h_pci_write_reg(pcie, 0, PERMISSION_REG);
}

void rzt2h_phy_write(struct rzt2h_pcie *pcie, u32 data, int where)
{
	rzt2h_pci_write_reg(pcie, CFG_PHYINIT_EN, PERMISSION_REG);
	rzt2h_pci_write_reg(pcie, data, where);
	rzt2h_pci_write_reg(pcie, 0, PERMISSION_REG);
}

void rzt2h_pcie_set_outbound(struct rzt2h_pcie *pcie, int win,
			    struct resource_entry *window)
{
	struct resource *res = window->res;
	resource_size_t size;
	u64 mask;
	u64 offset;

	size = resource_size(res);
	mask = size - 1;
	offset = res->start - window->offset;

	/* PW0 addr: PCIE_WINDOW_BASEU_REG: 0x1104   PCIE_WINDOW_BASEL_REG: 0x1100 */
	rzt2h_pci_write_reg(pcie, (u32)(res->start >> 32), PCIE_WINDOW_BASEU_REG(win));
	rzt2h_rmw(pcie, PCIE_WINDOW_BASEL_REG(win), 0xFFFFF000, (u32)(res->start & 0xFFFFF000));

	/* PW0 mask: PCIE_WINDOW_MASKU_REG: 0x110C PCIE_WINDOW_MASKL_REG: 0x1108 */
	rzt2h_pci_write_reg(pcie, (u32)(mask >> 32), PCIE_WINDOW_MASKU_REG(win));
	rzt2h_pci_write_reg(pcie, (u32)(mask & 0xFFFFFFFF), PCIE_WINDOW_MASKL_REG(win));

	/* PD0 addr: PCIE_DESTINATION_HI_REG: 0x1114 PCIE_DESTINATION_LO_REG: 0x1110 */
	rzt2h_pci_write_reg(pcie, (u32)(offset >> 32), PCIE_DESTINATION_HI_REG(win));
	rzt2h_pci_write_reg(pcie, (u32)(offset & 0xFFFFFFFF), PCIE_DESTINATION_LO_REG(win));

	rzt2h_rmw(pcie, PCIE_WINDOW_BASEL_REG(win), PCIE_WINDOW_ENABLE, PCIE_WINDOW_ENABLE);
}

void rzt2h_pcie_ep_set_outbound(struct rzt2h_pcie *pcie, int win, struct resource_entry *window,
			       phys_addr_t cpu_addr, u64 pci_addr, size_t size)
{
	struct resource *res = window->res;
	u64 mask;

	size = resource_size(res);
	if (size > 4096)
		mask = (roundup_pow_of_two(size) / SZ_4K) - 1;
	else
		mask = 0x0;

	/* PW0 addr: PCIE_WINDOW_BASEU_REG: 0x1104   PCIE_WINDOW_BASEL_REG: 0x1100 */
	rzt2h_pci_write_reg(pcie, (u32)(cpu_addr >> 32), PCIE_WINDOW_BASEU_REG(win));
	rzt2h_rmw(pcie, PCIE_WINDOW_BASEL_REG(win), 0xFFFFF000, (u32)(cpu_addr & 0xFFFFF000));

	/* PW0 mask: PCIE_WINDOW_MASKU_REG: 0x110C   PCIE_WINDOW_MASKL_REG: 0x1108 */
	rzt2h_pci_write_reg(pcie, (u64)(mask >> 32), PCIE_WINDOW_MASKU_REG(win));
	rzt2h_pci_write_reg(pcie, (u64)(mask << 12), PCIE_WINDOW_MASKL_REG(win));

	/* PD0 addr: PCIE_DESTINATION_HI_REG: 0x1114   PCIE_DESTINATION_LO_REG: 0x1110 */
	rzt2h_pci_write_reg(pcie, (u32)(pci_addr >> 32), PCIE_DESTINATION_HI_REG(win));
	rzt2h_pci_write_reg(pcie, (u32)(pci_addr & 0xFFFFFFFF), PCIE_DESTINATION_LO_REG(win));

	rzt2h_rmw(pcie, PCIE_WINDOW_BASEL_REG(win), PCIE_WINDOW_ENABLE, PCIE_WINDOW_ENABLE);
}

void rzt2h_pcie_set_inbound(struct rzt2h_pcie *pcie, u64 cpu_addr,
			   u64 pci_addr, u64 flags, int idx, bool host)
{
	/*
	 * Set up 64-bit inbound regions as the range parser doesn't
	 * distinguish between 32 and 64-bit types.
	 */
	/* AXI_WINDOW_BASEL_REG: 0x1000  AXI_WINDOW_BASEU_REG: 0x1004 */
	rzt2h_pci_write_reg(pcie, lower_32_bits(pci_addr), AXI_WINDOW_BASEL_REG(idx));
	rzt2h_pci_write_reg(pcie, upper_32_bits(pci_addr), AXI_WINDOW_BASEU_REG(idx));
	pcie->save_reg.axi_window.base_u[idx] = upper_32_bits(pci_addr);
	/* AXI_DESTINATIONL_REG: 0x1010  AXI_DESTINATIONU_REG: 0x1014 */
	rzt2h_pci_write_reg(pcie, lower_32_bits(cpu_addr), AXI_DESTINATIONL_REG(idx));
	rzt2h_pci_write_reg(pcie, upper_32_bits(cpu_addr), AXI_DESTINATIONU_REG(idx));
	pcie->save_reg.axi_window.dest_u[idx] = upper_32_bits(cpu_addr);
	/* AXI_WINDOW_MASKL_REG: AXI_WINDOW_MASKU_REG:  */
	rzt2h_pci_write_reg(pcie, lower_32_bits(flags), AXI_WINDOW_MASKL_REG(idx));
	rzt2h_pci_write_reg(pcie, upper_32_bits(flags), AXI_WINDOW_MASKU_REG(idx));

	rzt2h_rmw(pcie, AXI_WINDOW_BASEL_REG(idx), AXI_WINDOW_ENABLE, AXI_WINDOW_ENABLE);
}

void rzt2h_pcie_ep_set_inbound(struct rzt2h_pcie *pcie, phys_addr_t cpu_addr,
			      u64 pci_addr, u64 flags, int idx, bool host)
{
	/* AXI_WINDOW_BASEL_REG: 0x1000   AXI_WINDOW_BASEU_REG: 0x1004 */
	rzt2h_pci_write_reg(pcie, lower_32_bits(pci_addr), AXI_WINDOW_BASEL_REG(idx));
	rzt2h_pci_write_reg(pcie, upper_32_bits(pci_addr), AXI_WINDOW_BASEU_REG(idx));
	pcie->save_reg.axi_window.base_u[idx] = upper_32_bits(pci_addr);

	/* AXI_DESTINATIONL_REG: 0x1010   AXI_DESTINATIONU_REG: 0x1014 */
	rzt2h_pci_write_reg(pcie, lower_32_bits(cpu_addr), AXI_DESTINATIONL_REG(idx));
	rzt2h_pci_write_reg(pcie, upper_32_bits(cpu_addr), AXI_DESTINATIONU_REG(idx));
	pcie->save_reg.axi_window.dest_u[idx] = upper_32_bits(cpu_addr);

	/* AXI_WINDOW_MASKL_REG: 0x1008   AXI_WINDOW_MASKU_REG: 0x100C */
	rzt2h_pci_write_reg(pcie, lower_32_bits(flags), AXI_WINDOW_MASKL_REG(idx));
	rzt2h_pci_write_reg(pcie, upper_32_bits(flags), AXI_WINDOW_MASKU_REG(idx));

	rzt2h_rmw(pcie, AXI_WINDOW_BASEL_REG(idx), AXI_WINDOW_ENABLE, AXI_WINDOW_ENABLE);
}

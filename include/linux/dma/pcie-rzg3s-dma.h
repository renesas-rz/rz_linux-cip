/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCIe DMA for Renesas RZ/{G,V} SoCs
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 *
 */

/*
 * RZ PCIe DMA supports two different DMA models:
 * --------------------------------------------------------------------
 * 1. Local DMA (DMA engine is inside the SoC running this driver)
 * --------------------------------------------------------------------
 *
 * In this model, the DMA engine belongs to the SoC that executes the
 * test driver. The CPU directly programs the local DMA controller,
 * and the DMA engine generates interrupts locally.
 *
 * Two use cases exist depending on whether this SoC acts as RC or EP.
 *
 * Case 1.1: This SoC is PCIe Endpoint
 *
 *   +----------------------------+             +------------------------------+
 *   | Root Complex               |             | PCIe Endpoint (this SoC)     |
 *   |----------------------------|             |------------------------------|
 *   |                            |             |                              |
 *   |                            |  DMA TLPs   | internal DMA engine          |
 *   |                            |<----------->|                              |
 *   |                            |             | DMA interrupt via dma_int    |
 *   +----------------------------+             +------------------------------+
 *
 * Case 1.2: This SoC is PCIe Root Complex
 *
 *   +----------------------------+             +------------------------------+
 *   | Root Complex (this SoC)    |             | PCIe Endpoint                |
 *   |----------------------------|             |------------------------------|
 *   |                            |             |                              |
 *   | internal DMA engine        |  DMA TLPs   |                              |
 *   |                            |<----------->|                              |
 *   | DMA interrupt via dma_int  |             |                              |
 *   +----------------------------+             +------------------------------+
 *
 * In both cases above:
 *   - The DMA engine is local to this SoC
 *   - The CPU programs the DMA registers directly
 *   - Completion and error are reported via a local DMA interrupt
 *
 * --------------------------------------------------------------------
 * 2. Remote DMA (DMA engine is inside the PCIe Endpoint)
 * --------------------------------------------------------------------
 *
 * In this model, the DMA engine physically resides in the PCIe Endpoint,
 * but it is controlled remotely by the Root Complex CPU.
 *
 * The Root Complex programs the Endpoint’s DMA engine by issuing
 * PCIe Memory Write / Memory Read TLPs to a BAR-mapped register region (BAR4).
 *
 * Case 2.1: This SoC is PCIe Root Complex controlling EP DMA
 *
 *   +----------------------------+             +------------------------------+
 *   | Root Complex (this SoC)    |             | PCIe Endpoint                |
 *   |----------------------------|             |------------------------------|
 *   |                            |  BAR MMIO   |                              |
 *   | program DMA regs (BAR4)    |===========> | internal DMA engine          |
 *   |                            |  DMA TLPs   |                              |
 *   | wait for MSI interrupt     |<----------- | DMA interrupt via MSI        |
 *   +----------------------------+             +------------------------------+
 *
 * In this case:
 *   - The DMA engine belongs to the Endpoint SoC
 *   - The Root Complex controls it remotely via BAR MMIO accesses
 *   - DMA completion and error are signaled to the RC via MSI
 */

#ifndef _PCIE_RZG3S_DMA_H
#define _PCIE_RZG3S_DMA_H

#define RZG3S_PCI_DMA_MAX_CHANNEL	8

struct rzg3s_pcie_dmac;

/**
 * struct rzg3s_pcie_dma_region - DMA linked list/data region
 * @bar:		BAR mem that this region lives in
 * @paddr:		Physical start address of this region
 * @vaddr:		Virtual start address of this region
 * @off:		Offset from BAR address
 * @sz:			Size of this region
 */
struct rzg3s_pcie_dma_region {
	u8 bar;
	u64 paddr;
	void __iomem *vaddr;
	off_t	off;
	size_t sz;
};

/**
 * struct rz_pcie - holds data used to set up DMA controller
 * @dev:		Device bound with DMA engine
 * @base:		Base address of AXI Bridge Registers
 * @dmac:		Struct of internal DMA controller
 * @ll_region:		Memory region for DMA descriptor allocation
 * @dt_region:		Memory region for data
 * @irq_vector:		MSI IRQ number getter
 * @nr_irqs:		Number of MSI irqs to be allocated
 * @ch_cnt:		Channel count
 */
struct rz_pcie {
	struct device *dev;
	void __iomem *base;
	struct rzg3s_pcie_dmac *dmac;
	struct rzg3s_pcie_dma_region ll_region[RZG3S_PCI_DMA_MAX_CHANNEL];
	struct rzg3s_pcie_dma_region dt_region[RZG3S_PCI_DMA_MAX_CHANNEL];
	int (*irq_vector)(struct device *dev, unsigned int nr);
	int nr_irqs;
	int ch_cnt;
};

/* Export to the platform or pci drivers */
#if IS_ENABLED(CONFIG_PCIE_RENESAS_RZG3S_DMA)
int rzg3s_pcie_dma_probe(struct rz_pcie *pcie, bool remote_dma);
void rzg3s_pcie_dma_remove(struct rz_pcie *pcie);
#else
static inline int rzg3s_pcie_dma_probe(struct rz_pcie *pcie, bool remote_dma)
{
	return -ENODEV;
}
static inline void rzg3s_pcie_dma_remove(struct rz_pcie *pcie)
{
	return;
}
#endif /* CONFIG_PCIE_RENESAS_RZG3S_DMA */

void rzg3s_pcie_update_bits(void __iomem *base, u32 offset, u32 mask,
                                   u32 val);

#endif /* _PCIE_RZG3S_DMA_H */

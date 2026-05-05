/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCIe DMA for Renesas RZ/G3S Series SoCs
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 *
 */

#ifndef _PCIE_RZG3S_DMA_H
#define _PCIE_RZG3S_DMA_H

#define RZG3S_PCI_DMA_MAX_CHANNEL	8

struct rzg3s_pcie_dmac;
struct rz_pcie {
	struct device *dev;
	void __iomem *base;
	struct rzg3s_pcie_dmac *dmac;
};

#if IS_ENABLED(CONFIG_PCIE_RENESAS_RZG3S_DMA)
int rzg3s_pcie_dma_probe(struct rz_pcie *pcie);
void rzg3s_pcie_dma_remove(struct rz_pcie *pcie);
#else
static inline int rzg3s_pcie_dma_probe(struct rz_pcie *pcie)
{
	return -ENODEV;
}
static inline void rzg3s_pcie_dma_remove(struct rz_pcie *pcie)
{
}
#endif /* CONFIG_PCIE_RENESAS_RZG3S_DMA */

#endif /* _PCIE_RZG3S_DMA_H */

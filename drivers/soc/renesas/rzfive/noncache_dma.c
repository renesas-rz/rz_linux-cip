// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/Five L2 cache controller Driver
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 *
 */
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>

#include "proc.h"

static void dma_flush_page(struct page *page, size_t size)
{
	unsigned long k_d_vaddr;
	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	k_d_vaddr = (unsigned long)page_to_virt(page);
	memset((void *)k_d_vaddr, 0, size);
	cpu_dma_wb_range(k_d_vaddr, k_d_vaddr + size);
	cpu_dma_inval_range(k_d_vaddr, k_d_vaddr + size);
}


static inline void cache_op(phys_addr_t paddr, size_t size,
		void (*fn)(unsigned long start, unsigned long end))
{
	unsigned long start;

	start = (unsigned long)phys_to_virt(paddr);
	fn(start, start + size);
}

void arch_sync_dma_for_device(phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_FROM_DEVICE:
		cache_op(paddr, size, cpu_dma_inval_range);
		break;
	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
		cache_op(paddr, size, cpu_dma_wb_range);
		break;
	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		break;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		cache_op(paddr, size, cpu_dma_inval_range);
		break;
	default:
		BUG();
	}
}

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
               gfp_t gfp, unsigned long attrs)
{
	void* kvaddr, *coherent_kvaddr;
	size = PAGE_ALIGN(size);

	kvaddr = dma_direct_alloc_pages(dev, size, handle, gfp, attrs);
	if (!kvaddr)
		goto no_mem;

	coherent_kvaddr = ioremap(dma_to_phys(dev, *handle), size);
	if (!coherent_kvaddr)
		goto no_map;

	dma_flush_page(virt_to_page(kvaddr),size);

	return coherent_kvaddr;
no_map:
	dma_direct_free_pages(dev, size, kvaddr, *handle, attrs);
no_mem:
	return NULL;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
               dma_addr_t handle, unsigned long attrs)
{
       void *kvaddr = phys_to_virt(dma_to_phys(dev, handle));

       size = PAGE_ALIGN(size);
       iounmap(vaddr);
       dma_direct_free_pages(dev, size, kvaddr, handle, attrs);

       return;
}

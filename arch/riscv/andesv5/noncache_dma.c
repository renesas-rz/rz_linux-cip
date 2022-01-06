/*
 * Copyright (C) 2017 SiFive
 *   Wesley Terpstra <wesley@sifive.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 */

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swiotlb.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <asm/andesv5/proc.h>

static void dma_flush_page(struct page *page, size_t size)
{
	unsigned long k_d_vaddr;
	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	k_d_vaddr = (unsigned long)page_address(page);
	memset((void *)k_d_vaddr, 0, size);
	cpu_dma_wb_range(k_d_vaddr, k_d_vaddr + size);
	cpu_dma_inval_range(k_d_vaddr, k_d_vaddr + size);

}


static inline void cache_op(phys_addr_t paddr, size_t size,
		void (*fn)(unsigned long start, unsigned long end))
{
    struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
    unsigned offset = paddr & ~PAGE_MASK;
    size_t left = size;
    unsigned long start;

    do {
        size_t len = left;

        if (PageHighMem(page)) {
            void *addr;

            if (offset + len > PAGE_SIZE) {
                if (offset >= PAGE_SIZE) {
                    page += offset >> PAGE_SHIFT;
                    offset &= ~PAGE_MASK;
                }
                len = PAGE_SIZE - offset;
            }

            addr = kmap_atomic(page);
            start = (unsigned long)(addr + offset);
            fn(start, start + len);
            kunmap_atomic(addr);
        } else {
            start = (unsigned long)phys_to_virt(paddr);
            fn(start, start + size);
        }
        offset = 0;
        page++;
        left -= len;
    } while (left);
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
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

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
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

void *arch_dma_alloc(struct device *dev, size_t size,
			      dma_addr_t * handle, gfp_t gfp,
				      unsigned long attrs)
{
	void* kvaddr, *coherent_kvaddr;
	size = PAGE_ALIGN(size);

	kvaddr = swiotlb_alloc(dev, size, handle, gfp, attrs);
	if (!kvaddr)
		goto no_mem;
	coherent_kvaddr = dma_remap(dma_to_phys(dev, *handle), size);
	if (!coherent_kvaddr)
		goto no_map;

	dma_flush_page(virt_to_page(kvaddr),size);
	return coherent_kvaddr;
no_map:
	swiotlb_free(dev, size, kvaddr, *handle, attrs);
no_mem:
	return NULL;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
			   dma_addr_t handle, unsigned long attrs)
{
	void *swiotlb_addr = phys_to_virt(dma_to_phys(dev, handle));

	size = PAGE_ALIGN(size);
	dma_unmap(vaddr);
	swiotlb_free(dev, size, swiotlb_addr, handle, attrs);

	return;
}


static dma_addr_t dma_riscv_swiotlb_map_page(struct device *dev, 
				     struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	dma_addr_t dev_addr;

	dev_addr = swiotlb_map_page(dev, page, offset, size, dir, attrs);
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
                arch_sync_dma_for_device(dev, dma_to_phys(dev, dev_addr),
                                         size, dir);

	return dev_addr;
}
static int dma_riscv_swiotlb_map_sg(struct device *dev,
				  struct scatterlist *sgl,
				  int nelems, enum dma_data_direction dir,
				  unsigned long attrs)
{
	struct scatterlist *sg;
	int i, ret;

	ret = swiotlb_map_sg_attrs(dev, sgl, nelems, dir, attrs);
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		for_each_sg(sgl, sg, ret, i)
			arch_sync_dma_for_device(dev,
				       dma_to_phys(dev, sg->dma_address),
				       sg->length, dir);

	return ret;
}
static void dma_riscv_swiotlb_unmap_page(struct device *dev, dma_addr_t dev_addr,
				 size_t size, enum dma_data_direction dir,
				 unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_cpu(dev, dma_to_phys(dev, dev_addr),
					size, dir);
	swiotlb_unmap_page(dev, dev_addr, size, dir, attrs);
}
static void dma_riscv_swiotlb_unmap_sg(struct device *dev,
				     struct scatterlist *sgl, int nelems,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		for_each_sg(sgl, sg, nelems, i)
			arch_sync_dma_for_cpu(dev,
					dma_to_phys(dev, sg->dma_address),
					 sg->length, dir);
	swiotlb_unmap_sg_attrs(dev, sgl, nelems, dir, attrs);
}
static void dma_riscv_swiotlb_sync_single_for_cpu(struct device *dev,
					  dma_addr_t dev_addr, size_t size,
					  enum dma_data_direction dir)
{
	arch_sync_dma_for_cpu(dev, dma_to_phys(dev, dev_addr),
					size, dir);
	swiotlb_sync_single_for_cpu(dev, dev_addr, size, dir);
}
static void dma_riscv_swiotlb_sync_single_for_device(struct device *dev,
					     dma_addr_t dev_addr, size_t size,
					     enum dma_data_direction dir)
{
	swiotlb_sync_single_for_device(dev, dev_addr, size, dir);
	arch_sync_dma_for_device(dev, dma_to_phys(dev, dev_addr), size, dir);
}

static void dma_riscv_swiotlb_sync_sg_for_cpu(struct device *dev,
				      struct scatterlist *sgl, int nelems,
				      enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i)
		arch_sync_dma_for_cpu(dev,dma_to_phys(dev, sg->dma_address),
					 sg->length, dir);
	swiotlb_sync_sg_for_cpu(dev, sgl, nelems, dir);
}

static void dma_riscv_swiotlb_sync_sg_for_device(struct device *dev,
					 struct scatterlist *sgl, int nelems,
					 enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	swiotlb_sync_sg_for_device(dev, sgl, nelems, dir);
	for_each_sg(sgl, sg, nelems, i)
		arch_sync_dma_for_device(dev,dma_to_phys(dev, sg->dma_address),
				       sg->length, dir);
}
const struct dma_map_ops swiotlb_noncoh_dma_ops = {
	.alloc			= arch_dma_alloc,
	.free			= arch_dma_free,
	.dma_supported	     	= swiotlb_dma_supported,
	.map_page		= dma_riscv_swiotlb_map_page,
	.map_sg			= dma_riscv_swiotlb_map_sg,
	.unmap_page		= dma_riscv_swiotlb_unmap_page,
	.unmap_sg		= dma_riscv_swiotlb_unmap_sg,
	.sync_single_for_cpu	= dma_riscv_swiotlb_sync_single_for_cpu,
	.sync_single_for_device	= dma_riscv_swiotlb_sync_single_for_device,
	.sync_sg_for_cpu	= dma_riscv_swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device	= dma_riscv_swiotlb_sync_sg_for_device,
};

EXPORT_SYMBOL(swiotlb_noncoh_dma_ops);

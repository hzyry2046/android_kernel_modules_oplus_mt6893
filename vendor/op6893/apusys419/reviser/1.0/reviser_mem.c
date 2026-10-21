// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/kmemleak.h>
#include <linux/iommu.h>
#include <asm/mman.h>

#include "reviser_cmn.h"
#include "reviser_mem.h"
#include "reviser_ioctl.h"
#include "reviser_reg.h"

static int __reviser_get_sgt(const char *buf,
		size_t len, struct sg_table *sgt)
{
	struct page **pages = NULL;
	unsigned int nr_pages;
	unsigned int index;
	const char *p;
	int ret;

	nr_pages = DIV_ROUND_UP((unsigned long)buf + len, PAGE_SIZE)
		- ((unsigned long)buf / PAGE_SIZE);
	pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);

	if (!pages)
		return -ENOMEM;

	p = buf - offset_in_page(buf);
	LOG_DEBUG("start p: %llx buf: %llx\n",
			(uint64_t)p, (uint64_t)buf);

	for (index = 0; index < nr_pages; index++) {
		if (is_vmalloc_addr(p))
			pages[index] = vmalloc_to_page(p);
		else
			pages[index] = virt_to_page(p);
		if (!pages[index]) {
			kfree(pages);
			LOG_ERR("map failed\n");
			return -EFAULT;
		}
		p += PAGE_SIZE;
		//LOG_DEBUG("p: %llx PAGE_SIZE: %llx\n",
		//		(uint64_t)p, (uint64_t)PAGE_SIZE);
	}


	ret = sg_alloc_table_from_pages(sgt, pages, index,
		offset_in_page(buf), len, GFP_KERNEL);
	kfree(pages);
	if (ret) {
		LOG_ERR("sg_alloc_table_from_pages: %d\n", ret);
		return ret;
	}



	LOG_DEBUG("buf: %p, len: %lx, sgt: %p nr_pages: %d\n",
		buf, len, sgt, nr_pages);

	return 0;
}

/* The VLM domain must allocate the exact stock firmware remap window. */
int reviser_mem_alloc(struct device *dev, struct reviser_mem *mem)
{
	struct scatterlist *sg;
	dma_addr_t expected = REMAP_DRAM_BASE | BOUNDARY_MASK;
	u64 next;
	void *kva;
	unsigned int i;
	int ret;

	if (!dev || !mem || !mem->size || mem->kva)
		return -EINVAL;
	if (!iommu_get_domain_for_dev(dev))
		return -ENODEV;
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(34));
	if (ret)
		return ret;
	kva = kvzalloc(mem->size, GFP_KERNEL);
	if (!kva)
		return -ENOMEM;
	ret = __reviser_get_sgt(kva, mem->size, &mem->sgt);
	if (ret)
		goto free_kva;
	ret = dma_map_sgtable(dev, &mem->sgt, DMA_BIDIRECTIONAL, 0);
	if (ret)
		goto free_sgt;
	next = expected;
	for_each_sgtable_dma_sg(&mem->sgt, sg, i) {
		if (sg_dma_address(sg) != next) {
			ret = -ERANGE;
			goto unmap;
		}
		next += sg_dma_len(sg);
	}
	if (next < expected + mem->size || next > APUSYS_LEGACY_IOVA_LIMIT) {
		ret = -ERANGE;
		goto unmap;
	}
	kmemleak_no_scan(kva);
	mem->kva = (uintptr_t)kva;
	mem->iova = REMAP_DRAM_BASE;
	mem->dma_dev = get_device(dev);
	return 0;
unmap:
	dma_unmap_sgtable(dev, &mem->sgt, DMA_BIDIRECTIONAL, 0);
free_sgt:
	sg_free_table(&mem->sgt);
free_kva:
	kvfree(kva);
	return ret;
}

int reviser_mem_free(struct reviser_mem *mem)
{
	if (!mem || !mem->kva || !mem->dma_dev)
		return -EINVAL;
	dma_unmap_sgtable(mem->dma_dev, &mem->sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(&mem->sgt);
	put_device(mem->dma_dev);
	mem->dma_dev = NULL;
	kvfree((void *)(uintptr_t)mem->kva);
	mem->kva = 0;
	mem->iova = 0;
	return 0;
}

int reviser_mem_invalidate(struct device *dev, struct reviser_mem *mem)
{
	if (!mem || !mem->kva || !mem->dma_dev || dev != mem->dma_dev)
		return -EINVAL;
	dma_sync_sgtable_for_cpu(dev, &mem->sgt, DMA_BIDIRECTIONAL);
	return 0;
}

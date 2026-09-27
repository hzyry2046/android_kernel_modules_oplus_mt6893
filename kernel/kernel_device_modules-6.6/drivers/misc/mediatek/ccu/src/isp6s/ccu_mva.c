// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/fdtable.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include "ccu_cmn.h"
#include "ccu_mva.h"
#include "ccu_platform_def.h"

struct CcuMemHandle ccu_buffer_handle[2];

/*
 * op6893 6.6 bring-up: the frozen 4.19 DTB ccu node has no "iommus"/"larbs",
 * so the ccu platform device gets no IOMMU dma domain and dma_alloc_attrs()
 * falls back to dma-direct + physically-contiguous CMA -- a 16MB contiguous
 * request that the 16MB default CMA area can't satisfy (cma alloc -12).
 *
 * On 4.19 the CCU DDR buffer was never contiguous: it was ion_alloc()'d as
 * scattered pages and mapped through M4U into a reserved iova window.  This
 * SoC uses a single global M4U page table (see mtk_ion_compat / camera_isp
 * isp_ion_dev), so a buffer allocated through ANY device that carries an
 * IOMMU domain is reachable by the CCU DMA engines.  Borrow one such
 * `mediatek,mt-pseudo_m4u-port` device (exactly like camera_isp does) as the
 * dma_alloc_attrs() device: that routes the allocation through iommu-dma,
 * which hands back scattered pages mapped to a contiguous iova -- no
 * contiguous DRAM needed, and the size<<1 alignment slack costs only iova.
 */
struct device *ccu_iommu_dev(void)
{
	static struct device *cached;
	struct device_node *np;
	struct platform_device *pdev;

	if (cached)
		return cached;

	/*
	 * Prefer a CCU-specific pseudo-m4u port (pseudo_m4u-ccu-node /
	 * pseudo_m4u-ccu-larb): the buffer must be mapped under the CCU DMA
	 * port so the CCU core can fetch its firmware from the remapped iova.
	 * Mapping only through a CAM port left CCU_ST=0x10 but INIT_DONE never
	 * arrived ("CCU init timeout") because CCU couldn't reach the code.
	 */
	for_each_compatible_node(np, NULL, "mediatek,mt-pseudo_m4u-port") {
		if (!strstr(np->name ? np->name : "", "ccu"))
			continue;
		pdev = of_find_device_by_node(np);
		if (pdev && iommu_get_domain_for_dev(&pdev->dev)) {
			cached = &pdev->dev;
			of_node_put(np);
			return cached;
		}
	}
	/* fall back to any pseudo-m4u port with an iommu domain */
	for_each_compatible_node(np, NULL, "mediatek,mt-pseudo_m4u-port") {
		pdev = of_find_device_by_node(np);
		if (pdev && iommu_get_domain_for_dev(&pdev->dev)) {
			cached = &pdev->dev;
			of_node_put(np);
			return cached;
		}
	}
	LOG_ERR("no mt-pseudo_m4u-port dev with iommu domain (mtk_ion_compat loaded?)\n");
	return NULL;
}


int ccu_allocate_mem(struct ccu_device_s *dev, struct CcuMemHandle *memHandle,
			 int size, bool cached)
{
	int ssize = size << 1;
	dma_addr_t dsize = size;
	struct device *alloc_dev;

	if (dev == NULL)
		return -1;

	if (memHandle == NULL)
		return -2;

	/*
	 * op6893 6.6 bring-up: allocate through an IOMMU-domain device so this
	 * goes via iommu-dma (scattered pages -> contiguous iova) instead of
	 * dma-direct CMA.  Fall back to the ccu device if the pseudo-m4u port
	 * isn't available (behaves as before on a proper DTB).
	 */
	alloc_dev = ccu_iommu_dev();
	if (!alloc_dev)
		alloc_dev = dev->dev;

	LOG_DBG("size(%d) cached(%d)\n", ssize, cached);
	// get buffer virtual address
	memHandle->meminfo.size = ssize;
	memHandle->meminfo.cached = (cached) ? 1 : 0;

	memHandle->meminfo.va = dma_alloc_attrs(alloc_dev, ssize,
		&memHandle->mva, GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);

	if (memHandle->meminfo.va == NULL) {
		LOG_ERR("fail to get buffer kernl virtual address");
		return -1;
	}

	memHandle->align_mva = (memHandle->mva + (dsize - 1)) & ~(dsize - 1);

	LOG_INF_MUST("success:share_fd(%d),size(%x),cached(%d),va(%p),mva(%llx),align_mva(%llx)\n",
	memHandle->meminfo.shareFd, memHandle->meminfo.size,
	memHandle->meminfo.cached, memHandle->meminfo.va, memHandle->mva,
	memHandle->align_mva);

	memHandle->meminfo.mva = (uint32_t)memHandle->mva;
	memHandle->meminfo.align_mva = (uint32_t)memHandle->align_mva;

	ccu_buffer_handle[memHandle->meminfo.cached] = *memHandle;

	return 0;
}

int ccu_deallocate_mem(struct ccu_device_s *dev, struct CcuMemHandle *memHandle)
{
	struct CcuMemHandle *handle;
	struct device *alloc_dev;

	if (dev == NULL)
		return -1;

	if (memHandle == NULL)
		return -2;

	/* must free with the same device the buffer was allocated on */
	alloc_dev = ccu_iommu_dev();
	if (!alloc_dev)
		alloc_dev = dev->dev;

	handle = &ccu_buffer_handle[(memHandle->meminfo.cached) ? 1 : 0];

	if (handle->meminfo.va != NULL) {
		dma_free_attrs(alloc_dev, handle->meminfo.size,
		handle->meminfo.va, handle->mva, DMA_ATTR_WRITE_COMBINE);
	}
	memset(handle, 0, sizeof(struct CcuMemHandle));

	return 0;
}

struct CcuMemInfo *ccu_get_binary_memory(void)
{
	if (ccu_buffer_handle[0].meminfo.va != NULL)
		return &ccu_buffer_handle[0].meminfo;
	LOG_ERR("ccu ddr va not found!\n");
	return NULL;
}

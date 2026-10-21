// SPDX-License-Identifier: GPL-2.0
/* DMA-BUF backend for the legacy APUSYS memory ABI; no MM-larb fallback. */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/iosys-map.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include "apusys_legacy_platform.h"
#include "mdw_cmn.h"
#include "mdw_mem_cmn.h"

struct mdw_dmabuf {
	struct dma_buf *db;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct iosys_map map;
	bool cpu_access;
};

/* Held from init until destroy; this is the APU DATA client, never MM0/1. */
static struct device *mdw_dma_dev;

static struct mdw_dmabuf *mdw_dmabuf_get(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h;

	if (!mem || mem->cache > 1 ||
	    (mem->align && (mem->align > PAGE_SIZE || PAGE_SIZE % mem->align)))
		return ERR_PTR(-EINVAL);
	if (mem->khandle)
		return (void *)(uintptr_t)mem->khandle;
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);
	h->db = dma_buf_get(mem->fd);
	if (IS_ERR(h->db)) {
		int ret = PTR_ERR(h->db);
		kfree(h);
		return ERR_PTR(ret);
	}
	if (!h->db->size || h->db->size < mem->size || h->db->size > U32_MAX) {
		dma_buf_put(h->db);
		kfree(h);
		return ERR_PTR(-EINVAL);
	}
	mem->khandle = (uintptr_t)h;
	return h;
}

static void mdw_dmabuf_put_unused(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h = (void *)(uintptr_t)mem->khandle;

	if (!h || h->attach || iosys_map_is_set(&h->map))
		return;
	if (h->cpu_access)
		dma_buf_end_cpu_access(h->db, DMA_BIDIRECTIONAL);
	dma_buf_put(h->db);
	kfree(h);
	mem->khandle = 0;
}

static int mdw_dmabuf_map_kva(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h = mdw_dmabuf_get(mem);
	int ret;

	if (IS_ERR(h))
		return PTR_ERR(h);
	if (iosys_map_is_set(&h->map))
		return -EBUSY;
	ret = dma_buf_begin_cpu_access(h->db, DMA_BIDIRECTIONAL);
	if (ret)
		goto out;
	h->cpu_access = true;
	ret = dma_buf_vmap_unlocked(h->db, &h->map);
	if (ret)
		goto end_cpu;
	if (h->map.is_iomem) {
		ret = -EOPNOTSUPP;
		dma_buf_vunmap_unlocked(h->db, &h->map);
		iosys_map_clear(&h->map);
		goto end_cpu;
	}
	mem->kva = (uintptr_t)h->map.vaddr;
	return 0;
end_cpu:
	dma_buf_end_cpu_access(h->db, DMA_BIDIRECTIONAL);
	h->cpu_access = false;
out:
	mdw_dmabuf_put_unused(mem);
	return ret;
}

static int mdw_dmabuf_unmap_kva(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h;
	int ret = 0;

	if (!mem || !mem->khandle)
		return -EINVAL;
	h = (void *)(uintptr_t)mem->khandle;
	if (!iosys_map_is_set(&h->map))
		return -EINVAL;
	dma_buf_vunmap_unlocked(h->db, &h->map);
	iosys_map_clear(&h->map);
	if (h->cpu_access) {
		ret = dma_buf_end_cpu_access(h->db, DMA_BIDIRECTIONAL);
		h->cpu_access = false;
	}
	mem->kva = 0;
	mdw_dmabuf_put_unused(mem);
	return ret;
}

static int mdw_dmabuf_map_iova(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h;
	struct scatterlist *sg;
	u64 start, next, end;
	unsigned int i;
	int ret;

	if (!mdw_dma_dev || !iommu_get_domain_for_dev(mdw_dma_dev))
		return -ENODEV;
	h = mdw_dmabuf_get(mem);
	if (IS_ERR(h))
		return PTR_ERR(h);
	if (h->attach)
		return -EBUSY;
	h->attach = dma_buf_attach(h->db, mdw_dma_dev);
	if (IS_ERR(h->attach)) {
		ret = PTR_ERR(h->attach);
		h->attach = NULL;
		goto out;
	}
	h->sgt = dma_buf_map_attachment_unlocked(h->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(h->sgt)) {
		ret = PTR_ERR(h->sgt);
		h->sgt = NULL;
		goto detach;
	}
	/* Validate the full bank-3 address before encoding the legacy low bits. */
	start = sg_dma_address(h->sgt->sgl);
	next = start;
	for_each_sgtable_dma_sg(h->sgt, sg, i) {
		if (sg_dma_address(sg) != next) {
			ret = -ERANGE;
			goto unmap;
		}
		next += sg_dma_len(sg);
	}
	end = start + h->db->size;
	if (start <= APUSYS_LEGACY_IOVA_BASE ||
	    end > APUSYS_LEGACY_IOVA_LIMIT || next < end ||
	    (mem->align && start % mem->align)) {
		ret = -ERANGE;
		goto unmap;
	}
	mem->iova = start - APUSYS_LEGACY_IOVA_BASE;
	mem->iova_size = h->db->size;
	return 0;
unmap:
	dma_buf_unmap_attachment_unlocked(h->attach, h->sgt, DMA_BIDIRECTIONAL);
	h->sgt = NULL;
detach:
	dma_buf_detach(h->db, h->attach);
	h->attach = NULL;
out:
	mdw_dmabuf_put_unused(mem);
	return ret;
}

static int mdw_dmabuf_unmap_iova(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h;

	if (!mem || !mem->khandle)
		return -EINVAL;
	h = (void *)(uintptr_t)mem->khandle;
	if (!h->attach)
		return -EINVAL;
	dma_buf_unmap_attachment_unlocked(h->attach, h->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(h->db, h->attach);
	h->attach = NULL;
	h->sgt = NULL;
	mem->iova = 0;
	mem->iova_size = 0;
	mdw_dmabuf_put_unused(mem);
	return 0;
}

static int mdw_dmabuf_flush(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h;
	int ret;

	if (!mem || !mem->khandle)
		return -EINVAL;
	h = (void *)(uintptr_t)mem->khandle;
	if (!h->cpu_access) {
		ret = dma_buf_begin_cpu_access(h->db, DMA_BIDIRECTIONAL);
		if (ret)
			return ret;
		h->cpu_access = true;
	}
	ret = dma_buf_end_cpu_access(h->db, DMA_BIDIRECTIONAL);
	if (!ret)
		h->cpu_access = false;
	return ret;
}

static int mdw_dmabuf_invalidate(struct apusys_kmem *mem)
{
	struct mdw_dmabuf *h;
	int ret;

	if (!mem || !mem->khandle)
		return -EINVAL;
	h = (void *)(uintptr_t)mem->khandle;
	if (h->cpu_access) {
		ret = dma_buf_end_cpu_access(h->db, DMA_BIDIRECTIONAL);
		if (ret)
			return ret;
		h->cpu_access = false;
	}
	ret = dma_buf_begin_cpu_access(h->db, DMA_BIDIRECTIONAL);
	if (!ret)
		h->cpu_access = true;
	return ret;
}

/* The original driver also rejects MEM_ALLOC; userspace allocates via /dev/ion. */
static int mdw_dmabuf_unsupported(struct apusys_kmem *mem)
{
	return -EOPNOTSUPP;
}

static void mdw_dmabuf_destroy(void)
{
	put_device(mdw_dma_dev);
	mdw_dma_dev = NULL;
}

static struct mdw_mem_ops mdw_dmabuf_ops = {
	.alloc = mdw_dmabuf_unsupported,
	.free = mdw_dmabuf_unsupported,
	.flush = mdw_dmabuf_flush,
	.invalidate = mdw_dmabuf_invalidate,
	.map_kva = mdw_dmabuf_map_kva,
	.unmap_kva = mdw_dmabuf_unmap_kva,
	.map_iova = mdw_dmabuf_map_iova,
	.unmap_iova = mdw_dmabuf_unmap_iova,
	.destroy = mdw_dmabuf_destroy,
};

struct mdw_mem_ops *mdw_mem_ion_init(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct of_phandle_args args;
	u32 cell_index;
	int ret;

	np = of_find_node_by_name(NULL, "pseudo_m4u-vpu-data");
	if (!np)
		return ERR_PTR(-ENODEV);
	/* Validate the live legacy topology: APU IOMMU3, fake DATA port 21/1.
	 * The native binding uses a different domain encoding and needs a separate
	 * reviewed adapter; accepting an arbitrary registered domain is unsafe.
	 */
	ret = of_parse_phandle_with_args(np, "iommus", "#iommu-cells", 0, &args);
	if (ret) {
		of_node_put(np);
		return ERR_PTR(ret);
	}
	ret = of_property_read_u32(args.np, "cell-index", &cell_index);
	of_node_put(args.np);
	if (ret || cell_index != 3 || args.args_count != 1 || args.args[0] != 0x2a1) {
		of_node_put(np);
		return ERR_PTR(-EINVAL);
	}
	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-EPROBE_DEFER);
	if (!pdev->dev.driver || !iommu_get_domain_for_dev(&pdev->dev)) {
		put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}
	mdw_dma_dev = &pdev->dev;
	return &mdw_dmabuf_ops;
}

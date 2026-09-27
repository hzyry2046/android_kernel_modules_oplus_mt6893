// SPDX-License-Identifier: GPL-2.0
/*
 * op6893: a minimal MTK-style ION for the 6.6 port.
 *
 * Why this exists
 * ---------------
 * The vendor graphics stack on this device predates dma-buf heaps.  MTK HWC's
 * video path blits the video layer through the MDP (libdpframework's
 * DpAsyncBlitStream2 -> DpTilePath tRDMA0 -> tWROT0), and to do that
 * libdpframework asks ION for the *device* address (MVA) of every buffer it
 * touches.  The 6.6 kernel has no ION, so the mapping never happens,
 * DpTilePath::config fails with -6, and BliterNode::invalidate() aborts the HWC
 * process -- the green screen.
 *
 * What the vendor stack actually needs is far smaller than full ION.  Measured
 * from the two userspace libraries on the device:
 *
 *   libion.so      ion_open/close, ALLOC, FREE, MAP, SHARE, IMPORT, SYNC, CUSTOM
 *   libion_mtk.so  EVERYTHING through ION_IOC_CUSTOM (mt_ion_open is just
 *                  ion_open + a SET_CLIENT_NAME custom call)
 *
 * and from libdpframework itself, the only calls on the blit path are:
 *
 *   ion_is_legacy(fd)   -> probes ION_IOC_FREE with handle 0; anything that is
 *                          not ENOTTY means "legacy", and legacy is the path
 *                          that uses ION_IOC_CUSTOM
 *   ion_import(fd)      -> ION_IOC_IMPORT, fd -> handle
 *   ion_custom_ioctl(fd, ION_CMD_MULTIMEDIA, {mm_cmd = ION_MM_GET_IOVA, ...})
 *                       -> the one that matters: hand back an MVA
 *
 * So this driver implements the AOSP ioctl surface over a dma-buf handle table,
 * and answers ION_MM_GET_IOVA / ION_MM_CONFIG_BUFFER by mapping the buffer into
 * the MDP's IOMMU domain.  It does not implement heaps, carveouts, secure
 * buffers or the MTK mm-heap bookkeeping, because nothing on this path needs
 * them -- allocation and caching stay with the dma-buf heaps gralloc already
 * uses.
 *
 * How the MVA is produced
 * -----------------------
 * MTK's 6.6 IOMMU driver keeps a single page table shared by all four M4U
 * instances ("Use the exist domain as there is only one pgtable here." in
 * mtk_iommu_domain_finalise), so a buffer mapped through any device with an
 * IOMMU fwspec on iommu0..3 is reachable by the MDP.
 *
 * The MDP engine nodes in the frozen 4.19 DTB carry no `iommus`, so they cannot
 * provide such a device.  The `pseudo_m4u-larbN` nodes do, and their port
 * numbers are the ones libdpframework passes (MTK_M4U_TO_LARB(module_id)), so
 * this driver binds a tiny platform driver to them and keeps the resulting
 * device per larb id.  A device only gets an IOMMU domain in the driver-core
 * probe path, which is why binding is required rather than merely convenient.
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/iommu.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/uaccess.h>
#include <linux/version.h>

MODULE_IMPORT_NS(DMA_BUF);

/* include/dt-bindings/memory/mtk-memory-port.h, unreachable from a module. */
#define MTK_M4U_TO_LARB(id)	(((id) >> 5) & 0x3f)
#define MTK_M4U_TO_PORT(id)	((id) & 0x1f)
#define MTK_M4U_TO_TAB(id)	(((id) >> 20) & 0x3)

/* ---------------------------------------------------------------- uapi ---- */

#define ION_IOC_MAGIC		'I'

struct ion_allocation_data {
	__u64 len;
	__u64 align;
	__u32 heap_id_mask;
	__u32 flags;
	__s32 handle;
};

struct ion_fd_data {
	__s32 handle;
	__s32 fd;
};

struct ion_handle_data {
	__s32 handle;
};

struct ion_custom_data {
	__u32 cmd;
	__u64 arg;
};

#define ION_IOC_ALLOC	_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE	_IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP	_IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_SHARE	_IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_IMPORT	_IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)
#define ION_IOC_CUSTOM	_IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)
#define ION_IOC_SYNC	_IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data)

/*
 * 32-bit (compat) callers.
 *
 * /vendor/bin/vpud -- the daemon the whole vcodec path goes through -- is a
 * 32-bit binary (every library it links is under /vendor/lib, none under
 * lib64).  Its len/align are 32-bit, so the ioctl *numbers* differ from the
 * 64-bit ones for exactly two calls: ALLOC carries a 20-byte struct instead of
 * 32, and CUSTOM an 8-byte struct instead of 16.  The remaining five encode
 * the same size either way and need no special case.
 *
 * This matters because the two callers are built differently: libdpframework
 * (the MDP blit path) is 64-bit and matched from the start, while vpud is
 * 32-bit and never did -- every buffer it tried to allocate came back -ENOTTY,
 * which is what made it refuse AP_IPIMSG_DEC_INIT with status -1.
 *
 * A 32-bit process reaches compat_ioctl, which this driver points at the same
 * handler, so both encodings are accepted there.
 */
struct ion_allocation_data32 {
	__u32 len;
	__u32 align;
	__u32 heap_id_mask;
	__u32 flags;
	__s32 handle;
};

struct ion_custom_data32 {
	__u32 cmd;
	__u32 arg;
};

#define ION_IOC_ALLOC32		_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data32)
#define ION_IOC_CUSTOM32	_IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data32)

/*
 * The ioctl numbers above are derived from these sizes, so pin them: a change
 * here would silently stop matching one of the two callers.
 *   64-bit ALLOC 0xc0204900  CUSTOM 0xc0104906
 *   32-bit ALLOC 0xc0144900  CUSTOM 0xc0084906   <- vpud's, seen in dmesg
 */
static_assert(sizeof(struct ion_allocation_data) == 32);
static_assert(sizeof(struct ion_custom_data) == 16);
static_assert(sizeof(struct ion_allocation_data32) == 20);
static_assert(sizeof(struct ion_custom_data32) == 8);

/* mtk/ion_drv.h */
enum ION_CMDS {
	ION_CMD_SYSTEM,
	ION_CMD_MULTIMEDIA,
	ION_CMD_MULTIMEDIA_SEC
};

enum ION_MM_CMDS {
	ION_MM_CONFIG_BUFFER,
	ION_MM_SET_DEBUG_INFO,
	ION_MM_GET_DEBUG_INFO,
	ION_MM_SET_SF_BUF_INFO,
	ION_MM_GET_SF_BUF_INFO,
	ION_MM_CONFIG_BUFFER_EXT,
	ION_MM_ACQ_CACHE_POOL,
	ION_MM_QRY_CACHE_POOL,
	ION_MM_GET_IOVA,
	ION_MM_GET_IOVA_EXT,
};

enum ION_SYS_CMDS {
	ION_SYS_CACHE_SYNC,
	ION_SYS_GET_PHYS,
	ION_SYS_GET_CLIENT,
	ION_SYS_SET_HANDLE_BACKTRACE,
	ION_SYS_SET_CLIENT_NAME,
	ION_SYS_DMA_OP,
};

/*
 * struct ion_mm_data: mm_cmd @0, then the command's parameter block @8.
 * Offsets below are the absolute ones libdpframework uses -- verified against
 * DpIonHandler::mapHWAddress, which writes mm_cmd at sp+0x20, the handle at
 * sp+0x28 and module_id at sp+0x30, and reads the answer back from sp+0x48.
 */
#define ION_MM_OFF_CMD		0
#define ION_MM_OFF_HANDLE	8
#define ION_MM_OFF_MODULE_ID	16
#define ION_MM_OFF_SECURITY	20
#define ION_MM_OFF_COHERENT	24
#define ION_MM_OFF_IOVA_START	28
#define ION_MM_OFF_IOVA_END	32
#define ION_MM_OFF_PHY_ADDR	40
#define ION_MM_OFF_LEN		48

#define ION_SYS_OFF_CMD		0
#define ION_SYS_OFF_ARG		8

/*
 * struct ion_sys_get_phys_param, nested in struct ion_sys_data at ION_SYS_OFF_ARG.
 * The camera isp_drv (isp_drv_cam.cpp setDeviceInfo, "legacy ion") asks for the
 * *device* address via ION_SYS_GET_PHYS rather than the ION_MM_GET_IOVA that
 * libdpframework uses.  Layout is union{handle,kernel_ptr} then phy_addr then
 * len; the union is pointer-wide, so on lp64 the fields land at 8/16/24 from the
 * struct base.  32-bit callers pack them at 4/8/12.  Verified against the raw
 * arg dump this handler emits.
 */
#define ION_SYS_PHYS_OFF_HANDLE_64	(ION_SYS_OFF_ARG + 0)
#define ION_SYS_PHYS_OFF_PHYADDR_64	(ION_SYS_OFF_ARG + 8)
#define ION_SYS_PHYS_OFF_LEN_64		(ION_SYS_OFF_ARG + 16)
#define ION_SYS_PHYS_OFF_HANDLE_32	(ION_SYS_OFF_ARG - 4)
#define ION_SYS_PHYS_OFF_PHYADDR_32	(ION_SYS_OFF_ARG + 0)
#define ION_SYS_PHYS_OFF_LEN_32		(ION_SYS_OFF_ARG + 4)

/* ------------------------------------------------------------- larb map ---- */

#define ION_MAX_LARB	64

/*
 * The device that carries an IOMMU fwspec, one per larb.  Binding a driver to
 * `mediatek,mt-pseudo_m4u-port` is what makes the IOMMU core attach the device
 * and give it a domain; a bare of_find_device_by_node() would not.
 */
static struct device *ion_larb_dev[ION_MAX_LARB];
static DEFINE_MUTEX(ion_larb_lock);

static int ion_larb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	u32 id = 0;

	if (of_property_read_u32(dev->of_node, "mediatek,larbid", &id))
		return 0;	/* not a larb we can key on; leave it unbound */
	if (id >= ION_MAX_LARB)
		return 0;

	/*
	 * A device without an IOMMU domain maps through the direct DMA ops, so
	 * dma_buf_map_attachment() would hand back a *physical* address and the
	 * engine would read whatever lives there -- silently, and only for the
	 * ports that go through this larb.  The VPU larbs reach us long before
	 * their IOMMU is up, so wait for the domain instead of registering a
	 * device we cannot safely map through.  This costs nothing in the
	 * meantime: ion_larb_for_port() falls back to any registered larb, and
	 * this generation keeps one page table shared by every M4U instance, so
	 * a deferred larb still yields addresses its engines can use.
	 */
	if (!iommu_get_domain_for_dev(dev))
		return -EPROBE_DEFER;

	mutex_lock(&ion_larb_lock);
	ion_larb_dev[id] = dev;
	mutex_unlock(&ion_larb_lock);

	dev_info(dev, "ion: larb %u registered (iommu domain present)\n", id);
	return 0;
}

static const struct of_device_id ion_larb_of_match[] = {
	{ .compatible = "mediatek,mt-pseudo_m4u-port" },
	{ }
};
MODULE_DEVICE_TABLE(of, ion_larb_of_match);

static struct platform_driver ion_larb_driver = {
	.probe = ion_larb_probe,
	.driver = {
		.name = "mtk_ion_larb",
		.of_match_table = ion_larb_of_match,
	},
};

/*
 * The page table is global on this generation, so any registered larb would do;
 * we still prefer the one the caller's port names, and only fall back so that
 * an unexpected port id degrades instead of failing.
 */
static struct device *ion_larb_for_port(int module_id)
{
	struct device *dev = NULL;
	unsigned int larb = MTK_M4U_TO_LARB(module_id);
	int i;

	mutex_lock(&ion_larb_lock);
	if (larb < ION_MAX_LARB)
		dev = ion_larb_dev[larb];
	if (!dev) {
		for (i = 0; i < ION_MAX_LARB; i++) {
			if (ion_larb_dev[i]) {
				dev = ion_larb_dev[i];
				break;
			}
		}
	}
	mutex_unlock(&ion_larb_lock);
	return dev;
}

/* ------------------------------------------------------------ handles ----- */

struct ion_handle {
	struct dma_buf *dmabuf;
	/* Mapping state, kept so FREE can tear it down. */
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t iova;
	unsigned long len;
	int module_id;
	unsigned int security;
	unsigned int coherent;
	/* resolved from a synthetic fd: SHARE hands out a proxy, see below */
	bool synthetic;
	/*
	 * Device address the publisher already holds for this buffer.  GET_IOVA
	 * returns it instead of making a mapping of our own: vpud FREEs the
	 * handle right after GET_IOVA yet keeps using the address for reference
	 * frames, so a per-handle mapping is torn down (and its iova reused)
	 * while the hardware still reads through it -- P frames decode against
	 * garbage and are dropped.  4.19 m4u mappings lived with the buffer.
	 */
	dma_addr_t fixed_iova;
};

struct ion_client {
	struct xarray handles;
	/*
	 * op6893 camera diagnostic: a client that has issued a non-compat
	 * GET_IOVA is a camera-like caller (isp_drv / mtkcam).  Only such
	 * clients log their IMPORT/ALLOC/MAP/SHARE mix, so boot-time display
	 * and vcodec traffic cannot exhaust a global budget before the camera
	 * test even starts.
	 */
	bool verbose;
	atomic_t log_budget;
};

static DEFINE_IDA(ion_handle_ida);

static struct ion_handle *ion_handle_get(struct ion_client *client, int id)
{
	if (id < 0)
		return NULL;
	return xa_load(&client->handles, id);
}

static int ion_handle_add(struct ion_client *client, struct ion_handle *h)
{
	int id = ida_alloc(&ion_handle_ida, GFP_KERNEL);
	int ret;

	if (id < 0)
		return id;
	ret = xa_insert(&client->handles, id, h, GFP_KERNEL);
	if (ret) {
		ida_free(&ion_handle_ida, id);
		return ret;
	}
	return id;
}

/* ------------------------------------------------------------ mapping ----- */

static void ion_unmap(struct ion_handle *h)
{
	if (h->sgt) {
		dma_buf_unmap_attachment(h->attach, h->sgt, DMA_BIDIRECTIONAL);
		h->sgt = NULL;
	}
	if (h->attach) {
		dma_buf_detach(h->dmabuf, h->attach);
		h->attach = NULL;
	}
	h->iova = 0;
}

/*
 * The one operation the whole stack is really after: put this buffer where the
 * MDP can read it, and report the address it got.
 */
static int ion_map(struct ion_handle *h, int module_id)
{
	struct device *dev;
	int ret = 0;

	if (h->sgt && h->module_id == module_id)
		return 0;	/* already mapped for this port */

	ion_unmap(h);

	dev = ion_larb_for_port(module_id);
	if (!dev) {
		pr_err("ion: no larb device for port 0x%x (larb %u) -- is the larb driver bound?\n",
		       module_id, MTK_M4U_TO_LARB(module_id));
		return -ENODEV;
	}

	h->attach = dma_buf_attach(h->dmabuf, dev);
	if (IS_ERR(h->attach)) {
		ret = PTR_ERR(h->attach);
		h->attach = NULL;
		pr_info("ion: attach port 0x%x fail %d\n", module_id, ret);
		return ret;
	}

	h->sgt = dma_buf_map_attachment(h->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(h->sgt)) {
		ret = PTR_ERR(h->sgt);
		h->sgt = NULL;
		dma_buf_detach(h->dmabuf, h->attach);
		h->attach = NULL;
		pr_info("ion: map port 0x%x fail %d\n", module_id, ret);
		return ret;
	}

	h->iova = sg_dma_address(h->sgt->sgl);
	h->len = sg_dma_len(h->sgt->sgl);
	h->module_id = module_id;

	pr_info("ion: mapped %s for port 0x%x (larb %u) -> iova 0x%llx len %lu nents %u\n",
		dev_name(dev), module_id, MTK_M4U_TO_LARB(module_id),
		(unsigned long long)h->iova, h->len, h->sgt->nents);
	return 0;
}

/* ---------------------------------------------------------- ioctl impl ----- */

/* Shared by the 64-bit and 32-bit entry points. */
static int ion_do_alloc(struct ion_client *client, u64 len,
			u32 heap_id_mask, u32 flags, int *out_id)
{
	struct dma_heap *heap;
	struct dma_buf *dmabuf;
	struct ion_handle *h;
	const char *heap_name;
	bool cached = flags & 1;	/* ION_FLAG_CACHED */
	int id;

	if (!len)
		return -EINVAL;

	/*
	 * op6893: honour the requested ION heap.  The camera isp_drv allocates
	 * its CQ/config/dummy buffers from the multimedia heaps (mask 0x400 =
	 * ION_HEAP_MULTIMEDIA_ID uncached, 0x1000 = MM map-mva cached); feeding
	 * those back a plain "system" dma-buf made isp_drv's _set_ion_handle
	 * reject them (the MM engines need the mtk_mm heap's iommu-backed
	 * pages).  Map the MM masks to mtk_mm[-uncached]; everything else keeps
	 * the system heap.
	 */
	if (heap_id_mask & (0x400 | 0x1000))
		heap_name = cached ? "mtk_mm" : "mtk_mm-uncached";
	else
		heap_name = cached ? "system" : "system-uncached";

	heap = dma_heap_find(heap_name);
	if (!heap) {
		/* fall back to system so an unknown mask cannot fail alloc */
		heap = dma_heap_find("system");
		if (!heap)
			return -ENODEV;
	}
	dmabuf = dma_heap_buffer_alloc(heap, len, O_RDWR, 0);
	dma_heap_put(heap);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h) {
		dma_heap_buffer_free(dmabuf);
		return -ENOMEM;
	}
	h->dmabuf = dmabuf;

	id = ion_handle_add(client, h);
	if (id < 0) {
		kfree(h);
		dma_heap_buffer_free(dmabuf);
		return id;
	}

	if (client->verbose &&
	    atomic_dec_if_positive(&client->log_budget) >= 0)
		pr_info("ion: alloc[%s] handle=%d len=%llu heap_mask=0x%x flags=0x%x -> %s\n",
			current->comm, id, len, heap_id_mask, flags, heap_name);

	*out_id = id;
	return 0;
}

static void ion_do_alloc_undo(struct ion_client *client, int id)
{
	struct ion_handle *h = ion_handle_get(client, id);
	struct dma_buf *dmabuf;

	if (!h)
		return;
	dmabuf = h->dmabuf;
	xa_erase(&client->handles, id);
	ida_free(&ion_handle_ida, id);
	kfree(h);
	dma_heap_buffer_free(dmabuf);
}

static long ion_ioctl_alloc(struct ion_client *client, void __user *argp)
{
	struct ion_allocation_data data;
	int id, ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	ret = ion_do_alloc(client, data.len, data.heap_id_mask, data.flags, &id);
	if (ret)
		return ret;

	data.handle = id;
	if (copy_to_user(argp, &data, sizeof(data))) {
		ion_do_alloc_undo(client, id);
		return -EFAULT;
	}
	return 0;
}

static long ion_ioctl_alloc32(struct ion_client *client, void __user *argp)
{
	struct ion_allocation_data32 data;
	int id, ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	ret = ion_do_alloc(client, data.len, data.heap_id_mask, data.flags, &id);
	if (ret)
		return ret;

	data.handle = id;
	if (copy_to_user(argp, &data, sizeof(data))) {
		ion_do_alloc_undo(client, id);
		return -EFAULT;
	}
	return 0;
}

static long ion_ioctl_free(struct ion_client *client, void __user *argp)
{
	struct ion_handle_data data;
	struct ion_handle *h;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	/*
	 * A handle we do not own is -EINVAL rather than -ENOTTY: libion probes
	 * ion_is_legacy() by calling this with handle 0, and ENOTTY is the one
	 * answer that would make it take the non-legacy path.
	 */
	h = ion_handle_get(client, data.handle);
	if (!h)
		return -EINVAL;

	xa_erase(&client->handles, data.handle);
	ida_free(&ion_handle_ida, data.handle);
	ion_unmap(h);
	dma_buf_put(h->dmabuf);
	kfree(h);
	return 0;
}

/* ------------------------------------------------------ synthetic fds ------ */

/*
 * op6893: how the 4.19 vpud daemon is handed the decoder's buffers.
 *
 * The daemon reaches every bitstream and frame buffer through ion_import(fd).
 * Under 4.19 the kernel provided that fd itself: get_mapped_fd() allocated a
 * file descriptor inside the daemon's own file table, pointed it at the
 * buffer's dma_buf, and put the number into vsi->dec.bs_fd / fb_fd[].
 *
 * That helper is gone from 6.6 -- this tree hands the daemon dma addresses
 * instead -- and it cannot be ported, because __alloc_fd, __fd_install,
 * get_files_struct, task_rlimit and lock_task_sighand are all unexported.
 *
 * So publish the buffer under a *synthetic* fd number instead.  The base below
 * is far above anything a process can hold (a task's fd table is bounded by
 * RLIMIT_NOFILE), so dma_buf_get() rejects it, and this driver resolves it from
 * the registry when the daemon's ION_IOC_IMPORT arrives.
 *
 * Nothing else in the daemon's sequence needs changing, because it already runs
 * in the daemon's own context: ion_share() -> ion_ioctl_get_fd() ->
 * dma_buf_fd() installs a real descriptor into the daemon's table.
 *
 * The synthetic number is short-lived by design: mtk-vcodec unpublishes it once
 * the ack for the message that carried it comes back, which is exactly the
 * point at which 4.19 closed the injected fd.
 */
/*
 * Plain ints on purpose: these get compared against a signed fd in
 * mtk_ion_synthetic_get(), and an `U' suffix here would both turn a negative
 * descriptor into a huge unsigned value that passes the range check, and trip
 * -Wsign-compare (this tree builds with -Werror).
 */
#define MTK_ION_SYNTHETIC_FD_BASE	0x40000000
#define MTK_ION_SYNTHETIC_FD_LIMIT	0x10000		/* live at once */

/*
 * op6893: the encoder's venc_ap_ipi_msg_enc carries its buffer descriptors in
 * __s16 fields, so a descriptor only reaches vpud if it also fits in
 * -32768..-1.  Negative numbers can never be a real descriptor, so this range
 * cannot collide with one, and vpud's ldrsh.w sign-extends it back to the same
 * 32-bit value the ioctl then carries.  Same registry, same handle space; only
 * the number handed out differs.  See mtk_ion_publish_dmabuf_s16().
 */
#define MTK_ION_SYNTHETIC_SMALL_BASE	(-0x8000)

struct mtk_ion_synthetic {
	struct dma_buf *dmabuf;
	dma_addr_t iova;	/* publisher's own device address, 0 if none */
};

static DEFINE_XARRAY(mtk_ion_synthetic);
static DEFINE_IDA(mtk_ion_synthetic_ida);

/*
 * Map a descriptor back to its handle id, or -1 when it is not one of ours.
 * The two bases are checked separately because only their difference from the
 * right base yields an id.
 */
static int mtk_ion_synthetic_id(int fd)
{
	if (fd >= MTK_ION_SYNTHETIC_FD_BASE)
		return fd - MTK_ION_SYNTHETIC_FD_BASE;
	if (fd < 0 && fd >= MTK_ION_SYNTHETIC_SMALL_BASE)
		return fd - MTK_ION_SYNTHETIC_SMALL_BASE;
	return -1;
}

/*
 * Shared body.  The descriptor goes out through @fd rather than the return
 * value: one of the two bases is negative, so a descriptor and an errno are
 * both "a small negative int" and the caller could not tell them apart.
 */
static int mtk_ion_publish(struct dma_buf *dmabuf, dma_addr_t iova, int base,
			   int limit, int *fd)
{
	struct mtk_ion_synthetic *entry;
	void *old;
	int id;

	if (!dmabuf)
		return -EINVAL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	id = ida_alloc_max(&mtk_ion_synthetic_ida, limit - 1, GFP_KERNEL);
	if (id < 0) {
		kfree(entry);
		return id;
	}

	get_dma_buf(dmabuf);
	entry->dmabuf = dmabuf;
	entry->iova = iova;
	old = xa_store(&mtk_ion_synthetic, id, entry, GFP_KERNEL);
	if (xa_is_err(old)) {
		ida_free(&mtk_ion_synthetic_ida, id);
		dma_buf_put(dmabuf);
		kfree(entry);
		return xa_err(old);
	}

	*fd = base + id;
	return 0;
}

/**
 * mtk_ion_publish_dmabuf - make @dmabuf importable by descriptor elsewhere
 * @dmabuf: buffer to publish; the registry takes its own reference
 * @fd:     out, the descriptor: always positive, so a 32-bit caller storing it
 *          in an int is safe
 *
 * Returns 0 or a negative errno.
 */
int mtk_ion_publish_dmabuf(struct dma_buf *dmabuf, int *fd)
{
	return mtk_ion_publish(dmabuf, 0, MTK_ION_SYNTHETIC_FD_BASE,
			       MTK_ION_SYNTHETIC_FD_LIMIT, fd);
}
EXPORT_SYMBOL_GPL(mtk_ion_publish_dmabuf);

/**
 * mtk_ion_publish_dmabuf_iova - as mtk_ion_publish_dmabuf(), and GET_IOVA on
 * the imported handle answers @iova instead of mapping the buffer itself
 * @dmabuf: buffer to publish
 * @iova:   the publisher's device address for it, valid in the multimedia
 *          iommu domain for as long as the publisher keeps the buffer
 * @fd:     out, the descriptor
 */
int mtk_ion_publish_dmabuf_iova(struct dma_buf *dmabuf, dma_addr_t iova, int *fd)
{
	return mtk_ion_publish(dmabuf, iova, MTK_ION_SYNTHETIC_FD_BASE,
			       MTK_ION_SYNTHETIC_FD_LIMIT, fd);
}
EXPORT_SYMBOL_GPL(mtk_ion_publish_dmabuf_iova);

/**
 * mtk_ion_publish_dmabuf_s16 - as mtk_ion_publish_dmabuf(), but the descriptor
 * fits in a signed 16-bit field
 * @dmabuf: buffer to publish
 * @fd:     out, the descriptor: in [-32768, -1], which no real one can be, and
 *          which vpud's ldrsh.w sign-extends back to the same value
 *
 * For the wire structures whose descriptor fields are __s16 (the encoder's
 * venc_ap_ipi_msg_enc).  Returns 0 or a negative errno.
 */
int mtk_ion_publish_dmabuf_s16(struct dma_buf *dmabuf, int *fd)
{
	return mtk_ion_publish(dmabuf, 0, MTK_ION_SYNTHETIC_SMALL_BASE,
			       -MTK_ION_SYNTHETIC_SMALL_BASE, fd);
}
EXPORT_SYMBOL_GPL(mtk_ion_publish_dmabuf_s16);

/* The __s16 form of mtk_ion_publish_dmabuf_iova(). */
int mtk_ion_publish_dmabuf_s16_iova(struct dma_buf *dmabuf, dma_addr_t iova,
				    int *fd)
{
	return mtk_ion_publish(dmabuf, iova, MTK_ION_SYNTHETIC_SMALL_BASE,
			       -MTK_ION_SYNTHETIC_SMALL_BASE, fd);
}
EXPORT_SYMBOL_GPL(mtk_ion_publish_dmabuf_s16_iova);

/**
 * mtk_ion_unpublish_dmabuf - drop a descriptor published above
 * @fd: value returned by mtk_ion_publish_dmabuf(); anything else is ignored
 */
void mtk_ion_unpublish_dmabuf(int fd)
{
	struct mtk_ion_synthetic *entry;
	int id;

	id = mtk_ion_synthetic_id(fd);
	if (id < 0)
		return;

	entry = xa_erase(&mtk_ion_synthetic, id);
	if (!entry)
		return;

	ida_free(&mtk_ion_synthetic_ida, id);
	dma_buf_put(entry->dmabuf);
	kfree(entry);
}
EXPORT_SYMBOL_GPL(mtk_ion_unpublish_dmabuf);

/*
 * Resolve a synthetic descriptor, taking a reference for the caller so the
 * result can stand in for dma_buf_get().  NULL when @fd is not one of ours.
 */
static struct dma_buf *mtk_ion_synthetic_get(int fd, dma_addr_t *iova)
{
	struct mtk_ion_synthetic *entry;
	int id = mtk_ion_synthetic_id(fd);

	if (id < 0)
		return NULL;

	entry = xa_load(&mtk_ion_synthetic, id);
	if (!entry)
		return NULL;

	get_dma_buf(entry->dmabuf);
	*iova = entry->iova;
	return entry->dmabuf;
}

static long ion_ioctl_import(struct ion_client *client, void __user *argp)
{
	struct ion_fd_data data;
	struct dma_buf *dmabuf;
	struct ion_handle *h;
	bool synthetic = false;
	dma_addr_t fixed_iova = 0;
	int id, ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	dmabuf = dma_buf_get(data.fd);
	if (IS_ERR(dmabuf)) {
		long err = PTR_ERR(dmabuf);

		synthetic = true;

		/*
		 * Not a descriptor in this process.  It may be one the vcodec
		 * driver published for the 4.19 vpud daemon, which has no other
		 * way to name a buffer -- see mtk_ion_publish_dmabuf().
		 */
		dmabuf = mtk_ion_synthetic_get(data.fd, &fixed_iova);
		if (!dmabuf) {
			/*
			 * op6893 diagnostic: a vdec fd that is neither in this
			 * process nor in the registry is the one failure this
			 * layer cannot recover from, so name the caller.  Log
			 * every failure (not just synthetic) while chasing the
			 * camera isp_drv buffer path.
			 */
			pr_info("ion: import FAILED pid=%d(%s) fd=0x%x err=%ld\n",
				current->tgid, current->comm, data.fd, err);
			return err;
		}
		pr_info("ion: import synthetic pid=%d(%s) fd=0x%x ok\n",
			current->tgid, current->comm, data.fd);
	}

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h) {
		dma_buf_put(dmabuf);
		return -ENOMEM;
	}
	h->dmabuf = dmabuf;
	h->synthetic = synthetic;
	h->fixed_iova = fixed_iova;

	id = ion_handle_add(client, h);
	if (id < 0) {
		kfree(h);
		dma_buf_put(dmabuf);
		return id;
	}

	data.handle = id;
	ret = copy_to_user(argp, &data, sizeof(data));
	if (ret) {
		xa_erase(&client->handles, id);
		ida_free(&ion_handle_ida, id);
		kfree(h);
		dma_buf_put(dmabuf);
		return -EFAULT;
	}
	{
		if (client->verbose &&
		    atomic_dec_if_positive(&client->log_budget) >= 0)
			pr_info("ion: import[%s] fd=0x%x -> handle=%d size=%zu\n",
				current->comm, data.fd, id, dmabuf->size);
	}
	return 0;
}

/*
 * op6893: SHARE of a buffer that reached this client through a synthetic fd.
 *
 * Those dma-bufs are the vcodec's vb2 buffers, exported in the media client's
 * process, so their struct file carries that client's SELinux label.  Handing
 * the same file to vpud trips "vpud_native <client>:fd use" (vendor policy
 * only allows mediaserver) and the daemon's mmap() of the bitstream returns
 * -1 -> SIGSEGV in Vdec_Drv_H264_open.  4.19 MTK ION never hit this: its
 * SHARE exported a fresh dma-buf per call, created in the caller's context.
 * Do the same: export a proxy owned by the caller that forwards CPU access
 * and mmap to the real buffer.  Device mapping is not needed on this path
 * (vpud gets the iova through GET_IOVA) and is refused.
 */
static struct sg_table *ion_proxy_map(struct dma_buf_attachment *a,
				      enum dma_data_direction dir)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static void ion_proxy_unmap(struct dma_buf_attachment *a, struct sg_table *sgt,
			    enum dma_data_direction dir)
{
}

static void ion_proxy_release(struct dma_buf *proxy)
{
	dma_buf_put(proxy->priv);
}

static int ion_proxy_mmap(struct dma_buf *proxy, struct vm_area_struct *vma)
{
	return dma_buf_mmap(proxy->priv, vma, vma->vm_pgoff);
}

static int ion_proxy_begin_cpu(struct dma_buf *proxy,
			       enum dma_data_direction dir)
{
	return dma_buf_begin_cpu_access(proxy->priv, dir);
}

static int ion_proxy_end_cpu(struct dma_buf *proxy, enum dma_data_direction dir)
{
	return dma_buf_end_cpu_access(proxy->priv, dir);
}

static const struct dma_buf_ops ion_proxy_ops = {
	.map_dma_buf = ion_proxy_map,
	.unmap_dma_buf = ion_proxy_unmap,
	.release = ion_proxy_release,
	.mmap = ion_proxy_mmap,
	.begin_cpu_access = ion_proxy_begin_cpu,
	.end_cpu_access = ion_proxy_end_cpu,
};

static int ion_proxy_fd(struct dma_buf *real)
{
	DEFINE_DMA_BUF_EXPORT_INFO(info);
	struct dma_buf *proxy;
	int fd;

	info.exp_name = "ion_compat_proxy";
	info.ops = &ion_proxy_ops;
	info.size = real->size;
	info.flags = O_RDWR;
	info.priv = real;

	get_dma_buf(real);		/* dropped in ion_proxy_release() */
	proxy = dma_buf_export(&info);
	if (IS_ERR(proxy)) {
		dma_buf_put(real);
		return PTR_ERR(proxy);
	}
	fd = dma_buf_fd(proxy, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(proxy);	/* releases real too */
	return fd;
}

/* ION_IOC_SHARE / ION_IOC_MAP: hand back a new fd for the buffer. */
static long ion_ioctl_get_fd(struct ion_client *client, void __user *argp)
{
	struct ion_fd_data data;
	struct ion_handle *h;
	int fd, ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	h = ion_handle_get(client, data.handle);
	if (!h)
		return -EINVAL;

	if (h->synthetic) {
		fd = ion_proxy_fd(h->dmabuf);
		if (fd < 0)
			return fd;
	} else {
		/* dma_buf_fd() consumes a reference, so take one for it. */
		get_dma_buf(h->dmabuf);
		fd = dma_buf_fd(h->dmabuf, O_CLOEXEC);
		if (fd < 0) {
			dma_buf_put(h->dmabuf);
			return fd;
		}
	}

	data.fd = fd;
	ret = copy_to_user(argp, &data, sizeof(data));
	if (ret)
		return -EFAULT;
	return 0;
}

static long ion_ioctl_sync(struct ion_client *client, void __user *argp)
{
	struct ion_fd_data data;
	struct ion_handle *h;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	h = ion_handle_get(client, data.handle);
	if (!h)
		return -EINVAL;

	/*
	 * Coherency is the exporter's business now; make one round trip so a
	 * caller that syncs around CPU access still gets its barriers.
	 */
	if (dma_buf_begin_cpu_access(h->dmabuf, DMA_BIDIRECTIONAL))
		return -EINVAL;
	dma_buf_end_cpu_access(h->dmabuf, DMA_BIDIRECTIONAL);
	return 0;
}

/*
 * ION_SYS_CACHE_SYNC.  The 4.19 vpud issues this ~6x/s while decoding (4.19
 * kprobe baseline), around CPU access to buffers it also hands to the codec, so
 * the old "return 0" left it with stale lines on a cached mtk_mm buffer.
 *
 * 4.19 struct ion_sys_cache_sync_param, nested in ion_sys_data at the same
 * union offset GET_PHYS uses (4 for 32-bit callers, 8 for 64-bit):
 *	{ int handle / ptr; void *va; unsigned int size;
 *	  enum ION_CACHE_SYNC_TYPE sync_type; u64 iova; }
 * Only handle and sync_type are read.  The whole buffer is maintained, not the
 * [va, va+size) range: a superset of what 4.19 did, and independent of the
 * va/size offsets, which have not been measured for 32-bit callers.  The
 * dma-buf exporter performs the maintenance on its mapped attachments.
 */
enum {
	ION_CACHE_CLEAN_BY_RANGE,
	ION_CACHE_INVALID_BY_RANGE,
	ION_CACHE_FLUSH_BY_RANGE,
	ION_CACHE_CLEAN_BY_RANGE_USE_PA,
	ION_CACHE_INVALID_BY_RANGE_USE_PA,
	ION_CACHE_FLUSH_BY_RANGE_USE_PA,
	ION_CACHE_CLEAN_ALL,
	ION_CACHE_INVALID_ALL,
	ION_CACHE_FLUSH_ALL,
};

#define ION_SYS_SYNC_OFF_HANDLE_64	(ION_SYS_OFF_ARG + 0)
#define ION_SYS_SYNC_OFF_TYPE_64	(ION_SYS_OFF_ARG + 20)
/*
 * 32-bit: 4.19 compat_ion_sys_cache_sync_param holds a u64 iova, so the union
 * is 8-aligned: handle @8, va @12, size @16, sync_type @20, iova @24.  Checked
 * against vpud's raw args (arg[2]=handle, arg[4]=size, arg[5]=type).  The old
 * @4/@16 guess read the size as the type, which always looked like an *_ALL
 * op, so every vpud cache sync was dropped.
 */
#define ION_SYS_SYNC_OFF_HANDLE_32	(ION_SYS_OFF_ARG + 0)
#define ION_SYS_SYNC_OFF_TYPE_32	(ION_SYS_OFF_ARG + 12)

static atomic_t ion_sync_log_budget = ATOMIC_INIT(32);

static long ion_sys_cache_sync(struct ion_client *client, void __user *uarg,
			       bool compat)
{
	int off_handle = compat ? ION_SYS_SYNC_OFF_HANDLE_32
				: ION_SYS_SYNC_OFF_HANDLE_64;
	int off_type = compat ? ION_SYS_SYNC_OFF_TYPE_32
			      : ION_SYS_SYNC_OFF_TYPE_64;
	struct ion_handle *h;
	s32 handle;
	u32 type, word;
	int i, ret = 0;

	if (copy_from_user(&handle, uarg + off_handle, sizeof(handle)) ||
	    copy_from_user(&type, uarg + off_type, sizeof(type)))
		return -EFAULT;

	if (atomic_dec_if_positive(&ion_sync_log_budget) >= 0) {
		pr_info("ion: cache_sync handle %d type %u (compat=%d)\n",
			handle, type, compat);
		for (i = 0; i < 10; i++)
			if (!copy_from_user(&word, uarg + i * 4, sizeof(word)))
				pr_info("ion: cache_sync arg[%d] = 0x%x\n", i, word);
	}

	if (type >= ION_CACHE_CLEAN_ALL) {
		/* No dma-buf equivalent of a whole-cache operation. */
		pr_info_ratelimited("ion: cache_sync type %u (ALL) ignored\n", type);
		return 0;
	}

	h = ion_handle_get(client, handle);
	if (!h) {
		pr_info_ratelimited("ion: cache_sync bad handle %d (compat=%d)\n",
				    handle, compat);
		return -EINVAL;
	}

	switch (type % 3) {
	case ION_CACHE_CLEAN_BY_RANGE:		/* CPU wrote -> device reads */
		ret = dma_buf_end_cpu_access(h->dmabuf, DMA_TO_DEVICE);
		break;
	case ION_CACHE_INVALID_BY_RANGE:	/* device wrote -> CPU reads */
		ret = dma_buf_begin_cpu_access(h->dmabuf, DMA_FROM_DEVICE);
		break;
	case ION_CACHE_FLUSH_BY_RANGE:
		ret = dma_buf_end_cpu_access(h->dmabuf, DMA_TO_DEVICE);
		if (!ret)
			ret = dma_buf_begin_cpu_access(h->dmabuf, DMA_FROM_DEVICE);
		break;
	}
	return ret ? -EINVAL : 0;
}

static long ion_custom_system(struct ion_client *client, u64 arg, bool compat)
{
	void __user *uarg = (void __user *)(uintptr_t)arg;
	int off_handle = compat ? ION_SYS_PHYS_OFF_HANDLE_32
				: ION_SYS_PHYS_OFF_HANDLE_64;
	int off_phy = compat ? ION_SYS_PHYS_OFF_PHYADDR_32
			     : ION_SYS_PHYS_OFF_PHYADDR_64;
	int off_len = compat ? ION_SYS_PHYS_OFF_LEN_32
			     : ION_SYS_PHYS_OFF_LEN_64;
	struct ion_handle *h;
	u32 sys_cmd, word;
	s32 handle;
	u64 phys;
	int ret, i;

	if (copy_from_user(&sys_cmd, uarg + ION_SYS_OFF_CMD, sizeof(sys_cmd)))
		return -EFAULT;

	switch (sys_cmd) {
	case ION_SYS_GET_PHYS:
		/*
		 * The camera's "legacy ion" path: hand back a device address for
		 * the buffer, exactly as ION_MM_GET_IOVA does.  Answering 0 here
		 * (the old no-op) made isp_drv_cam's Res_Attach reject the buffer
		 * ("can't be 0") and configPipe fail -22, so P1 never started.
		 */
		if (copy_from_user(&handle, uarg + off_handle, sizeof(handle)))
			return -EFAULT;
		h = ion_handle_get(client, handle);
		if (!h) {
			pr_info("ion: get_phys bad handle %d (compat=%d)\n",
				handle, compat);
			for (i = 0; i < 8; i++) {
				if (!copy_from_user(&word, uarg + i * 4,
						    sizeof(word)))
					pr_info("ion: phys arg[%d] = 0x%x\n",
						i, word);
			}
			return -EINVAL;
		}

		ret = ion_map(h, h->module_id);
		if (ret)
			return ret;

		phys = h->iova;
		pr_info("ion: get_phys handle %d (port 0x%x) -> 0x%llx len %lu\n",
			handle, h->module_id, (unsigned long long)phys, h->len);
		if (copy_to_user(uarg + off_phy, &phys, sizeof(phys)))
			return -EFAULT;
		if (copy_to_user(uarg + off_len, &h->len, sizeof(h->len)))
			return -EFAULT;
		return 0;

	case ION_SYS_CACHE_SYNC:
		return ion_sys_cache_sync(client, uarg, compat);

	case ION_SYS_SET_CLIENT_NAME:
	case ION_SYS_DMA_OP:
		/* Nothing to do at this layer. */
		return 0;
	default:
		pr_info("ion: unhandled system cmd %u\n", sys_cmd);
		return 0;
	}
}

static long ion_custom_mm(struct ion_client *client, u64 arg, bool compat)
{
	void __user *uarg = (void __user *)(uintptr_t)arg;
	struct ion_handle *h;
	u32 mm_cmd;
	s32 handle;
	u32 word;
	int module_id, ret;
	int i;
	/* op6893: vpud is 32-bit.  Its struct ion_mm_data is the EABI layout
	 * of 4.19's compat_ion_mm_get_iova_param (compat_ion.c): the union
	 * holds a u64, so it is 8-aligned -- handle @8, module_id @12,
	 * security @16, coherent @20, reserve_iova_start/end @24/@28,
	 * phy_addr @32, len (compat_size_t, 4 bytes) @40.  The 64-bit layout
	 * (libdpframework) has module_id/phy_addr/len @16/@40/@48.
	 * An earlier @16/@24 guess wrote the iova over security/coherent and
	 * left phy_addr zero, so vpud dropped every bitstream buffer right
	 * after GET_IOVA (IMPORT -> CUSTOM -> FREE, no SHARE/mmap) and each
	 * AP_IPIMSG_DEC_START came back -1 ("need first seq header").
	 */
	int off_module = compat ? 12 : ION_MM_OFF_MODULE_ID;
	int off_phy = compat ? 32 : ION_MM_OFF_PHY_ADDR;
	int off_len = compat ? 40 : ION_MM_OFF_LEN;

	if (compat) {
		/*
		 * op6893: dump the caller's own layout.  The two callers do not
		 * have to agree on the internal offsets of struct ion_mm_data
		 * (libdpframework is 64-bit, vpud 32-bit), and reading a handle
		 * out of the wrong word fails silently -- it yields a plausible
		 * small integer that simply is not a handle.  This is a bring-up
		 * diagnostic; drop it once the vdec path is settled.
		 */
		for (i = 0; i < 8; i++) {
			if (copy_from_user(&word, uarg + i * sizeof(u32),
					   sizeof(word)))
				return -EFAULT;
			pr_info("ion: mm arg[%d] = 0x%x\n", i, word);
		}
	}

	if (copy_from_user(&mm_cmd, uarg + ION_MM_OFF_CMD, sizeof(mm_cmd)))
		return -EFAULT;
	if (copy_from_user(&handle, uarg + ION_MM_OFF_HANDLE, sizeof(handle)))
		return -EFAULT;

	/*
	 * op6893 camera bring-up diagnostic: the camera isp_drv ("legacy ion",
	 * isp_drv_cam.cpp setDeviceInfo) reports _set_ion_handle error(7_<handle>_<plane>)
	 * for the image-output buffers.  struct layout is cmd@0, handle@8,
	 * module_id@16 (same as libdpframework).  Log the outcome for the buffer
	 * commands so we can see what the failing handles get.  Bounded, compact.
	 */
	h = ion_handle_get(client, handle);
	if (!h) {
		if (mm_cmd != ION_MM_SET_DEBUG_INFO)
			pr_info("ion: mm[%s] cmd=%u handle=%d NOT FOUND in client\n",
				current->comm, mm_cmd, handle);
		return -EINVAL;
	}

	switch (mm_cmd) {
	case ION_MM_CONFIG_BUFFER:
	case ION_MM_CONFIG_BUFFER_EXT:
		if (copy_from_user(&module_id, uarg + off_module,
				   sizeof(module_id)))
			return -EFAULT;
		h->module_id = module_id;
		pr_info("ion: config buffer handle %d for port 0x%x\n",
			handle, module_id);
		/*
		 * MTK defers the actual mapping to GET_IOVA.  Record it only,
		 * so an early CONFIG_BUFFER cannot fail the caller.
		 */
		return 0;

	case ION_MM_GET_IOVA:
	case ION_MM_GET_IOVA_EXT:
		if (copy_from_user(&module_id, uarg + off_module,
				   sizeof(module_id)))
			return -EFAULT;

		if (h->fixed_iova) {
			h->iova = h->fixed_iova;
			h->len = h->dmabuf->size;
			h->module_id = module_id;
			ret = 0;
		} else {
			ret = ion_map(h, module_id);
		}
		if (!compat)
			client->verbose = true;	/* camera-like caller */
		{
			static atomic_t giova_budget = ATOMIC_INIT(400);

			/* skip the port 0x200 CQ retry spam; log the rest */
			if (module_id != 0x200 &&
			    atomic_dec_if_positive(&giova_budget) >= 0)
				pr_info("ion: getiova[%s] handle=%d port=0x%x ret=%d iova=0x%llx len=%lu\n",
					current->comm, handle, module_id, ret,
					(unsigned long long)h->iova, h->len);
		}
		if (ret)
			return ret;

		if (copy_to_user(uarg + off_phy, &h->iova,
				 sizeof(u64)))
			return -EFAULT;
		if (compat) {
			u32 len32 = (u32)h->len;

			if (copy_to_user(uarg + off_len, &len32, sizeof(len32)))
				return -EFAULT;
		} else if (copy_to_user(uarg + off_len, &h->len,
					sizeof(unsigned long))) {
			return -EFAULT;
		}
		return 0;

	default:
		pr_info("ion: unhandled mm cmd %u (handle %d)\n", mm_cmd, handle);
		return 0;
	}
}

static long ion_ioctl_custom(struct ion_client *client, void __user *argp)
{
	struct ion_custom_data data;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	switch (data.cmd) {
	case ION_CMD_SYSTEM:
		return ion_custom_system(client, data.arg, false);
	case ION_CMD_MULTIMEDIA:
		return ion_custom_mm(client, data.arg, false);
	default:
		pr_info("ion: unhandled custom cmd %u\n", data.cmd);
		return 0;
	}
}

static long ion_ioctl_custom32(struct ion_client *client, void __user *argp)
{
	struct ion_custom_data32 data;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	switch (data.cmd) {
	case ION_CMD_SYSTEM:
		return ion_custom_system(client, (u64)data.arg, true);
	case ION_CMD_MULTIMEDIA:
		return ion_custom_mm(client, (u64)data.arg, true);
	default:
		pr_info("ion: unhandled compat custom cmd %u\n", data.cmd);
		return 0;
	}
}

static long ion_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ion_client *client = file->private_data;
	void __user *argp = (void __user *)arg;

	/*
	 * op6893 camera diagnostic: log the non-CUSTOM ioctl mix per caller so
	 * we can see whether isp_drv's failing "_set_ion_handle" buffer path
	 * uses MAP/SHARE/ALLOC (which we answer with an fd, not an address) or
	 * hits an import failure.  Bounded so it cannot flood; CUSTOM has its
	 * own logging already.
	 */
	if (client->verbose && cmd != ION_IOC_CUSTOM &&
	    cmd != ION_IOC_CUSTOM32 && cmd != ION_IOC_SYNC) {
		const char *nm =
			cmd == ION_IOC_ALLOC   ? "ALLOC"   :
			cmd == ION_IOC_ALLOC32 ? "ALLOC32" :
			cmd == ION_IOC_FREE    ? "FREE"    :
			cmd == ION_IOC_IMPORT  ? "IMPORT"  :
			cmd == ION_IOC_MAP     ? "MAP"     :
			cmd == ION_IOC_SHARE   ? "SHARE"   : "?";

		if (atomic_dec_if_positive(&client->log_budget) >= 0)
			pr_info("ion: ioctl[%s] %s\n", current->comm, nm);
	}

	switch (cmd) {
	case ION_IOC_ALLOC:
		return ion_ioctl_alloc(client, argp);
	case ION_IOC_ALLOC32:
		return ion_ioctl_alloc32(client, argp);
	case ION_IOC_FREE:
		return ion_ioctl_free(client, argp);
	case ION_IOC_IMPORT:
		return ion_ioctl_import(client, argp);
	case ION_IOC_MAP:
	case ION_IOC_SHARE:
		return ion_ioctl_get_fd(client, argp);
	case ION_IOC_SYNC:
		return ion_ioctl_sync(client, argp);
	case ION_IOC_CUSTOM:
		return ion_ioctl_custom(client, argp);
	case ION_IOC_CUSTOM32:
		return ion_ioctl_custom32(client, argp);
	default:
		pr_info("ion: unhandled ioctl 0x%x\n", cmd);
		return -ENOTTY;
	}
}

static int ion_open(struct inode *inode, struct file *file)
{
	struct ion_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	xa_init(&client->handles);
	client->verbose = false;
	atomic_set(&client->log_budget, 800);
	file->private_data = client;
	return 0;
}

static int ion_release(struct inode *inode, struct file *file)
{
	struct ion_client *client = file->private_data;
	struct ion_handle *h;
	unsigned long id;

	xa_for_each(&client->handles, id, h) {
		xa_erase(&client->handles, id);
		ida_free(&ion_handle_ida, id);
		ion_unmap(h);
		dma_buf_put(h->dmabuf);
		kfree(h);
	}
	xa_destroy(&client->handles);
	kfree(client);
	return 0;
}

static const struct file_operations ion_fops = {
	.owner = THIS_MODULE,
	.open = ion_open,
	.release = ion_release,
	.unlocked_ioctl = ion_ioctl,
	.compat_ioctl = ion_ioctl,
};

static struct miscdevice ion_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &ion_fops,
	.mode = 0666,
};

static int __init ion_init(void)
{
	int ret;

	ret = platform_driver_register(&ion_larb_driver);
	if (ret)
		return ret;

	ret = misc_register(&ion_misc);
	if (ret) {
		platform_driver_unregister(&ion_larb_driver);
		return ret;
	}

	pr_info("ion: ready\n");
	return 0;
}

static void __exit ion_exit(void)
{
	misc_deregister(&ion_misc);
	platform_driver_unregister(&ion_larb_driver);
}

module_init(ion_init);
module_exit(ion_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("op6893: minimal MTK ION for the 6.6 port (MVA for the MDP blit)");

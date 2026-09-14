/* SPDX-License-Identifier: GPL-2.0 */
/*
 * op6893: the synthetic-descriptor API mtk_ion_compat exports so that the 4.19
 * vpud daemon can still reach the decoder's buffers.
 *
 * The daemon only knows how to name a buffer through ion_import(fd), and 6.6
 * has no get_mapped_fd() to hand it one.  mtk_ion_publish_dmabuf() registers a
 * buffer under a number that cannot be a real descriptor (see the base in
 * mtk_ion_compat.c), the kernel writes that number into the shared vsi, and the
 * daemon's ION_IOC_IMPORT resolves it.
 *
 * Usage is publish -> send the message -> wait for the ack -> unpublish, which
 * mirrors where 4.19 closed the injected descriptor.
 *
 * Implemented in drivers/misc/mediatek/ion_compat/mtk_ion_compat.c.
 *
 * mtk_ion_compat is built unconditionally (obj-m, no Kconfig symbol), so this
 * is a hard link-time dependency: without the module loaded, mtk-vcodec-dec-v2
 * will not resolve its symbols.
 */

#ifndef _MTK_ION_COMPAT_H_
#define _MTK_ION_COMPAT_H_

struct dma_buf;

int mtk_ion_publish_dmabuf(struct dma_buf *dmabuf);
void mtk_ion_unpublish_dmabuf(int fd);

#endif /* _MTK_ION_COMPAT_H_ */

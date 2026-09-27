/* SPDX-License-Identifier: GPL-2.0 */
/* 4.19 ION types.  The vcodec's only ION use (mtk_*_ion_config_buff) is already
 * #if 0 in the 4.19 source; buffers reach the HW through dma-buf attach + iommus.
 * Only the opaque types survive, for the dead declarations. */
#ifndef _C419_ION_DRV_H
#define _C419_ION_DRV_H
struct ion_client;
struct ion_device;
struct ion_handle;
#endif

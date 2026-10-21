/* SPDX-License-Identifier: GPL-2.0 */
#ifndef APU_LEGACY_DEVINFO_H
#define APU_LEGACY_DEVINFO_H
#include <linux/types.h>
int apu_legacy_devinfo_init(void);
u32 apu_legacy_devinfo_read(u32 index);
#endif

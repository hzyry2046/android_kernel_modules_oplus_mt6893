// SPDX-License-Identifier: GPL-2.0
/* Lock the existing ARM64 vendor protocol while kernel internals are ported. */
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include "apusys_drv.h"
#include "apu_cmd.h"

static_assert(APUSYS_IOCTL_HANDSHAKE == 0xc0284100UL);
static_assert(sizeof(struct apusys_ioctl_hs) == 40);
static_assert(offsetof(struct apusys_ioctl_hs, type) == 12);
static_assert(offsetof(struct apusys_ioctl_hs, begin) == 16);
static_assert(sizeof(struct apusys_mem) == 56);
static_assert(offsetof(struct apusys_mem, khandle) == 40);
static_assert(offsetof(struct apusys_mem, ctl_data) == 48);
static_assert(sizeof(struct apusys_ioctl_cmd) == 24);
static_assert(offsetof(struct apusys_ioctl_cmd, offset) == 12);
static_assert(sizeof(struct apu_cmd_hdr) == 48);
static_assert(offsetof(struct apu_cmd_hdr, flags) == 24);
static_assert(offsetof(struct apu_cmd_hdr, num_sc) == 32);
static_assert(sizeof(struct apu_sc_hdr_cmn) == 40);
static_assert(offsetof(struct apu_sc_hdr_cmn, mem_ctx) == 28);
static_assert(sizeof(struct apu_fence_hdr) == 16);
static_assert(sizeof(struct apu_mdla_hdr) == 12);

/* Independent MT6893 SSPM layout, never cast from native qos_bound. */
#include "mnoc/1.0/mt6893_qos_snapshot.h"

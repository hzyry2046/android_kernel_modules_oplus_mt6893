/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __SENINF_CFG_H__
#define __SENINF_CFG_H__
#define SENINF_IRQ

#define _CAM_MUX_SWITCH
/*
 * op6893 6.6 bring-up: mt6893 has 8 seninf instances and the DTB /
 * GET_SENINF_MAX_NUM_ID shim reports 8, but this mt6877 config sized the
 * array for 6.  seninf_reg_of_dev() then wrote pseninf_base[6..7] past the
 * end of struct SENINF, clobbering the seninf_mutex that sits right after
 * it -- seninf_open()'s mutex_lock then faulted in queued_spin_lock_slowpath
 * (owner/wait_lock held an of_iomap'd SENINF base).  Size the array for 8.
 */
#define SENINF_MAX_NUM 8

#define SENINF_MAP_BASE_REG  0x1A004000
#define SENINF_MAP_BASE_ANA  0x11C80000
#define SENINF_MAP_BASE_GPIO 0xFFFFFFFF

#define SENINF_MAP_LENGTH_REG  0xA000
#define SENINF_MAP_LENGTH_ANA  0x10000
#define SENINF_MAP_LENGTH_GPIO 0x00000

#endif


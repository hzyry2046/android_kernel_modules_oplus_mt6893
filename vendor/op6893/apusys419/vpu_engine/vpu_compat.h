/* SPDX-License-Identifier: GPL-2.0
 *
 * Forced-include compatibility header for the standalone VPU engine build.
 *
 * struct vpu_device embeds `struct apu_dev_power_data pd` as its first member.
 * That type lives in the native power 2.5 header; this build uses the 4.19
 * power 2.0 header (the enum values must be 4.19's, see the Makefile), so the
 * type has to come from here.
 *
 * The VPU driver only ever writes dev_type/dev_core into it (vpu_power.c), and
 * nothing in this configuration hands the block to a 2.5 registration API, so
 * the layout below is the whole contract that is used.
 */
#ifndef _VPU_COMPAT_H_
#define _VPU_COMPAT_H_

#ifndef APU_DEV_POWER_DATA_DEFINED
#define APU_DEV_POWER_DATA_DEFINED
struct apu_dev_power_data {
	int dev_type;
	int dev_core;
	void *pdata;
};
#endif

#endif /* _VPU_COMPAT_H_ */

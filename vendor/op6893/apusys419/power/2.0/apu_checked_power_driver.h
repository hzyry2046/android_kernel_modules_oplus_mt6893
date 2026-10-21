/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __APU_CHECKED_POWER_DRIVER_H__
#define __APU_CHECKED_POWER_DRIVER_H__

#include "apu_checked_power.h"
#include "apusys_power_user.h"

/* New module API; no vendor ioctl layout changes. Register returns a caller
 * reference. Use checked get/put and close/unref with the core's contract.
 */
int apu_power_checked_client_register(enum DVFS_USER user, struct device *device,
	struct module *module, const struct apu_checked_client_ops *ops, void *ctx,
	struct apu_checked_client **out);
int apu_power_checked_owner_query(struct apu_checked_snapshot *out);
/* Public metadata/request guards include callback faults as well as HAL faults.
 * Backend fault() must keep using the HAL-only cached accessor.
 */
int apu_power_checked_error(void);

/* Internal adapter. These do not authorize resource initialization or PM. */
/* Monotonic module-epoch mode, also true after detach; not a ready/power flag. */
bool apusys_checked_power_present(void);
bool apusys_checked_power_status(enum DVFS_USER user);
int apu_checked_driver_init(struct device *device);
int apu_checked_driver_detach(void);
int apu_checked_driver_run(int (*fn)(void *arg), void *arg);

#endif

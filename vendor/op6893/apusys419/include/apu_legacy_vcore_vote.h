/* SPDX-License-Identifier: GPL-2.0 */
#ifndef APU_LEGACY_VCORE_VOTE_H
#define APU_LEGACY_VCORE_VOTE_H
#include <linux/errno.h>

/* Old OPP 0 is the highest voltage; a released request is -1. */
static inline int apu_legacy_vcore_target(const int *requests, unsigned int users,
		const int *voltages, unsigned int opps, int *target_uv)
{
	unsigned int i;
	int target = 0, opp;

	if (!requests || !voltages || !target_uv || !users || !opps)
		return -EINVAL;
	for (i = 0; i < users; i++) {
		opp = requests[i];
		if (opp == -1)
			continue;
		if (opp < 0 || (unsigned int)opp >= opps || voltages[opp] <= 0)
			return -EINVAL;
		if (voltages[opp] > target)
			target = voltages[opp];
	}
	*target_uv = target;
	return 0;
}
#endif

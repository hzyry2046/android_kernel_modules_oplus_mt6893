/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT6893_LEGACY_VCORE_H
#define MT6893_LEGACY_VCORE_H

#include <linux/errno.h>

/* Old SPM consumers distinguish loaded (1) and fully ready (2). */
static inline int mt6893_legacy_firmware_ready(unsigned long status)
{
	return status == 1 || status == 2;
}

/* Old helio-dvfsrc-opp-mt6893.c, expressed in ascending HW gear order. */
static inline void mt6893_legacy_vcore_table(unsigned int rsrv,
		unsigned int dev69, unsigned int dev134, unsigned int uv[5])
{
	unsigned int rise = dev134 & 7, vb = (dev69 >> 8) & 15;
	unsigned int mode = rsrv & 3, i;

	if (rise > 2)
		rise = 2;
	uv[0] = 575000 + rise * 25000;
	uv[1] = 600000 + rise * 25000;
	uv[2] = 650000 + rise * 25000;
	uv[3] = 725000;
	uv[4] = 750000;
	if ((rsrv & (1U << 7)) && !rise) {
		if (vb > 3)
			vb = 3;
		if (!(rsrv & (1U << 6)) && vb)
			vb--;
		uv[4] -= vb * 12500;
		if (uv[3] > uv[4])
			uv[3] = uv[4];
	}
	for (i = 0; i < 5; i++) {
		if (mode == 3)
			uv[i] = (uv[i] * 95 / 100) / 6250 * 6250;
		else if (mode == 1)
			uv[i] = ((uv[i] * 105 / 100 + 6249) / 6250) * 6250;
	}
}

/* CURRENT_LEVEL bit order is the old 24-entry combined OPP order. */
static inline int mt6893_legacy_current_vcore(unsigned int level)
{
	static const unsigned char gear[] = {
		4, 4, 3, 3, 2, 3, 2, 1, 3, 2, 1, 0,
		3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0,
	};
	unsigned int bit = 0;

	if (!level || (level & (level - 1)) || level >= (1U << 24))
		return -EINVAL;
	while ((level >>= 1))
		bit++;
	return gear[bit];
}

static inline int mt6893_legacy_voltage_check(int voltage, int gear,
		const unsigned int uv[5])
{
	if (voltage < 0)
		return voltage;
	if (!uv || gear < 0 || gear >= 5)
		return -EINVAL;
	/* A larger value alone is not evidence that a request was satisfied. */
	if ((unsigned int)voltage < uv[gear] || (unsigned int)voltage > uv[4])
		return -ERANGE;
	return 0;
}
#endif

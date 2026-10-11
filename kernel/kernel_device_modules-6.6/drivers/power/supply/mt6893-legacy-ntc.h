/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MT6893_LEGACY_NTC_H
#define _MT6893_LEGACY_NTC_H

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/types.h>

/* Original MT6893 board NTC: 166 points, -40..125 C, 1800 mV pull-up. */
static const int legacy_battery_ntc_resistance[] = {
	4251000, 3962000, 3695000, 3447000, 3218000, 3005000, 2807000, 2624000,
	2454000, 2296000, 2149000, 2012000, 1885000, 1767000, 1656000, 1554000,
	1458000, 1369000, 1286000, 1208000, 1135000, 1068000, 1004000, 945000,
	889600, 837800, 789300, 743900, 701300, 661500, 624100, 589000,
	556200, 525300, 496300, 469100, 443500, 419500, 396900, 375600,
	355600, 336800, 319100, 302400, 286700, 271800, 257800, 244700,
	232200, 220500, 209400, 198900, 189000, 179700, 170900, 162500,
	154600, 147200, 140100, 133400, 127000, 121000, 115400, 110000,
	104800, 100000, 95400, 91040, 86900, 82970, 79230, 75690,
	72320, 69120, 66070, 63180, 60420, 57810, 55310, 52940,
	50680, 48530, 46490, 44530, 42670, 40900, 39210, 37601,
	36063, 34595, 33195, 31859, 30584, 29366, 28203, 27091,
	26028, 25013, 24042, 23113, 22224, 21374, 20561, 19782,
	19036, 18323, 17640, 16986, 16360, 15760, 15184, 14631,
	14101, 13592, 13104, 12635, 12187, 11757, 11344, 10947,
	10566, 10200, 9848, 9510, 9185, 8873, 8572, 8283,
	8006, 7738, 7481, 7234, 6997, 6769, 6548, 6337,
	6132, 5934, 5744, 5561, 5384, 5214, 5051, 4893,
	4741, 4594, 4453, 4316, 4184, 4057, 3934, 3816,
	3701, 3591, 3484, 3380, 3281, 3185, 3093, 3003,
	2916, 2832, 2751, 2672, 2596, 2522,
};

static inline int mt6893_legacy_ntc_temperature(int voltage, u32 pull_up_r,
					       int *temperature)
{
	s64 resistance;
	int index, upper, lower;

	if (voltage <= 0 || voltage >= 1800 || !pull_up_r)
		return -ERANGE;

	resistance = div_s64((s64)pull_up_r * voltage,
		1800 - voltage);
	if (resistance >= legacy_battery_ntc_resistance[0]) {
		*temperature = -400;
		return 0;
	}
	if (resistance <= legacy_battery_ntc_resistance[
		ARRAY_SIZE(legacy_battery_ntc_resistance) - 1]) {
		*temperature = 1250;
		return 0;
	}

	for (index = 1; index < ARRAY_SIZE(legacy_battery_ntc_resistance); index++) {
		upper = legacy_battery_ntc_resistance[index - 1];
		lower = legacy_battery_ntc_resistance[index];
		if (resistance >= lower) {
			*temperature = div_s64(((s64)(resistance - lower) *
				(index - 41) + (s64)(upper - resistance) *
				(index - 40)) * 10, upper - lower);
			return 0;
		}
	}
	return -ERANGE;
}

#endif

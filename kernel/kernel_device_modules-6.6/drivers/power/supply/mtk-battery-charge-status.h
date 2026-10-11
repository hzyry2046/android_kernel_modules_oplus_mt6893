/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MTK_BATTERY_CHARGE_STATUS_H
#define _MTK_BATTERY_CHARGE_STATUS_H

/* Caller supplies power_supply status constants and bool. Only valid reads
 * may advance this state; a transient read failure must retain the EOC latch.
 */
static inline int mtk_battery_charge_status(bool online, int status)
{
	if (!online)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	switch (status) {
	case POWER_SUPPLY_STATUS_CHARGING:
	case POWER_SUPPLY_STATUS_DISCHARGING:
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	case POWER_SUPPLY_STATUS_FULL:
		return status;
	default:
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
}

static inline bool mtk_battery_eoc_update(int status, bool *latched)
{
	bool full = status == POWER_SUPPLY_STATUS_FULL;
	bool notify = full && !*latched;

	*latched = full;
	return notify;
}

#endif

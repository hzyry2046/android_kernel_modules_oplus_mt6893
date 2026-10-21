/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MT6893_CHARGER_STATUS_H
#define _MT6893_CHARGER_STATUS_H

#include <linux/types.h>

/* Only successful hardware reads advance the last reported state. */
struct mt6893_charger_status {
	bool valid;
	bool online;
	int status;
	unsigned int changes;
};

static inline bool mt6893_charger_status_update(struct mt6893_charger_status *s,
					      int rc, bool online, int status)
{
	if (rc)
		return false;
	if (s->valid && s->online == online && s->status == status)
		return false;
	s->valid = true;
	s->online = online;
	s->status = status;
	s->changes++;
	return true;
}

#endif

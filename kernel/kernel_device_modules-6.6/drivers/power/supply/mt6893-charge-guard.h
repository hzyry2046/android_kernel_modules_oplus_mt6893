/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MT6893_CHARGE_GUARD_H
#define _MT6893_CHARGE_GUARD_H

#include <linux/types.h>

struct mt6893_guard_temp_state {
	bool ntc_fault;
	bool cold;
	bool hot;
	unsigned int good_reads;
};

/* 4.19 cold/hot boundaries use strict comparisons and 2 C hysteresis. */
static inline void mt6893_guard_temp_update(struct mt6893_guard_temp_state *s,
		int read_rc, int temp_decic, int cold_decic, int hot_decic)
{
	if (read_rc < 0) {
		s->ntc_fault = true;
		s->good_reads = 0;
		return;
	}
	if (s->good_reads < 3)
		s->good_reads++;
	if (s->good_reads == 3)
		s->ntc_fault = false;
	s->cold = temp_decic < cold_decic + (s->cold ? 20 : 0);
	s->hot = temp_decic > hot_decic - (s->hot ? 20 : 0);
}

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MT6893_FULL_OBSERVER_H
#define _MT6893_FULL_OBSERVER_H

#include <linux/types.h>

enum mt6893_full_reason {
	MT6893_FULL_NONE,
	MT6893_FULL_TAPER,
	MT6893_FULL_NONCHARGING,
	MT6893_FULL_VOLTAGE,
	MT6893_FULL_HARDWARE,
};

struct mt6893_full_input {
	bool attached, valid, allowed, enabled, hw_done;
	int zone, vbat_mv, current_ua;
	int sw_mv, hw_mv, recharge_mv, iterm_ua;
	unsigned long long now_ms;
};

/* Observation only: this state never votes, enables charging or signals EOC. */
struct mt6893_full_observer {
	bool full, sampled;
	unsigned int sw_count, hw_count, done_count, recharge_count;
	unsigned int full_events, recharge_events, samples;
	int reason, sw_reason, zone;
	int sw_mv, hw_mv, recharge_mv, iterm_ua;
	unsigned long long last_ms;
};

static inline void mt6893_full_clear_counts(struct mt6893_full_observer *s)
{
	s->sw_count = s->hw_count = s->done_count = s->recharge_count = 0;
	s->sw_reason = MT6893_FULL_NONE;
}

static inline void mt6893_full_observe(struct mt6893_full_observer *s,
				      const struct mt6893_full_input *in)
{
	int reason = MT6893_FULL_NONE;
	unsigned int needed;

	if (!in->attached) {
		s->full = false;
		s->reason = MT6893_FULL_NONE;
		s->sampled = false;
		mt6893_full_clear_counts(s);
		return;
	}
	if (!in->valid || !in->allowed) {
		/* Never join samples across a sensor error, test restriction or stop. */
		s->sampled = false;
		mt6893_full_clear_counts(s);
		return;
	}
	if (s->samples && (s->zone != in->zone || s->sw_mv != in->sw_mv ||
			   s->hw_mv != in->hw_mv || s->recharge_mv != in->recharge_mv ||
			   s->iterm_ua != in->iterm_ua)) {
		/* A warm-zone full candidate must not become a normal-zone EOC. */
		s->full = false;
		s->reason = MT6893_FULL_NONE;
		s->sampled = false;
		mt6893_full_clear_counts(s);
	}
	if (s->sampled) {
		if (in->now_ms < s->last_ms || in->now_ms - s->last_ms > 10000) {
			mt6893_full_clear_counts(s);
		} else if (in->now_ms - s->last_ms < 5000) {
			return; /* TCPC/guard events are not independent 5-second samples. */
		}
	}
	s->sampled = true;
	s->last_ms = in->now_ms;
	s->zone = in->zone;
	s->sw_mv = in->sw_mv;
	s->hw_mv = in->hw_mv;
	s->recharge_mv = in->recharge_mv;
	s->iterm_ua = in->iterm_ua;
	s->samples++;
	if (s->full) {
		if (in->vbat_mv <= in->recharge_mv)
			s->recharge_count++;
		else
			s->recharge_count = 0;
		if (s->recharge_count > 5) {
			s->full = false;
			s->reason = MT6893_FULL_NONE;
			s->recharge_events++;
			mt6893_full_clear_counts(s);
		}
		return;
	}
	if (!in->enabled && !in->hw_done) {
		mt6893_full_clear_counts(s);
		return;
	}
	if (in->enabled && in->vbat_mv > in->sw_mv) {
		if (in->current_ua > 0 && in->current_ua <= in->iterm_ua)
			reason = MT6893_FULL_TAPER;
		else if (in->current_ua <= 0)
			reason = MT6893_FULL_NONCHARGING;
	}
	/* Positive native gauge current is charging; old Oplus icharging is inverted. */
	if (reason == MT6893_FULL_NONE || reason != s->sw_reason)
		s->sw_count = 0;
	s->sw_reason = reason;
	if (reason != MT6893_FULL_NONE)
		s->sw_count++;
	needed = reason == MT6893_FULL_NONCHARGING ? 11 : 6;
	s->hw_count = in->enabled && in->vbat_mv >= in->hw_mv ? s->hw_count + 1 : 0;
	s->done_count = in->hw_done ? s->done_count + 1 : 0;
	if (reason != MT6893_FULL_NONE && s->sw_count >= needed)
		s->reason = reason;
	else if (s->hw_count >= 4)
		s->reason = MT6893_FULL_VOLTAGE;
	else if (s->done_count >= 4)
		s->reason = MT6893_FULL_HARDWARE;
	if (s->reason != MT6893_FULL_NONE) {
		s->full = true;
		s->full_events++;
		mt6893_full_clear_counts(s);
	}
}
#endif

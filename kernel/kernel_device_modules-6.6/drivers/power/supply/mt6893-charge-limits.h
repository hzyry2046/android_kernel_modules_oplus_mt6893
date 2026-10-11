/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MT6893_CHARGE_LIMITS_H
#define _MT6893_CHARGE_LIMITS_H

#define MT6893_LIMIT_ZONES 6

/* Units match the old charger DT: 0.1 C, mA and mV. */
struct mt6893_charge_limits {
	int low[MT6893_LIMIT_ZONES];
	int fcc[2][MT6893_LIMIT_ZONES];
	int fv[MT6893_LIMIT_ZONES];
	int usb_icl, cdp_icl, dcp_icl, pd_icl;
};

/* Tighten at the DT boundary; only relaxation has hysteresis. */
static inline int mt6893_limits_zone(const struct mt6893_charge_limits *c,
				     int previous, int temp, int pd)
{
	int target = 0, next, edge, margin;

	pd = !!pd;
	while (target + 1 < MT6893_LIMIT_ZONES && temp >= c->low[target + 1])
		target++;
	if (previous < 0 || previous >= MT6893_LIMIT_ZONES)
		return target;
	while (previous != target) {
		next = previous + (target > previous ? 1 : -1);
		if (next < 0 || next >= MT6893_LIMIT_ZONES)
			return target;
		edge = target > previous ? c->low[next] : c->low[previous];
		margin = edge == c->low[1] ? 5 : 20;
		if (c->fcc[pd][next] > c->fcc[pd][previous] ||
		    c->fv[next] > c->fv[previous]) {
			if (target > previous ? temp < edge + margin : temp > edge - margin)
				break;
		}
		previous = next;
	}
	return previous;
}

/* Explicit PD contract takes precedence over the BC1.2 classification. */
static inline int mt6893_limits_source(const struct mt6893_charge_limits *c,
				       int usb_kind, int contract_ma)
{
	if (contract_ma > 0)
		return contract_ma < c->pd_icl ? contract_ma : c->pd_icl;
	switch (usb_kind) {
	case 1: return c->usb_icl;
	case 2: return c->cdp_icl;
	case 3: return c->dcp_icl;
	default: return 100; /* No positive source classification yet. */
	}
}
#endif

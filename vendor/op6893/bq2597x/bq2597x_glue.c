// SPDX-License-Identifier: GPL-2.0-only
/*
 * Glue for building the op6893 BQ2597x charge pump outside the v1 tree.
 *
 * Why this exists: on this board the chip's job that matters is OTG VBUS
 * (4.19 log: "oplus_bq2597x_otg_en ok" immediately after "typec vbus_on").
 * The 6.6 charging stack is CONFIG_OPLUS_CHG_V2, and the charger Makefile
 * only descends into v1/ when v2 is off -- so the driver was never built.
 *
 * oplus_bq2597x.c is otherwise self-contained; the only v1 symbol it needs is
 * oplus_vooc_init_cp(), which in the real tree is a one-line setter
 * (v1/oplus_vooc.c: g_vooc_cp = cp).  Providing it here avoids pulling in
 * v1/oplus_vooc.c and its whole vooc state machine, which this board does not
 * run.  Nothing reads the stored pointer on 6.6 -- the v1 vooc core that would
 * is not built -- but keeping the assignment means the driver behaves
 * identically if that ever changes.
 */

#include <linux/module.h>

#include "oplus_vooc.h"

static struct oplus_vooc_cp *g_vooc_cp;

void oplus_vooc_init_cp(struct oplus_vooc_cp *cp)
{
	g_vooc_cp = cp;
}
EXPORT_SYMBOL_GPL(oplus_vooc_init_cp);

/*
 * OTG VBUS control for the rest of the tree.  oplus_bq2597x_otg_en() lives in
 * oplus_bq2597x.c; re-export it under a name that says what it is for, so the
 * extcon VBUS path does not have to know about charge-pump naming.
 */
extern int oplus_bq2597x_otg_en(int en);

int op6893_otg_boost_enable(bool en)
{
	return oplus_bq2597x_otg_en(en ? 1 : 0);
}
EXPORT_SYMBOL_GPL(op6893_otg_boost_enable);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("op6893 BQ2597x charge pump glue (OTG boost entry point)");

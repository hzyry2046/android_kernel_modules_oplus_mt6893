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
#include <linux/delay.h>

#include "oplus_vooc.h"
#include "tcpm.h"

static struct oplus_vooc_cp *g_vooc_cp;

void oplus_vooc_init_cp(struct oplus_vooc_cp *cp)
{
	g_vooc_cp = cp;
}
EXPORT_SYMBOL_GPL(oplus_vooc_init_cp);

/*
 * OTG VBUS control for the rest of the tree.
 *
 * The charge pump is the only thing this entry point has to touch.
 * oplus_bq2597x_otg_en() writes the BQ2597x registers that put 5V on VBUS
 * (0x0f = 0x20 then 0x22 to enable); without it the port is dead no matter
 * what the Type-C state machine thinks.
 *
 * There used to be a tcpm_typec_change_role() here as well, because this
 * board's DTB sets mt-tcpc,role_def = 1 (TYPEC_ROLE_SNK) and a sink-only port
 * never presents Rp.  That is handled earlier now: tcpc_mt6360 promotes
 * role_def SNK to DRP while parsing the property, so the port toggles on its
 * own and walks AttachWait.SRC -> Attached.SRC without help.  Keeping the
 * role change here actively broke it -- by the time this runs the state
 * machine is already in Attached.SRC with wait_ps=Disable, and resetting the
 * role mid-flight restarted it, which emitted a spurious "source vbus 0mV"
 * and switched the pump straight back off:
 *
 *   193.041  ** Attached.SRC / wait_ps=SRC_VSafe5V
 *   193.041  source vbus = 5000mv -> bq2597x on
 *   193.045  wait_ps=Disable
 *   193.046  otg on, typec role -> TRY_SNK        <-- restarts the machine
 *   193.046  source vbus = 0mV -> bq2597x off     <-- pump killed 119us later
 *
 * The 4.19 capture does call this from userspace, but there it happens before
 * the plug event, not in the middle of the attach.
 */
extern int oplus_bq2597x_otg_en(int en);

int op6893_otg_boost_enable(bool en)
{
	int ret;

	ret = oplus_bq2597x_otg_en(en ? 1 : 0);
	if (ret)
		pr_err("%s: bq2597x otg %s failed, ret=%d\n", __func__,
		       en ? "enable" : "disable", ret);

	pr_info("%s: otg %s\n", __func__, en ? "on" : "off");

	return ret;
}
EXPORT_SYMBOL_GPL(op6893_otg_boost_enable);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("op6893 BQ2597x charge pump glue (OTG boost entry point)");

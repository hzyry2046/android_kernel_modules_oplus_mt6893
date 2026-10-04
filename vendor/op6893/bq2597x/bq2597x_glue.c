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
 * Two halves, both needed before anything enumerates:
 *
 *   1. The charge pump.  oplus_bq2597x_otg_en() writes the BQ2597x registers
 *      that put 5V on VBUS (0x0f = 0x20 then 0x22 to enable).  Without it the
 *      port is dead no matter what the Type-C state machine thinks.
 *
 *   2. The Type-C role.  This board's DTB sets mt-tcpc,role_def = 1
 *      (TYPEC_ROLE_SNK), so the tcpc starts in Unattached.SNK / AttachWait.SNK
 *      and only ever presents Rd on CC -- a connected device sees no Rp and
 *      never starts.  4.19 flipped this from userspace
 *      (oplus_set_otg_switch_status -> tcpm_typec_change_role(DRP|SNK)); the
 *      same call is what makes the port capable of being a source here.
 *
 * Ordering follows the 4.19 log, which shows vbus_on before the plug-in event:
 * raise VBUS first, then let the state machine renegotiate.
 */
extern int oplus_bq2597x_otg_en(int en);

static struct tcpc_device *otg_tcpc;

static struct tcpc_device *otg_get_tcpc(void)
{
	if (!otg_tcpc)
		otg_tcpc = tcpc_dev_get_by_name("type_c_port0");
	return otg_tcpc;
}

int op6893_otg_boost_enable(bool en)
{
	struct tcpc_device *tcpc;
	int ret;

	ret = oplus_bq2597x_otg_en(en ? 1 : 0);
	if (ret)
		pr_err("%s: bq2597x otg %s failed, ret=%d\n", __func__,
		       en ? "enable" : "disable", ret);

	tcpc = otg_get_tcpc();
	if (!tcpc) {
		pr_info("%s: tcpc not up yet, role left as-is\n", __func__);
		return ret;
	}

	/*
	 * DRP rather than SRC: 4.19 used DRP, and it keeps a charger able to
	 * attach the normal way once OTG is switched off again.
	 */
	tcpm_typec_change_role(tcpc, en ? TYPEC_ROLE_DRP : TYPEC_ROLE_SNK);
	pr_info("%s: otg %s, typec role -> %s\n", __func__,
		en ? "on" : "off", en ? "DRP" : "SNK");

	return ret;
}
EXPORT_SYMBOL_GPL(op6893_otg_boost_enable);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("op6893 BQ2597x charge pump glue (OTG boost entry point)");

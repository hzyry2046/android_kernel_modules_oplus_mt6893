// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#include <linux/extcon-provider.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>

#include "extcon-mtk-usb.h"

#if IS_ENABLED(CONFIG_MTK_CHARGER)
#include "charger_class.h"
#endif
/* op6893 6.6 OTG bring-up: matches the 4.19 MTK6765S-era default. */
#define OTG_BOOST_CURR_DEFAULT	1100000

/*
 * OTG VBUS enable, exported by vendor/op6893/bq2597x/bq2597x-charger.ko.
 *
 * Do NOT declare this weak.  A weak reference makes the compiler emit
 * R_AARCH64_PLT32 (311) through the PLT, and arch/arm64/kernel/module.c has no
 * case for it -- "module extcon_mtk_usb: unsupported RELA relocation: 311",
 * the module fails to load and init dies with it.  symbol_request() resolves
 * the name at runtime and pulls the module in if needed, which also keeps this
 * driver loadable on a board without the pump.
 */
extern int op6893_otg_boost_enable(bool en);

#if IS_ENABLED(CONFIG_TCPC_CLASS)
#ifdef CONFIG_OPLUS_PD_EXT_SUPPORT
#include "../../../power/oplus/pd_ext/inc/tcpm.h"
#else
#include <tcpm.h>
#endif
#endif

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static void mtk_usb_extcon_update_role(struct work_struct *work)
{
	struct usb_role_info *role = container_of(to_delayed_work(work),
					struct usb_role_info, dwork);
	struct mtk_extcon_info *extcon = role->extcon;
	unsigned int cur_dr, new_dr;

	cur_dr = extcon->c_role;
	new_dr = role->d_role;

	extcon->c_role = new_dr;

	dev_info(extcon->dev, "cur_dr(%d) new_dr(%d)\n", cur_dr, new_dr);

	/* none -> device */
	if (cur_dr == USB_ROLE_NONE &&
			new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	/* none -> host */
	} else if (cur_dr == USB_ROLE_NONE &&
			new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* device -> none */
	} else if (cur_dr == USB_ROLE_DEVICE &&
			new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
	/* host -> none */
	} else if (cur_dr == USB_ROLE_HOST &&
			new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
	/* device -> host */
	} else if (cur_dr == USB_ROLE_DEVICE &&
			new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
		extcon_set_state_sync(extcon->edev,	EXTCON_USB_HOST, true);
	/* host -> device */
	} else if (cur_dr == USB_ROLE_HOST &&
			new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
		extcon_set_state_sync(extcon->edev,	EXTCON_USB, true);
	}

	/* usb role switch */
	if (extcon->role_sw)
		usb_role_switch_set_role(extcon->role_sw, new_dr);

	kfree(role);
}

static int mtk_usb_extcon_set_role(struct mtk_extcon_info *extcon,
						unsigned int role)
{
	struct usb_role_info *role_info;

	/* create and prepare worker */
	role_info = kzalloc(sizeof(*role_info), GFP_ATOMIC);
	if (!role_info)
		return -ENOMEM;

	INIT_DELAYED_WORK(&role_info->dwork, mtk_usb_extcon_update_role);

	role_info->extcon = extcon;
	role_info->d_role = role;
	/* issue connection work */
	queue_delayed_work(extcon->extcon_wq, &role_info->dwork, 0);

	return 0;
}

static bool usb_is_online(struct mtk_extcon_info *extcon)
{
	union power_supply_propval pval;
	union power_supply_propval tval;
	int ret;

	/* op6893 6.6 OTG: bind late -- primary_chg probes after us. */
	if (!extcon->usb_psy)
		extcon->usb_psy = power_supply_get_by_name("primary_chg");
	if (!extcon->usb_psy)
		return false;

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get online prop\n");
		return false;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_TYPE, &tval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get usb type\n");
		return false;
	}

	dev_info(extcon->dev, "online=%d, type=%d\n", pval.intval, tval.intval);

	if (pval.intval && (tval.intval == POWER_SUPPLY_TYPE_USB ||
			tval.intval == POWER_SUPPLY_TYPE_USB_CDP))
		return true;
	else
		return false;
}

static void mtk_usb_extcon_psy_detector(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_psy);

	/* Workaround for PR_SWAP, IF tcpc_dev, then do not switch role. */
	/* Since we will set USB to none when type-c plug out */
	#if IS_ENABLED(CONFIG_TCPC_CLASS)
	if (extcon->tcpc_dev) {
		if (usb_is_online(extcon) && extcon->c_role == USB_ROLE_NONE)
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
	} else {
	#endif
		if (usb_is_online(extcon))
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		else
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	}
#endif
}

static int mtk_usb_extcon_psy_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct mtk_extcon_info *extcon = container_of(nb,
					struct mtk_extcon_info, psy_nb);

	if (event != PSY_EVENT_PROP_CHANGED || psy != extcon->usb_psy)
		return NOTIFY_DONE;

	queue_delayed_work(system_power_efficient_wq, &extcon->wq_psy, 0);

	return NOTIFY_DONE;
}

static int mtk_usb_extcon_psy_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	if (!of_property_read_bool(dev->of_node, "charger")) {
		ret = -EINVAL;
		goto fail;
	}

	extcon->usb_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR_OR_NULL(extcon->usb_psy)) {
		/* try to get by name */
		extcon->usb_psy = power_supply_get_by_name("primary_chg");
		if (IS_ERR_OR_NULL(extcon->usb_psy)) {
			/*
			 * op6893 6.6 OTG bring-up: primary_chg is an i2c device
			 * that probes after extcon, so the lookup legitimately
			 * fails here.  Don't give up on the whole psy chain --
			 * retry lazily from the notifier path, the same way the
			 * charging stack binds its own psy handles.
			 */
			dev_info(dev, "usb_psy not up yet, will retry lazily\n");
			extcon->usb_psy = NULL;
		}
	}

	INIT_DELAYED_WORK(&extcon->wq_psy, mtk_usb_extcon_psy_detector);

	extcon->psy_nb.notifier_call = mtk_usb_extcon_psy_notifier;
	ret = power_supply_reg_notifier(&extcon->psy_nb);
	if (ret)
		dev_err(dev, "fail to register notifer\n");
fail:
	return ret;
}

/*
 * op6893 6.6 OTG bring-up: the regulator path below is dead on this board.
 * extcon_usb has no vbus-supply in the stock 4.19 DTB, so extcon->vbus stays
 * NULL and the whole function used to return early -- OTG VBUS was never
 * driven.  Fall back to the real supply.
 *
 * Two supplies have to come up, and they are not interchangeable:
 *
 *   1. The BQ2597x charge pump (i2c-7 @0x65), which is what actually puts
 *      5V on the port.  The 4.19 capture shows it plainly --
 *        typec vbus_on -> oplus_bq2597x_otg_en ok -> OTG plug in
 *      It comes from vendor/op6893/bq2597x/ (see that Kbuild for why it is
 *      built out of tree).
 *
 *   2. The MT6360 PMU charger's own boost, via primary_chg.  4.19 does this
 *      too, right beside the pump call:
 *        vooc_enable_cp_for_otg(1) / charger_dev_enable_otg(primary_chg, 1)
 *        charger_dev_set_boost_current_limit(primary_chg, 1100000)
 *      This is the half that makes the port enumerate.  The MT6360 is its
 *      own VBUS sensor -- TCPC_V10_REG_POWER_STATUS.VBUS_PRES and the
 *      MT6360_M_VBUS_VALID vendor alert both come off this rail -- so with
 *      only the pump running the chip never reports VBUS_PRES, the tcpc
 *      stays at wait_ps=SRC_VSafe5V, TYPEC_ATTACHED_SRC is never assigned
 *      and mtu3 never switches to host.  Measured on the live chip with the
 *      pump on and this call missing:
 *        reg0x1e (POWER_STATUS) = 0x08   <- bit2 VBUS_PRES clear
 *        reg0x9b (MT_ST1)       = 0x02   <- bit5 VBUS_VALID clear
 *
 * Order matters: boost first, then the pump, matching 4.19.
 */
static int mtk_usb_extcon_set_vbus_charger(struct mtk_extcon_info *extcon,
							bool is_on)
{
	int (*otg_en)(bool);
	struct charger_device *chg;
	int ret = 0;

	dev_info(extcon->dev, "%s: is_on=%d\n", __func__, is_on);

	chg = get_charger_by_name("primary_chg");
	if (chg) {
		if (is_on) {
			ret = charger_dev_set_boost_current_limit(chg,
								  extcon->vbus_cur ?
								  extcon->vbus_cur :
								  OTG_BOOST_CURR_DEFAULT);
			if (ret < 0)
				dev_info(extcon->dev, "set boost current failed\n");
			ret = charger_dev_enable_otg(chg, true);
		} else {
			ret = charger_dev_enable_otg(chg, false);
		}
		if (ret < 0)
			dev_info(extcon->dev, "charger otg %s failed, ret=%d\n",
				 is_on ? "enable" : "disable", ret);
	} else {
		dev_info(extcon->dev, "no primary_chg; pump only\n");
	}

	/* Resolve at runtime; see the note on the declaration above for why
	 * this must not be a direct call. */
	otg_en = symbol_request(op6893_otg_boost_enable);
	if (otg_en) {
		ret = otg_en(is_on);
		symbol_put(op6893_otg_boost_enable);
	}

	extcon->vbus_on = is_on;
	return ret;
}

static int mtk_usb_extcon_set_vbus(struct mtk_extcon_info *extcon,
							bool is_on)
{
	struct regulator *vbus = extcon->vbus;
	struct device *dev = extcon->dev;
	int ret;

	/*
	 * op6893 6.6: no regulator on this DTB -- drive the charger's boost
	 * instead of silently doing nothing.
	 */
	if (!vbus)
		return mtk_usb_extcon_set_vbus_charger(extcon, is_on);

	if (extcon->vbus_on == is_on)
		return 0;

	dev_info(dev, "vbus turn %s\n", is_on ? "on" : "off");

	if (is_on) {
		if (extcon->vbus_vol) {
			ret = regulator_set_voltage(vbus,
					extcon->vbus_vol, extcon->vbus_vol);
			if (ret) {
				dev_err(dev, "vbus regulator set voltage failed\n");
				return ret;
			}
		}

		if (extcon->vbus_cur) {
			ret = regulator_set_current_limit(vbus,
					extcon->vbus_cur, extcon->vbus_cur);
			if (ret) {
				dev_err(dev, "vbus regulator set current failed\n");
				return ret;
			}
		}

		ret = regulator_enable(vbus);
		if (ret) {
			dev_info(dev, "vbus regulator enable failed\n");
			return ret;
		}
	} else {
		regulator_disable(vbus);
		/* Restore to default state */
		extcon->vbus_cur_inlimit = 0;
	}

	extcon->vbus_on = is_on;

	return 0;
}

static ssize_t vbus_limit_cur_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);
	struct regulator *vbus = extcon->vbus;

	if (!vbus)
		return sprintf(buf, "0");
	else
		return sprintf(buf, "%d\n", extcon->vbus_cur_inlimit);
}

static ssize_t vbus_limit_cur_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);
	struct regulator *vbus = extcon->vbus;
	int ret, in_limit = 0;
	unsigned int vbus_cur = 0;
	/* Check whether we have vbus instance */
	if (!vbus) {
		dev_info(dev, "No vbus instance\n");
		return -EOPNOTSUPP;
	}
	if ((kstrtoint(buf, 10, &in_limit) != 0) || (in_limit != 1  && in_limit != 0)) {
		dev_info(dev, "Invalid input\n");
		return -EINVAL;
	}

	extcon->vbus_cur_inlimit = (in_limit == 1);
	/* Only operate while vbus is on */
	if (extcon->vbus_on) {
		if (extcon->vbus_cur_inlimit && extcon->vbus_limit_cur)
			vbus_cur = extcon->vbus_limit_cur;
		else if (!extcon->vbus_cur_inlimit && extcon->vbus_cur)
			vbus_cur = extcon->vbus_cur;
	}
	if (vbus_cur) {
		ret = regulator_set_current_limit(vbus, vbus_cur, vbus_cur);
		if (ret) {
			dev_info(dev, "vbus regulator set current failed\n");
			return -EIO;
		}
	} else
		dev_info(dev, "Do not change current\n");
	return count;
}

static DEVICE_ATTR_RW(vbus_limit_cur);

static ssize_t vbus_switch_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);
	struct regulator *vbus = extcon->vbus;

	if (!vbus)
		return sprintf(buf, "0");
	else
		return sprintf(buf, "%d\n", extcon->vbus_on);
}

static ssize_t vbus_switch_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);
	int is_on = 0;

	/*
	 * op6893 6.6 OTG: there is no vbus regulator on this DTB, but
	 * mtk_usb_extcon_set_vbus() now falls back to the BQ2597x charge pump,
	 * so do not refuse the write just because extcon->vbus is NULL.  This
	 * attribute is also the manual trigger for OTG while the automatic path
	 * is still being wired up.
	 */
	if ((kstrtoint(buf, 10, &is_on) != 0) || (is_on != 1  && is_on != 0)) {
		dev_info(dev, "Invalid input\n");
		return -EINVAL;
	}

	/* Mark the request so the tcpc's spurious 0 mV SOURCE_VBUS report
	 * cannot cancel it (see mtk_extcon_tcpc_notifier). */
	extcon->otg_requested = !!is_on;

	mtk_usb_extcon_set_vbus(extcon, is_on);

	return count;
}

static DEVICE_ATTR_RW(vbus_switch);

static int mtk_usb_extcon_vbus_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	/*
	 * op6893 6.6 OTG: this DTB has no vbus-supply, so the regulator is
	 * unavailable -- but that is no longer a reason to bail out of the whole
	 * function.  mtk_usb_extcon_set_vbus() drives the BQ2597x charge pump
	 * when extcon->vbus is NULL, and the vbus_switch attribute at the end of
	 * this function is the manual trigger for that.  Returning early here
	 * skipped device_create_file() and left no way to switch OTG on at all.
	 */
	if (!of_property_read_bool(dev->of_node, "vbus-supply")) {
		dev_info(dev, "no vbus-supply; using charge-pump path\n");
		extcon->vbus = NULL;
		goto no_regulator;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	extcon->vbus = devm_regulator_get(dev, "vbus");
#else
	extcon->vbus =  devm_regulator_get_exclusive(dev, "vbus");
#endif

	if (IS_ERR(extcon->vbus)) {
		/* try to get by name */
#ifdef OPLUS_FEATURE_CHG_BASIC
		extcon->vbus = devm_regulator_get(dev, "usb-otg-vbus");
#else
		extcon->vbus =  devm_regulator_get_exclusive(dev, "usb-otg-vbus");
#endif

		if (IS_ERR(extcon->vbus)) {
			dev_err(dev, "failed to get vbus\n");
			ret = PTR_ERR(extcon->vbus);
			extcon->vbus = NULL;
			goto fail;
		}
	}

	/* sync vbus state */
	extcon->vbus_on = regulator_is_enabled(extcon->vbus);
	dev_info(dev, "vbus is %s\n", extcon->vbus_on ? "on" : "off");

no_regulator:
	if (!of_property_read_u32(dev->of_node, "vbus-voltage",
				&extcon->vbus_vol))
		dev_info(dev, "vbus-voltage=%d", extcon->vbus_vol);

	if (!of_property_read_u32(dev->of_node, "vbus-current",
				&extcon->vbus_cur))
		dev_info(dev, "vbus-current=%d", extcon->vbus_cur);
	if (!of_property_read_u32(dev->of_node, "vbus-limit-current",
				&extcon->vbus_limit_cur)) {
		dev_info(dev, "vbus limited current=%d", extcon->vbus_limit_cur);
		extcon->vbus_cur_inlimit = 0;
		ret = device_create_file(dev, &dev_attr_vbus_limit_cur);
		if (ret)
			dev_info(dev, "failed to create vbus currnet limit control node\n");
	}

	ret = device_create_file(dev, &dev_attr_vbus_switch);
	if (ret)
		dev_info(dev, "failed to create vbus switch node\n");

	ret = 0;
fail:
	return ret;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
static int mtk_extcon_tcpc_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct mtk_extcon_info *extcon =
			container_of(nb, struct mtk_extcon_info, tcpc_nb);
	struct device *dev = extcon->dev;

#ifndef OPLUS_FEATURE_CHG_BASIC
	/* oplus add for DX-4 charge */
	bool vbus_on;
#endif

	switch (event) {

	case TCP_NOTIFY_SOURCE_VBUS:
		dev_info(dev, "source vbus = %dmv\n",
				 noti->vbus_state.mv);
		/*
		 * op6893 6.6 OTG: two independent mistakes were fixed here.
		 *
		 * (a) The event used to only ever switch VBUS *on*: the 0 mV
		 * case was swallowed by the `if (mv)` guard, so nothing ever
		 * called set_vbus(false).  That left our own boost running
		 * after the dock was unplugged -- the port still had 5V on it
		 * and no Rp, which the tcpc duly read as a charger:
		 *
		 *   269.515  Attached-> NULL            <- dock removed
		 *   269.518  detached, tcpc_attach=0
		 *   269.518  source vbus = 0mv          <- but boost stayed on
		 *   269.820  ** Attached-> NORP_SRC     <- 5V with no Rp
		 *   269.820  Charger plug in
		 *   269.825  SNK attached, tcpc_attach=1 <- false charging
		 *
		 * 4.19 drives this both ways (`vbus_on = mv ? true : false`),
		 * so do the same.
		 *
		 * (b) The `otg_requested` guard below exists because the tcpc
		 * reports 0 mV in the window between a role change and the
		 * supply ramping; acting on that used to switch OTG straight
		 * back off (set_vbus(1) -> role DRP -> "0mV" -> set_vbus(0)).
		 * That guard is still right for the *ramp* window, but it must
		 * not outlive the attach: on a real detach the 0 mV means
		 * VBUS should go away, so clear the flag and honour it.
		 */
		if (!noti->vbus_state.mv && extcon->otg_requested) {
			if (extcon->c_role == USB_ROLE_HOST) {
				dev_info(dev, "source vbus 0mV while host -- ignored\n");
				break;
			}
			extcon->otg_requested = false;
		}
#ifdef OPLUS_FEATURE_CHG_BASIC
		/*
		 * op6893 6.6: this DTB has no oplus,vbus_set, so the guard below
		 * swallowed the event and the charger boost was never told to
		 * turn on.  Run it when there is no regulator either -- that is
		 * exactly the case the charger fallback exists for.
		 */
		if (extcon->oplus_vbus_set || !extcon->vbus)
			mtk_usb_extcon_set_vbus(extcon, !!noti->vbus_state.mv);
#else
		vbus_on = (noti->vbus_state.mv) ? true : false;
		mtk_usb_extcon_set_vbus(extcon, vbus_on);
#endif /* OPLUS_FEATURE_CHG_BASIC */

		break;

	case TCP_NOTIFY_TYPEC_STATE:
		dev_info(dev, "old_state=%d, new_state=%d\n",
				noti->typec_state.old_state,
				noti->typec_state.new_state);
		/*
		 * op6893 6.6 OTG: match on new_state alone.
		 *
		 * Requiring old_state == TYPEC_UNATTACHED missed the case that
		 * actually happens here.  With a dock attached the port is first
		 * seen as a sink, so the sequence is
		 *   AttachWait.SRC -> Try.SNK -> TryWait.SRC -> Attached.SRC
		 * and by the time TYPEC_ATTACHED_SRC arrives the old_state is
		 * TYPEC_ATTACHED_SNK.  Every branch below also keys on
		 * TYPEC_UNATTACHED, so the role was never switched to host, mtu3
		 * never brought up xhci, and nothing enumerated -- while the
		 * tcpc and the charge pump had both done their part.
		 */
		if (noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			dev_info(dev, "Type-C SRC plug in\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
		} else if (!(extcon->bypss_typec_sink) &&
			noti->typec_state.old_state == TYPEC_UNATTACHED &&
			(noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.new_state == TYPEC_ATTACHED_DBGACC_SNK)) {
			dev_info(dev, "Type-C SINK plug in\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if (extcon->bypss_typec_sink &&
			noti->typec_state.old_state == TYPEC_UNATTACHED &&
			(noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC)) {
			/* op6893 6.6 bring-up: bypass prop is set on this DTB,
			 * but with no ID/VBUS GPIO and no other role driver,
			 * nothing else cuts device role on a PD power bank. */
			dev_info(dev, "Type-C SINK plug in (bypass compat)\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_DBGACC_SNK) &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			dev_info(dev, "Type-C plug out\n");
			/*
			 * op6893 6.6 OTG: take VBUS down with the role.
			 *
			 * This is a real detach, so whatever we were sourcing has
			 * to stop -- otherwise the port keeps 5V with no Rp and
			 * the tcpc reads that as a charger on the next scan (see
			 * the NORP_SRC note in TCP_NOTIFY_SOURCE_VBUS below).
			 * 4.19 does this from the same place: it calls set_vbus()
			 * right beside set_role(), so both go down together.
			 */
			mtk_usb_extcon_set_vbus(extcon, false);
			extcon->otg_requested = false;
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
		}
		break;
	case TCP_NOTIFY_DR_SWAP:
		dev_info(dev, "%s dr_swap, new role=%d\n",
				__func__, noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_UFP &&
				extcon->c_role != USB_ROLE_DEVICE) {
			dev_info(dev, "switch role to device\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if (noti->swap_state.new_role == PD_ROLE_DFP &&
				extcon->c_role != USB_ROLE_HOST) {
			dev_info(dev, "switch role to host\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
		} else
			dev_info(dev, "wrong condition\n");

		break;
	}

	return NOTIFY_OK;
}

static int mtk_usb_extcon_tcpc_init(struct mtk_extcon_info *extcon)
{
	struct tcpc_device *tcpc_dev;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0) {
		/* op6893 6.6 bring-up: no "tcpc" prop on this DTB; fall back
		 * to the canonical name instead of leaving the notifier
		 * unregistered.  Defer silently (-EPROBE_DEFER style) when
		 * the tcpc device is not up yet. */
		tcpc_name = "type_c_port0";
		dev_info(extcon->dev, "no tcpc prop, fallback to %s\n",
			 tcpc_name);
	}

	tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!tcpc_dev) {
		if (extcon->tcpc_fallback == 0)
			dev_info(extcon->dev,
				 "tcpc %s not ready, defer fallback bind\n",
				 tcpc_name);
		extcon->tcpc_fallback = -1;
		return -ENODEV;
	}

	extcon->tcpc_nb.notifier_call = mtk_extcon_tcpc_notifier;
	ret = register_tcp_dev_notifier(tcpc_dev, &extcon->tcpc_nb,
		TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS |
		TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		dev_err(extcon->dev, "register notifer fail\n");
		return -EINVAL;
	}

	extcon->tcpc_dev = tcpc_dev;
	if (extcon->tcpc_fallback < 0)
		dev_info(extcon->dev, "tcpc fallback bind ok\n");
	extcon->tcpc_fallback = 1;

	return 0;
}
#endif

static void mtk_usb_extcon_tcpc_retry_work(struct work_struct *work)
{
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_extcon_info *extcon = container_of(dwork,
		struct mtk_extcon_info, wq_tcpc_retry);
	int ret;

	if (extcon->tcpc_dev)
		return;
	ret = mtk_usb_extcon_tcpc_init(extcon);
	if (ret < 0)
		queue_delayed_work(system_power_efficient_wq,
			&extcon->wq_tcpc_retry, msecs_to_jiffies(2000));
#endif
}

static void mtk_usb_extcon_detect_cable(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_detcable);
	enum usb_role role;
	int id, vbus;

	/* check ID and VBUS */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;
	vbus = extcon->vbus_gpiod ?
		gpiod_get_value_cansleep(extcon->vbus_gpiod) : id;

	/* check if vbus detect by charger */
	if (extcon->usb_psy)
		vbus = 0;

	if (!id)
		role = USB_ROLE_HOST;
	else if (vbus)
		role = USB_ROLE_DEVICE;
	else
		role = USB_ROLE_NONE;

	dev_dbg(extcon->dev, "id %d, vbus %d, set role: %s\n",
			id, vbus, usb_role_string(role));

	if (role == USB_ROLE_HOST)
		mtk_usb_extcon_set_vbus(extcon, true);
	else
		mtk_usb_extcon_set_vbus(extcon, false);

	mtk_usb_extcon_set_role(extcon, role);
}

static irqreturn_t mtk_usb_gpio_handle(int irq, void *dev_id)
{
	struct mtk_extcon_info *extcon = dev_id;

	/* issue detection work */
	queue_delayed_work(system_power_efficient_wq, &extcon->wq_detcable, 0);

	return IRQ_HANDLED;
}

static int mtk_usb_extcon_gpio_init(struct mtk_extcon_info *extcon)
{
	struct device *dev = extcon->dev;
	int ret = 0;

	extcon->id_gpiod = devm_gpiod_get_optional(dev, "id", GPIOD_IN);
	if (IS_ERR(extcon->id_gpiod)) {
		dev_info(dev, "get id gpio err\n");
		return PTR_ERR(extcon->id_gpiod);
	}

	extcon->vbus_gpiod = devm_gpiod_get_optional(dev, "vbus", GPIOD_IN);
	if (IS_ERR(extcon->vbus_gpiod)) {
		dev_info(dev, "get vbus gpio err\n");
		return PTR_ERR(extcon->vbus_gpiod);
	}

	if (!extcon->id_gpiod && !extcon->vbus_gpiod) {
		dev_info(dev, "failed to get gpios\n");
		return -ENODEV;
	}

	if (extcon->id_gpiod)
		ret = gpiod_set_debounce(extcon->id_gpiod, USB_GPIO_DEB_US);
	if (extcon->vbus_gpiod)
		ret = gpiod_set_debounce(extcon->vbus_gpiod, USB_GPIO_DEB_US);

	INIT_DELAYED_WORK(&extcon->wq_detcable, mtk_usb_extcon_detect_cable);

	if (extcon->id_gpiod) {
		extcon->id_irq = gpiod_to_irq(extcon->id_gpiod);
		if (extcon->id_irq < 0) {
			dev_info(dev, "failed to get ID IRQ\n");
			return extcon->id_irq;
		}
		ret = devm_request_threaded_irq(dev, extcon->id_irq,
						NULL, mtk_usb_gpio_handle,
						USB_GPIO_IRQ_FLAG,
						dev_name(dev), extcon);
		if (ret < 0) {
			dev_info(dev, "failed to request ID IRQ\n");
			return ret;
		}
	}

	if (extcon->vbus_gpiod) {
		extcon->vbus_irq = gpiod_to_irq(extcon->vbus_gpiod);
		if (extcon->vbus_irq < 0) {
			dev_info(dev, "failed to get VBUS IRQ\n");
			return extcon->vbus_irq;
		}
		ret = devm_request_threaded_irq(dev, extcon->vbus_irq,
						NULL, mtk_usb_gpio_handle,
						USB_GPIO_IRQ_FLAG,
						dev_name(dev), extcon);
		if (ret < 0) {
			dev_info(dev, "failed to request VBUS IRQ\n");
			return ret;
		}
	}

	/* get id/vbus pin value when boot on */
	queue_delayed_work(system_power_efficient_wq,
			   &extcon->wq_detcable,
			   msecs_to_jiffies(10));

	return 0;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
#define PROC_FILE_SMT "mtk_typec"
#define FILE_SMT_U2_CC_MODE "smt_u2_cc_mode"

static int usb_cc_smt_procfs_show(struct seq_file *s, void *unused)
{
	struct mtk_extcon_info *extcon = s->private;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	uint8_t cc1, cc2;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	extcon->tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!extcon->tcpc_dev)
		return -ENODEV;

	tcpm_inquire_remote_cc(extcon->tcpc_dev, &cc1, &cc2, false);
	dev_info(extcon->dev, "cc1=%d, cc2=%d\n", cc1, cc2);

	if (cc1 == TYPEC_CC_VOLT_OPEN || cc1 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else if (cc2 == TYPEC_CC_VOLT_OPEN || cc2 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else
		seq_puts(s, "1\n");

	return 0;
}

static int usb_cc_smt_procfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, usb_cc_smt_procfs_show, pde_data(inode));
}

static const struct  proc_ops usb_cc_smt_procfs_fops = {
	.proc_open = usb_cc_smt_procfs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int mtk_usb_extcon_procfs_init(struct mtk_extcon_info *extcon)
{
	struct proc_dir_entry *file, *root;
	int ret = 0;

	root = proc_mkdir(PROC_FILE_SMT, NULL);
	if (!root) {
		dev_info(extcon->dev, "fail creating proc dir: %s\n",
			PROC_FILE_SMT);
		ret = -ENOMEM;
		goto error;
	}

	file = proc_create_data(FILE_SMT_U2_CC_MODE, 0400, root,
		&usb_cc_smt_procfs_fops, extcon);
	if (!file) {
		dev_info(extcon->dev, "fail creating proc file: %s\n",
			FILE_SMT_U2_CC_MODE);
		ret = -ENOMEM;
		goto error;
	}

	dev_info(extcon->dev, "success creating proc file: %s\n",
		FILE_SMT_U2_CC_MODE);

error:
	dev_info(extcon->dev, "%s ret:%d\n", __func__, ret);
	return ret;
}
#endif

static int mtk_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_extcon_info *extcon;
	struct device_node *conn_np;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	const char *tcpc_name;
#endif
	int ret;

	extcon = devm_kzalloc(&pdev->dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon)
		return -ENOMEM;

	extcon->dev = dev;

	/* extcon */
	extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(extcon->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, extcon->edev);
	if (ret < 0) {
		dev_info(dev, "failed to register extcon device\n");
		return ret;
	}

	/*
	 * usb role switch
	 *
	 * op6893 6.6 OTG: this DTB does not use the generic usb-role-switch
	 * binding between extcon_usb and mtu3; it points at the controller
	 * with MTK's own "dev-conn" phandle.  usb_role_switch_get() walks
	 * fwnode_connection_find_match(), which looks for a property *named*
	 * "usb-role-switch" and finds nothing here, so it returns NULL -- and
	 * mtk_usb_extcon_update_role() then skips usb_role_switch_set_role()
	 * entirely, leaving mtu3 in whatever role it probed with.  That is why
	 * the port stayed host after the dock was removed, why the UDC could
	 * never bind (no adb over USB), and why no "set role" ever appeared in
	 * mtu3's log.
	 *
	 * 4.19 solves this by building a device_connection by hand.  That API
	 * is gone in 6.6 (struct device_connection and device_connection_add()
	 * were removed once everything moved to fwnode), so resolve it the
	 * modern way instead: follow dev-conn ourselves and ask the role-switch
	 * class for the switch registered on that fwnode.  mtu3 registers its
	 * switch with role_sx_desc.fwnode = dev_fwnode(dev), so this lands on
	 * the right device.
	 */
	conn_np = of_parse_phandle(dev->of_node, "dev-conn", 0);
	if (!conn_np) {
		dev_info(dev, "failed to get dev-conn node\n");
		return -EINVAL;
	}

	extcon->role_sw = usb_role_switch_find_by_fwnode(of_fwnode_handle(conn_np));
	of_node_put(conn_np);
	if (IS_ERR(extcon->role_sw)) {
		dev_err(dev, "failed to get usb role\n");
		return PTR_ERR(extcon->role_sw);
	}

	if (!extcon->role_sw)
		dev_info(dev, "no usb role switch resolved\n");
	else
		dev_info(dev, "usb role switch resolved via dev-conn\n");

	/* initial usb role */
	if (extcon->role_sw)
		extcon->c_role = USB_ROLE_NONE;

	/* vbus */
	ret = mtk_usb_extcon_vbus_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init vbus\n");

	extcon->bypss_typec_sink =
		of_property_read_bool(dev->of_node,
			"mediatek,bypss-typec-sink");
#ifdef OPLUS_FEATURE_CHG_BASIC
	extcon->oplus_vbus_set =
		of_property_read_bool(dev->of_node,
			"oplus,vbus_set");
#endif /* OPLUS_FEATURE_CHG_BASIC */
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	ret = of_property_read_string(dev->of_node, "tcpc", &tcpc_name);
	if (of_property_read_bool(dev->of_node, "mediatek,u2") && ret == 0
		&& strcmp(tcpc_name, "type_c_port0") == 0) {
		mtk_usb_extcon_procfs_init(extcon);
	}
#endif

	extcon->extcon_wq = create_singlethread_workqueue("extcon_usb");
	if (!extcon->extcon_wq)
		return -ENOMEM;

	/* power psy */
	ret = mtk_usb_extcon_psy_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init psy\n");

	/* get id/vbus gpio resources */
	ret = mtk_usb_extcon_gpio_init(extcon);
	if (ret < 0)
		dev_info(dev, "failed to init id/vbus pin\n");

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	/* tcpc */
	INIT_DELAYED_WORK(&extcon->wq_tcpc_retry,
		mtk_usb_extcon_tcpc_retry_work);
	ret = mtk_usb_extcon_tcpc_init(extcon);
	if (ret < 0) {
		dev_info(dev, "tcpc bind deferred, retry in background\n");
		queue_delayed_work(system_power_efficient_wq,
			&extcon->wq_tcpc_retry, msecs_to_jiffies(2000));
	}
#endif

	platform_set_drvdata(pdev, extcon);

	return 0;
}

static int mtk_usb_extcon_remove(struct platform_device *pdev)
{
	return 0;
}

static void mtk_usb_extcon_shutdown(struct platform_device *pdev)
{
	struct mtk_extcon_info *extcon = platform_get_drvdata(pdev);

	dev_info(extcon->dev, "shutdown\n");

	mtk_usb_extcon_set_vbus(extcon, false);
}

static int __maybe_unused mtk_usb_extcon_suspend(struct device *dev)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (extcon->id_gpiod)
			enable_irq_wake(extcon->id_irq);
		if (extcon->vbus_gpiod)
			enable_irq_wake(extcon->vbus_irq);
		return 0;
	}

	if (extcon->id_gpiod)
		disable_irq(extcon->id_irq);
	if (extcon->vbus_gpiod)
		disable_irq(extcon->vbus_irq);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused mtk_usb_extcon_resume(struct device *dev)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (extcon->id_gpiod)
			disable_irq_wake(extcon->id_irq);
		if (extcon->vbus_gpiod)
			disable_irq_wake(extcon->vbus_irq);
		return 0;
	}

	pinctrl_pm_select_default_state(dev);

	if (extcon->id_gpiod)
		enable_irq(extcon->id_irq);
	if (extcon->vbus_gpiod)
		enable_irq(extcon->vbus_irq);

	if (extcon->id_gpiod || extcon->vbus_gpiod)
		queue_delayed_work(system_power_efficient_wq,
				   &extcon->wq_detcable, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(mtk_usb_extcon_pm_ops,
			mtk_usb_extcon_suspend, mtk_usb_extcon_resume);

static const struct of_device_id mtk_usb_extcon_of_match[] = {
	{ .compatible = "mediatek,extcon-usb", },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_usb_extcon_of_match);

static struct platform_driver mtk_usb_extcon_driver = {
	.probe		= mtk_usb_extcon_probe,
	.remove		= mtk_usb_extcon_remove,
	.shutdown	= mtk_usb_extcon_shutdown,
	.driver		= {
		.name	= "mtk-extcon-usb",
		.pm	= &mtk_usb_extcon_pm_ops,
		.of_match_table = mtk_usb_extcon_of_match,
	},
};

static int __init mtk_usb_extcon_init(void)
{
	return platform_driver_register(&mtk_usb_extcon_driver);
}
late_initcall(mtk_usb_extcon_init);

static void __exit mtk_usb_extcon_exit(void)
{
	platform_driver_unregister(&mtk_usb_extcon_driver);
}
module_exit(mtk_usb_extcon_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Extcon USB Driver");


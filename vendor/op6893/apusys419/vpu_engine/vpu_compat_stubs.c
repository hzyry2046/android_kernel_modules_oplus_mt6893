// SPDX-License-Identifier: GPL-2.0
/* Native-core stubs for the standalone VPU engine build.
 *
 * The VPU driver normally lives inside the native apusys.ko, which cannot be
 * loaded here: it owns the power 2.5 contract this board's DT does not have,
 * and it exports the same apu_* names as the legacy 2.0 provider.  Building
 * only the VPU engine on top of that provider needs the handful of native-core
 * symbols it calls, and none of them are needed to answer the question this
 * build exists for: can the VPU firmware be booted on 6.6, and does the APU
 * then acknowledge a VPU0 power-off?
 *
 *   apusys_register_device/unregister_device - scheduler registration.  No
 *       scheduler exists in this configuration, so the device simply never
 *       becomes visible to one.
 *   apusys_reg_dump / skip_gals                - register dumps from the native
 *       debug driver; the VPU driver only calls them on an explicit dump.
 *   apu_tags_*                                 - the native debug tag ring
 *       (CONFIG_MTK_APUSYS_DEBUG=y here, so apu_tags.h declares them extern
 *       instead of inlining the stubs).
 *
 * Nothing here fakes success for a hardware operation.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include "apusys_device.h"
#include "apusys_debug_api.h"
#include "apu_tags.h"
#include "apusys_trace.h"
#include "apu_tp.h"
#include "apusys_core.h"
#include "apusys_power.h"
#include "apu_checked_power.h"
#include "apu_checked_power_driver.h"

int apusys_register_device(struct apusys_device *dev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(apusys_register_device);

int apusys_unregister_device(struct apusys_device *dev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(apusys_unregister_device);

void apusys_reg_dump_skip_gals(int onoff)
{
}

void apusys_reg_dump(char *module_name, bool dump_vpu)
{
}

struct apu_tags *apu_tags_alloc(const char *name, int size, int cnt,
				apu_tags_seq_f seq_tag, apu_tags_seq_f seq_info,
				void *priv)
{
	return NULL;
}

void apu_tag_add(struct apu_tags *at, void *tag)
{
}

void apu_tags_free(struct apu_tags *at)
{
}

void apu_tags_seq_time(struct seq_file *s, uint64_t time)
{
}

int apu_tags_seq(struct apu_tags *at, struct seq_file *s)
{
	return 0;
}

/* apu_bmap.c logs and traces through the middleware's globals, which live in
 * apusys.ko.  They are diagnostic only: g_mdw_klog gates printk verbosity and
 * cfg_apusys_trace gates the tracepoints. */
u32 g_mdw_klog;
u8 cfg_apusys_trace;

void trace_async_tag(bool isBegin, const char *format, ...)
{
}

void trace_tag_customer(const char *fmt, ...)
{
}

int apu_tp_init_mod(struct apu_tp_tbl *tbl, struct module *mod)
{
	return 0;
}

void apu_tp_exit(struct apu_tp_tbl *tbl)
{
}

/* The legacy 2.0 provider fences apu_power_device_register() with -EOPNOTSUPP
 * once its checked owner is published, and that owner is exactly what makes the
 * provider safe to probe.  The VPU driver only knows the legacy calls, so map
 * them onto the checked API: one client per DVFS user, a lease held for as long
 * as the driver believes the core is powered.
 *
 * These are deliberately not EXPORT_SYMBOL'd -- they shadow the provider's
 * definitions for this module only, which is the point.
 */
static struct apu_checked_client *vpu_clients[APUSYS_DVFS_USER_NUM];
static struct apu_checked_lease *vpu_leases[APUSYS_DVFS_USER_NUM];

/*
 * Experiment hook: take the VPU0 lease during probe so the APU VPU domain is
 * actually up while the engine maps its firmware and builds its algorithm
 * tables.  Without it the domain is only powered when the char device is
 * opened, which this standalone module does not provide, and every mapping is
 * done against a powered-down APU M4U.
 */
static bool power_on_probe;
module_param(power_on_probe, bool, 0400);
MODULE_PARM_DESC(power_on_probe,
		 "Take the VPU0 checked lease during probe (experiment only)");

int apu_power_device_register(enum DVFS_USER user, struct platform_device *pdev)
{
	int ret;

	if (!apusys_power_check())
		return -EPROBE_DEFER;
	if (user < 0 || user >= APUSYS_DVFS_USER_NUM || !pdev)
		return -EINVAL;
	if (vpu_clients[user])
		return 0;
	ret = apu_power_checked_client_register(user, &pdev->dev, THIS_MODULE,
						NULL, NULL, &vpu_clients[user]);
	if (ret)
		return ret;
	if (power_on_probe && user == VPU0) {
		ret = apu_device_power_on(user);
		if (ret)
			pr_info("apusys_legacy_vpu: probe-time VPU0 power-on failed: %d\n",
				ret);
	}
	return 0;
}

int apu_device_power_on(enum DVFS_USER user)
{
	if (user < 0 || user >= APUSYS_DVFS_USER_NUM || !vpu_clients[user])
		return -ENODEV;
	if (vpu_leases[user])
		return 0;
	return apu_checked_get(vpu_clients[user], &vpu_leases[user]);
}

int apu_device_power_off(enum DVFS_USER user)
{
	int ret;

	if (user < 0 || user >= APUSYS_DVFS_USER_NUM || !vpu_leases[user])
		return -ENODEV;
	ret = apu_checked_put(vpu_leases[user]);
	if (!ret)
		vpu_leases[user] = NULL;
	return ret;
}

/* The VPU driver has no module_init of its own: inside apusys.ko the core
 * calls vpu_init() from its own init.  Standalone, this module is the core. */
static int __init apusys_legacy_vpu_init(void)
{
	return vpu_init(NULL);
}

static void __exit apusys_legacy_vpu_exit(void)
{
	vpu_exit();
}

module_init(apusys_legacy_vpu_init);
module_exit(apusys_legacy_vpu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT6893 VPU engine on the legacy APUSYS 2.0 power provider");

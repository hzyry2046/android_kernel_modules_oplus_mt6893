// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/err.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/sched/clock.h>
#include <linux/of.h>
#include <linux/smp.h>

#include "apu_legacy_devinfo.h"
#include "apu_log.h"
#include "apusys_power_ctl.h"
#include "apusys_power_cust.h"
#include "apusys_power_debug.h"
#include "apu_platform_resource.h"
#include "hal_config_power.h"
#include "apu_power_api.h"
#if AUTO_BUCK_OFF_SUSPEND || AUTO_BUCK_OFF_DEEPIDLE
#include "pmic_api_buck.h"
#endif
#include "apusys_power_rule_check.h"
// #include "apusys_dbg.h"
#include "apusys_power.h"
#ifdef APUPWR_TAG_TP
#include "apu_power_tag.h"
#endif

int g_pwr_log_level = APUSYS_PWR_LOG_ERR;
int g_pm_procedure;
int power_on_off_stress;
static int apu_power_counter;
static int apusys_power_broken;
static uint64_t power_info_id;
static uint8_t power_info_force_print;

static bool enable;
static bool power_ready;
module_param(enable, bool, 0400);
MODULE_PARM_DESC(enable, "Opt in to blocked-probe diagnostics; physical owner/bootstrap is not implemented");

bool apusys_power_check(void)
{
	return smp_load_acquire(&power_ready);
}
EXPORT_SYMBOL(apusys_power_check);

struct power_device {
	enum DVFS_USER dev_usr;
	int is_power_on;
	struct platform_device *pdev;
	struct list_head list;
};

struct power_callback_device {
	enum POWER_CALLBACK_USER power_callback_usr;
	void (*power_on_callback)(void *para);
	void (*power_off_callback)(void *para);
	struct list_head list;
};

static LIST_HEAD(power_device_list);
static LIST_HEAD(power_callback_device_list);
static struct mutex power_device_list_mtx;
static struct mutex power_ctl_mtx;
static struct mutex power_opp_mtx;
static spinlock_t power_info_lock;
static int power_callback_counter;
static struct task_struct *power_task_handle;
static uint64_t timestamp;
static struct hal_param_init_power init_power_data;

static struct workqueue_struct *wq;
static void d_work_power_info_func(struct work_struct *work);
static DECLARE_WORK(d_work_power_info, d_work_power_info_func);
static DECLARE_COMPLETION(power_task_started);

#ifdef CONFIG_PM_SLEEP
struct wakeup_source *pwr_wake_lock;
#endif

static void apu_pwr_wake_lock(void)
{
#ifdef CONFIG_PM_SLEEP
	__pm_stay_awake(pwr_wake_lock);
#endif
}

static void apu_pwr_wake_unlock(void)
{
#ifdef CONFIG_PM_SLEEP
	__pm_relax(pwr_wake_lock);
#endif
}

static void apu_pwr_wake_init(void)
{
#ifdef CONFIG_PM_SLEEP
	pwr_wake_lock = wakeup_source_register(NULL, "apupwr_wakelock");
	if (IS_ERR_OR_NULL(pwr_wake_lock))
		pr_info("%s: wakeup_source_register fail\n", __func__);
#endif
}

/**
 * print_time() - Brief description of print_time.
 * @ts: nanosecond
 * @buf: buffer to put [%5lu.%06lu]
 *
 * Transfer nanoseconds to format as [second.micro_second].
 *
 * Return: length of valid string
 **/
static size_t print_time(u64 ts, char *buf)
{
	unsigned long rem_nsec;

	rem_nsec = do_div(ts, 1000000000);

	if (!buf)
		return snprintf(NULL, 0, "[%5lu.000000] ", (unsigned long)ts);

	return sprintf(buf, "[%5lu.%06lu] ",
		       (unsigned long)ts, rem_nsec / 1000);
}

uint64_t apu_get_power_info(int force)
{
	uint64_t ret = 0;

	if (!apusys_power_check() || apu_power_checked_error() ||
	    !READ_ONCE(wq))
		return 0;
	spin_lock(&power_info_lock);
	power_info_id = sched_clock();
	ret = power_info_id;
	power_info_force_print = force;
	spin_unlock(&power_info_lock);

	queue_work(wq, &d_work_power_info);

	return ret;
}
EXPORT_SYMBOL(apu_get_power_info);

static int checked_reg_dump(void *arg)
{
	(void)arg;
	return hal_config_power(PWR_CMD_REG_DUMP, VPU0, NULL);
}

void apu_power_reg_dump(void)
{
	if (!apusys_power_check() || apu_power_checked_error())
		return;
	if (apusys_checked_power_present()) {
		apu_checked_driver_run(checked_reg_dump, NULL);
		return;
	}
	mutex_lock(&power_ctl_mtx);
	if (apu_power_checked_error()) {
		mutex_unlock(&power_ctl_mtx);
		return;
	}
	// keep 26M vcore clk make we can dump reg directly
	hal_config_power(PWR_CMD_REG_DUMP, VPU0, NULL);
	mutex_unlock(&power_ctl_mtx);
}
EXPORT_SYMBOL(apu_power_reg_dump);


void apu_set_vcore_boost(bool enable)
{

}
EXPORT_SYMBOL(apu_set_vcore_boost);

void apu_qos_set_vcore(int target_volt)
{
	if (!apusys_power_check() || apu_power_checked_error() ||
	    !READ_ONCE(power_task_handle))
		return;
#ifdef MTK_FPGA_PORTING
		LOG_WRN("%s FPGA porting bypass DVFS\n", __func__);
#else

#if !BYPASS_DVFS_CTL
		mutex_lock(&power_opp_mtx);
		if (apu_power_checked_error()) {
			mutex_unlock(&power_opp_mtx);
			return;
		}

#if SUPPORT_VCORE_TO_IPUIF
		// set opp value
		apusys_set_apu_vcore(target_volt);
#endif

		// change regulator and clock if need
		wake_up_process(power_task_handle);

		mutex_unlock(&power_opp_mtx);
#else
		LOG_WRN("%s bypass since not support DVFS\n", __func__);
#endif

#endif
}
EXPORT_SYMBOL(apu_qos_set_vcore);

static struct power_device *find_out_device_by_user(enum DVFS_USER user)
{
	struct power_device *pwr_dev = NULL;

	if (!list_empty(&power_device_list)) {

		list_for_each_entry(pwr_dev,
				&power_device_list, list) {

			if (pwr_dev && pwr_dev->dev_usr == user)
				return pwr_dev;
		}

	} else {
		LOG_ERR("%s empty list\n", __func__);
	}

	return NULL;
}

bool apu_get_power_on_status(enum DVFS_USER user)
{
	bool power_on_status = false;
	struct power_device *pwr_dev;

	if (!apusys_power_check() || apu_power_checked_error())
		return false;
	if (apusys_checked_power_present())
		return apusys_checked_power_status(user);
	/* HAL fault recording can already hold power_ctl_mtx. */
	mutex_lock(&power_device_list_mtx);
	pwr_dev = find_out_device_by_user(user);
	if (pwr_dev && !apu_power_checked_error())
		power_on_status = READ_ONCE(pwr_dev->is_power_on);
	mutex_unlock(&power_device_list_mtx);
	return power_on_status;
}
EXPORT_SYMBOL(apu_get_power_on_status);

#if !BYPASS_POWER_CTL
static void power_callback_caller(int power_on)
{
	struct power_callback_device *pwr_dev = NULL;

	LOG_DBG("%s begin (%d)\n", __func__, power_on);

	mutex_lock(&power_device_list_mtx);
	if (!list_empty(&power_callback_device_list)) {
		list_for_each_entry(pwr_dev,
			&power_callback_device_list, list) {

			LOG_DBG("%s calling %d in state %d\n", __func__,
					pwr_dev->power_callback_usr, power_on);

			if (power_on) {
				if (pwr_dev && pwr_dev->power_on_callback)
					pwr_dev->power_on_callback(NULL);
			} else {
				if (pwr_dev && pwr_dev->power_off_callback)
					pwr_dev->power_off_callback(NULL);
			}
		}
	}

	mutex_unlock(&power_device_list_mtx);
	LOG_DBG("%s end (%d)\n", __func__, power_on);
}
#endif

static struct power_callback_device*
find_out_callback_device_by_user(enum POWER_CALLBACK_USER user)
{
	struct power_callback_device *pwr_dev = NULL;

	if (!list_empty(&power_callback_device_list)) {

		list_for_each_entry(pwr_dev,
				&power_callback_device_list, list) {

			if (pwr_dev && pwr_dev->power_callback_usr == user)
				return pwr_dev;
		}

	} else {
		LOG_ERR("%s empty list\n", __func__);
	}

	return NULL;
}

static bool apu_power_user_valid(enum DVFS_USER user)
{
	return (user >= VPU0 && user < APUSYS_DVFS_USER_NUM) ||
		user == EDMA || user == EDMA2 || user == REVISER;
}

static bool power_has_off_callback(void)
{
	struct power_callback_device *pwr_dev;
	bool found = false;

	mutex_lock(&power_device_list_mtx);
	list_for_each_entry(pwr_dev, &power_callback_device_list, list) {
		if (pwr_dev->power_off_callback) {
			found = true;
			break;
		}
	}
	mutex_unlock(&power_device_list_mtx);
	return found;
}

int apu_device_power_suspend(enum DVFS_USER user, int is_suspend)
{
	int ret = 0;
#if !BYPASS_POWER_OFF
	char time_stmp[32];
	struct power_device *pwr_dev = NULL;
#if TIME_PROFILING
	struct profiling_timestamp power_profiling;

	power_profiling.begin = sched_clock();
#endif
	if (!apu_power_user_valid(user) || (is_suspend != 0 && is_suspend != 1))
		return -EINVAL;
	if (!apusys_power_check())
		return -ENODEV;
	/* User-only legacy calls cannot carry checked client/lease ownership. */
	if (apusys_checked_power_present())
		return -EOPNOTSUPP;
	ret = apu_power_checked_error();
	if (ret)
		return ret;
	mutex_lock(&power_ctl_mtx);
	ret = apu_power_checked_error();
	if (ret) {
		mutex_unlock(&power_ctl_mtx);
		return ret;
	}
	mutex_lock(&power_device_list_mtx);
	pwr_dev = find_out_device_by_user(user);
	mutex_unlock(&power_device_list_mtx);
	if (pwr_dev == NULL) {
		LOG_ERR("%s fail, dev of user %d is NULL\n", __func__, user);
		mutex_unlock(&power_ctl_mtx);
		return -ENODEV;
	}

	LOG_DBG("%s waiting for lock, user = %d\n", __func__, user);

	if (apusys_power_broken) {
		mutex_unlock(&power_ctl_mtx);
		hal_config_power(PWR_CMD_DUMP_FAIL_STATE, VPU0, NULL);
		LOG_ERR(
		"APUPWR_BROKEN, user:%d fail to pwr off, is_suspend:%d\n",
							user, is_suspend);
		return -ENODEV;
	}

	if (pwr_dev->is_power_on == 0) {
		mutex_unlock(&power_ctl_mtx);
		LOG_ERR(
		"APUPWR_OFF_FAIL, not allow user:%d to pwr off twice, is_suspend:%d\n",
							user, is_suspend);
		return -ECANCELED;
	}
	if (power_callback_counter <= 0) {
		mutex_unlock(&power_ctl_mtx);
		return -EINVAL;
	}
	/* Void callbacks cannot veto an unsafe last-user shutdown. */
	if (power_callback_counter == 1 && power_has_off_callback()) {
		mutex_unlock(&power_ctl_mtx);
		return -EOPNOTSUPP;
	}
	if (is_suspend)
		g_pm_procedure = 1;

	LOG_DBG("%s for user:%d, cnt:%d, is_suspend:%d\n", __func__,
				user, power_callback_counter, is_suspend);

	if (!g_pm_procedure)
		apu_pwr_wake_lock();

	// for debug
	// dump_stack();
#if !BYPASS_POWER_CTL
	ret = apusys_power_off(user);
#endif
	if (!ret) {
		WRITE_ONCE(pwr_dev->is_power_on, 0);
		power_callback_counter--;
	} else {
		apusys_power_broken = 1;
	}

	if (!g_pm_procedure)
		apu_pwr_wake_unlock();

	mutex_unlock(&power_ctl_mtx);

	// for pwr saving, get pwr info in case pwr ctl fail or enable debug log
	if (g_pwr_log_level >= APUSYS_PWR_LOG_WARN || ret) {
		/* prepare time stamp format */
		memset(time_stmp, 0, sizeof(time_stmp));
		print_time(apu_get_power_info(0), time_stmp);
		LOG_PM(
		"%s for user:%d, ret:%d, cnt:%d, is_suspend:%d, info_id: %s\n",
				__func__, user, ret,
				power_callback_counter, is_suspend,
				ret ? 0 : time_stmp);
	}
#else
	LOG_WRN("%s by user:%d bypass\n", __func__, user);
#endif // BYPASS_POWER_OFF

	if (ret) {
		hal_config_power(PWR_CMD_DUMP_FAIL_STATE, VPU0, NULL);
#ifndef APUSYS_POWER_BRINGUP
		// apusys_reg_dump();
#endif
		apu_aee_warn("APUPWR_OFF_FAIL", "user:%d, is_suspend:%d\n",
							user, is_suspend);
		return ret;
	} else {
#if TIME_PROFILING
		power_profiling.end = sched_clock();
		apu_profiling(&power_profiling, __func__);
#endif
		return 0;
	}
}
EXPORT_SYMBOL(apu_device_power_suspend);

int apu_device_power_off(enum DVFS_USER user)
{
	return apu_device_power_suspend(user, 0);
}
EXPORT_SYMBOL(apu_device_power_off);

int apu_device_power_on(enum DVFS_USER user)
{
	struct power_device *pwr_dev = NULL;
	int ret = 0;
	char time_stmp[32];
#if TIME_PROFILING
	struct profiling_timestamp power_profiling;

	power_profiling.begin = sched_clock();
#endif
	if (!apu_power_user_valid(user))
		return -EINVAL;
	if (!apusys_power_check())
		return -ENODEV;
	if (apusys_checked_power_present())
		return -EOPNOTSUPP;
	ret = apu_power_checked_error();
	if (ret)
		return ret;
	mutex_lock(&power_ctl_mtx);
	ret = apu_power_checked_error();
	if (ret) {
		mutex_unlock(&power_ctl_mtx);
		return ret;
	}
	mutex_lock(&power_device_list_mtx);
	pwr_dev = find_out_device_by_user(user);
	mutex_unlock(&power_device_list_mtx);
	if (pwr_dev == NULL) {
		LOG_ERR("%s fail, dev of user %d is NULL\n", __func__, user);
		mutex_unlock(&power_ctl_mtx);
		return -ENODEV;
	}

	LOG_DBG("%s waiting for lock, user:%d\n", __func__, user);

	g_pm_procedure = 0;

	if (apusys_power_broken) {
		mutex_unlock(&power_ctl_mtx);
		hal_config_power(PWR_CMD_DUMP_FAIL_STATE, VPU0, NULL);
		LOG_ERR("APUPWR_BROKEN, user:%d fail to pwr on\n", user);
		return -ENODEV;
	}

	if (pwr_dev->is_power_on == 1) {
		mutex_unlock(&power_ctl_mtx);
#if !BYPASS_POWER_OFF
		LOG_ERR("APUPWR_ON_FAIL, not allow user:%d to pwr on twice\n",
									user);
		return -ECANCELED;
#else
		LOG_WRN("%s by user:%d bypass\n", __func__, user);
		return 0;
#endif
	}

	LOG_DBG("%s for user:%d, cnt:%d\n", __func__,
						user, power_callback_counter);

	if (!g_pm_procedure)
		apu_pwr_wake_lock();
	// for debug
	// dump_stack();
#if !BYPASS_POWER_CTL
	ret = apusys_power_on(user);
#endif
	if (!ret) {
		WRITE_ONCE(pwr_dev->is_power_on, 1);
		power_callback_counter++;
	} else {
		apusys_power_broken = 1;
	}

	if (!ret && power_callback_counter == 1) {
		// call passive power on function list
#if !BYPASS_POWER_CTL
		power_callback_caller(1);
#endif
	}

	if (!g_pm_procedure)
		apu_pwr_wake_unlock();

	mutex_unlock(&power_ctl_mtx);

	// for pwr saving, get pwr info in case pwr ctl fail or enable debug log
	if (g_pwr_log_level >= APUSYS_PWR_LOG_WARN || ret) {
		/* prepare time stamp format */
		memset(time_stmp, 0, sizeof(time_stmp));
		print_time(apu_get_power_info(0), time_stmp);
		LOG_PM("%s for user:%d, ret:%d, cnt:%d, info_id: %s\n",
				__func__, user, ret, power_callback_counter,
				ret ? 0 : time_stmp);
	}

	if (ret) {
		hal_config_power(PWR_CMD_DUMP_FAIL_STATE, VPU0, NULL);
#ifndef APUSYS_POWER_BRINGUP
		// apusys_reg_dump();
#endif
		apu_aee_warn("APUPWR_ON_FAIL", "user:%d\n", user);
		return ret;
	} else {
#if TIME_PROFILING
		power_profiling.end = sched_clock();
		apu_profiling(&power_profiling, __func__);
#endif
		return 0;
	}
}
EXPORT_SYMBOL(apu_device_power_on);

int apu_power_device_register(enum DVFS_USER user, struct platform_device *pdev)
{
	struct power_device *pwr_dev = NULL;

	if (!apusys_power_check())
		return -EPROBE_DEFER;
	if (apusys_checked_power_present())
		return -EOPNOTSUPP;
	/* EDMA/EDMA2/Reviser are passive power users outside the DVFS arrays. */
	if (!pdev || !apu_power_user_valid(user))
		return -EINVAL;
	pwr_dev = kzalloc(sizeof(struct power_device), GFP_KERNEL);

	if (pwr_dev == NULL) {
		LOG_ERR("%s fail in dvfs user %d\n", __func__, user);
		return -1;
	}

	pwr_dev->dev_usr = user;
	pwr_dev->is_power_on = 0;
	pwr_dev->pdev = pdev;

	/* add to device link list */
	mutex_lock(&power_device_list_mtx);
	if (find_out_device_by_user(user)) {
		mutex_unlock(&power_device_list_mtx);
		kfree(pwr_dev);
		return -EEXIST;
	}

	list_add_tail(&pwr_dev->list, &power_device_list);
// call apusys_power_init in probe
#if 0
	if (apu_power_counter == 0) {
		// prepare clock and get regulator handle
		apusys_power_init(user, (void *)&init_power_data);
	}
#endif
	apu_power_counter++;

	mutex_unlock(&power_device_list_mtx);

	LOG_INF("%s add dvfs user %d success\n", __func__, user);

	return 0;
}
EXPORT_SYMBOL(apu_power_device_register);

int apu_power_callback_device_register(enum POWER_CALLBACK_USER user,
void (*power_on_callback)(void *para), void (*power_off_callback)(void *para))
{
	struct power_callback_device *pwr_dev = NULL;

	if (!apusys_power_check())
		return -EPROBE_DEFER;
	/* A void callback cannot report init, drain or uncertain firmware errors. */
	if (apusys_checked_power_present())
		return -EOPNOTSUPP;
	if (user < 0 || user >= APUSYS_POWER_CALLBACK_USER_NUM ||
	    (!power_on_callback && !power_off_callback))
		return -EINVAL;
	pwr_dev = kzalloc(sizeof(struct power_callback_device), GFP_KERNEL);

	if (pwr_dev == NULL) {
		LOG_ERR("%s fail in power callback user %d\n", __func__, user);
		return -1;
	}

	pwr_dev->power_callback_usr = user;
	pwr_dev->power_on_callback = power_on_callback;
	pwr_dev->power_off_callback = power_off_callback;

	/* add to device link list */
	mutex_lock(&power_device_list_mtx);
	if (find_out_callback_device_by_user(user)) {
		mutex_unlock(&power_device_list_mtx);
		kfree(pwr_dev);
		return -EEXIST;
	}

	list_add_tail(&pwr_dev->list, &power_callback_device_list);

	mutex_unlock(&power_device_list_mtx);

	LOG_INF("%s add power callback user %d success\n", __func__, user);

	return 0;
}
EXPORT_SYMBOL(apu_power_callback_device_register);

void apu_power_device_unregister(enum DVFS_USER user)
{
	struct power_device *pwr_dev;

	if (!apusys_power_check())
		return;
	mutex_lock(&power_ctl_mtx);
	mutex_lock(&power_device_list_mtx);
	pwr_dev = find_out_device_by_user(user);
	if (pwr_dev) {
		apu_power_counter--;
		list_del_init(&pwr_dev->list);
		kfree(pwr_dev);
	}
	mutex_unlock(&power_device_list_mtx);
	mutex_unlock(&power_ctl_mtx);
}
EXPORT_SYMBOL(apu_power_device_unregister);

void apu_power_callback_device_unregister(enum POWER_CALLBACK_USER user)
{
	struct power_callback_device *pwr_dev;

	if (!apusys_power_check())
		return;
	mutex_lock(&power_device_list_mtx);
	pwr_dev = find_out_callback_device_by_user(user);
	if (pwr_dev) {
		list_del_init(&pwr_dev->list);
		kfree(pwr_dev);
	}
	mutex_unlock(&power_device_list_mtx);
}
EXPORT_SYMBOL(apu_power_callback_device_unregister);

static int checked_get_power_info(void *arg)
{
	return hal_config_power(PWR_CMD_GET_POWER_INFO, VPU0, arg);
}

static void d_work_power_info_func(struct work_struct *work)
{
	struct apu_power_info info = {0};

	if (!apusys_power_check() || apu_power_checked_error())
		return;
	spin_lock(&power_info_lock);
	info.id = power_info_id;
	info.force_print = power_info_force_print;
	spin_unlock(&power_info_lock);

	info.type = 1;
	if (apusys_checked_power_present())
		apu_checked_driver_run(checked_get_power_info, &info);
	else
		hal_config_power(PWR_CMD_GET_POWER_INFO, VPU0, &info);
}

#if DEFAULT_POWER_ON
static void default_power_on_func(void)
{
	int dev_id = 0;

	LOG_ERR("### apusys power on all device ###\n");

	for (dev_id = 0 ; dev_id < APUSYS_DVFS_USER_NUM ; dev_id++) {
		apu_power_counter++;
		apusys_power_on(dev_id);
		udelay(200);
	}

	apu_get_power_info(1);
}
#endif

/* A failed stage must not produce telemetry or run later electrical work. */
static int apusys_run_dvfs_round(struct apu_power_info *info)
{
	int ret;

	if (!info)
		return -EINVAL;
	ret = apusys_dvfs_policy(info->id);
	if (ret)
		return ret;
#if SUPPORT_VCORE_TO_IPUIF
	ret = apusys_ipuif_opp_change();
	if (ret)
		return ret;
#endif
	ret = hal_config_power(PWR_CMD_GET_POWER_INFO, VPU0, info);
	if (ret)
		return ret;
#if DVFS_ASSERTION_CHECK
	apu_power_assert_check(info);
#endif
#ifdef APUPWR_TASK_DEBOUNCE
	task_debounce();
#endif
	return 0;
}

static int checked_dvfs_round(void *arg)
{
	return apusys_run_dvfs_round(arg);
}

static int apusys_power_task(void *arg)
{
	int keep_loop = 0;
	int ret;
	struct apu_power_info info = {0};
	unsigned long rem_nsec;

	set_current_state(TASK_INTERRUPTIBLE);
	complete(&power_task_started);

	LOG_INF("%s first time wakeup and enter sleep now\n", __func__);
	schedule();

	for (;;) {

		if (kthread_should_stop())
			break;
		/* A latched electrical error requires owner recovery, not retries. */
		if (apu_power_checked_error())
			goto sleep;
#if 1
		mutex_lock(&power_opp_mtx);
		keep_loop = apusys_check_opp_change();
		mutex_unlock(&power_opp_mtx);
#else
		keep_loop = 1;
		mdelay(1000);
#endif
		if (keep_loop) {
			timestamp = sched_clock();
			info.id = timestamp;
			info.type = 0;
			// call dvfs API and bring timestamp to id
			rem_nsec = do_div(timestamp, 1000000000);
			LOG_INF("%s call DVFS handler, id:[%5lu.%06lu]\n",
				__func__,
				(unsigned long)timestamp, rem_nsec / 1000);

			if (apusys_checked_power_present())
				ret = apu_checked_driver_run(checked_dvfs_round, &info);
			else
				ret = apusys_run_dvfs_round(&info);
			if (ret) {
				LOG_ERR("%s DVFS round failed: %d, enter sleep\n",
					__func__, ret);
				hal_config_power(PWR_CMD_DUMP_FAIL_STATE, VPU0, NULL);
				goto sleep;
			}
			continue;
		}
	sleep:
		LOG_DBG("%s enter sleep\n", __func__);
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop())
			break;
		schedule();
	}

	__set_current_state(TASK_RUNNING);
	LOG_WRN("%s task stop\n", __func__);
	return 0;
}

void apu_device_set_opp(enum DVFS_USER user, uint8_t opp)
{
	if (!apusys_power_check() || apu_power_checked_error() ||
	    !READ_ONCE(power_task_handle))
		return;
#ifdef MTK_FPGA_PORTING
	LOG_WRN("%s FPGA porting bypass DVFS\n", __func__);
#else

#if !BYPASS_DVFS_CTL
	if (user >= 0 && user < APUSYS_DVFS_USER_NUM
		&& opp < APUSYS_MAX_NUM_OPPS) {

		mutex_lock(&power_opp_mtx);
		if (apu_power_checked_error()) {
			mutex_unlock(&power_opp_mtx);
			return;
		}

		// set opp value
		apusys_set_opp(user, opp);

		// change regulator and clock if need
		wake_up_process(power_task_handle);

		mutex_unlock(&power_opp_mtx);
	}
#else
	LOG_WRN("%s bypass since not support DVFS\n", __func__);
#endif

#endif
}
EXPORT_SYMBOL(apu_device_set_opp);

/**
 * apu_profiling() - Brief description of apu_profiling.
 * @profile: struct profiling_timestamp with begin/end timestamp.
 * @tag: the string need to print prefix.
 *
 * Calcualte abs(end-begin) and show result in unit of us with "tag" prefix.
 *
 * Return: void
 **/
void apu_profiling(struct profiling_timestamp *profile, const char *tag)
{
	u64 nanosec = 0;
	u64 time = 0;

	time = abs(profile->end - profile->begin),
	nanosec = do_div(time, 1000000000);
	pr_info("%s: %s take %lu (us)\n", __func__, tag,
		((unsigned long)nanosec / 1000));
}
EXPORT_SYMBOL(apu_profiling);

void event_trigger_dvfs_policy(void)
{
	struct task_struct *task;

	if (!apusys_power_check() || apu_power_checked_error())
		return;
	task = READ_ONCE(power_task_handle);
	if (task)
		wake_up_process(task);
}

static int apu_power_probe(struct platform_device *pdev)
{
	int ret;

	if (!enable)
		return -ENODEV;
	/* Failure propagation alone does not establish a safe hardware owner. */
	dev_err(&pdev->dev,
		"legacy APU power blocked: physical resource ledger, PG, PM and IOMMU lifecycle missing\n");
	return -EOPNOTSUPP;

	ret = apu_legacy_devinfo_init();
	if (ret)
		return ret;
	ret = init_platform_resource(pdev, &init_power_data);
	if (ret)
		return ret;
	mutex_init(&power_device_list_mtx);
	mutex_init(&power_ctl_mtx);
	mutex_init(&power_opp_mtx);
	spin_lock_init(&power_info_lock);
	ret = apusys_power_init(VPU0, &init_power_data);
	if (ret)
		return ret;
	ret = apu_checked_driver_init(&pdev->dev);
	if (ret)
		goto uninit_power;
	wq = alloc_workqueue("apu_pwr_drv_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!wq) {
		ret = -ENOMEM;
		goto uninit_power;
	}
	reinit_completion(&power_task_started);
	power_task_handle = kthread_create(apusys_power_task, NULL, "apu_pwr_policy");
	if (IS_ERR(power_task_handle)) {
		ret = PTR_ERR(power_task_handle);
		power_task_handle = NULL;
		goto destroy_wq;
	}
	apu_pwr_wake_init();
	wake_up_process(power_task_handle);
	wait_for_completion(&power_task_started);
	apusys_power_debugfs_init();
	/* Consumers can register only after regulators/clocks/RPC init succeeded. */
	smp_store_release(&power_ready, true);
	queue_work(wq, &d_work_power_info);
	return 0;
destroy_wq:
	destroy_workqueue(wq);
	wq = NULL;
uninit_power:
	apu_checked_driver_detach();
	apusys_power_uninit(VPU0);
	return ret;
}

static int apu_power_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct hal_param_pm pm;
	enum DVFS_USER user;

	(void)pdev;
	(void)mesg;
	if (!apusys_power_check())
		return -ENODEV;
	/* No checked suspend/restore owner exists yet. */
	return -EOPNOTSUPP;

	LOG_PM("%s begin\n", __func__);

	if (power_on_off_stress) {
		LOG_PM("%s, power_on_off_stress: %d\n",
				__func__, power_on_off_stress);
		for (user = VPU0; user < APUSYS_DVFS_USER_NUM; user++)
			apu_device_power_suspend(user, 1);
	}

	mutex_lock(&power_ctl_mtx);
	pm.is_suspend = 1;
	hal_config_power(PWR_CMD_PM_HANDLER, VPU0, &pm);
	mutex_unlock(&power_ctl_mtx);

	LOG_PM("%s end\n", __func__);
	return 0;
}

static int apu_power_resume(struct platform_device *pdev)
{
	struct hal_param_pm pm;
	enum DVFS_USER user;

	(void)pdev;
	if (!apusys_power_check())
		return -ENODEV;
	return -EOPNOTSUPP;

	LOG_PM("%s begin\n", __func__);

	mutex_lock(&power_ctl_mtx);
	pm.is_suspend = 0;
	hal_config_power(PWR_CMD_PM_HANDLER, VPU0, &pm);
	g_pm_procedure = 0;
	mutex_unlock(&power_ctl_mtx);

	if (power_on_off_stress) {
		LOG_PM("%s, power_on_off_stress: %d\n",
				__func__, power_on_off_stress);
		for (user = VPU0; user < APUSYS_DVFS_USER_NUM; user++)
			apu_device_power_on(user);
	}

	LOG_PM("%s end\n", __func__);
	return 0;
}

static int apu_power_remove(struct platform_device *pdev)
{
	int ret;

	smp_store_release(&power_ready, false);
	ret = apu_checked_driver_detach();
	if (ret) {
		dev_err(&pdev->dev, "checked power detach incomplete: %d; probe must remain blocked\n", ret);
		return ret;
	}
	apusys_power_debugfs_exit();
	if (power_task_handle) {
		kthread_stop(power_task_handle);
		power_task_handle = NULL;
	}
	if (wq) {
		cancel_work_sync(&d_work_power_info);
		destroy_workqueue(wq);
		wq = NULL;
	}
	apusys_power_uninit(VPU0);
#ifdef CONFIG_PM_SLEEP
	wakeup_source_unregister(pwr_wake_lock);
	pwr_wake_lock = NULL;
#endif
	return 0;
}

static const struct of_device_id apu_power_of_match[] = {
	{ .compatible = "mediatek,apusys_power" },
	{ /* end of list */},
};


static struct platform_driver apu_power_driver = {
	.probe	= apu_power_probe,
	.remove	= apu_power_remove,
	.suspend = apu_power_suspend,
	.resume = apu_power_resume,
	.driver = {
		.name = "apusys_power",
		.suppress_bind_attrs = true,
		.of_match_table = apu_power_of_match,
	},
};

static int __init apu_power_drv_init(void)
{
	if (!enable)
		return -ENODEV;
	return platform_driver_register(&apu_power_driver);
}

module_init(apu_power_drv_init)

static void __exit apu_power_drv_exit(void)
{
	platform_driver_unregister(&apu_power_driver);
}
module_exit(apu_power_drv_exit)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("apu power driver");


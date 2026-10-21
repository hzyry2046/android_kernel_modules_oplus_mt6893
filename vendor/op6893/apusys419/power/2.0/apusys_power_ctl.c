// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */


#ifndef BUILD_POLICY_TEST
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#endif

#include "apu_power_api.h"
#include "apusys_power_debug.h"
#include "apusys_power_ctl.h"
#include "apusys_power_cust.h"
#include "apusys_power.h"
#include "apu_power_table.h"
#include "hal_config_power.h"
#include "apu_log.h"
#ifdef APUPWR_TAG_TP
#include "apupwr_events.h"
#endif

static struct mutex power_dvfs_mtx;
struct apusys_dvfs_opps apusys_opps;
static spinlock_t ipuif_lock;

bool dvfs_user_support(enum DVFS_USER user)
{
	return apusys_dvfs_user_support[user];
}

bool dvfs_power_domain_support(enum DVFS_VOLTAGE_DOMAIN domain)
{
	return apusys_dvfs_buck_domain_support[domain];
}

int32_t apusys_thermal_en_throttle_cb(enum DVFS_USER user,
					enum APU_OPP_INDEX opp)
{
	// need to check constraint voltage, fixed me

	switch (user) {
#ifdef CONFIG_MTK_APUSYS_VPU
	case VPU0:
		for (user = VPU0; user < VPU0 + APUSYS_VPU_NUM; user++)
			apusys_opps.thermal_opp[user] = opp;
		break;
#endif
#ifdef CONFIG_MTK_APUSYS_MDLA_SUPPORT
	case MDLA0:
		for (user = MDLA0; user < MDLA0 + APUSYS_MDLA_NUM; user++)
			apusys_opps.thermal_opp[user] = opp;
		break;
#endif
	default:
		apusys_opps.thermal_opp[user] = opp;
		break;
	}
	PWR_LOG_INF("%s, user=%d, opp=%d\n", __func__, user, opp);

	if (apusys_opps.is_power_on[user])
		event_trigger_dvfs_policy();

	return 0;
}

int32_t apusys_thermal_dis_throttle_cb(enum DVFS_USER user)
{
	switch (user) {
#ifdef CONFIG_MTK_APUSYS_VPU
	case VPU0:
		for (user = VPU0; user < VPU0 + APUSYS_VPU_NUM; user++)
			apusys_opps.thermal_opp[user] = 0;
		break;
#endif
#ifdef CONFIG_MTK_APUSYS_MDLA_SUPPORT
	case MDLA0:
		for (user = MDLA0; user < MDLA0 + APUSYS_MDLA_NUM; user++)
			apusys_opps.thermal_opp[user] = 0;
		break;
#endif
	default:
		apusys_opps.thermal_opp[user] = 0;
		break;
	}

	PWR_LOG_INF("%s, user=%d, opp=0\n", __func__, user);

	if (apusys_opps.is_power_on[user])
		event_trigger_dvfs_policy();

	return 0;
}

int apusys_opp_to_boost_value(enum DVFS_USER user, uint8_t opp)
{
	int boost_value;
	uint32_t max_freq = 0, freq = 0;
	enum DVFS_VOLTAGE_DOMAIN buck_domain;

	if (user < VPU0 || user >= APUSYS_DVFS_USER_NUM ||
	    opp >= APUSYS_MAX_NUM_OPPS) {
		PWR_LOG_ERR("%s invalid opp : %d\n", __func__, opp);
		return -EINVAL;
	}
	if (!apusys_opps.opps)
		return -ENODEV;
	buck_domain = apusys_user_to_buck_domain[user];
	if (buck_domain < V_VPU0 || buck_domain >= APUSYS_BUCK_DOMAIN_NUM)
		return -EINVAL;

	max_freq = apusys_opps.opps[0][buck_domain].freq;
	if (!max_freq)
		return -EINVAL;
	freq = apusys_opps.opps[opp][buck_domain].freq;
	boost_value = (freq * 100) / max_freq;

	if (boost_value > 100 || boost_value < 0) {
		PWR_LOG_ERR("%s invalid boost : %d\n", __func__, boost_value);
		return -1;
	}

	return boost_value;
}

uint8_t apusys_boost_value_to_opp(enum DVFS_USER user, uint8_t boost_value)
{
	uint8_t i = 0;
	uint8_t opp = APUSYS_MAX_NUM_OPPS-1;
	uint32_t max_freq = 0, freq = 0;
	enum DVFS_VOLTAGE_DOMAIN buck_domain;

	if (!apusys_opps.opps || user < VPU0 || user >= APUSYS_DVFS_USER_NUM)
		return U8_MAX;
	buck_domain = apusys_user_to_buck_domain[user];
	if (buck_domain < V_VPU0 || buck_domain >= APUSYS_BUCK_DOMAIN_NUM ||
	    !apusys_opps.opps[0][buck_domain].freq)
		return U8_MAX;

	if (boost_value >= 100) {
		opp = 0;
	} else {
		max_freq = apusys_opps.opps[0][buck_domain].freq;
		freq = boost_value * max_freq / 100;
		for (i = 1; i < APUSYS_MAX_NUM_OPPS; i++) {
			if (freq > apusys_opps.opps[i][buck_domain].freq) {
				opp = i-1;
				break;
			}
		}
	}
	PWR_LOG_INF(
		"%s, user=%d, boost_value=%d,max_freq=%d, freq=%d, opp=%d\n",
		__func__, user, boost_value, max_freq, freq, opp);
	return opp;

}
EXPORT_SYMBOL(apusys_boost_value_to_opp);


enum DVFS_FREQ apusys_opp_to_freq(enum DVFS_USER user, uint8_t opp)
{
	enum DVFS_FREQ freq = DVFS_FREQ_NOT_SUPPORT;
	enum DVFS_VOLTAGE_DOMAIN buck_domain;

	if (!apusys_opps.opps || user < VPU0 || user >= APUSYS_DVFS_USER_NUM ||
	    opp >= APUSYS_MAX_NUM_OPPS)
		return freq;

	buck_domain = apusys_user_to_buck_domain[user];
	if (buck_domain < V_VPU0 || buck_domain >= APUSYS_BUCK_DOMAIN_NUM)
		return freq;

	if (opp < APUSYS_MAX_NUM_OPPS)
		freq = apusys_opps.opps[opp][buck_domain].freq;

	return freq;
}
EXPORT_SYMBOL(apusys_opp_to_freq);

uint8_t apusys_freq_to_opp(enum DVFS_VOLTAGE_DOMAIN buck_domain, uint32_t freq)
{
	uint8_t opp = 0;
	uint32_t next_freq = 0;

	if (!apusys_opps.opps || buck_domain < V_VPU0 ||
	    buck_domain >= APUSYS_BUCK_DOMAIN_NUM || !freq ||
	    !apusys_opps.opps[0][buck_domain].freq)
		return U8_MAX;

	for (opp = 0 ; opp < APUSYS_MAX_NUM_OPPS - 1 ; opp++) {
		next_freq = apusys_opps.opps[opp+1][buck_domain].freq + 1;
		if (freq >= next_freq &&
			freq <= (apusys_opps.opps[opp][buck_domain].freq + 1))
			break;
	}

	return opp;
}
EXPORT_SYMBOL(apusys_freq_to_opp);

int8_t apusys_get_opp(enum DVFS_USER user)
{
	enum DVFS_VOLTAGE_DOMAIN buck_domain;
	int ret;

	if (user < VPU0 || user >= APUSYS_DVFS_USER_NUM)
		return -EINVAL;
	if (!apusys_power_check() || !apusys_opps.opps)
		return -ENODEV;
	ret = apu_power_checked_error();
	if (ret)
		return ret < S8_MIN ? -EIO : ret;

	buck_domain = apusys_user_to_buck_domain[user];
	if (buck_domain < V_VPU0 || buck_domain >= APUSYS_BUCK_DOMAIN_NUM ||
	    apusys_opps.cur_opp_index[buck_domain] >= APUSYS_MAX_NUM_OPPS)
		return -EINVAL;

	return apusys_opps.cur_opp_index[buck_domain];
}
EXPORT_SYMBOL(apusys_get_opp);


int8_t apusys_get_ceiling_opp(enum DVFS_USER user)
{
	uint8_t used_opp;
	enum DVFS_VOLTAGE_DOMAIN buck_domain;
	int ret;

	if (user < VPU0 || user >= APUSYS_DVFS_USER_NUM)
		return -EINVAL;
	if (!apusys_power_check() || !apusys_opps.opps)
		return -ENODEV;
	ret = apu_power_checked_error();
	if (ret)
		return ret < S8_MIN ? -EIO : ret;

	buck_domain = apusys_user_to_buck_domain[user];
	if (buck_domain < V_VPU0 || buck_domain >= APUSYS_BUCK_DOMAIN_NUM ||
	    apusys_opps.cur_opp_index[buck_domain] >= APUSYS_MAX_NUM_OPPS ||
	    apusys_opps.power_lock_min_opp[user] >= APUSYS_MAX_NUM_OPPS ||
	    apusys_opps.power_lock_max_opp[user] >= APUSYS_MAX_NUM_OPPS ||
	    apusys_opps.thermal_opp[user] >= APUSYS_MAX_NUM_OPPS)
		return -EINVAL;
	used_opp = apusys_opps.cur_opp_index[buck_domain];

	// upper bound for power hal
	used_opp = MAX(used_opp, apusys_opps.power_lock_min_opp[user]);
	// lower bound for power hal
	used_opp = MIN(used_opp, apusys_opps.power_lock_max_opp[user]);

	// upper bound for thermal
	used_opp = MAX(used_opp, apusys_opps.thermal_opp[user]);

	return used_opp;
}
EXPORT_SYMBOL(apusys_get_ceiling_opp);


void apusys_set_pwr_lock(enum DVFS_USER user, uint8_t min_opp, uint8_t max_opp)
{
	apusys_opps.power_lock_min_opp[user] = min_opp;
	apusys_opps.power_lock_max_opp[user] = max_opp;
	PWR_LOG_INF("%s, user=%d, min_opp=%d, max_opp=%d,\n",
		__func__, user, min_opp, max_opp);
}


uint8_t apusys_pwr_max_min_check(enum DVFS_USER user, uint8_t opp)
{
	uint8_t used_opp = opp;

	// upper bound for power hal
	used_opp = MAX(used_opp, apusys_opps.power_lock_min_opp[user]);
	// lower bound for power hal
	used_opp = MIN(used_opp, apusys_opps.power_lock_max_opp[user]);

	// upper bound for thermal
	used_opp = MAX(used_opp, apusys_opps.thermal_opp[user]);

		PWR_LOG_INF(
	"%s, %s, used_opp=%d,thermal_opp=%d,pwr_lock_min=%d,pwr_lock_max=%d\n",
		__func__,
		user_str[user],
		used_opp,
		apusys_opps.thermal_opp[user],
		apusys_opps.power_lock_min_opp[user],
		apusys_opps.power_lock_max_opp[user]);

	return used_opp;
}


void apusys_clk_path_update_pwr(enum DVFS_USER user, enum DVFS_VOLTAGE voltage)
{
	uint8_t  path_volt_index = 0;

	if (apusys_opps.is_power_on[user] == false)
		return;

	for (path_volt_index = 0;
	path_volt_index < APUSYS_PATH_USER_NUM; path_volt_index++){
		apusys_opps.user_path_volt[user][path_volt_index] =
			(voltage >
			dvfs_clk_path_max_vol[user][path_volt_index] ?
			dvfs_clk_path_max_vol[user][path_volt_index] :
			voltage);

			PWR_LOG_INF("%s, volt=%d, user_path_volt[%s][%d]=%d\n",
			__func__,
			voltage,
			user_str[user],
			path_volt_index,
			apusys_opps.user_path_volt[user][path_volt_index]);
	}
}


void apusys_final_volt_check(void)
{
	uint8_t  user_index = 0, path_index = 0, buck_index = 0;

// for every buck domain, check clk path matrix for buck shared relation
for (buck_index = 0;
buck_index < APUSYS_BUCK_NUM; buck_index++) {
	#if !VCORE_DVFS_SUPPORT
	if (buck_index == VCORE_BUCK)
		continue;
	#endif
	for (user_index = 0; user_index < APUSYS_DVFS_USER_NUM; user_index++) {
		if (dvfs_user_support(user_index) == false)
			continue;
		if (apusys_opps.is_power_on[user_index] == false)
			continue;
		for (path_index = 0;
		path_index < APUSYS_PATH_USER_NUM; path_index++) {
			if (buck_shared[buck_index][user_index][path_index]
		== true) {
				apusys_opps.next_buck_volt[buck_index] =
		MAX(apusys_opps.next_buck_volt[buck_index],
		apusys_opps.user_path_volt[user_index][path_index]);

		PWR_LOG_INF("%s, %s = %d,(%s,%d)=%d\n",
		__func__,
		buck_str[buck_index],
		apusys_opps.next_buck_volt[buck_index],
		user_str[user_index],
		path_index,
		apusys_opps.user_path_volt[user_index][path_index]);
			}
			}
		}
	}
}


void apusys_pwr_constraint_check(void)
{
	uint8_t i = 0;
	int8_t opp_index = APUSYS_MAX_NUM_OPPS-1;
	enum DVFS_BUCK buck0;
	enum DVFS_BUCK buck1;
	enum DVFS_VOLTAGE voltage0;
	enum DVFS_VOLTAGE voltage1;
	enum DVFS_VOLTAGE_DOMAIN buck_domain;
	bool vcore_constraint = false;

	for (i = 0; i < APUSYS_DVFS_CONSTRAINT_NUM; i++) {
		buck0 = dvfs_constraint_table[i].buck0;
	    buck1 = dvfs_constraint_table[i].buck1;
	    voltage0 = dvfs_constraint_table[i].voltage0;
	    voltage1 = dvfs_constraint_table[i].voltage1;
if (apusys_opps.next_buck_volt[buck0] == voltage0 &&
	apusys_opps.next_buck_volt[buck1] == voltage1) {
	if (buck0 == VCORE_BUCK)
		vcore_constraint = true;
	for (opp_index = APUSYS_MAX_NUM_OPPS-1;
		opp_index >= 0; opp_index--) {
		if (voltage0 < voltage1) {
			buck_domain = apusys_buck_to_buck_domain[buck0];
			if (apusys_opps.opps[opp_index][buck_domain].voltage
	> voltage0) {
				apusys_opps.next_buck_volt[buck0] =
			apusys_opps.opps[opp_index][buck_domain].voltage;

				PWR_LOG_INF("%s, %s from %d --> %d\n",
				__func__,
				buck_str[buck0],
				voltage0,
			apusys_opps.next_buck_volt[buck0]);
				break;
				}
		} else if (voltage0 > voltage1) {
			buck_domain = apusys_buck_to_buck_domain[buck1];
			if (apusys_opps.opps[opp_index][buck_domain].voltage
			> voltage1) {
				apusys_opps.next_buck_volt[buck1] =
			apusys_opps.opps[opp_index][buck_domain].voltage;

				PWR_LOG_INF("%s, %s from %d --> %d\n",
				__func__,
				buck_str[buck1],
				voltage1,
			apusys_opps.next_buck_volt[buck1]);
				break;
				}
			}
		}
	}
}

	if (vcore_constraint == false)
		apusys_opps.next_buck_volt[VCORE_BUCK] = VCORE_DEFAULT_VOLT;

}


void apusys_pwr_efficiency_check(void)
{
	uint8_t buck_domain_index = 0;
	uint8_t buck_index = 0;
	uint8_t opp_index = 0;
	enum DVFS_USER user;

	for (buck_domain_index = 0;
	buck_domain_index < APUSYS_BUCK_DOMAIN_NUM; buck_domain_index++) {
		if (dvfs_power_domain_support(buck_domain_index) == false)
			continue;
		buck_index = apusys_buck_domain_to_buck[buck_domain_index];
	for (opp_index = 0;	opp_index < APUSYS_MAX_NUM_OPPS; opp_index++) {
#ifndef CONFIG_MACH_MT6877
		if (apusys_opps.opps[opp_index][buck_domain_index].voltage <=
			apusys_opps.next_buck_volt[buck_index]){
#else
		if ((apusys_opps.opps[opp_index][buck_domain_index].voltage <=
			apusys_opps.next_buck_volt[buck_index]) ||
			(opp_index == (APUSYS_MAX_NUM_OPPS - 1))) {
#endif
			user = apusys_buck_domain_to_user[buck_domain_index];
			if (user < APUSYS_DVFS_USER_NUM) {
				if (apusys_opps.is_power_on[user] == false)
					continue;
				apusys_opps.next_opp_index[buck_domain_index] =
				apusys_pwr_max_min_check(user, opp_index);
			} else {
				apusys_opps.next_opp_index[buck_domain_index] =
				opp_index;
			}
				PWR_LOG_INF("%s, %s, opp=%d\n",
			__func__,
			buck_domain_str[buck_domain_index],
			apusys_opps.next_opp_index[buck_domain_index]);
			break;
			}
		}
	}
}

/* These are last successfully completed stages, not a power owner state.
 * A later HAL error may leave hardware uncertain; its fault latch takes
 * priority.
 */
static int apusys_apply_voltage(enum DVFS_BUCK buck, enum DVFS_VOLTAGE voltage)
{
	struct hal_param_volt data = {
	    .target_buck = buck,
	    .target_volt = voltage,
	};
	int ret;

	ret = hal_config_power(PWR_CMD_SET_VOLT, VPU0, &data);
	if (ret)
		return ret;
	if (buck == SRAM_BUCK)
		apusys_opps.vsram_volatge = voltage;
	else
		apusys_opps.cur_buck_volt[buck] = voltage;
	return 0;
}

static bool apusys_any_power_on(void)
{
	int user;

	for (user = VPU0; user < APUSYS_DVFS_USER_NUM; user++)
		if (dvfs_user_support(user) && apusys_opps.is_power_on[user])
			return true;
	return apusys_opps.is_power_on[EDMA] ||
	       apusys_opps.is_power_on[EDMA2] ||
	       apusys_opps.is_power_on[REVISER];
}

static bool apusys_dvfs_domain_active(enum DVFS_VOLTAGE_DOMAIN domain)
{
	enum DVFS_USER user = apusys_buck_domain_to_user[domain];

	if (user >= VPU0 && user < APUSYS_DVFS_USER_NUM)
		return dvfs_user_support(user) && apusys_opps.is_power_on[user];
	return apusys_any_power_on();
}

static int apusys_validate_dvfs_plan(void)
{
	int domain, buck;
	uint8_t cur_index, next_index;
	enum DVFS_USER user;

	if (!apusys_opps.opps)
		return -ENODEV;
	if (apusys_opps.vsram_volatge != VSRAM_LOW_VOLT &&
	    apusys_opps.vsram_volatge != VSRAM_HIGH_VOLT)
		return -EINVAL;
	for (buck = 0; buck < APUSYS_BUCK_NUM; buck++) {
		if (apusys_opps.cur_buck_volt[buck] <= DVFS_VOLT_NOT_SUPPORT ||
		    apusys_opps.cur_buck_volt[buck] >= DVFS_VOLT_MAX ||
		    apusys_opps.next_buck_volt[buck] <= DVFS_VOLT_NOT_SUPPORT ||
		    apusys_opps.next_buck_volt[buck] >= DVFS_VOLT_MAX)
			return -EINVAL;
	}
	for (domain = 0; domain < APUSYS_BUCK_DOMAIN_NUM; domain++) {
		if (!dvfs_power_domain_support(domain))
			continue;
		user = apusys_buck_domain_to_user[domain];
		buck = apusys_buck_domain_to_buck[domain];
		if (user < VPU0 || user > APUSYS_DVFS_USER_NUM ||
		    buck < VPU_BUCK || buck >= APUSYS_BUCK_NUM)
			return -EINVAL;
		cur_index = apusys_opps.cur_opp_index[domain];
		next_index = apusys_opps.next_opp_index[domain];
		if (cur_index >= APUSYS_MAX_NUM_OPPS ||
		    next_index >= APUSYS_MAX_NUM_OPPS ||
		    apusys_opps.opps[cur_index][domain].freq <=
			DVFS_FREQ_NOT_SUPPORT ||
		    apusys_opps.opps[next_index][domain].freq <=
			DVFS_FREQ_NOT_SUPPORT)
			return -EINVAL;
		/* Validate the shared rail selected by the production domain map.
		 * A nonzero frequency alone cannot justify a lower voltage.
		 */
		if (apusys_dvfs_domain_active(domain) &&
		    (apusys_opps.opps[next_index][domain].voltage <= DVFS_VOLT_NOT_SUPPORT ||
		     apusys_opps.opps[next_index][domain].voltage >= DVFS_VOLT_MAX ||
		     apusys_opps.next_buck_volt[buck] <
			apusys_opps.opps[next_index][domain].voltage))
			return -EINVAL;
	}
	return 0;
}

static int apusys_buck_up_check(void)
{
	uint8_t buck_small_index = 0, buck_large_index = 0;
	struct hal_param_volt volt_data;
	int ret;
	enum DVFS_VOLTAGE original[APUSYS_BUCK_NUM];

	memcpy(original, apusys_opps.cur_buck_volt, sizeof(original));

	if (original[VCORE_BUCK] < apusys_opps.next_buck_volt[VCORE_BUCK]) {
		volt_data.target_buck = VCORE_BUCK;
		volt_data.target_volt = apusys_opps.next_buck_volt[VCORE_BUCK];

		{
			ret = apusys_apply_voltage(volt_data.target_buck,
						   volt_data.target_volt);
			if (ret)
				return ret;
		}
	}

	if (original[VPU_BUCK] < original[MDLA_BUCK]) {
		buck_small_index = VPU_BUCK;
		buck_large_index = MDLA_BUCK;
	} else if (original[VPU_BUCK] > original[MDLA_BUCK]) {
		buck_small_index = MDLA_BUCK;
		buck_large_index = VPU_BUCK;
	} else {
		if (apusys_opps.next_buck_volt[VPU_BUCK] <
		    apusys_opps.next_buck_volt[MDLA_BUCK]) {
			buck_small_index = VPU_BUCK;
			buck_large_index = MDLA_BUCK;
		} else {
			buck_small_index = MDLA_BUCK;
			buck_large_index = VPU_BUCK;
		}
	}

	PWR_LOG_INF("%s,cur vpu=%d, cur mdla=%d, vpu=%d, mdla=%d, vsrm=%d\n",
		    __func__, original[VPU_BUCK], original[MDLA_BUCK],
		    apusys_opps.next_buck_volt[VPU_BUCK],
		    apusys_opps.next_buck_volt[MDLA_BUCK],
		    apusys_opps.vsram_volatge);

	if (apusys_opps.vsram_volatge == VSRAM_LOW_VOLT &&
	    (apusys_opps.next_buck_volt[VPU_BUCK] > VSRAM_TRANS_VOLT ||
	     apusys_opps.next_buck_volt[MDLA_BUCK] > VSRAM_TRANS_VOLT)) {
		if ((original[buck_small_index] <
		     apusys_opps.next_buck_volt[buck_small_index])) {
			if (apusys_opps.next_buck_volt[buck_small_index] >
			    VSRAM_TRANS_VOLT)
				volt_data.target_volt = VSRAM_TRANS_VOLT;
			else
				volt_data.target_volt =
				    apusys_opps
					.next_buck_volt[buck_small_index];
			volt_data.target_buck = buck_small_index;
			if (original[buck_small_index] !=
			    volt_data.target_volt) {
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}

		if ((original[buck_large_index] <
		     apusys_opps.next_buck_volt[buck_large_index])) {
			if (apusys_opps.next_buck_volt[buck_large_index] >
			    VSRAM_TRANS_VOLT)
				volt_data.target_volt = VSRAM_TRANS_VOLT;
			else
				volt_data.target_volt =
				    apusys_opps
					.next_buck_volt[buck_large_index];
			volt_data.target_buck = buck_large_index;
			if (original[buck_large_index] !=
			    volt_data.target_volt) {
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}

		volt_data.target_buck = SRAM_BUCK;
		volt_data.target_volt = VSRAM_HIGH_VOLT;
		{
			ret = apusys_apply_voltage(volt_data.target_buck,
						   volt_data.target_volt);
			if (ret)
				return ret;
		}

		if ((apusys_opps.next_buck_volt[buck_small_index] >
		     VSRAM_TRANS_VOLT) &&
		    (apusys_opps.next_buck_volt[buck_small_index] >
		     original[buck_small_index])) {
			volt_data.target_buck = buck_small_index;
			volt_data.target_volt =
			    apusys_opps.next_buck_volt[buck_small_index];
			{
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}

		if ((apusys_opps.next_buck_volt[buck_large_index] >
		     VSRAM_TRANS_VOLT) &&
		    (apusys_opps.next_buck_volt[buck_large_index] >
		     original[buck_large_index])) {
			volt_data.target_buck = buck_large_index;
			volt_data.target_volt =
			    apusys_opps.next_buck_volt[buck_large_index];
			{
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}
	} else {
		if (apusys_opps.next_buck_volt[buck_small_index] >
		    original[buck_small_index]) {
			volt_data.target_buck = buck_small_index;
			volt_data.target_volt =
			    apusys_opps.next_buck_volt[buck_small_index];
			{
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}

		if (apusys_opps.next_buck_volt[buck_large_index] >
		    original[buck_large_index]) {
			volt_data.target_buck = buck_large_index;
			volt_data.target_volt =
			    apusys_opps.next_buck_volt[buck_large_index];
			{
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}
	}
	return 0;
}

static int apusys_frequency_check(void)
{
	uint8_t buck_domain_index = 0;
	uint8_t next_opp_index = 0, cur_opp_index = 0;
	struct hal_param_freq freq_data;
	enum DVFS_USER user;
	bool apusys_power_on = false;
	int ret;

	for (buck_domain_index = 0; buck_domain_index < APUSYS_BUCK_DOMAIN_NUM;
	     buck_domain_index++) {
		if (dvfs_power_domain_support(buck_domain_index) == false)
			continue;

		user = apusys_buck_domain_to_user[buck_domain_index];
		if (user < APUSYS_DVFS_USER_NUM) {
			if (apusys_opps.is_power_on[user] == false)
				continue;
			else
				apusys_power_on = true;
		} else {
			if (apusys_power_on == false &&
#ifndef CONFIG_MACH_MT6853
			    apusys_opps.is_power_on[EDMA] == false &&
#if !defined(CONFIG_MACH_MT6873) && !defined(CONFIG_MACH_MT6877)
			    apusys_opps.is_power_on[EDMA2] == false &&
#endif
#endif
			    apusys_opps.is_power_on[REVISER] == false)
				continue;
		}

		if (apusys_opps.cur_opp_index[buck_domain_index] ==
		    apusys_opps.next_opp_index[buck_domain_index])
			continue;

		next_opp_index = apusys_opps.next_opp_index[buck_domain_index];
		cur_opp_index = apusys_opps.cur_opp_index[buck_domain_index];

		if (apusys_opps.opps[next_opp_index][buck_domain_index].freq !=
		    apusys_opps.opps[cur_opp_index][buck_domain_index].freq) {
			freq_data.target_volt_domain = buck_domain_index;
			freq_data.target_freq =
			    apusys_opps.opps[next_opp_index][buck_domain_index]
				.freq;
			ret = hal_config_power(PWR_CMD_SET_FREQ, VPU0,
					       (void *)&freq_data);
			if (ret)
				return ret;

			PWR_LOG_INF(
			    "%s, %s, freq from %d --> %d\n", __func__,
			    buck_domain_str[buck_domain_index],
			    apusys_opps.opps[cur_opp_index][buck_domain_index]
				.freq,
			    apusys_opps.opps[next_opp_index][buck_domain_index]
				.freq);
		}
		apusys_opps.cur_opp_index[buck_domain_index] = next_opp_index;
	}
	return 0;
}

static int apusys_buck_down_check(void)
{
	uint8_t buck_small_index = 0, buck_large_index = 0;
	struct hal_param_volt volt_data;
	int ret;
	enum DVFS_VOLTAGE original[APUSYS_BUCK_NUM];

	memcpy(original, apusys_opps.cur_buck_volt, sizeof(original));

	if (original[VPU_BUCK] < original[MDLA_BUCK]) {
		buck_small_index = VPU_BUCK;
		buck_large_index = MDLA_BUCK;
	} else if (original[VPU_BUCK] > original[MDLA_BUCK]) {
		buck_small_index = MDLA_BUCK;
		buck_large_index = VPU_BUCK;
	} else {
		if (apusys_opps.next_buck_volt[VPU_BUCK] <
		    apusys_opps.next_buck_volt[MDLA_BUCK]) {
			buck_small_index = VPU_BUCK;
			buck_large_index = MDLA_BUCK;
		} else {
			buck_small_index = MDLA_BUCK;
			buck_large_index = VPU_BUCK;
		}
	}

	PWR_LOG_INF("%s,cur vpu=%d, cur mdla=%d, vpu=%d, mdla=%d, vsrm=%d\n",
		    __func__, original[VPU_BUCK], original[MDLA_BUCK],
		    apusys_opps.next_buck_volt[VPU_BUCK],
		    apusys_opps.next_buck_volt[MDLA_BUCK],
		    apusys_opps.vsram_volatge);

	if (apusys_opps.vsram_volatge == VSRAM_HIGH_VOLT &&
	    (apusys_opps.next_buck_volt[VPU_BUCK] <= VSRAM_TRANS_VOLT &&
	     apusys_opps.next_buck_volt[MDLA_BUCK] <= VSRAM_TRANS_VOLT)) {
		if (original[buck_large_index] >
		    apusys_opps.next_buck_volt[buck_large_index]) {
			if (apusys_opps.next_buck_volt[buck_large_index] <=
			    VSRAM_TRANS_VOLT)
				volt_data.target_volt = VSRAM_TRANS_VOLT;
			else
				volt_data.target_volt =
				    apusys_opps
					.next_buck_volt[buck_large_index];
			volt_data.target_buck = buck_large_index;

			if (original[buck_large_index] !=
			    volt_data.target_volt) {
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}
		if (original[buck_small_index] >
		    apusys_opps.next_buck_volt[buck_small_index]) {
			if (apusys_opps.next_buck_volt[buck_small_index] <=
			    VSRAM_TRANS_VOLT)
				volt_data.target_volt = VSRAM_TRANS_VOLT;
			else
				volt_data.target_volt =
				    apusys_opps
					.next_buck_volt[buck_small_index];
			volt_data.target_buck = buck_small_index;

			if (original[buck_small_index] !=
			    volt_data.target_volt) {
				ret =
				    apusys_apply_voltage(volt_data.target_buck,
							 volt_data.target_volt);
				if (ret)
					return ret;
			}
		}

		volt_data.target_buck = SRAM_BUCK;
		volt_data.target_volt = VSRAM_LOW_VOLT;
		{
			ret = apusys_apply_voltage(volt_data.target_buck,
						   volt_data.target_volt);
			if (ret)
				return ret;
		}
	}

	if (original[buck_large_index] >
	    apusys_opps.next_buck_volt[buck_large_index]) {
		volt_data.target_buck = buck_large_index;
		volt_data.target_volt =
		    apusys_opps.next_buck_volt[buck_large_index];
		{
			ret = apusys_apply_voltage(volt_data.target_buck,
						   volt_data.target_volt);
			if (ret)
				return ret;
		}
	}

	if (original[buck_small_index] >
	    apusys_opps.next_buck_volt[buck_small_index]) {
		volt_data.target_buck = buck_small_index;
		volt_data.target_volt =
		    apusys_opps.next_buck_volt[buck_small_index];
		{
			ret = apusys_apply_voltage(volt_data.target_buck,
						   volt_data.target_volt);
			if (ret)
				return ret;
		}
	}

	if (original[VCORE_BUCK] > apusys_opps.next_buck_volt[VCORE_BUCK]) {
		volt_data.target_buck = VCORE_BUCK;
		volt_data.target_volt = apusys_opps.next_buck_volt[VCORE_BUCK];

		{
			ret = apusys_apply_voltage(volt_data.target_buck,
						   volt_data.target_volt);
			if (ret)
				return ret;
		}
	}
	return 0;
}

void apusys_dvfs_info(void)
{
	char logv_str[128], logf_str[128], log_str[128];
	uint8_t domain;
	uint8_t user;
	int div = 1000;
	uint8_t cur_opp_index[APUSYS_BUCK_DOMAIN_NUM];
	uint8_t next_opp_index[APUSYS_BUCK_DOMAIN_NUM];
	uint8_t c_opp_index;
	uint8_t n_opp_index;
	unsigned long rem_nsec;
	int ret, ret_v, ret_f = 0;

	ret = sprintf(log_str, "(u_op,T,min,max)");
	if (ret < 0)
		LOG_ERR("%s sprintf fail (%d)\n", __func__, ret);

	ret_v = sprintf(logv_str, "v[");
	if (ret_v < 0)
		LOG_ERR("%s sprintf fail (%d)\n", __func__, ret_v);

	ret_f = sprintf(logf_str, "f[");
	if (ret_f < 0)
		LOG_ERR("%s sprintf fail (%d)\n", __func__, ret_f);

	for (user = 0; user < APUSYS_DVFS_USER_NUM; user++) {
		if (dvfs_user_support(user) == false)
			continue;
		ret = sprintf(log_str + strlen(log_str), ",(%d,%d,%d,%d)",
			apusys_opps.driver_opp_index[user],
			apusys_opps.thermal_opp[user],
			apusys_opps.power_lock_min_opp[user],
			apusys_opps.power_lock_max_opp[user]);

		if (ret < 0)
			LOG_ERR("%s sprintf fail\n", __func__);
	}

	for (domain = 0; domain < APUSYS_BUCK_DOMAIN_NUM; domain++) {
		if (dvfs_power_domain_support(domain) == false)
			continue;

		cur_opp_index[domain] =
			apusys_opps.cur_opp_index[domain];
		c_opp_index = cur_opp_index[domain];

		next_opp_index[domain] =
			apusys_opps.next_opp_index[domain];
		n_opp_index = next_opp_index[domain];

		ret_v = sprintf(logv_str + strlen(logv_str), ",(%d,%d)",
			apusys_opps.opps[c_opp_index][domain].voltage / div,
			apusys_opps.opps[n_opp_index][domain].voltage / div);
		if (ret_v < 0)
			LOG_ERR("%s sprintf fail (%d)\n", __func__, ret_v);

		ret_f = sprintf(logf_str + strlen(logf_str), ",(%d,%d)",
			apusys_opps.opps[c_opp_index][domain].freq / div,
			apusys_opps.opps[n_opp_index][domain].freq / div);
		if (ret_f < 0)
			LOG_ERR("%s sprintf fail (%d)\n", __func__, ret_f);
	}

	rem_nsec = do_div(apusys_opps.id, 1000000000);
	ret = sprintf(log_str + strlen(log_str), "] [%5lu.%06lu]",
		(unsigned long)apusys_opps.id, rem_nsec / 1000);
	if (ret < 0)
		LOG_ERR("%s sprintf fail (%d)\n", __func__, ret);

	ret_v = sprintf(logv_str + strlen(logv_str), "] [%5lu.%06lu]",
		(unsigned long)apusys_opps.id, rem_nsec / 1000);
	if (ret_v < 0)
		LOG_ERR("%s sprintf fail (%d)\n", __func__, ret_v);

	ret_f = sprintf(logf_str + strlen(logf_str), "] [%5lu.%06lu]",
		(unsigned long)apusys_opps.id, rem_nsec / 1000);
	if (ret_f < 0)
		LOG_ERR("%s sprintf fail (%d)\n", __func__, ret_f);


	PWR_LOG_PM("APUPWR DVFS %s\n", log_str);
	PWR_LOG_PM("APUPWR DVFS %s\n", logv_str);
	PWR_LOG_PM("APUPWR DVFS %s\n", logf_str);

#if APUSYS_SETTLE_TIME_TEST
	/*
	 * upper bound (APUSYS_BUCK_NUM + 1)
	 * is due to index of Vsram as -1.
	 * And borrow div to be index of buck.
	 */
	for (div = 0; div < (APUSYS_BUCK_NUM + 1); div++) {
		if (apusys_opps.st[div].end) {
			snprintf(log_str, sizeof(log_str),
				 "APUSYS_SETTLE_TIME_TEST buck_id:%d, total settle_time",
				 div - 1);
			apu_profiling(&apusys_opps.st[div], log_str);
		}

		/* clear voltage timestamp for next round */
		memset(&apusys_opps.st[div].end, 0, sizeof(u64));

		/*
		 * set freq timestamp as right now,
		 * since there will be case that
		 * F --> no change
		 * V --> change
		 * such as Vsram.
		 */
		apusys_opps.st[div].begin = sched_clock();
	}
#endif

#ifdef APUPWR_TAG_TP
	trace_apupwr_dvfs(log_str);
	trace_apupwr_dvfs(logv_str);
	trace_apupwr_dvfs(logf_str);
#endif

	// debug ring buffer
	LOG_DUMP("APUPWR DVFS %s\n", log_str);
	LOG_DUMP("APUPWR DVFS %s\n", logv_str);
	LOG_DUMP("APUPWR DVFS %s\n", logf_str);
}

static int apusys_dvfs_state_machine(void)
{
	int ret = apusys_power_transition_error();

	if (ret)
		return ret;
	ret = apusys_validate_dvfs_plan();
	if (ret || !apusys_any_power_on())
		return ret;
	ret = apusys_buck_up_check();
	if (ret)
		return ret;
	ret = apusys_frequency_check();
	if (ret)
		return ret;
	return apusys_buck_down_check();
}

int apusys_dvfs_policy(uint64_t round_id)
{
	uint8_t user;
	uint8_t opp;
	uint8_t use_opp;
	uint8_t requested_opp[APUSYS_DVFS_USER_NUM];
	enum DVFS_VOLTAGE_DOMAIN buck_domain;
	enum DVFS_VOLTAGE voltage;
	uint8_t buck_index;
	uint8_t i = 0, j = 0;
	int ret;

	mutex_lock(&power_dvfs_mtx);
	ret = apusys_power_transition_error();
	if (ret)
		goto unlock;
	if (!apusys_opps.opps) {
		ret = -ENODEV;
		goto unlock;
	}
	/* Validate every policy index before any table access or electrical
	 * stage. */
	for (user = 0; user < APUSYS_DVFS_USER_NUM; user++) {
		requested_opp[user] = READ_ONCE(apusys_opps.user_opp_index[user]);
		if (!dvfs_user_support(user))
			continue;
		buck_domain = apusys_user_to_buck_domain[user];
		if (buck_domain < V_VPU0 ||
		    buck_domain >= APUSYS_BUCK_DOMAIN_NUM ||
		    requested_opp[user] >= APUSYS_MAX_NUM_OPPS ||
		    apusys_opps.thermal_opp[user] >= APUSYS_MAX_NUM_OPPS ||
		    apusys_opps.power_lock_min_opp[user] >=
			APUSYS_MAX_NUM_OPPS ||
		    apusys_opps.power_lock_max_opp[user] >=
			APUSYS_MAX_NUM_OPPS) {
			ret = -EINVAL;
			goto unlock;
		}
	}
	apusys_opps.id = round_id;

	for (user = 0; user < APUSYS_DVFS_USER_NUM; user++) {
		if (is_power_debug_lock == false) {
			if (dvfs_user_support(user) == false)
				continue;
			opp = requested_opp[user];
			buck_domain = apusys_user_to_buck_domain[user];
			use_opp = apusys_pwr_max_min_check(user, opp);
			if (use_opp >= APUSYS_MAX_NUM_OPPS) {
				ret = -EINVAL;
				goto reset_plan;
			}

			voltage =
			    apusys_opps.opps[use_opp][buck_domain].voltage;
			apusys_clk_path_update_pwr(user, voltage);
		}
	}

	if (is_power_debug_lock == false) {
		apusys_final_volt_check();

		apusys_pwr_constraint_check();

		apusys_pwr_efficiency_check();
	}

	ret = apusys_dvfs_state_machine();
	if (ret) {
		PWR_LOG_ERR("%s transition failed: %d\n", __func__, ret);
		goto reset_plan;
	}
	for (user = 0; user < APUSYS_DVFS_USER_NUM; user++)
		apusys_opps.driver_opp_index[user] = requested_opp[user];

	apusys_dvfs_info();

reset_plan:
	for (buck_index = 0; buck_index < APUSYS_BUCK_NUM; buck_index++) {
		apusys_opps.next_buck_volt[buck_index] = DVFS_VOLT_00_575000_V;
	}

	for (i = 0; i < APUSYS_DVFS_USER_NUM; i++) {
		for (j = 0; j < APUSYS_PATH_USER_NUM; j++)
			apusys_opps.user_path_volt[i][j] =
			    DVFS_VOLT_00_575000_V;
	}

unlock:
	mutex_unlock(&power_dvfs_mtx);
	return ret;
}

void apusys_set_opp(enum DVFS_USER user, uint8_t opp)
{
	if (user < VPU0 || user >= APUSYS_DVFS_USER_NUM ||
	    opp >= APUSYS_MAX_NUM_OPPS || apusys_power_transition_error())
		return;
	if (is_power_debug_lock == false) {
		if (apusys_opps.is_power_on[user] == true) {
			apusys_opps.user_opp_index[user] = opp;

			PWR_LOG_INF("%s, %s, user_opp=%d\n", __func__,
				    user_str[user], opp);
		}
	}
}

#if SUPPORT_VCORE_TO_IPUIF
int apusys_ipuif_opp_change(void)
{
	int prev_ipuif, next_ipuif, ret;
	struct hal_param_volt volt_data;
	struct hal_param_freq freq_data;
	int opp;

	mutex_lock(&power_dvfs_mtx);
	ret = apusys_power_transition_error();
	if (ret)
		goto out;
	spin_lock(&ipuif_lock);
	prev_ipuif = apusys_opps.driver_apu_vcore;
	next_ipuif = apusys_opps.qos_apu_vcore;
	spin_unlock(&ipuif_lock);
	ret = 0;
	if (next_ipuif == prev_ipuif)
		goto out;
	/* No fallback mapping: reject an invalid vote before either stage. */
	for (opp = 0; opp < VCORE_OPP_NUM; opp++)
		if (g_ipuif_opp_table[opp].ipuif_vcore == next_ipuif)
			break;
	if (opp == VCORE_OPP_NUM) {
		ret = -EINVAL;
		goto out;
	}
	if (conn_mtcmos_on != 1) {
		ret = -ENODEV;
		goto out;
	}
	volt_data.target_buck = VCORE_BUCK;
	volt_data.target_volt = next_ipuif;
	freq_data.target_volt_domain = V_VCORE;
	freq_data.target_freq = g_ipuif_opp_table[opp].ipuif_khz;
	if (next_ipuif > prev_ipuif) {
		ret = hal_config_power(PWR_CMD_SET_VOLT, MDLA0, &volt_data);
		if (ret)
			goto out;
		ret = hal_config_power(PWR_CMD_SET_FREQ, MDLA0, &freq_data);
	} else {
		ret = hal_config_power(PWR_CMD_SET_FREQ, MDLA0, &freq_data);
		if (ret)
			goto out;
		ret = hal_config_power(PWR_CMD_SET_VOLT, MDLA0, &volt_data);
	}
	if (!ret) {
		spin_lock(&ipuif_lock);
		apusys_opps.driver_apu_vcore = next_ipuif;
		spin_unlock(&ipuif_lock);
	}
out:
	mutex_unlock(&power_dvfs_mtx);
	return ret;
}

void apusys_set_apu_vcore(int target_volt)
{
	if (apusys_power_transition_error())
		return;
	if (is_power_debug_lock == false) {
		if (conn_mtcmos_on == 1) {
			spin_lock(&ipuif_lock);
			apusys_opps.qos_apu_vcore = target_volt;
			spin_unlock(&ipuif_lock);
			PWR_LOG_INF("%s, qos_apu_vcore, target_volt=%d\n",
				    __func__, target_volt);
		}
	}
}
#endif

// this function will be called in DVFS thread and be protected by mutex lock
bool apusys_check_opp_change(void)
{
	static uint8_t prev_thermal_opp[APUSYS_DVFS_USER_NUM];
	static bool prev_is_power_on[APUSYS_POWER_USER_NUM];
	uint8_t user;

	if (is_power_debug_lock == true || apusys_power_transition_error())
		return false;

	for (user = 0; user < APUSYS_DVFS_USER_NUM; user++) {
		if (dvfs_user_support(user) == false)
			continue;
		if (apusys_opps.user_opp_index[user] !=
			apusys_opps.driver_opp_index[user]) {
			PWR_LOG_INF("%s DVFS since opp change\n", __func__);
			return true;
		}

		if (apusys_opps.is_power_on[user] != prev_is_power_on[user]) {
			prev_is_power_on[user] = apusys_opps.is_power_on[user];
			PWR_LOG_INF("%s DVFS since power change\n", __func__);
			return true;
		}

		if (apusys_opps.thermal_opp[user] != prev_thermal_opp[user]) {
			prev_thermal_opp[user] = apusys_opps.thermal_opp[user];
			PWR_LOG_INF("%s DVFS since thermal event\n", __func__);
			return true;
		}

#if SUPPORT_VCORE_TO_IPUIF
		if (apusys_opps.driver_apu_vcore !=
			apusys_opps.qos_apu_vcore) {
			PWR_LOG_INF("%s DVFS since qos change\n", __func__);
			return true;
		}
#endif
	}

	return false;
}


static bool apusys_power_user_valid(enum DVFS_USER user)
{
	return (user >= VPU0 && user < APUSYS_DVFS_USER_NUM) ||
		user == EDMA || user == EDMA2 || user == REVISER;
}

int apusys_power_on(enum DVFS_USER user)
{
	int ret = 0;
	struct hal_param_pwr_mask pwr_mask = {0};
	enum DVFS_VOLTAGE_DOMAIN buck_domain;

	if (!apusys_power_user_valid(user))
		return -EINVAL;
	mutex_lock(&power_dvfs_mtx);
	ret = apusys_power_transition_error();
	if (ret)
		goto unlock;
	if (is_power_debug_lock) {
		ret = -EOPNOTSUPP;
		goto unlock;
	}
	if (apusys_opps.is_power_on[user]) {
		ret = -ECANCELED;
		goto unlock;
	}

	ret = hal_config_power(PWR_CMD_SET_BOOT_UP, user, (void *)&pwr_mask);

	if (ret == 0) {
		if (apusys_opps.power_bit_mask == 0) {	// first power on
			PWR_LOG_INF("%s first power on\n", __func__);
			apusys_opps.cur_buck_volt[VPU_BUCK] =
				VVPU_DEFAULT_VOLT;
			apusys_opps.cur_buck_volt[MDLA_BUCK] =
				VMDLA_DEFAULT_VOLT;
			apusys_opps.cur_buck_volt[VCORE_BUCK] =
				VCORE_DEFAULT_VOLT;
			apusys_opps.vsram_volatge = VSRAM_DEFAULT_VOLT;

			apusys_opps.cur_opp_index[V_APU_CONN] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.next_opp_index[V_APU_CONN] =
				APUSYS_DEFAULT_OPP;
#if !defined(CONFIG_MACH_MT6873) && !defined(CONFIG_MACH_MT6853)
			apusys_opps.cur_opp_index[V_TOP_IOMMU] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.next_opp_index[V_TOP_IOMMU] =
				APUSYS_DEFAULT_OPP;
#endif
		}

		if (user < APUSYS_DVFS_USER_NUM) {
			buck_domain = apusys_user_to_buck_domain[user];
			apusys_opps.cur_opp_index[buck_domain] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.next_opp_index[buck_domain] =
				APUSYS_DEFAULT_OPP;
		}

		apusys_opps.is_power_on[user] = true;
		apusys_opps.power_bit_mask |= (1U << user);
	}

unlock:
	mutex_unlock(&power_dvfs_mtx);
	return ret;
}

int apusys_power_off(enum DVFS_USER user)
{
	int id = 0; // dvfs user id
	int ret = 0;
	struct hal_param_pwr_mask pwr_mask = {0};
	enum DVFS_VOLTAGE_DOMAIN buck_domain;
	bool trigger_dvfs = false;

	if (!apusys_power_user_valid(user))
		return -EINVAL;
	mutex_lock(&power_dvfs_mtx);
	ret = apusys_power_transition_error();
	if (ret)
		goto unlock;
	if (!apusys_opps.is_power_on[user]) {
		ret = -ECANCELED;
		goto unlock;
	}

	ret = hal_config_power(PWR_CMD_SET_SHUT_DOWN, user, (void *)&pwr_mask);

	if (ret == 0) {
		/* PWR_CMD_SET_SHUT_DOWN pass */
		apusys_opps.is_power_on[user] = false;
		apusys_opps.power_bit_mask &= ~(1U << user);
		if (user < APUSYS_DVFS_USER_NUM) {
			buck_domain = apusys_user_to_buck_domain[user];
			apusys_opps.cur_opp_index[buck_domain] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.next_opp_index[buck_domain] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.user_opp_index[user] =
				 APUSYS_DEFAULT_OPP;
			apusys_opps.driver_opp_index[user] =
				 APUSYS_DEFAULT_OPP;
		}
		if (apusys_opps.power_bit_mask == 0) {
			PWR_LOG_INF("%s all power off\n", __func__);
			apusys_opps.cur_opp_index[V_APU_CONN] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.next_opp_index[V_APU_CONN] =
				APUSYS_DEFAULT_OPP;
#if !defined(CONFIG_MACH_MT6873) && !defined(CONFIG_MACH_MT6853)
			apusys_opps.cur_opp_index[V_TOP_IOMMU] =
				APUSYS_DEFAULT_OPP;
			apusys_opps.next_opp_index[V_TOP_IOMMU] =
				APUSYS_DEFAULT_OPP;
#endif
#if SUPPORT_VCORE_TO_IPUIF
			apusys_opps.driver_apu_vcore =
				VCORE_SHUTDOWN_VOLT;
#endif
		}

		if (is_power_debug_lock == false) {
			for (id = 0; id < APUSYS_DVFS_USER_NUM; id++) {
				if (dvfs_user_support(id) &&
					apusys_opps.is_power_on[id]) {
					trigger_dvfs = true;
					break;
				}
			}
		}
	}
unlock:
	mutex_unlock(&power_dvfs_mtx);
	if (trigger_dvfs) {
		PWR_LOG_INF("%s trigger DVFS\n", __func__);
		event_trigger_dvfs_policy();
	}
	return ret;
}

int apusys_power_init(enum DVFS_USER user, void *init_power_data)
{
	int i = 0, j = 0;
	struct hal_param_seg_support seg_data = {0};
	enum DVFS_VOLTAGE_DOMAIN domain;
	int ret = 0;

	mutex_init(&power_dvfs_mtx);
	spin_lock_init(&ipuif_lock);
	ret = hal_config_power(PWR_CMD_SEGMENT_CHECK, VPU0, (void *)&seg_data);
	if (ret)
		goto out;

#if defined(CONFIG_MACH_MT6893)
	if (seg_data.seg == SEGMENT_0)
		apusys_opps.opps = dvfs_table_b0;
	else
		apusys_opps.opps = dvfs_table_b1;
#else
	if (seg_data.seg == SEGMENT_0)
		apusys_opps.opps = dvfs_table_0;
	else if (seg_data.seg == SEGMENT_2)
		apusys_opps.opps = dvfs_table_2;
	else
		apusys_opps.opps = dvfs_table_1;
#endif
	ret = hal_config_power(PWR_CMD_BINNING_CHECK, VPU0, NULL);
	if (ret)
		goto out;

	for (i = 0; i < APUSYS_DVFS_USER_NUM; i++)	{
		seg_data.user = i;
		hal_config_power(PWR_CMD_SEGMENT_CHECK,
			VPU0, (void *)&seg_data);
		domain = apusys_user_to_buck_domain[i];
		apusys_dvfs_user_support[i] = seg_data.support;
		apusys_dvfs_buck_domain_support[domain] = seg_data.support;
		if (dvfs_user_support(user) == false)
			continue;
		apusys_opps.thermal_opp[i] = 0;
		apusys_opps.user_opp_index[i] = APUSYS_DEFAULT_OPP;
		apusys_opps.driver_opp_index[i] = APUSYS_DEFAULT_OPP;
		apusys_opps.power_lock_max_opp[i] = APUSYS_MAX_NUM_OPPS-1;
		apusys_opps.power_lock_min_opp[i] = 0;
		apusys_opps.is_power_on[i] = false;
		for (j = 0; j < APUSYS_PATH_USER_NUM; j++)
			apusys_opps.user_path_volt[i][j] =
			DVFS_VOLT_00_575000_V;
	}

	for (i = 0; i < APUSYS_BUCK_DOMAIN_NUM; i++) {
		apusys_opps.next_opp_index[i] = APUSYS_DEFAULT_OPP;
		apusys_opps.cur_opp_index[i] = APUSYS_DEFAULT_OPP;
	}

	apusys_opps.cur_buck_volt[VPU_BUCK] = VVPU_DEFAULT_VOLT;
	apusys_opps.cur_buck_volt[MDLA_BUCK] = VMDLA_DEFAULT_VOLT;
	apusys_opps.cur_buck_volt[VCORE_BUCK] = VCORE_DEFAULT_VOLT;
	apusys_opps.next_buck_volt[VPU_BUCK] = DVFS_VOLT_00_575000_V;
	apusys_opps.next_buck_volt[MDLA_BUCK] = DVFS_VOLT_00_575000_V;
	apusys_opps.next_buck_volt[VCORE_BUCK] = VCORE_DEFAULT_VOLT;
	apusys_opps.vsram_volatge = VSRAM_DEFAULT_VOLT;

	apusys_opps.power_bit_mask = 0;

	ret = hal_config_power(PWR_CMD_INIT_POWER, user, init_power_data);
out:
	PWR_LOG_INF("%s done ret %d\n", __func__, ret);
	return ret;
}


void apusys_power_uninit(enum DVFS_USER user)
{
	hal_config_power(PWR_CMD_UNINIT_POWER, user, NULL);
	PWR_LOG_INF("%s done,\n", __func__);
}

enum DVFS_FREQ apusys_get_dvfs_freq(enum DVFS_VOLTAGE_DOMAIN domain)
{
	if (!apusys_opps.opps || domain < V_VPU0 ||
	    domain >= APUSYS_BUCK_DOMAIN_NUM || apusys_power_transition_error() ||
	    apusys_opps.cur_opp_index[domain] >= APUSYS_MAX_NUM_OPPS)
		return DVFS_FREQ_NOT_SUPPORT;
	return apusys_opps.opps[apusys_opps.cur_opp_index[domain]][domain].freq;
}

bool apusys_get_power_on_status(enum DVFS_USER user)
{
	if (!apusys_power_user_valid(user) || apusys_power_transition_error())
		return false;
	return apusys_opps.is_power_on[user];
}

// SPDX-License-Identifier: GPL-2.0
/*
 * op6893: serve the 4.19 fuel-gauge daemon.
 *
 * /vendor/bin/fuelgauged and libfgauge_gm30.so are Oplus's 4.19 GM3.0
 * build.  This kernel's daemon interface descends from it, but under the
 * same NETLINK_FGD socket nearly everything moved:
 *
 *  - afw_header grew a leading instance_id/datatype word (28 -> 32 bytes);
 *  - fg_daemon_cmds gained GET_CUSTOM_TABLE..GET_VERSION_CONTROL ahead of
 *    IS_BAT_EXIST, so 4.19's 4..83 are this kernel's 7..86;
 *  - 4.19's typed GET_DATA/SEND_DATA table transfer, the typed get/set of
 *    COMMUNICATION_INT and GET_HW_INFO's sub-selector became separate
 *    commands;
 *  - fuel_gauge_custom_data and fuel_gauge_table_custom_data grew fields
 *    (800 / 55124 bytes against 764 / 39020).
 *
 * fgauge_version_check() reads five words back out of the custom data and
 * wants 88/764/16/12/1016.  It never got them, so the daemon idled in a 10 s
 * "FG Version not match" loop while the kernel-mode fallback drove the
 * gauge -- and that fallback has no FG_INTR_CHR_FULL handling and no
 * voltage-SOC fusion.
 *
 * With fgd419=1, the default, each daemon launch is served here end to end
 * in the 4.19 layout: each request is decoded, renumbered or rebuilt into
 * what the 6.6 handler (gm->netlink_handler) expects, and its reply and
 * every kernel-initiated notification are re-encoded.  Where the 4.19 and
 * 6.6 handlers differ in meaning rather than layout, 4.19 wins; those cases
 * are handled by name in fgd419_handle().
 *
 * The flag is latched at each SET_DAEMON_PID.  Off, the header-only
 * translation and kernel-mode fallback in mtk-battery-manager.c run as
 * before, so writing the parameter and restarting fuelgauged switches either
 * way without a reboot.
 */

#include <linux/build_bug.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netlink.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/sock.h>

#include "mtk_battery.h"
#include "mtk_battery_fgd419.h"

static bool fgd419_enable = true;
module_param_named(fgd419, fgd419_enable, bool, 0644);
MODULE_PARM_DESC(fgd419,
	"serve the 4.19 fuelgauged protocol from the daemon's next launch");

static_assert(sizeof(struct fgd419_hdr) == 28);
static_assert(sizeof(struct fgd419_custom_data) == FGD419_CUSTOM_DATA_LEN);
static_assert(sizeof(struct fgd419_profile) == 32);
static_assert(sizeof(struct fgd419_table) == 3260);
static_assert(sizeof(struct fgd419_table_custom_data) == 39020);
static_assert(sizeof(struct fgd419_param_t_7) == 16);
/* afw_data_param is 4.19's fgd_cmd_param_t_6: type/total/size/idx/input */
static_assert(offsetof(struct afw_data_param, input) == 16);
static_assert(sizeof(struct afw_data_param) == 16 + 2048);
static_assert(MAX_TABLE == 10 && MAX_CHARGE_RDC == 5);
/*
 * 43 ints, 4 bytes of padding, 3 timespec64: 224, the same bytes 4.19's arm64
 * kernel sent with its 64-bit struct timespec.
 */
static_assert(sizeof(struct ag_center_data_st) == 224);
/* the renumbered block is contiguous on both sides */
static_assert(FG_DAEMON_CMD_IS_BAT_EXIST == FGD419_IS_BAT_EXIST + 3);
static_assert(FG_DAEMON_CMD_GET_INIT_FLAG == FGD419_GET_INIT_FLAG + 3);
static_assert(FG_DAEMON_CMD_NOTIFY_DAEMON == FGD419_NOTIFY_DAEMON + 3);
static_assert(FG_DAEMON_CMD_IS_CHARGER_EXIST == FGD419_IS_CHARGER_EXIST + 3);
static_assert(FG_DAEMON_CMD_GET_CHARGER_STATUS ==
	FGD419_GET_CHARGER_STATUS + 3);
static_assert(FG_DAEMON_CMD_GET_HW_INFO == FGD419_GET_HW_INFO + 3);
static_assert(FG_DAEMON_CMD_SET_KERNEL_UISOC == FGD419_SET_KERNEL_UISOC + 3);
static_assert(FG_DAEMON_CMD_SET_CAR_TUNE_VALUE ==
	FGD419_SET_CAR_TUNE_VALUE + 3);
static_assert(FG_DAEMON_CMD_GET_RTC_UI_SOC == FGD419_GET_RTC_UI_SOC + 3);
static_assert(FG_DAEMON_CMD_DUMP_LOG == FGD419_DUMP_LOG + 3);
/* 4.19 knew kernel commands 0..15 and interrupt flags up to KERNEL_CMD */
static_assert(FG_KERNEL_CMD_SEND_BH_DATA == FGD419_KERNEL_CMD_NUMBER - 1);

#define FGD419_SHIFT		(FG_DAEMON_CMD_IS_BAT_EXIST - FGD419_IS_BAT_EXIST)
#define FGD419_INTR_MASK	((((u32)FG_INTR_KERNEL_CMD) << 1) - 1)

static DEFINE_MUTEX(fgd419_lock);
static bool fgd419_on;
static int fgd419_full_mv;	/* from fgd419_full_threshold() */
static struct afw_header *fgd419_req, *fgd419_rep;
static char *fgd419_out;
static struct fgd419_table_custom_data *fgd419_table;
static int fgd419_ptim_i;

/* COMMUNICATION_INT's typed get/set, each its own command in 6.6 */
static const struct {
	int type;
	unsigned int cmd;
} fgd419_comm_int_map[] = {
	{ 1, FG_DAEMON_CMD_GET_SHUTDOWN_CAR },
	{ 2, FG_DAEMON_CMD_GET_NCAR },
	{ 3, FG_DAEMON_CMD_GET_CURR_1 },
	{ 4, FG_DAEMON_CMD_GET_CURR_2 },
	{ 5, FG_DAEMON_CMD_GET_REFRESH },
	{ 6, FG_DAEMON_CMD_GET_IS_AGING_RESET },
	{ 7, FG_DAEMON_CMD_GET_SOC_DECIMAL_RATE },
	{ 8, FG_DAEMON_CMD_GET_DIFF_SOC_SET },
	{ 9, FG_DAEMON_CMD_GET_IS_FORCE_FULL },
	{ 10, FG_DAEMON_CMD_GET_ZCV_INTR_CURR },
	{ 11, FG_DAEMON_CMD_GET_CHARGE_POWER_SEL },
	{ 1000, FG_DAEMON_CMD_SET_SOC },
	{ 1001, FG_DAEMON_CMD_SET_C_D0_SOC },
	{ 1002, FG_DAEMON_CMD_SET_V_D0_SOC },
	{ 1003, FG_DAEMON_CMD_SET_C_SOC },
	{ 1004, FG_DAEMON_CMD_SET_V_SOC },
	{ 1005, FG_DAEMON_CMD_SET_QMAX_T_AGING },
	{ 1006, FG_DAEMON_CMD_SET_SAVED_CAR },
	{ 1007, FG_DAEMON_CMD_SET_AGING_FACTOR },
	{ 1008, FG_DAEMON_CMD_SET_QMAX },
	{ 1009, FG_DAEMON_CMD_SET_BAT_CYCLES },
	{ 1010, FG_DAEMON_CMD_SET_NCAR },
	{ 1011, FG_DAEMON_CMD_SET_OCV_MAH },
	{ 1012, FG_DAEMON_CMD_SET_OCV_VTEMP },
	{ 1013, FG_DAEMON_CMD_SET_OCV_SOC },
	{ 1014, FG_DAEMON_CMD_SET_CON0_SOFF_VALID },
	{ 1015, FG_DAEMON_CMD_SET_ZCV_INTR_EN },
};

/*
 * 4.19 took these from mt6885 mach/mtk_battery_property.h.  This tree's
 * mtk_battery.h defaults differ and the stock DTB sets none of them, so the
 * daemon would otherwise run with constants it was never tuned for.  Only a
 * value still at the 6.6 default is replaced, so a DT override survives.
 */
#define FGD419_DEFAULT(f, d66, d419) \
	do { if (s->f == (d66)) c->f = (d419); } while (0)

static void fgd419_fill_custom(const struct mtk_battery *gm,
	struct fgd419_custom_data *c)
{
	const struct fuel_gauge_custom_data *s = &gm->fg_cust_data;

	memset(c, 0, sizeof(*c));
	/* generated: every 4.19 field this kernel still has, by name */
	c->versionID1 = s->versionID1;
	c->versionID2 = s->versionID2;
	c->versionID3 = s->versionID3;
	c->hardwareVersion = s->hardwareVersion;
	c->low_temp_mode = s->low_temp_mode;
	c->low_temp_mode_temp = s->low_temp_mode_temp;
	c->q_max_L_current = s->q_max_L_current;
	c->q_max_H_current = s->q_max_H_current;
	c->q_max_sys_voltage = s->q_max_sys_voltage;
	c->pseudo1_en = s->pseudo1_en;
	c->pseudo100_en = s->pseudo100_en;
	c->pseudo100_en_dis = s->pseudo100_en_dis;
	c->pseudo1_iq_offset = s->pseudo1_iq_offset;
	c->qmax_sel = s->qmax_sel;
	c->iboot_sel = s->iboot_sel;
	c->shutdown_system_iboot = s->shutdown_system_iboot;
	c->multi_temp_gauge0 = s->multi_temp_gauge0;
	c->car_tune_value = s->car_tune_value;
	c->fg_meter_resistance = s->fg_meter_resistance;
	c->com_fg_meter_resistance = s->com_fg_meter_resistance;
	c->r_fg_value = s->r_fg_value;
	c->com_r_fg_value = s->com_r_fg_value;
	c->mtk_chr_exist = s->mtk_chr_exist;
	c->aging_one_en = s->aging_one_en;
	c->aging1_update_soc = s->aging1_update_soc;
	c->aging1_load_soc = s->aging1_load_soc;
	c->aging4_update_soc = s->aging4_update_soc;
	c->aging4_load_soc = s->aging4_load_soc;
	c->aging5_update_soc = s->aging5_update_soc;
	c->aging5_load_soc = s->aging5_load_soc;
	c->aging6_update_soc = s->aging6_update_soc;
	c->aging6_load_soc = s->aging6_load_soc;
	c->aging_temp_diff = s->aging_temp_diff;
	c->aging_temp_low_limit = s->aging_temp_low_limit;
	c->aging_temp_high_limit = s->aging_temp_high_limit;
	c->aging_100_en = s->aging_100_en;
	c->difference_voltage_update = s->difference_voltage_update;
	c->aging_two_en = s->aging_two_en;
	c->aging_third_en = s->aging_third_en;
	c->aging_4_en = s->aging_4_en;
	c->aging_5_en = s->aging_5_en;
	c->aging_6_en = s->aging_6_en;
	c->diff_soc_setting = s->diff_soc_setting;
	c->keep_100_percent = s->keep_100_percent;
	c->difference_full_cv = s->difference_full_cv;
	c->diff_bat_temp_setting = s->diff_bat_temp_setting;
	c->diff_bat_temp_setting_c = s->diff_bat_temp_setting_c;
	c->discharge_tracking_time = s->discharge_tracking_time;
	c->charge_tracking_time = s->charge_tracking_time;
	c->difference_fullocv_vth = s->difference_fullocv_vth;
	c->difference_fullocv_ith = s->difference_fullocv_ith;
	c->charge_pseudo_full_level = s->charge_pseudo_full_level;
	c->over_discharge_level = s->over_discharge_level;
	c->full_tracking_bat_int2_multiply = s->full_tracking_bat_int2_multiply;
	c->hwocv_swocv_diff = s->hwocv_swocv_diff;
	c->hwocv_swocv_diff_lt = s->hwocv_swocv_diff_lt;
	c->hwocv_swocv_diff_lt_temp = s->hwocv_swocv_diff_lt_temp;
	c->hwocv_oldocv_diff = s->hwocv_oldocv_diff;
	c->hwocv_oldocv_diff_chr = s->hwocv_oldocv_diff_chr;
	c->swocv_oldocv_diff = s->swocv_oldocv_diff;
	c->swocv_oldocv_diff_chr = s->swocv_oldocv_diff_chr;
	c->vbat_oldocv_diff = s->vbat_oldocv_diff;
	c->tnew_told_pon_diff = s->tnew_told_pon_diff;
	c->tnew_told_pon_diff2 = s->tnew_told_pon_diff2;
	c->pmic_shutdown_time = s->pmic_shutdown_time;
	c->bat_plug_out_time = s->bat_plug_out_time;
	c->swocv_oldocv_diff_emb = s->swocv_oldocv_diff_emb;
	c->vir_oldocv_diff_emb = s->vir_oldocv_diff_emb;
	c->vir_oldocv_diff_emb_lt = s->vir_oldocv_diff_emb_lt;
	c->vir_oldocv_diff_emb_tmp = s->vir_oldocv_diff_emb_tmp;
	c->difference_fgc_fgv_th1 = s->difference_fgc_fgv_th1;
	c->difference_fgc_fgv_th2 = s->difference_fgc_fgv_th2;
	c->difference_fgc_fgv_th3 = s->difference_fgc_fgv_th3;
	c->difference_fgc_fgv_th_soc1 = s->difference_fgc_fgv_th_soc1;
	c->difference_fgc_fgv_th_soc2 = s->difference_fgc_fgv_th_soc2;
	c->nafg_time_setting = s->nafg_time_setting;
	c->nafg_ratio = s->nafg_ratio;
	c->nafg_ratio_en = s->nafg_ratio_en;
	c->nafg_ratio_tmp_thr = s->nafg_ratio_tmp_thr;
	c->nafg_resistance = s->nafg_resistance;
	c->pmic_shutdown_current = s->pmic_shutdown_current;
	c->pmic_shutdown_sw_en = s->pmic_shutdown_sw_en;
	c->force_vc_mode = s->force_vc_mode;
	c->embedded_sel = s->embedded_sel;
	c->loading_1_en = s->loading_1_en;
	c->loading_2_en = s->loading_2_en;
	c->diff_iavg_th = s->diff_iavg_th;
	c->r_bat_sense = s->r_bat_sense;
	c->r_i_sense = s->r_i_sense;
	c->r_charger_1 = s->r_charger_1;
	c->r_charger_2 = s->r_charger_2;
	c->fg_pre_tracking_en = s->fg_pre_tracking_en;
	c->vbat2_det_time = s->vbat2_det_time;
	c->vbat2_det_counter = s->vbat2_det_counter;
	c->vbat2_det_voltage1 = s->vbat2_det_voltage1;
	c->vbat2_det_voltage2 = s->vbat2_det_voltage2;
	c->vbat2_det_voltage3 = s->vbat2_det_voltage3;
	c->shutdown_1_time = s->shutdown_1_time;
	c->shutdown_gauge0 = s->shutdown_gauge0;
	c->shutdown_gauge1_xmins = s->shutdown_gauge1_xmins;
	c->shutdown_gauge1_mins = s->shutdown_gauge1_mins;
	c->shutdown_gauge0_voltage = s->shutdown_gauge0_voltage;
	c->shutdown_gauge1_vbat_en = s->shutdown_gauge1_vbat_en;
	c->shutdown_gauge1_vbat = s->shutdown_gauge1_vbat;
	c->zcv_suspend_time = s->zcv_suspend_time;
	c->sleep_current_avg = s->sleep_current_avg;
	c->zcv_com_vol_limit = s->zcv_com_vol_limit;
	c->dc_ratio_sel = s->dc_ratio_sel;
	c->dc_r_cnt = s->dc_r_cnt;
	c->pseudo1_sel = s->pseudo1_sel;
	c->ui_full_limit_en = s->ui_full_limit_en;
	c->ui_full_limit_soc0 = s->ui_full_limit_soc0;
	c->ui_full_limit_ith0 = s->ui_full_limit_ith0;
	c->ui_full_limit_soc1 = s->ui_full_limit_soc1;
	c->ui_full_limit_ith1 = s->ui_full_limit_ith1;
	c->ui_full_limit_soc2 = s->ui_full_limit_soc2;
	c->ui_full_limit_ith2 = s->ui_full_limit_ith2;
	c->ui_full_limit_soc3 = s->ui_full_limit_soc3;
	c->ui_full_limit_ith3 = s->ui_full_limit_ith3;
	c->ui_full_limit_soc4 = s->ui_full_limit_soc4;
	c->ui_full_limit_ith4 = s->ui_full_limit_ith4;
	c->ui_full_limit_time = s->ui_full_limit_time;
	c->ui_full_limit_fc_soc0 = s->ui_full_limit_fc_soc0;
	c->ui_full_limit_fc_ith0 = s->ui_full_limit_fc_ith0;
	c->ui_full_limit_fc_soc1 = s->ui_full_limit_fc_soc1;
	c->ui_full_limit_fc_ith1 = s->ui_full_limit_fc_ith1;
	c->ui_full_limit_fc_soc2 = s->ui_full_limit_fc_soc2;
	c->ui_full_limit_fc_ith2 = s->ui_full_limit_fc_ith2;
	c->ui_full_limit_fc_soc3 = s->ui_full_limit_fc_soc3;
	c->ui_full_limit_fc_ith3 = s->ui_full_limit_fc_ith3;
	c->ui_full_limit_fc_soc4 = s->ui_full_limit_fc_soc4;
	c->ui_full_limit_fc_ith4 = s->ui_full_limit_fc_ith4;
	c->ui_low_limit_en = s->ui_low_limit_en;
	c->ui_low_limit_soc0 = s->ui_low_limit_soc0;
	c->ui_low_limit_vth0 = s->ui_low_limit_vth0;
	c->ui_low_limit_soc1 = s->ui_low_limit_soc1;
	c->ui_low_limit_vth1 = s->ui_low_limit_vth1;
	c->ui_low_limit_soc2 = s->ui_low_limit_soc2;
	c->ui_low_limit_vth2 = s->ui_low_limit_vth2;
	c->ui_low_limit_soc3 = s->ui_low_limit_soc3;
	c->ui_low_limit_vth3 = s->ui_low_limit_vth3;
	c->ui_low_limit_soc4 = s->ui_low_limit_soc4;
	c->ui_low_limit_vth4 = s->ui_low_limit_vth4;
	c->ui_low_limit_time = s->ui_low_limit_time;
	c->moving_battemp_en = s->moving_battemp_en;
	c->moving_battemp_thr = s->moving_battemp_thr;
	c->d0_sel = s->d0_sel;
	c->dod_init_sel = s->dod_init_sel;
	c->aging_sel = s->aging_sel;
	c->fg_tracking_current = s->fg_tracking_current;
	c->fg_tracking_current_iboot_en = s->fg_tracking_current_iboot_en;
	c->ui_fast_tracking_en = s->ui_fast_tracking_en;
	c->ui_fast_tracking_gap = s->ui_fast_tracking_gap;
	c->bat_par_i = s->bat_par_i;
	c->c_old_d0 = s->c_old_d0;
	c->v_old_d0 = s->v_old_d0;
	c->c_soc = s->c_soc;
	c->v_soc = s->v_soc;
	c->ui_old_soc = s->ui_old_soc;
	c->dlpt_ui_remap_en = s->dlpt_ui_remap_en;
	c->aging_factor_min = s->aging_factor_min;
	c->aging_factor_diff = s->aging_factor_diff;
	c->keep_100_percent_minsoc = s->keep_100_percent_minsoc;
	c->battery_tmp_to_disable_gm30 = s->battery_tmp_to_disable_gm30;
	c->battery_tmp_to_disable_nafg = s->battery_tmp_to_disable_nafg;
	c->battery_tmp_to_enable_nafg = s->battery_tmp_to_enable_nafg;
	c->disable_nafg = s->disable_nafg;
	c->zcv_car_gap_percentage = s->zcv_car_gap_percentage;
	c->uisoc_update_type = s->uisoc_update_type;
	c->pl_charger_status = s->pl_charger_status;
	c->power_on_car_chr = s->power_on_car_chr;
	c->power_on_car_nochr = s->power_on_car_nochr;
	c->shutdown_car_ratio = s->shutdown_car_ratio;
	c->aging_diff_max_threshold = s->aging_diff_max_threshold;
	c->aging_diff_max_level = s->aging_diff_max_level;
	c->aging_factor_t_min = s->aging_factor_t_min;
	c->cycle_diff = s->cycle_diff;
	c->aging_count_min = s->aging_count_min;
	c->default_score = s->default_score;
	c->default_score_quantity = s->default_score_quantity;
	c->fast_cycle_set = s->fast_cycle_set;
	c->level_max_change_bat = s->level_max_change_bat;
	c->diff_max_change_bat = s->diff_max_change_bat;
	c->aging_tracking_start = s->aging_tracking_start;
	c->max_aging_data = s->max_aging_data;
	c->max_fast_data = s->max_fast_data;
	c->fast_data_threshold_score = s->fast_data_threshold_score;
	c->daemon_log_level = s->daemon_log_level;
	c->record_log = s->record_log;
	c->versionID1 = FGD419_CMD_NUMBER;
	c->versionID2 = sizeof(*c);
	c->versionID3 = FGD419_KERNEL_CMD_NUMBER;
	c->fg_get_max = FGD419_GET_MAX;
	c->fg_set_max = FGD419_SET_DATA_MAX;

	FGD419_DEFAULT(pseudo100_en_dis, PSEUDO100_EN_DIS, 1);
	FGD419_DEFAULT(aging_factor_min, UNIT_TRANS_100 * AGING_FACTOR_MIN,
		UNIT_TRANS_100 * 75);
	FGD419_DEFAULT(aging_4_en, AGING_4_EN, 1);
	FGD419_DEFAULT(aging_5_en, AGING_5_EN, 1);
	FGD419_DEFAULT(aging_6_en, AGING_6_EN, 1);
	FGD419_DEFAULT(vbat2_det_voltage1, VBAT2_DET_VOLTAGE1, 34500);
	FGD419_DEFAULT(vbat2_det_voltage2, VBAT2_DET_VOLTAGE2, 32000);
	FGD419_DEFAULT(zcv_car_gap_percentage, ZCV_CAR_GAP_PERCENTAGE, 1);
	FGD419_DEFAULT(hwocv_oldocv_diff, HWOCV_OLDOCV_DIFF, 300);
	FGD419_DEFAULT(fg_tracking_current, FG_TRACKING_CURRENT, 30000);
}

static void fgd419_copy_profile(struct fgd419_profile *d,
	const struct fuelgauge_profile_struct *p, int n)
{
	int i, j;

	for (i = 0; i < n; i++) {
		d[i].mah = p[i].mah;
		d[i].voltage = p[i].voltage;
		d[i].resistance = p[i].resistance;
		d[i].percentage = (int)p[i].percentage;
		for (j = 0; j < MAX_CHARGE_RDC; j++)
			d[i].rdc[j] = p[i].charge_r.rdc[j];
	}
}

static void fgd419_fill_table(const struct mtk_battery *gm,
	struct fgd419_table_custom_data *t)
{
	const struct fuel_gauge_table_custom_data *s = &gm->fg_table_cust_data;
	int i, j;

	memset(t, 0, sizeof(*t));
	t->active_table_number = s->active_table_number;
	for (i = 0; i < MAX_TABLE; i++) {
		const struct fuel_gauge_table *a = &s->fg_profile[i];
		struct fgd419_table *b = &t->fg_profile[i];

		b->temperature = a->temperature;
		b->q_max = a->q_max;
		b->q_max_h_current = a->q_max_h_current;
		b->pseudo1 = a->pseudo1;
		b->pseudo100 = a->pseudo100;
		b->pmic_min_vol = a->pmic_min_vol;
		b->pon_iboot = a->pon_iboot;
		b->qmax_sys_vol = a->qmax_sys_vol;
		b->shutdown_hl_zcv = a->shutdown_hl_zcv;
		b->size = a->size;
		for (j = 0; j < MAX_CHARGE_RDC; j++)
			b->r_pseudo100[j] = a->r_pseudo100.pseudo[j];
		fgd419_copy_profile(b->fg_profile, a->fg_profile, 100);
	}
	t->temperature_tb0 = s->temperature_tb0;
	t->fg_profile_temperature_0_size = s->fg_profile_temperature_0_size;
	fgd419_copy_profile(t->fg_profile_temperature_0,
		s->fg_profile_temperature_0, 100);
	t->temperature_tb1 = s->temperature_tb1;
	t->fg_profile_temperature_1_size = s->fg_profile_temperature_1_size;
	fgd419_copy_profile(t->fg_profile_temperature_1,
		s->fg_profile_temperature_1, 100);
}

/*
 * GET_PTIM_VBAT, whose current GET_PTIM_I then returns.  4.19 latched
 * battery voltage and current together with the PMIC's impedance (IMP)
 * measurement, and before init_flag used the pair LK measured at boot.
 * This kernel has no IMP channel for the gauge, so the 6.6 handler answers
 * 0 V, from which the daemon's boot-time swocv lands at the bottom of the
 * table.  Without an LK pair -- there is none on this board; 4.19 fell back
 * to BATADC with zero current -- take the BATADC voltage and the gauge's
 * instant current, the two readings PTIM would have latched (0.1 mV and
 * 0.1 mA, charging positive), so the daemon can take the IR drop out.
 */
static int fgd419_ptim(struct mtk_battery *gm)
{
	int vbat = 0;

	if (gm->ptim_lk_v > 0) {
		fgd419_ptim_i = gm->ptim_lk_i;
		vbat = gm->ptim_lk_v;
		/* LK reports mV */
		return vbat < 5000 ? vbat * 10 : vbat;
	}
	gauge_get_property_control(gm, GAUGE_PROP_BATTERY_VOLTAGE, &vbat, 0);
	fgd419_ptim_i = gauge_get_int_property(gm, GAUGE_PROP_BATTERY_CURRENT);
	return vbat * 10;
}

/*
 * IS_CHARGER_EXIST.  4.19 asked the PMIC charger-detect bit and the USB role;
 * the 6.6 handler looks up a "charger" phandle this DTB does not have (and
 * leaks a devres entry per call doing so).  Same supplier names as
 * get_charger_exist() in mtk_battery_algo.c, which lives in the gauge module;
 * the battery manager's low-voltage check uses this one.
 */
int fgd419_charger_exist(void)
{
	static const char * const names[] = {
		"primary_chg", "mtk-master-charger", "ac", "usb",
	};
	union power_supply_propval val;
	struct power_supply *psy;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		psy = power_supply_get_by_name(names[i]);
		if (!psy)
			continue;
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE,
			&val);
		power_supply_put(psy);
		if (!ret && val.intval)
			return 1;
	}
	return 0;
}

static void fgd419_send(struct mtk_battery_manager *bm, u32 pid, int seq,
	const struct fgd419_hdr *hdr, const void *data, u32 len, gfp_t gfp)
{
	struct fgd419_hdr *h;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int ret;

	if (!pid || !bm->mtk_bm_sk)
		return;
	if (len > AFW_MSG_MAX_LEN)
		len = AFW_MSG_MAX_LEN;
	skb = alloc_skb(NLMSG_SPACE(FGD419_HDR_LEN + len), gfp);
	if (!skb)
		return;
	nlh = nlmsg_put(skb, pid, seq, 0, FGD419_HDR_LEN + len, 0);
	if (!nlh) {
		kfree_skb(skb);
		return;
	}
	h = nlmsg_data(nlh);
	*h = *hdr;
	h->data_len = len;
	h->ret_data_len = 0;
	h->identity = AFW_MAGIC;
	if (len)
		memcpy(h + 1, data, len);
	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;
	ret = netlink_unicast(bm->mtk_bm_sk, skb, pid, MSG_DONTWAIT);
	if (ret < 0)
		pr_err("[fgd419] send cmd %u to %u failed %d\n", hdr->cmd, pid, ret);
}

/* Run one 6.6 command; *fgd419_rep holds its reply afterwards. */
static void fgd419_call(struct mtk_battery *gm, unsigned int cmd,
	u32 subcmd, u32 para1, const void *in, u32 len)
{
	struct afw_header *req = fgd419_req;

	/*
	 * The daemon marks a plain request with subcmd 0 and its own header
	 * length in para1.  fg_cmd_check() wants this kernel's header length
	 * there, or it logs a version error and sleeps 10 s.  A semantic para1
	 * (SET_FG_TIME's timer, SET_KERNEL_SOC's type) only ever tests zero,
	 * which the rewrite preserves.
	 */
	if (subcmd == 0 && para1 == FGD419_HDR_LEN)
		para1 = AFW_MSG_HEADER_LEN;

	memset(req, 0, sizeof(*req));
	req->cmd = cmd;
	req->subcmd = subcmd;
	req->subcmd_para1 = para1;
	req->data_len = len;
	req->identity = AFW_MAGIC;
	if (len)
		memcpy(req->data, in, len);
	memset(fgd419_rep, 0, sizeof(*fgd419_rep));
	gm->netlink_handler(gm, req, fgd419_rep);
	if (fgd419_rep->data_len > AFW_MSG_MAX_LEN)
		fgd419_rep->data_len = AFW_MSG_MAX_LEN;
}

/*
 * 4.19's Oplus charger told the gauge about a full battery only in the
 * temperature zones that charge to the full float voltage: little cold to
 * normal, plus cold with qcom,support_3p6_standard (oplus_charger.c).  The
 * other zones stop lower.  Here every MT6360 charge-done reaches the gauge,
 * and the daemon would take such a voltage-limited stop for full and pull
 * SOC to 100%.  The stop voltage tells the zones apart: forward a
 * charge-done only above the midpoint between the lowest float voltage of a
 * zone 4.19 reported and the highest of a zone it kept quiet, both from the
 * Oplus charger node.  Without such a gap in the DT, forward them all.
 */
static int fgd419_full_threshold(void)
{
	static const char * const told[] = {
		"qcom,temp_little_cold_vfloat_mv", "qcom,temp_cool_vfloat_mv",
		"qcom,temp_little_cool_vfloat_mv", "qcom,temp_normal_vfloat_mv",
	};
	struct device_node *np = of_find_node_by_path("/charger");
	int lo = INT_MAX, hi = 0, i;
	bool cold_told;
	u32 mv;

	cold_told = of_property_read_bool(np, "qcom,support_3p6_standard");
	for (i = 0; i < ARRAY_SIZE(told); i++)
		if (!of_property_read_u32(np, told[i], &mv))
			lo = min_t(int, lo, mv);
	if (!of_property_read_u32(np, "qcom,temp_warm_vfloat_mv", &mv))
		hi = max_t(int, hi, mv);
	if (!of_property_read_u32(np, "qcom,temp_cold_vfloat_mv", &mv)) {
		if (cold_told)
			lo = min_t(int, lo, mv);
		else
			hi = max_t(int, hi, mv);
	}
	of_node_put(np);

	if (lo == INT_MAX || !hi || lo <= hi)
		return 0;
	return (lo + hi) / 2;
}

static void fgd419_set_daemon_pid(struct mtk_battery_manager *bm,
	struct mtk_battery *gm, const char *in, u32 len)
{
	u32 pid = 0;
	bool relaunch = bm->fgd_pid != 0;

	memcpy(&pid, in, min_t(u32, len, sizeof(pid)));
	bm->fgd_pid = pid;

	/*
	 * Taking over from the kernel-mode fallback: stop it consuming the
	 * interrupts and let the daemon run its boot-time initialisation
	 * (dod_init from OCV, RTC and its saved state) instead of the
	 * kill-and-restart recovery an init_flag of 1 would select.
	 */
	if (gm->algo.active) {
		gm->algo.active = false;
		gm->init_flag = 0;
		pr_err("[fgd419] daemon %u takes over from kernel-mode gauge\n",
			pid);
	}

	if (relaunch && gm->Bat_EC_ctrl.debug_kill_daemontest != 1 &&
	    gm->init_flag == 1)
		gm->fg_cust_data.dod_init_sel = 14;
	else
		gm->fg_cust_data.dod_init_sel = 0;

	WRITE_ONCE(fgd419_full_mv, fgd419_full_threshold());

	pr_err("[fgd419] SET_DAEMON_PID %u (%s) init_flag %d dod_init_sel %d, charge-done forwarded from %d mV\n",
		pid, relaunch ? "relaunch" : "first launch", gm->init_flag,
		gm->fg_cust_data.dod_init_sel, fgd419_full_mv);
}

static u32 fgd419_get_data(struct mtk_battery *gm, const char *in, u32 len)
{
	const struct afw_data_param *q = (const void *)in;
	struct afw_data_param *p = (void *)fgd419_out;

	memset(p, 0, sizeof(*p));
	if (len < offsetof(struct afw_data_param, input)) {
		pr_err("[fgd419] GET_DATA short request %u\n", len);
		return sizeof(*p);
	}
	p->type = q->type;
	p->total_size = q->total_size;
	p->size = q->size;
	p->idx = q->idx;

	if (q->type != FGD419_DATA_TABLE) {
		pr_err("[fgd419] GET_DATA unknown type %u\n", q->type);
		return sizeof(*p);
	}
	/* rebuild at the start of each transfer, serve chunks from it */
	if (q->idx == 0)
		fgd419_fill_table(gm, fgd419_table);
	if (q->total_size != sizeof(*fgd419_table))
		pr_err("[fgd419] GET_DATA table size %u, have %zu\n",
			q->total_size, sizeof(*fgd419_table));
	if (q->size <= sizeof(p->input) && q->idx <= sizeof(*fgd419_table) &&
	    q->size <= sizeof(*fgd419_table) - q->idx)
		memcpy(p->input, (char *)fgd419_table + q->idx, q->size);
	else
		pr_err("[fgd419] GET_DATA chunk %u+%u out of range\n",
			q->idx, q->size);
	return sizeof(*p);
}

static u32 fgd419_comm_int(struct mtk_battery *gm, const char *in, u32 len)
{
	struct fgd419_param_t_7 q = { 0 }, *p = (void *)fgd419_out;
	int i;

	memcpy(&q, in, min_t(u32, len, sizeof(q)));
	memset(p, 0, sizeof(*p));
	p->type = q.type;
	for (i = 0; i < ARRAY_SIZE(fgd419_comm_int_map); i++)
		if (fgd419_comm_int_map[i].type == q.type)
			break;
	if (i == ARRAY_SIZE(fgd419_comm_int_map)) {
		p->status = -1;
		pr_err("[fgd419] COMMUNICATION_INT unknown type %d\n", q.type);
		return sizeof(*p);
	}
	fgd419_call(gm, fgd419_comm_int_map[i].cmd, 0, AFW_MSG_HEADER_LEN,
		&q.input, sizeof(q.input));
	if (fgd419_rep->data_len >= sizeof(p->output))
		memcpy(&p->output, fgd419_rep->data, sizeof(p->output));
	return sizeof(*p);
}

/*
 * SET_BATTERY_CAPACITY: 4.19's payload is a bare fgd_cmd_param_t_8 at data[0]
 * (the 6.6 handler expects it wrapped in an afw_data_param, and pushes it
 * into ENERGY_* properties the mtk-gauge supply rejects).  data[4] is FCC,
 * data[6] the remaining percentage x100, data[10]/[11] show_ag/health.
 */
static void fgd419_set_battery_capacity(struct mtk_battery *gm,
	const char *in, u32 len)
{
	int d[12] = { 0 };

	if (len < sizeof(int) + sizeof(d)) {
		pr_err("[fgd419] SET_BATTERY_CAPACITY short %u\n", len);
		return;
	}
	memcpy(d, in + sizeof(int), sizeof(d));
	if (d[10] != 0 && d[11] != 0)
		gm->show_ag = d[10];
	if (gm->prev_batt_fcc != d[4])
		pr_err("[fgd419] SET_BATTERY_CAPACITY fcc %d soc %d show_ag %d health %d\n",
			d[4], d[6], d[10], d[11]);
	gm->prev_batt_fcc = d[4];
	gm->prev_batt_remaining_capacity = d[4] / 10 * d[6] / 10000;
}

static void fgd419_handle(struct mtk_battery_manager *bm,
	struct mtk_battery *gm, const struct fgd419_hdr *h, const char *in,
	u32 len, u32 portid, int seq)
{
	struct fgd419_hdr reply = { .cmd = h->cmd };
	const void *out = NULL;
	u32 olen = 0;
	int v;

	switch (h->cmd) {
	case FGD419_PRINT_LOG:
		pr_err("[%sd]%.*s", gm->gauge->name, (int)strnlen(in, len), in);
		break;
	case FGD419_SET_DAEMON_PID:
		fgd419_set_daemon_pid(bm, gm, in, len);
		break;
	case FGD419_GET_CUSTOM_SETTING:
		fgd419_fill_custom(gm, (void *)fgd419_out);
		out = fgd419_out;
		olen = sizeof(struct fgd419_custom_data);
		break;
	case FGD419_GET_DATA:
		olen = fgd419_get_data(gm, in, len);
		out = fgd419_out;
		break;
	case FGD419_SEND_DATA:
		/* the daemon's copy of the custom data; 4.19 kept it for dumps */
		break;
	case FGD419_COMMUNICATION_INT:
		olen = fgd419_comm_int(gm, in, len);
		out = fgd419_out;
		break;
	case FGD419_SET_BATTERY_CAPACITY:
		fgd419_set_battery_capacity(gm, in, len);
		break;
	case FGD419_GET_BH_DATA:
		out = &gm->bh_data;
		olen = sizeof(gm->bh_data);
		break;
	case FGD419_GET_HW_INFO:
		if (h->subcmd_para1 == FGD419_HW_INFO_SHUTDOWN_CAR)
			fgd419_call(gm, FG_DAEMON_CMD_GET_SHUTDOWN_CAR, 0,
				AFW_MSG_HEADER_LEN, NULL, 0);
		else if (h->subcmd_para1 == FGD419_HW_INFO_NCAR)
			fgd419_call(gm, FG_DAEMON_CMD_GET_NCAR, 0,
				AFW_MSG_HEADER_LEN, NULL, 0);
		else
			fgd419_call(gm, FG_DAEMON_CMD_GET_HW_INFO, h->subcmd,
				h->subcmd_para1, in, len);
		out = fgd419_rep->data;
		olen = fgd419_rep->data_len;
		break;
	case FGD419_SET_CAR_TUNE_VALUE:
		/*
		 * 4.19 was built without CALIBRATE_CAR_TUNE_VALUE_BY_META_TOOL and
		 * kept the DT value; the 6.6 handler would adopt the nvram
		 * number the daemon read and rescale every CAR reading with it.
		 */
		memcpy(&v, in, min_t(u32, len, sizeof(v)));
		pr_err("[fgd419] ignore daemon car_tune %d, keep %d\n", v,
			gm->fg_cust_data.car_tune_value);
		break;
	case FGD419_GET_CHARGER_STATUS:
		/* OPLUS_FEATURE_CHG_BASIC 4.19 always answered CHR_NORMAL */
		v = 0;
		out = &v;
		olen = sizeof(v);
		break;
	case FGD419_GET_PTIM_VBAT:
		v = fgd419_ptim(gm);
		out = &v;
		olen = sizeof(v);
		break;
	case FGD419_GET_PTIM_I:
		out = &fgd419_ptim_i;
		olen = sizeof(fgd419_ptim_i);
		break;
	case FGD419_GET_IMIX:
		/*
		 * 4.19 answered DLPT's estimate of the largest current the cell
		 * can deliver before cut-off, and the daemon resets the gauge
		 * when that is positive but below shutdown_system_iboot.  The 6.6
		 * handler answers the average current, which is exactly that
		 * whenever the phone idles on a charger.  There is no DLPT here;
		 * 0 is what 4.19 reported until DLPT's first sample.
		 */
		v = 0;
		out = &v;
		olen = sizeof(v);
		break;
	case FGD419_IS_CHARGER_EXIST:
		v = fgd419_charger_exist();
		out = &v;
		olen = sizeof(v);
		break;
	default:
		if (h->cmd < FGD419_IS_BAT_EXIST || h->cmd > FGD419_DUMP_LOG) {
			pr_err("[fgd419] unknown cmd %u\n", h->cmd);
			break;
		}
		fgd419_call(gm, h->cmd + FGD419_SHIFT, h->subcmd,
			h->subcmd_para1, in, len);
		out = fgd419_rep->data;
		olen = fgd419_rep->data_len;
		/* init_flag is a bool here, an int on the daemon's side */
		if (h->cmd == FGD419_GET_INIT_FLAG && olen < sizeof(int))
			olen = sizeof(int);
		break;
	}

	fgd419_send(bm, portid, seq, &reply, out, olen, GFP_KERNEL);
}

static int fgd419_alloc(void)
{
	if (fgd419_req)
		return 0;
	fgd419_req = kvzalloc(sizeof(*fgd419_req), GFP_KERNEL);
	fgd419_rep = kvzalloc(sizeof(*fgd419_rep), GFP_KERNEL);
	fgd419_out = kvzalloc(AFW_MSG_MAX_LEN, GFP_KERNEL);
	fgd419_table = kvzalloc(sizeof(*fgd419_table), GFP_KERNEL);
	if (fgd419_req && fgd419_rep && fgd419_out && fgd419_table)
		return 0;
	kvfree(fgd419_req);
	kvfree(fgd419_rep);
	kvfree(fgd419_out);
	kvfree(fgd419_table);
	fgd419_req = fgd419_rep = NULL;
	fgd419_out = NULL;
	fgd419_table = NULL;
	return -ENOMEM;
}

/*
 * A packet already identified as the 28-byte layout.  Returns false to leave
 * it to the existing translation (flag off for this daemon launch).
 */
bool fgd419_rx(struct mtk_battery_manager *bm, const void *data, int len,
	u32 portid, int seq)
{
	const struct fgd419_hdr *h = data;
	struct mtk_battery *gm = bm->gm1;
	u32 dlen;

	if (len < (int)FGD419_HDR_LEN)
		return false;

	mutex_lock(&fgd419_lock);
	if (h->cmd == FGD419_SET_DAEMON_PID) {
		bool on = READ_ONCE(fgd419_enable);

		if (on && fgd419_alloc()) {
			pr_err("[fgd419] no memory, staying on translation\n");
			on = false;
		}
		if (on != fgd419_on)
			pr_err("[fgd419] 4.19 protocol %s for this daemon\n",
				on ? "served" : "off");
		fgd419_on = on;
	}
	if (!fgd419_on) {
		mutex_unlock(&fgd419_lock);
		return false;
	}

	dlen = min_t(u32, h->data_len, len - FGD419_HDR_LEN);
	dlen = min_t(u32, dlen, AFW_MSG_MAX_LEN);
	if (gm && gm->netlink_handler && gm->gauge) {
		fgd419_handle(bm, gm, h, (const char *)data + FGD419_HDR_LEN,
			dlen, portid, seq);
	} else {
		/* the daemon blocks on a reply to every request */
		struct fgd419_hdr reply = { .cmd = h->cmd };

		pr_err("[fgd419] gauge not ready, cmd %u unanswered\n", h->cmd);
		fgd419_send(bm, portid, seq, &reply, NULL, 0, GFP_KERNEL);
	}
	mutex_unlock(&fgd419_lock);
	return true;
}

/*
 * Kernel-initiated message (wakeup_fg_daemon).  4.19 carried the kernel
 * command in subcmd and its argument in para1 -- where the 28-byte header
 * puts them, not where shifting the 32-byte one would.
 */
bool fgd419_tx(struct mtk_battery_manager *bm, struct afw_header *msg)
{
	struct fgd419_hdr h = { 0 };
	u32 flow;

	if (!bm || !bm->fgd_legacy || !READ_ONCE(fgd419_on))
		return false;

	if (msg->cmd != FG_DAEMON_CMD_NOTIFY_DAEMON || msg->data_len < sizeof(flow)) {
		pr_err("[fgd419] drop kernel message cmd %u\n", msg->cmd);
		return true;
	}
	memcpy(&flow, msg->data, sizeof(flow));
	/* BAT_INT2_CHECK, BAT_PLUGIN, LAST_MODE: nothing 4.19 would act on */
	if (flow & ~FGD419_INTR_MASK)
		return true;
	if (flow == FG_INTR_KERNEL_CMD && msg->subcmd >= FGD419_KERNEL_CMD_NUMBER)
		return true;
	/* voltage-limited stops stay away from the daemon */
	if (flow == FG_INTR_CHR_FULL) {
		int vbat = READ_ONCE(bm->bs_data.bat_batt_vol);
		int full_mv = READ_ONCE(fgd419_full_mv);

		pr_err("[fgd419] charge done at %d mV, %s\n", vbat,
			vbat >= full_mv ? "full" : "voltage-limited, held back");
		if (vbat < full_mv)
			return true;
	}

	h.cmd = FGD419_NOTIFY_DAEMON;
	h.subcmd = msg->subcmd;
	h.subcmd_para1 = msg->subcmd_para1;
	fgd419_send(bm, bm->fgd_pid, 0, &h, &flow, sizeof(flow), GFP_ATOMIC);
	return true;
}

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * op6893: wire format of the 4.19 fuel-gauge daemon (GM3.0 fgd_nl_msg_t).
 *
 * The shipped /vendor/bin/fuelgauged + libfgauge_gm30.so are Oplus's 4.19
 * build.  Everything here is copied from the 4.19 tree the stock image was
 * built from (drivers/power/supply/mediatek/battery/mtk_battery_internal.h,
 * mt6885 mach headers) and is pinned by the static_asserts in
 * mtk_battery_fgd419.c: the daemon refuses to run unless the custom data it
 * reads back is exactly 764 bytes and the table 39020.
 */
#ifndef __MTK_BATTERY_FGD419_H__
#define __MTK_BATTERY_FGD419_H__

#include <linux/types.h>

struct mtk_battery_manager;
struct mtk_battery;
struct afw_header;

/* Seven words; no leading instance_id/datatype word as in afw_header. */
struct fgd419_hdr {
	u32 cmd;
	u32 subcmd;
	u32 subcmd_para1;
	u32 subcmd_para2;
	u32 data_len;
	u32 ret_data_len;
	u32 identity;
};

#define FGD419_HDR_LEN		((u32)sizeof(struct fgd419_hdr))	/* 28 */

/* enum Fg_daemon_cmds, 4.19 numbering (FG_DAEMON_CMD_FROM_USER_NUMBER 88) */
enum fgd419_cmd {
	FGD419_PRINT_LOG		= 0,
	FGD419_SET_DAEMON_PID		= 1,
	FGD419_GET_CUSTOM_SETTING	= 2,
	FGD419_GET_DATA			= 3,
	FGD419_IS_BAT_EXIST		= 4,	/* 4..83 are 6.6's 7..86 */
	FGD419_GET_INIT_FLAG		= 5,
	FGD419_NOTIFY_DAEMON		= 7,
	FGD419_GET_PTIM_VBAT		= 12,
	FGD419_GET_PTIM_I		= 13,
	FGD419_IS_CHARGER_EXIST		= 14,
	FGD419_GET_CHARGER_STATUS	= 25,
	FGD419_GET_IMIX			= 50,
	FGD419_GET_HW_INFO		= 57,
	FGD419_SET_KERNEL_UISOC		= 59,
	FGD419_SET_CAR_TUNE_VALUE	= 69,
	FGD419_GET_RTC_UI_SOC		= 71,
	FGD419_DUMP_LOG			= 83,
	FGD419_SEND_DATA		= 84,
	FGD419_COMMUNICATION_INT	= 85,
	FGD419_SET_BATTERY_CAPACITY	= 86,
	FGD419_GET_BH_DATA		= 87,
	FGD419_CMD_NUMBER		= 88,
};

/* The four other values fgauge_version_check() insists on. */
#define FGD419_CUSTOM_DATA_LEN		764
#define FGD419_KERNEL_CMD_NUMBER	16	/* FG_KERNEL_CMD_FROM_USER_NUMBER */
#define FGD419_GET_MAX			12	/* FG_GET_MAX */
#define FGD419_SET_DATA_MAX		1016	/* FG_SET_DATA_MAX */

/* GET_DATA / SEND_DATA type (enum Fg_data_type) */
#define FGD419_DATA_TABLE		0	/* FUEL_GAUGE_TABLE_CUSTOM_DATA */
#define FGD419_DATA_CUSTOM		1	/* FGD_CMD_PARAM_T_CUSTOM */

/* GET_HW_INFO selector, carried in subcmd_para1 */
#define FGD419_HW_INFO_SHUTDOWN_CAR	1
#define FGD419_HW_INFO_NCAR		2

/* COMMUNICATION_INT payload (fgd_cmd_param_t_7) */
struct fgd419_param_t_7 {
	int type;
	int input;
	int output;
	int status;
};

/* struct fuel_gauge_custom_data, 4.19 field order */
struct fgd419_custom_data {
	int versionID1;
	int versionID2;
	int versionID3;
	int fg_get_max;
	int fg_set_max;
	int hardwareVersion;
	int low_temp_mode;
	int low_temp_mode_temp;
	int q_max_L_current;
	int q_max_H_current;
	int q_max_sys_voltage;
	int pseudo1_en;
	int pseudo100_en;
	int pseudo100_en_dis;
	int pseudo1_iq_offset;
	int qmax_sel;
	int iboot_sel;
	int shutdown_system_iboot;
	int multi_temp_gauge0;
	int car_tune_value;
	int fg_meter_resistance;
	int com_fg_meter_resistance;
	int r_fg_value;
	int com_r_fg_value;
	int mtk_chr_exist;
	int aging_one_en;
	int aging1_update_soc;
	int aging1_load_soc;
	int aging4_update_soc;
	int aging4_load_soc;
	int aging5_update_soc;
	int aging5_load_soc;
	int aging6_update_soc;
	int aging6_load_soc;
	int aging_temp_diff;
	int aging_temp_low_limit;
	int aging_temp_high_limit;
	int aging_100_en;
	int difference_voltage_update;
	int aging_two_en;
	int aging_third_en;
	int aging_4_en;
	int aging_5_en;
	int aging_6_en;
	int diff_soc_setting;
	int keep_100_percent;
	int difference_full_cv;
	int diff_bat_temp_setting;
	int diff_bat_temp_setting_c;
	int discharge_tracking_time;
	int charge_tracking_time;
	int difference_fullocv_vth;
	int difference_fullocv_ith;
	int charge_pseudo_full_level;
	int over_discharge_level;
	int full_tracking_bat_int2_multiply;
	int hwocv_swocv_diff;
	int hwocv_swocv_diff_lt;
	int hwocv_swocv_diff_lt_temp;
	int hwocv_oldocv_diff;
	int hwocv_oldocv_diff_chr;
	int swocv_oldocv_diff;
	int swocv_oldocv_diff_chr;
	int vbat_oldocv_diff;
	int tnew_told_pon_diff;
	int tnew_told_pon_diff2;
	int pmic_shutdown_time;
	int bat_plug_out_time;
	int swocv_oldocv_diff_emb;
	int vir_oldocv_diff_emb;
	int vir_oldocv_diff_emb_lt;
	int vir_oldocv_diff_emb_tmp;
	int difference_fgc_fgv_th1;
	int difference_fgc_fgv_th2;
	int difference_fgc_fgv_th3;
	int difference_fgc_fgv_th_soc1;
	int difference_fgc_fgv_th_soc2;
	int nafg_time_setting;
	int nafg_ratio;
	int nafg_ratio_en;
	int nafg_ratio_tmp_thr;
	int nafg_resistance;
	int pmic_shutdown_current;
	int pmic_shutdown_sw_en;
	int force_vc_mode;
	int embedded_sel;
	int loading_1_en;
	int loading_2_en;
	int diff_iavg_th;
	int r_bat_sense;
	int r_i_sense;
	int r_charger_1;
	int r_charger_2;
	int fg_pre_tracking_en;
	int vbat2_det_time;
	int vbat2_det_counter;
	int vbat2_det_voltage1;
	int vbat2_det_voltage2;
	int vbat2_det_voltage3;
	int shutdown_1_time;
	int shutdown_gauge0;
	int shutdown_gauge1_xmins;
	int shutdown_gauge1_mins;
	int shutdown_gauge0_voltage;
	int shutdown_gauge1_vbat_en;
	int shutdown_gauge1_vbat;
	int zcv_suspend_time;
	int sleep_current_avg;
	int zcv_com_vol_limit;
	int dc_ratio_sel;
	int dc_r_cnt;
	int pseudo1_sel;
	int ui_full_limit_en;
	int ui_full_limit_soc0;
	int ui_full_limit_ith0;
	int ui_full_limit_soc1;
	int ui_full_limit_ith1;
	int ui_full_limit_soc2;
	int ui_full_limit_ith2;
	int ui_full_limit_soc3;
	int ui_full_limit_ith3;
	int ui_full_limit_soc4;
	int ui_full_limit_ith4;
	int ui_full_limit_time;
	int ui_full_limit_fc_soc0;
	int ui_full_limit_fc_ith0;
	int ui_full_limit_fc_soc1;
	int ui_full_limit_fc_ith1;
	int ui_full_limit_fc_soc2;
	int ui_full_limit_fc_ith2;
	int ui_full_limit_fc_soc3;
	int ui_full_limit_fc_ith3;
	int ui_full_limit_fc_soc4;
	int ui_full_limit_fc_ith4;
	int ui_low_limit_en;
	int ui_low_limit_soc0;
	int ui_low_limit_vth0;
	int ui_low_limit_soc1;
	int ui_low_limit_vth1;
	int ui_low_limit_soc2;
	int ui_low_limit_vth2;
	int ui_low_limit_soc3;
	int ui_low_limit_vth3;
	int ui_low_limit_soc4;
	int ui_low_limit_vth4;
	int ui_low_limit_time;
	int moving_battemp_en;
	int moving_battemp_thr;
	int d0_sel;
	int dod_init_sel;
	int aging_sel;
	int fg_tracking_current;
	int fg_tracking_current_iboot_en;
	int ui_fast_tracking_en;
	int ui_fast_tracking_gap;
	int bat_par_i;
	int c_old_d0;
	int v_old_d0;
	int c_soc;
	int v_soc;
	int ui_old_soc;
	int dlpt_ui_remap_en;
	int aging_factor_min;
	int aging_factor_diff;
	int keep_100_percent_minsoc;
	int battery_tmp_to_disable_gm30;
	int battery_tmp_to_disable_nafg;
	int battery_tmp_to_enable_nafg;
	int disable_nafg;
	int zcv_car_gap_percentage;
	int uisoc_update_type;
	int pl_charger_status;
	int power_on_car_chr;
	int power_on_car_nochr;
	int shutdown_car_ratio;
	int aging_diff_max_threshold;
	int aging_diff_max_level;
	int aging_factor_t_min;
	int cycle_diff;
	int aging_count_min;
	int default_score;
	int default_score_quantity;
	int fast_cycle_set;
	int level_max_change_bat;
	int diff_max_change_bat;
	int aging_tracking_start;
	int max_aging_data;
	int max_fast_data;
	int fast_data_threshold_score;
	int daemon_log_level;
	int record_log;
};

/* FUELGAUGE_PROFILE_STRUCT: percentage is a full u32 here, u16 in 6.6 */
struct fgd419_profile {
	u32 mah;
	u16 voltage;
	u16 resistance;
	u32 percentage;
	int rdc[5];
};

/* struct fuel_gauge_table: no r_ratio fields or r_profile arrays */
struct fgd419_table {
	int temperature;
	int q_max;
	int q_max_h_current;
	int pseudo1;
	int pseudo100;
	int pmic_min_vol;
	int pon_iboot;
	int qmax_sys_vol;
	int shutdown_hl_zcv;
	int size;
	int r_pseudo100[5];
	struct fgd419_profile fg_profile[100];
};

/* struct fuel_gauge_table_custom_data: no ratio enables after the count */
struct fgd419_table_custom_data {
	int active_table_number;
	struct fgd419_table fg_profile[10];
	int temperature_tb0;
	int fg_profile_temperature_0_size;
	struct fgd419_profile fg_profile_temperature_0[100];
	int temperature_tb1;
	int fg_profile_temperature_1_size;
	struct fgd419_profile fg_profile_temperature_1[100];
};

bool fgd419_rx(struct mtk_battery_manager *bm, const void *data, int len,
	u32 portid, int seq);
bool fgd419_tx(struct mtk_battery_manager *bm, struct afw_header *msg);
int fgd419_charger_exist(void);

#endif /* __MTK_BATTERY_FGD419_H__ */

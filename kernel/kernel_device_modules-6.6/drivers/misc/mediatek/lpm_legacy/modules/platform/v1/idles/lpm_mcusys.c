// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 *
 * op6893 6.6 bring-up: the MCUSYS-off idle model.
 *
 * lpm_legacy kept the generic platform half (lpm_plat_apmcu.c, which tracks
 * which cores have requested what, and lpm_plat.c, which owns the
 * lpm_do_mcusys_prepare_pdn/on pair) and the per-SoC debug half
 * (modules/debug/k6893).  What it does not have is the per-SoC idle model that
 * ties them together -- 4.19 carried it as
 * drivers/misc/mediatek/lpm/modules/platform/mt6885/idles/mt6885_mcusys.c, and
 * the newer lpm/ tree dropped that layer entirely.  The result on device is
 * that nothing in the whole tree calls lpm_plat_set_mcusys_off(),
 * lpm_plat_is_mcusys_off() or lpm_do_mcusys_prepare_pdn() on the idle path,
 * and lpm_model_register() has no callers at all.
 *
 * Measured before this file existed: with mcusysoff enabled on all eight cores
 * and thousands of entries counted in cpuidle sysfs, the SYSRAM counters that
 * ATF itself writes (/proc/mtk_lpm/cpuidle/info) stayed at "cluster: 0
 * mcusys: 0", and the logger reported "mcusysoff didn't enter MCUSYS off,
 * MCUSYS cnt is no update" -- i.e. the cstate enter function returned success
 * and the hardware still did not power MCUSYS down.  ATF serviced the request
 * at a shallower level because the kernel never told it a MCUSYS-off was
 * prepared.
 *
 * This is a port of the 4.19 file to the lpm_legacy API names.  The logic is
 * unchanged: on the last core in, ask ATF what the resource manager will allow
 * and set the matching PLAT_* status, and on the way out clear it.  The
 * issuer->log() in reflect is what produced 4.19's familiar
 * "[SPM] MCUSYSOFF wake up by ..." line.
 */

#include <linux/cpuidle.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/clock.h>

#include <lpm.h>
#include <lpm_module.h>
#include <lpm_plat_apmcu.h>
#include <lpm_resource_constraint_v1.h>

#include "lpm_plat.h"
#include "lpm_plat_comm.h"

/* Rate-limit the issuer log the same way 4.19 did: one line per 5 s. */
#define MCUSYS_DUMP_INFO_INTERVAL_NS	5000000000ULL

static u64 lpm_mcusysoff_last_ns;
static unsigned int lpm_mcusys_status;

static int lpm_mcusys_prompt(int cpu, const struct lpm_issuer *issuer)
{
	unsigned int smc_res;
	unsigned int mcusys_status;

	lpm_plat_set_mcusys_off(cpu);

	/* Only the last core into idle can speak for the whole MCUSYS. */
	if (!lpm_plat_is_mcusys_off())
		return 0;

	smc_res = lpm_smc_cpu_pm(MCUSYS_STATUS, MT_LPM_SMC_ACT_GET,
				 MCUSYS_STATUS_PDN, 0);

	/*
	 * Deepest allowance first -- these are ordered, not a bitmask test in
	 * disguise, and the 4.19 order is kept deliberately.
	 */
	if (MT_RM_STATUS_CHECK(smc_res, VCORE_LP_CLK_26M_OFF))
		mcusys_status = (PLAT_VCORE_LP_MODE
				| PLAT_PMIC_VCORE_SRCLKEN0
				| PLAT_MCUSYS_PROTECTED);
	else if (MT_RM_STATUS_CHECK(smc_res, VCORE_LP_CLK_26M_ON))
		mcusys_status = (PLAT_VCORE_LP_MODE
				| PLAT_MAINPLL_OFF
				| PLAT_PMIC_VCORE_SRCLKEN2
				| PLAT_MCUSYS_PROTECTED);
	else if (MT_RM_STATUS_CHECK(smc_res, MAINPLL_OFF))
		mcusys_status = (PLAT_MAINPLL_OFF
				| PLAT_MCUSYS_PROTECTED);
	else if (MT_RM_STATUS_CHECK(smc_res, DRAM_OFF) |
		 MT_RM_STATUS_CHECK(smc_res, CPU_BUCK_OFF))
		mcusys_status = PLAT_MCUSYS_PROTECTED;
	else
		mcusys_status = 0;

	lpm_mcusys_status = mcusys_status;
	if (lpm_mcusys_status)
		lpm_do_mcusys_prepare_pdn(lpm_mcusys_status, &smc_res);

	return 0;
}

static void lpm_mcusys_reflect(int cpu, const struct lpm_issuer *issuer)
{
	if (lpm_plat_is_mcusys_off()) {
		if (lpm_mcusys_status) {
			lpm_do_mcusys_prepare_on();
			lpm_mcusys_status = 0;
		}
		if (issuer) {
			u64 delta_ns = sched_clock() - lpm_mcusysoff_last_ns;

			if (delta_ns > MCUSYS_DUMP_INFO_INTERVAL_NS) {
				issuer->log(LPM_ISSUER_CPUIDLE,
					    "MCUSYSOFF", NULL);
				lpm_mcusysoff_last_ns = sched_clock();
			}
		}
	}
	lpm_plat_clr_mcusys_off(cpu);
}

static struct lpm_model lpm_model_mcusys = {
	.flag = LPM_REQ_NONE,
	.op = {
		.prompt = lpm_mcusys_prompt,
		.reflect = lpm_mcusys_reflect,
		.prepare_enter = NULL,
		.prepare_resume = NULL,
	}
};

/*
 * The name has to match the cpuidle state name, which on this board comes
 * straight from the DT node name -- init_state_node() strncpy()s
 * state_node->name into drv->states[].name, and lpm_model_percpu_set()
 * strcmp()s against that.  Our idle-states node is called "mcusysoff".
 */
int lpm_model_mcusys_init(void)
{
	return lpm_model_register("mcusysoff", &lpm_model_mcusys);
}

void lpm_model_mcusys_deinit(void)
{
	lpm_model_unregister("mcusysoff");
}

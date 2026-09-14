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
 * and set the matching PLAT_* status, and on the way out clear it.  Reporting
 * is NOT this file's job -- see the comment above lpm_mcusys_reflect().
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

/* Rate-limit for the bring-up diagnostic below: one line per 5 s. */
#define MCUSYS_DUMP_INFO_INTERVAL_NS	5000000000ULL

static unsigned int lpm_mcusys_status;

/*
 * op6893 6.6 bring-up diagnostic.  Registering the model was necessary but is
 * not on its own sufficient -- MCUSYS still does not power down -- and from
 * outside there is no way to tell "we are never the last core in" from "ATF's
 * resource manager allows nothing", because both end with mcusys_status 0 and
 * no prepare.  smc_res is a mask of MT_RM_CONSTRAINT_ALLOW_*; 0 means the
 * resource manager refused everything, which would fit the SPM side reporting
 * spmfw ready: 0.  Rate-limited to one line per 5 s: this is the idle path.
 * Drop this once the answer is in.
 *
 * Measured with the counter alone: min settles at 1 within seconds of enabling
 * the state and never once reaches 0, over hundreds of seconds and thousands
 * of entries, and it stays at 1 even with cores taken offline (which makes
 * cpuhp recompute the count).  That is one slot held permanently rather than a
 * rendezvous the eight cores keep missing by luck -- so the useful question is
 * no longer "how close does it get" but "which core is it".  in_mask is the
 * set of cores currently inside mcusysoff, maintained under the same
 * lpm_mod_locker that serialises prompt and reflect; miss_mask is the set that
 * was still out at the moment the floor was reached.  A miss_mask that names
 * the same core every time is a stuck slot; one that wanders is scheduling.
 */
static u64 lpm_mcusys_dbg_last_ns;
static unsigned int lpm_mcusys_dbg_min = UINT_MAX;
static unsigned long lpm_mcusys_in_mask;
static unsigned long lpm_mcusys_miss_mask;
static unsigned int lpm_mcusys_dbg_lastcore;

static bool lpm_mcusys_oneshot = true;
module_param_named(oneshot, lpm_mcusys_oneshot, bool, 0644);
MODULE_PARM_DESC(oneshot,
	"take exactly one MCUSYS-off then demote to WFI (1: default, survives; 0: free-run, hangs the SoC)");

static void lpm_mcusys_dbg(bool last_core, unsigned int smc_res,
			   unsigned int status)
{
	unsigned int cnt = lpm_plat_mcusys_pwr_cnt();
	unsigned long online = cpumask_bits(cpu_online_mask)[0];
	u64 now;

	/*
	 * Track the floor on every call, not just the ones we print: the
	 * whole question is whether the count ever gets close to 0.  Stuck at
	 * the core count means nothing is decrementing; reaching 1 or 2 means
	 * the eight cores are simply never all in mcusysoff at once, which is
	 * a scheduling problem and not a plumbing one.
	 */
	if (cnt <= lpm_mcusys_dbg_min) {
		lpm_mcusys_dbg_min = cnt;
		lpm_mcusys_miss_mask = online & ~lpm_mcusys_in_mask;
	}

	now = sched_clock();
	if (now - lpm_mcusys_dbg_last_ns <= MCUSYS_DUMP_INFO_INTERVAL_NS)
		return;
	lpm_mcusys_dbg_last_ns = now;
	pr_info("[name:mtk_lpm][P] - mcusys prompt: last_core=%d cnt=%u min=%u in=0x%lx miss=0x%lx online=0x%lx smc_res=0x%x status=0x%x\n",
		last_core, cnt, lpm_mcusys_dbg_min, lpm_mcusys_in_mask,
		lpm_mcusys_miss_mask, online, smc_res, status);
}

static int lpm_mcusys_prompt(int cpu, const struct lpm_issuer *issuer)
{
	unsigned int smc_res;
	unsigned int mcusys_status;

	lpm_plat_set_mcusys_off(cpu);
	lpm_mcusys_in_mask |= BIT(cpu);

	/* Only the last core into idle can speak for the whole MCUSYS. */
	if (!lpm_plat_is_mcusys_off()) {
		lpm_mcusys_dbg(false, 0, 0);
		return 0;
	}

	/*
	 * op6893 bring-up: one-shot mode, on by default.
	 *
	 * Letting MCUSYS-off run free hangs the SoC and the watchdog resets it,
	 * within two or three all-eight events every time -- and always before
	 * anything can be read back.  But the FIRST event is survived, reliably
	 * (measured: died on #2, #2 and #3 across three runs).  So take exactly
	 * one, then veto every later one: lpm_state_enter() demotes a negative
	 * prompt return to WFI, which keeps MCUSYS up and leaves the machine
	 * alive to be interrogated.
	 *
	 * That buys the readings that matter and that no run has survived to
	 * take: /proc/mtk_lpm/lpm/trace/common (ATF's last constraint and its
	 * valid mask -- 4.19 shows `rc_id:3, valid:0x203`),
	 * /proc/mtk_lpm/lpm/rc/<name>/state counts, and the SYSRAM mcusys
	 * counter in /proc/mtk_lpm/cpuidle/info.
	 *
	 * Undo the decrement by hand: after a veto lpm_state_enter() enters
	 * index 0, so lpm_cpuidle_resume() looks up mod[0], finds NULL, and
	 * never calls our reflect -- the count would leak upward forever.
	 */
	if (lpm_mcusys_oneshot && lpm_mcusys_dbg_lastcore >= 1) {
		lpm_mcusys_in_mask &= ~BIT(cpu);
		lpm_plat_clr_mcusys_off(cpu);
		return -EBUSY;
	}

	smc_res = lpm_smc_cpu_pm(MCUSYS_STATUS, MT_LPM_SMC_ACT_GET,
				 MCUSYS_STATUS_PDN, 0);

	/*
	 * op6893 bring-up: the one line we have never managed to read.
	 *
	 * Everything else about this state is now understood -- with the
	 * tick-broadcast device in place the count does reach 0, and cluster
	 * power-down went from never to 649170 -- but the machine hangs on the
	 * first real MCUSYS-off and the watchdog resets it, so the interesting
	 * moment is also the last one.  The 5 s rate limit below has meant that
	 * in every run so far this branch executed and printed nothing: `min`
	 * dropped to 0 unseen and the sampled line still said min=1.
	 *
	 * smc_res is the MT_RM_CONSTRAINT_ALLOW_* mask ATF's resource manager
	 * answers with.  4.19 services every MCUSYS-off under a constraint
	 * (rc/state count:823, all of it cpu-buck-ldo) and wakes via SPM's
	 * R12_SYS_TIMER_EVENT_B; ours reports count:0.  If smc_res is 0 here
	 * then ATF is allowing no constraint at all, which would mean it powers
	 * MCUSYS down without SPM being programmed to wake it -- exactly the
	 * observed hang.  Unconditional for the first few, because console
	 * ramoops survives the hang and a rate-limited line does not.
	 */
	if (lpm_mcusys_dbg_lastcore < 20) {
		lpm_mcusys_dbg_lastcore++;
		pr_info("[name:mtk_lpm][P] - mcusys LAST CORE #%u cpu=%d smc_res=0x%x in=0x%lx online=0x%lx\n",
			lpm_mcusys_dbg_lastcore, cpu, smc_res,
			lpm_mcusys_in_mask, cpumask_bits(cpu_online_mask)[0]);
	}

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

	lpm_mcusys_dbg(true, smc_res, mcusys_status);

	return 0;
}

/*
 * Deliberately does NOT call issuer->log().  An earlier version of this file
 * did, and it crashed the device the first time a MCUSYS-off actually
 * succeeded:
 *
 *   Unable to handle kernel NULL pointer dereference at virtual address 8
 *   pc : lpm_show_message+0xcc [mtk_lpm_dbg_mt6893_legacy]
 *
 * -- because lpm_show_message() starts with
 * `((struct lpm_issuer *)data)->log_type`, so the issuer has to be passed as
 * `data`, and we passed NULL.  But the argument was not the real mistake: the
 * whole call was.  lpm_dbg_logger.c already owns this, from its own 5 s timer
 * in lpm_log_timer_func(): it compares the SYSRAM MCUSYS counter against the
 * previous sample, sets issuer.log_type (LOG_MCUSYS_NOT_OFF when it did not
 * move), and calls issuer.log(LPM_ISSUER_CPUIDLE, state_name, &issuer).  That
 * is what emits 4.19's "[SPM] MCUSYSOFF wake up by ..." line -- not this
 * function.  And reflect is the wrong place for it regardless: it runs under
 * lpm_mod_locker with interrupts off, while lpm_show_message() formats the
 * best part of a kilobyte.
 */
static void lpm_mcusys_reflect(int cpu, const struct lpm_issuer *issuer)
{
	if (lpm_plat_is_mcusys_off() && lpm_mcusys_status) {
		lpm_do_mcusys_prepare_on();
		lpm_mcusys_status = 0;
	}
	lpm_mcusys_in_mask &= ~BIT(cpu);
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

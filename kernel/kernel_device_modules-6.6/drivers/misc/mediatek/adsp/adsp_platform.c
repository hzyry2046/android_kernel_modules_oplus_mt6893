// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include "adsp_reg.h"
#include "adsp_core.h"
#include "adsp_platform_driver.h"
#include "adsp_platform.h"

#ifdef ADSP_BASE
#undef ADSP_BASE
#endif

#define ADSP_BASE                  mt_base
#define INFRA_RSV_BASE             mt_infra_rsv

#define SET_BITS(addr, mask) writel(readl(addr) | (mask), addr)
#define CLR_BITS(addr, mask) writel(readl(addr) & ~(mask), addr)
#define READ_BITS(addr, mask) (readl(addr) & (mask))

static void __iomem *mt_base;
static void __iomem *mt_infra_rsv;
static u32 axibus_idle_val;

/* MT6893's original BL31 leaves ADSP reset/run/remap control to the AP.
 * Only the old DT layout selects this backend; newer platforms retain SMC.
 */
void adsp_legacy_sw_reset(u32 cid)
{
	u32 mask = cid ? 0x22 : 0x11;

	SET_BITS(mt_base, mask);
	udelay(1);
	CLR_BITS(mt_base, mask);
}

int adsp_legacy_remap(void)
{
	struct adsp_priv *a = adsp_cores[0], *b = adsp_cores[1];
	size_t size;
	void __iomem *secure = adspsys->cfg_secure;

	if (!a || !b || a->sysram_phys + a->sysram_size != b->sysram_phys)
		return -EINVAL;
	size = a->sysram_size + b->sysram_size;
	if ((a->sysram_phys | size) & 0xffff || a->sysram_phys > U32_MAX)
		return -EINVAL;
	/* DSP view is fixed by the firmware; physical RAM comes from LK's DT. */
	writel(0xf, secure + 0x20);
	writel(((0x56000000 + size) & 0xffff0000) | 0x5600, secure + 0x24);
	writel(a->sysram_phys >> 16, secure + 0x28);
	return 0;
}

long adsp_legacy_control(u32 op, u32 val)
{
	u32 status, cid, ch;
	struct adsp_priv *pdata;
	int ret;

	switch (op) {
	case MTK_ADSP_KERNEL_OP_SYS_CLEAR:
		writel(0, mt_base);
		writel(0xc0001002, mt_base + 0xc);
		writel(0xdf, ADSP_CLK_CTRL_BASE);
		writel(0, ADSP_A_IRQ_EN);
		writel(0, ADSP_B_IRQ_EN);
		writel(0, ADSP_A_WDT_REG);
		writel(0, ADSP_B_WDT_REG);
		for (ch = 0; ch < 4; ch++) {
			CLR_BITS(mt_base + 0x2000 + (ch + 1) * 0x100 + 0x18, 0x8000);
			SET_BITS(mt_base + 0x2000 + (ch + 1) * 0x100 + 0x20, 0x8000);
		}
		return 0;
	case MTK_ADSP_KERNEL_OP_CORE_START:
		if (val >= adspsys->num_cores)
			return -EINVAL;
		pdata = adsp_cores[val];
		if (!pdata)
			return -ENODEV;
		/* The old firmware resumes through its reset vector. RUNSTALL
		 * alone does not restart a suspended core. Match 4.19's order:
		 * core1 clock on, SW reset, core0 remap, then resource/run.
		 */
		if (val == ADSP_B_ID)
			SET_BITS(ADSP_CLK_CTRL_BASE, ADSP_CLK_CORE_1_EN);
		if (get_adsp_state(pdata) == ADSP_SUSPEND)
			adsp_legacy_sw_reset(val);
		/* MTCMOS-off loses secure remap registers. 4.19 restores them
		 * on every core0 resume; modern ATF does this inside CORE_START.
		 */
		if (!val) {
			ret = adsp_legacy_remap();
			if (ret)
				return ret;
		}
		SET_BITS(val ? ADSP_B_DDREN_REQ : ADSP_A_DDREN_REQ, ADSP_SPM_SRC_BITS);
		ret = readl_poll_timeout(ADSP_SPM_ACK, status,
					status == ADSP_SPM_SRC_BITS, 10, 10000);
		if (ret)
			return ret;
		CLR_BITS(mt_base + 0xc, val ? BIT(30) : BIT(31));
		return 0;
	case MTK_ADSP_KERNEL_OP_CORE_STOP:
		if (val >= adspsys->num_cores)
			return -EINVAL;
		SET_BITS(mt_base + 0xc, val ? BIT(30) : BIT(31));
		if (val == ADSP_B_ID)
			CLR_BITS(ADSP_CLK_CTRL_BASE, ADSP_CLK_CORE_1_EN);
		return 0;
	case MTK_ADSP_KERNEL_OP_CFG_LATCH:
		/* The caller's access_lock serializes old AP-side register dumps. */
		return 0;
	case MTK_ADSP_KERNEL_OP_RELOAD:
		for (cid = 0; cid < adspsys->num_cores; cid++) {
			pdata = adsp_cores[cid];
			if (!pdata || !pdata->itcm_snapshot || !pdata->dtcm_snapshot)
				return -EINVAL;
		}
		for (cid = 0; cid < adspsys->num_cores; cid++) {
			pdata = adsp_cores[cid];
			memcpy_toio(pdata->itcm, pdata->itcm_snapshot, pdata->itcm_size);
			memcpy_toio(pdata->dtcm, pdata->dtcm_snapshot, pdata->dtcm_size);
			adsp_legacy_sw_reset(cid);
		}
		return 0;
	case MTK_ADSP_KERNEL_OP_QUERY_STATE:
		return is_adsp_system_running();
	case MTK_ADSP_KERNEL_OP_DUMP_PWR_CLK:
		pr_info("ADSP legacy runstall=%08x spm_ack=%08x clock=%08x\n",
			readl(mt_base + 0xc), readl(ADSP_SPM_ACK), readl(ADSP_CLK_CTRL_BASE));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

/* below access adsp register necessary */
void adsp_mt_set_swirq(u32 cid)
{
	if (unlikely(cid >= get_adsp_core_total()))
		return;

	if (cid == ADSP_A_ID)
		writel(ADSP_A_SW_INT, ADSP_SW_INT_SET);
	else
		writel(ADSP_B_SW_INT, ADSP_SW_INT_SET);
}

u32 adsp_mt_check_swirq(u32 cid)
{
	if (unlikely(cid >= get_adsp_core_total()))
		return 0;

	if (cid == ADSP_A_ID)
		return readl(ADSP_SW_INT_SET) & ADSP_A_SW_INT;
	else
		return readl(ADSP_SW_INT_SET) & ADSP_B_SW_INT;
}

void adsp_mt_clr_sysirq(u32 cid)
{
	if (unlikely(cid >= get_adsp_core_total()))
		return;

	if (cid == ADSP_A_ID)
		writel(ADSP_A_2HOST_IRQ_BIT, ADSP_GENERAL_IRQ_CLR);
	else
		writel(ADSP_B_2HOST_IRQ_BIT, ADSP_GENERAL_IRQ_CLR);
}
EXPORT_SYMBOL(adsp_mt_clr_sysirq);

void adsp_mt_clr_auidoirq(u32 cid)
{
	if (unlikely(cid >= get_adsp_core_total()))
		return;
	/* just clear correct bits*/
	if (cid == ADSP_A_ID)
		writel(ADSP_A_AFE2HOST_IRQ_BIT, ADSP_GENERAL_IRQ_CLR);
	else
		writel(ADSP_B_AFE2HOST_IRQ_BIT, ADSP_GENERAL_IRQ_CLR);
}
EXPORT_SYMBOL(adsp_mt_clr_auidoirq);

void adsp_mt_disable_wdt(u32 cid)
{
	if (unlikely(cid >= get_adsp_core_total()))
		return;

	if (cid == ADSP_A_ID)
		CLR_BITS(ADSP_A_WDT_REG, WDT_EN_BIT);
	else
		CLR_BITS(ADSP_B_WDT_REG, WDT_EN_BIT);
}
EXPORT_SYMBOL(adsp_mt_disable_wdt);

void adsp_mt_clr_spm(u32 cid)
{
	if (unlikely(cid >= get_adsp_core_total()))
		return;

	if (cid == ADSP_A_ID)
		CLR_BITS(ADSP_A_SPM_WAKEUPSRC, ADSP_WAKEUP_SPM);
	else
		CLR_BITS(ADSP_B_SPM_WAKEUPSRC, ADSP_WAKEUP_SPM);
}

bool check_hifi_status(u32 mask)
{
	return !!(readl(ADSP_SLEEP_STATUS_REG) & mask);
}

u32 read_adsp_sys_status(u32 cid)
{
	if (cid == ADSP_A_ID)
		return readl(ADSP_CFGREG_RSV_RW_REG0);
	else
		return readl(ADSP_CFGREG_RSV_RW_REG1);
}

u32 get_adsp_sys_status(struct adsp_priv *pdata)
{
	u32 status;

	if (has_system_l2sram())
		status = read_adsp_sys_status(pdata->id);
	else
		adsp_copy_from_sharedmem(pdata,
				 ADSP_SHAREDMEM_SYS_STATUS,
				 &status, sizeof(status));
	return status;
}

bool is_adsp_axibus_idle(u32 *backup)
{
	u32 value = readl(ADSP_DBG_PEND_CNT);

	if (backup)
		*backup = value;

	return (value == axibus_idle_val);
}

bool is_infrabus_timeout(void)
{
	return 0;
}

void adsp_mt_toggle_semaphore(u32 bit)
{
	writel((1 << bit), ADSP_SEMAPHORE);
}

u32 adsp_mt_get_semaphore(u32 bit)
{
	return (readl(ADSP_SEMAPHORE) >> bit) & 0x1;
}

bool check_core_active(u32 cid)
{
	if (cid == ADSP_A_ID)
		return READ_BITS(ADSP_A_CORE_AWAKE_REG, ADSP_A_PRELOCK_MASK) != 0;
	else
		return READ_BITS(ADSP_B_CORE_AWAKE_REG, ADSP_B_PRELOCK_MASK) != 0;
}

bool adsp_is_pre_lock_support(void)
{
	return (mt_infra_rsv != NULL);
}

int _adsp_mt_pre_lock(u32 cid, bool is_lock)
{
	void __iomem *reg;
	u32 mask;

	if (unlikely(cid >= get_adsp_core_total()))
		return -1;

	if (cid == ADSP_A_ID) {
		reg = ADSP_A_PRELOCK_REG;
		mask = ADSP_A_PRELOCK_MASK;
	} else {
		reg = ADSP_B_PRELOCK_REG;
		mask = ADSP_B_PRELOCK_MASK;
	}

	if (is_lock)
		SET_BITS(reg, mask);
	else if (READ_BITS(reg, mask)) /* unlock */
		CLR_BITS(reg, mask);

	return (int)!!READ_BITS(reg, mask);
}

void adsp_hardware_init(struct adspsys_priv *adspsys)
{
	if (unlikely(!adspsys))
		return;

	mt_base = adspsys->cfg;
	mt_infra_rsv = adspsys->infracfg_rsv;
	axibus_idle_val = adspsys->desc->axibus_idle_val;

	if (mt_infra_rsv != NULL) {
		writel(0x0, ADSP_A_PRELOCK_REG);
		writel(0x0, ADSP_B_PRELOCK_REG);
	}
}

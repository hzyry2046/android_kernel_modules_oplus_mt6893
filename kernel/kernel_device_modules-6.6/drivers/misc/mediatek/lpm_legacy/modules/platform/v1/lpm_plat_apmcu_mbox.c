// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 *
 * op6893 6.6 bring-up: the APMCU->MCUPM mailbox.
 *
 * On mt6893 the thing that actually powers MCUSYS down is MCUPM, and it only
 * does so once the AP has written MCUPM_MCUSYS_CTRL into mailbox slot
 * APMCU_MCUPM_MBOX_PWR_CTRL_EN.  4.19 does exactly that, unconditionally, from
 * mtk_lp_plat_wait_depd_condition() (mt6885/mtk_lp_plat_apmcu_mbox.c), over the
 * MCUPM hardware mailbox -- its SSPM path is commented out in the vendor source.
 *
 * lpm_legacy shipped only the SSPM half of this file, and built the file at all
 * only under CONFIG_MTK_LPM_MT6781, which is off for us.  So on 6.6 nothing ever
 * wrote that slot: MCUPM was never told it may power MCUSYS off.  Measured
 * before this change, with all eight cores demonstrably inside mcusysoff
 * (in=0xff) -- ATF's own counters stayed at zero across the board:
 *
 *   /proc/mtk_lpm/cpuidle/info      cluster: 126936   mcusys: 0
 *   /proc/mtk_lpm/lpm/rc/state      count:0     (4.19: 461106)
 *   /proc/mtk_lpm/lpm/trace/common  valid:0x0   (4.19: rc_id:3, valid:0x203)
 *   MCUSYS_STATUS GET PDN           0x0         -- RM allows no constraint
 *
 * -- cluster-off works because CPC does that in hardware without MCUPM policy.
 *
 * So both mailbox back-ends now exist: MBOX_SSPM (the lpm_legacy original, an
 * SSPM shared-SRAM window handed over by IPI, mt6781) and MBOX_MCUPM (the
 * mt6893/4.19 one, mcupm_mbox_write/read on hardware mailbox 3).  APMCU_MBOX
 * picks per SoC.
 */

#include <linux/workqueue.h>
#include <linux/delay.h>

#include <lpm_plat_apmcu_mbox.h>
#include <lpm_module.h>

/*
 * The SSPM back-end needs both the SoC that uses it and the SSPM driver; its
 * headers only reach the include path under the same condition (see the
 * Makefile).  MBOX_SSPM does not even exist in the enum otherwise.
 */
#if IS_ENABLED(CONFIG_MTK_LPM_MT6781) && IS_ENABLED(CONFIG_MTK_TINYSYS_SSPM_SUPPORT)
#define LPM_APMCU_MBOX_SSPM	1
#else
#define LPM_APMCU_MBOX_SSPM	0
#endif

#if LPM_APMCU_MBOX_SSPM
#include <sspm_helper.h>
#include <sspm_ipi_id.h>
#include <sspm_define.h>
#include <sspm_reservedmem.h>
#endif

#if IS_ENABLED(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT)
#include <mcupm_driver.h>
#endif

/* Which back-end the APMCU_MCUPM_MBOX_* slots actually live behind. */
#if IS_ENABLED(CONFIG_MTK_LPM_MT6781)
#define APMCU_MBOX	MBOX_SSPM
#else
#define APMCU_MBOX	MBOX_MCUPM
#endif

struct lpm_apmcu_mbox {
	unsigned int ap_ready;
	unsigned int reserved1;
	unsigned int reserved2;
	unsigned int reserved3;
	unsigned int pwr_ctrl_en;
	unsigned int l3_cache_mode;
	unsigned int buck_mode;
	unsigned int armpll_mode;
	unsigned int task_sta;
	unsigned int reserved9;
	unsigned int reserved10;
	unsigned int reserved11;
	unsigned int wakeup_cpu;
};

struct lpm_apmcu_ipi_data {
	unsigned int ipi_id	: 24;
	unsigned int magic	: 8;
	unsigned int type;
	unsigned int reserved[6];
};

struct lpm_apmcu_ipi_reply {
	unsigned int value;
};

struct mbox_ops {
	void (*write)(int id, int *buf, unsigned int len);
	void (*read)(int id, int *buf, unsigned int len);
};

#if LPM_APMCU_MBOX_SSPM
static struct lpm_apmcu_ipi_reply lpm_apmcu_mbox_ipi_reply;
static struct lpm_apmcu_mbox *lpm_apmcu_mbox_data;
#endif


#if LPM_APMCU_MBOX_SSPM
#define APMCU_SSPM_MBOX_SHARE_SRAM(_wr, _target, _buf) ({\
	if (_wr)\
		lpm_apmcu_mbox_data->_target = _buf;\
	else\
		_buf = lpm_apmcu_mbox_data->_target; })

static void __apmcu_sspm_mailbox(int IsWrite, int id,
					int *buf, unsigned int len)
{
	if (!lpm_apmcu_mbox_data || !buf || (len == 0))
		return;

	switch (id) {
	case APMCU_MCUPM_MBOX_AP_READY:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, ap_ready, *buf);
		break;
	case APMCU_MCUPM_MBOX_RESERVED_1:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, reserved1, *buf);
		break;
	case APMCU_MCUPM_MBOX_RESERVED_2:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, reserved2, *buf);
		break;
	case APMCU_MCUPM_MBOX_RESERVED_3:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, reserved3, *buf);
		break;
	case APMCU_MCUPM_MBOX_PWR_CTRL_EN:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, pwr_ctrl_en, *buf);
		break;
	case APMCU_MCUPM_MBOX_L3_CACHE_MODE:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, l3_cache_mode, *buf);
		break;
	case APMCU_MCUPM_MBOX_BUCK_MODE:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, buck_mode, *buf);
		break;
	case APMCU_MCUPM_MBOX_ARMPLL_MODE:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, armpll_mode, *buf);
		break;
	case APMCU_MCUPM_MBOX_TASK_STA:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, task_sta, *buf);
		break;
	case APMCU_MCUPM_MBOX_RESERVED_9:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, reserved9, *buf);
		break;
	case APMCU_MCUPM_MBOX_RESERVED_10:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, reserved10, *buf);
		break;
	case APMCU_MCUPM_MBOX_RESERVED_11:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, reserved11, *buf);
		break;
	case APMCU_MCUPM_MBOX_WAKEUP_CPU:
		APMCU_SSPM_MBOX_SHARE_SRAM(IsWrite, wakeup_cpu, *buf);
		break;
	}
}
#endif

#if LPM_APMCU_MBOX_SSPM
static void apmcu_sspm_mailbox_write(int id, int *buf, unsigned int len)
{
	__apmcu_sspm_mailbox(1, id, buf, len);
}

static void apmcu_sspm_mailbox_read(int id, int *buf, unsigned int len)
{
	__apmcu_sspm_mailbox(0, id, buf, len);
}
#endif

/*
 * mcupm_mbox_{write,read}() take a slot index and a length in mailbox slots,
 * exactly like the 4.19 mt6885 wrappers this is copied from.  Without the
 * MCUPM driver they are no-ops rather than a link error, so a kernel built
 * without it still boots -- it just never gets MCUSYS-off, which is the
 * behaviour we already had.
 */
static void apmcu_mcupm_mailbox_write(int id, int *buf, unsigned int len)
{
#if IS_ENABLED(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT)
	mcupm_mbox_write(APMCU_MCUPM_MBOX_ID, id, (void *)buf, len);
#endif
}

static void apmcu_mcupm_mailbox_read(int id, int *buf, unsigned int len)
{
#if IS_ENABLED(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT)
	mcupm_mbox_read(APMCU_MCUPM_MBOX_ID, id, (void *)buf, len);
#endif
}

static struct mbox_ops mbox[NF_MBOX] = {
#if LPM_APMCU_MBOX_SSPM
	[MBOX_SSPM] = {
		.write = apmcu_sspm_mailbox_write,
		.read = apmcu_sspm_mailbox_read
	},
#endif
	[MBOX_MCUPM] = {
		.write = apmcu_mcupm_mailbox_write,
		.read = apmcu_mcupm_mailbox_read
	},
};

static void mtk_lp_apmcu_pwr_ctrl_setting(int dev)
{
	mbox[APMCU_MBOX].write(APMCU_MCUPM_MBOX_PWR_CTRL_EN, &dev, 1);
}

void mtk_set_lp_apmcu_pll_mode(unsigned int mode)
{
	if (mode < NF_MCUPM_ARMPLL_MODE)
		mbox[APMCU_MBOX].write(APMCU_MCUPM_MBOX_ARMPLL_MODE,
				       (int *)&mode, 1);
}
EXPORT_SYMBOL(mtk_set_lp_apmcu_pll_mode);

void mtk_set_lp_apmcu_buck_mode(unsigned int mode)
{
	if (mode < NF_MCUPM_BUCK_MODE)
		mbox[APMCU_MBOX].write(APMCU_MCUPM_MBOX_BUCK_MODE,
				       (int *)&mode, 1);
}
EXPORT_SYMBOL(mtk_set_lp_apmcu_buck_mode);

bool mtk_lp_apmcu_is_ready(void)
{
	int sta = MCUPM_TASK_INIT_FINISH;

	mbox[APMCU_MBOX].read(APMCU_MCUPM_MBOX_TASK_STA, &sta, 1);

	return sta == MCUPM_TASK_WAIT || sta == MCUPM_TASK_INIT_FINISH;
}

/*
 * op6893 6.6 bring-up: 4.19 waits here forever.  On a bring-up tree that is a
 * bad trade -- cpu-off is held blocked by the pm_qos request until this
 * returns, so an MCUPM that never reports in would cost us cpu-off and
 * cluster-off too, which do work.  Bound it, say so loudly, and leave
 * pwr_ctrl_en unwritten in that case: exactly the behaviour we had before this
 * file existed.
 */
#define MBOX_INIT_WAIT_LIMIT	120

void mtk_wait_mbox_init_done(void)
{
	int sta = MCUPM_TASK_UNINIT;
	unsigned int waited = 0;
#if LPM_APMCU_MBOX_SSPM
	int ret = 0;
	struct lpm_apmcu_ipi_data d_lpm_apmcu_ipi = {
					.ipi_id = APMCU_PM_IPI_UID_MCDI,
					.magic = MCDI_IPI_MAGIC_NUM,
					.type = MCDI_IPI_SHARE_SRAM_INFO_GET};
#endif
	while (1) {

#if LPM_APMCU_MBOX_SSPM
		if (!lpm_apmcu_mbox_data) {
			if (!is_sspm_ready()) {
				pr_info("[name:mtk_lpm] - sspm mbox not ready !\n");
				msleep(1000);
				continue;
			}

			ret = mtk_ipi_send_compl(&sspm_ipidev, IPIS_C_SPM_SUSPEND,
					IPI_SEND_POLLING, &d_lpm_apmcu_ipi,
					sizeof(d_lpm_apmcu_ipi) / SSPM_MBOX_SLOT_SIZE, 2000);
			if (ret) {
				msleep(1000);
				continue;
			}

			lpm_apmcu_mbox_data = (struct lpm_apmcu_mbox *)
					sspm_sbuf_get(lpm_apmcu_mbox_ipi_reply.value);

			if (lpm_apmcu_mbox_data) {
				lpm_smc_cpu_pm(MBOX_INFO, MT_LPM_SMC_ACT_SET,
						   lpm_apmcu_mbox_ipi_reply.value, 0);
			}
		} else
#endif
			mbox[APMCU_MBOX].read(APMCU_MCUPM_MBOX_TASK_STA, &sta, 1);

		if (sta == MCUPM_TASK_INIT)
			break;

		msleep(1000);

		if (++waited >= MBOX_INIT_WAIT_LIMIT) {
			pr_notice("[name:mtk_lpm][P] - mcupm mbox not ready after %us (task_sta=%d); MCUSYS-off stays off\n",
				  waited, sta);
			return;
		}
	}

	mtk_set_lp_apmcu_pll_mode(MCUPM_ARMPLL_OFF);
	mtk_set_lp_apmcu_buck_mode(MCUPM_BUCK_OFF_MODE);

	/*
	 * The write that matters: MCUPM_MCUSYS_CTRL is MCUPM's permission to
	 * power MCUSYS down.  4.19 gates MCUPM_CM_CTRL on CONFIG_MTK_CM_MGR,
	 * which we do not build yet, so it is left out -- asking MCUPM to run
	 * cache-mode management for a manager that is not there is not a
	 * favour.
	 */
	mtk_lp_apmcu_pwr_ctrl_setting(
			 MCUPM_MCUSYS_CTRL |
#if IS_ENABLED(CONFIG_MTK_CM_MGR)
			 MCUPM_CM_CTRL |
#endif
			 MCUPM_BUCK_CTRL |
			 MCUPM_ARMPLL_CTRL);

	pr_info("[name:mtk_lpm][P] - mcupm mbox ready, pwr_ctrl_en set\n");
}

void mtk_notify_subsys_ap_ready(void)
{
	int ready = 1;

	mbox[APMCU_MBOX].write(APMCU_MCUPM_MBOX_AP_READY, &ready, 1);
}

int mtk_apmcu_mbox_init(void)
{
	unsigned int ret = 0;

#if LPM_APMCU_MBOX_SSPM
	/* for AP to SSPM */
	if (is_sspm_ready())
		ret = mtk_ipi_register(&sspm_ipidev, IPIS_C_SPM_SUSPEND, NULL, NULL,
			      (void *) &lpm_apmcu_mbox_ipi_reply);
	if (ret)
		pr_info("IPIS_C_SPM_SUSPEND ipi_register fail, ret %d\n", ret);
#endif
	return ret;
}

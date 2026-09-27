/* SPDX-License-Identifier: GPL-2.0 */
/* 4.19 MTK pm_qos classes (PM_QOS_VDEC_FREQ, ...) -- gone in 6.6. Stubbed. */
#ifndef _C419_MTK_PM_QOS_H
#define _C419_MTK_PM_QOS_H

#include <linux/pm_qos.h>

enum {
	PM_QOS_VDEC_FREQ = 100,
	PM_QOS_VENC_FREQ,
	PM_QOS_MM_MEMORY_BANDWIDTH,
};

struct mtk_pm_qos_request {
	int pm_qos_class;
	s32 value;
};

static inline void mtk_pm_qos_add_request(struct mtk_pm_qos_request *req,
					  int pm_qos_class, s32 value)
{
	req->pm_qos_class = pm_qos_class;
	req->value = value;
}

static inline void mtk_pm_qos_update_request(struct mtk_pm_qos_request *req,
					     s32 new_value)
{
	req->value = new_value;
}

static inline void mtk_pm_qos_remove_request(struct mtk_pm_qos_request *req) {}

#endif

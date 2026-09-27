/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 4.19 mmdvfs/mm_qos surface for the forward-ported vcodec.
 *
 * Phase 1 stubs: no EMI bandwidth votes, no MMDVFS steps.  The codec still
 * works; it just runs at whatever vcore/mm clock the rest of the system holds.
 * Replace with mtk_icc_set_bw() / OPP once the interconnect DT exists.
 */
#ifndef _C419_MMDVFS_PMQOS_H
#define _C419_MMDVFS_PMQOS_H

#include <linux/plist.h>
#include <linux/soc/mediatek/mtk-pm-qos.h>

#define MAX_FREQ_STEP 6

enum {
	BW_COMP_NONE = 0,
	BW_COMP_DEFAULT,
	BW_COMP_VENC,
	BW_COMP_END
};

struct mm_qos_request {
	u32 master_id;
	u32 bw_value;
	u32 hrt_value;
	u32 comp_type;
};

static inline s32 mm_qos_add_request(struct plist_head *owner_list,
				     struct mm_qos_request *req, u32 master_id)
{
	req->master_id = master_id;
	return 0;
}

static inline s32 mm_qos_set_request(struct mm_qos_request *req,
				     u32 bw_value, u32 hrt_value, u32 comp_type)
{
	req->bw_value = bw_value;
	req->hrt_value = hrt_value;
	req->comp_type = comp_type;
	return 0;
}

static inline void mm_qos_update_all_request(struct plist_head *owner_list) {}
static inline void mm_qos_remove_all_request(struct plist_head *owner_list) {}

/* One step at 0 Hz: callers index freq_steps[step_size - 1] and push it into
 * mtk_pm_qos_update_request(), which is itself a no-op here. */
static inline int mmdvfs_qos_get_freq_steps(u32 pm_qos_class, u64 *freq_steps,
					    u32 *step_size)
{
	freq_steps[0] = 0;
	*step_size = 1;
	return 0;
}

#endif

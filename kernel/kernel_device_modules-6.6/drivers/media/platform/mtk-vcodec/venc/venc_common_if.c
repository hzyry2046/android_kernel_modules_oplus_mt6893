// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Longfei Wang <longfei.wang@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "../mtk_vcodec_drv.h"
#include "../mtk_vcodec_util.h"
#include "../mtk_vcodec_enc.h"
#include "../venc_drv_base.h"
#include "../venc_ipi_msg.h"
#include "../venc_vcu_if.h"
#include "mtk_vcodec_enc_pm.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcu.h"
#include "mtk_heap.h"
#include "iommu_pseudo.h"

static unsigned int venc_h265_get_profile(struct venc_inst *inst,
	unsigned int profile)
{
	switch (profile) {
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN:
		return 1;
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10:
		return 2;
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE:
		return 4;
	default:
		mtk_vcodec_debug(inst, "unsupported profile %d", profile);
		return 1;
	}
}

static unsigned int venc_h265_get_level(struct venc_inst *inst,
	unsigned int level, unsigned int tier)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_1:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 2 : 3;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 8 : 9;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 10 : 11;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 13 : 14;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 15 : 16;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 18 : 19;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 20 : 21;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 23 : 24;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 25 : 26;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 27 : 28;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_6:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 29 : 30;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 31 : 32;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2:
		return (tier == V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) ? 33 : 34;
	default:
		mtk_vcodec_debug(inst, "unsupported level %d", level);
		return 25;
	}
}

static unsigned int venc_mpeg4_get_profile(struct venc_inst *inst,
	unsigned int profile)
{
	switch (profile) {
	case V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE:
		return 0;
	case V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE:
		return 1;
	case V4L2_MPEG_VIDEO_MPEG4_PROFILE_CORE:
		return 2;
	case V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE_SCALABLE:
		return 3;
	case V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_CODING_EFFICIENCY:
		return 4;
	default:
		mtk_vcodec_debug(inst, "unsupported mpeg4 profile %d", profile);
		return 100;
	}
}

static unsigned int venc_mpeg4_get_level(struct venc_inst *inst,
	unsigned int level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_0:
		return 0;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_0B:
		return 1;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_1:
		return 2;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_2:
		return 3;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_3:
		return 4;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_3B:
		return 5;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_4:
		return 6;
	case V4L2_MPEG_VIDEO_MPEG4_LEVEL_5:
		return 7;
	default:
		mtk_vcodec_debug(inst, "unsupported mpeg4 level %d", level);
		return 4;
	}
}

static int venc_encode_header(struct venc_inst *inst,
	struct mtk_vcodec_mem *bs_buf,
	unsigned int *bs_size)
{
	int ret = 0;

	mtk_vcodec_debug_enter(inst);
	if (bs_buf == NULL)
		inst->vsi->venc.venc_bs_va = 0;
	else
		inst->vsi->venc.venc_bs_va = (u64)(uintptr_t)bs_buf;

	inst->vsi->venc.venc_fb_va = 0;

	mtk_vcodec_debug(inst, "vsi venc_bs_va %llx",
			 inst->vsi->venc.venc_bs_va);

	ret = vcu_enc_encode(&inst->vcu_inst, VENC_BS_MODE_SEQ_HDR, NULL,
						 bs_buf, bs_size);

	return ret;
}

static int venc_encode_frame(struct venc_inst *inst,
	struct venc_frm_buf *frm_buf,
	struct mtk_vcodec_mem *bs_buf,
	unsigned int *bs_size)
{
	int ret = 0;
	unsigned int fm_fourcc = inst->ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
	unsigned int bs_fourcc = inst->ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;

	mtk_vcodec_debug_enter(inst);

	if (bs_buf == NULL)
		inst->vsi->venc.venc_bs_va = 0;
	else
		inst->vsi->venc.venc_bs_va = (u64)(uintptr_t)bs_buf;

	if (frm_buf == NULL)
		inst->vsi->venc.venc_fb_va = 0;
	else {
		inst->vsi->venc.venc_fb_va = (u64)(uintptr_t)frm_buf;
		inst->vsi->venc.timestamp = frm_buf->timestamp;
	}
	ret = vcu_enc_encode(&inst->vcu_inst, VENC_BS_MODE_FRAME, frm_buf,
						 bs_buf, bs_size);
	if (ret)
		return ret;

	++inst->frm_cnt;
	mtk_vcodec_debug(inst,
		 "Format: frame_va %llx (%c%c%c%c) bs_va:%llx (%c%c%c%c)",
		  inst->vsi->venc.venc_fb_va,
		  fm_fourcc & 0xFF, (fm_fourcc >> 8) & 0xFF,
		  (fm_fourcc >> 16) & 0xFF, (fm_fourcc >> 24) & 0xFF,
		  inst->vsi->venc.venc_bs_va,
		  bs_fourcc & 0xFF, (bs_fourcc >> 8) & 0xFF,
		  (bs_fourcc >> 16) & 0xFF, (bs_fourcc >> 24) & 0xFF);

	return ret;
}

static int venc_encode_frame_final(struct venc_inst *inst,
	struct venc_frm_buf *frm_buf,
	struct mtk_vcodec_mem *bs_buf,
	unsigned int *bs_size)
{
	int ret = 0;

	mtk_v4l2_debug(0, "check inst->vsi %p +", inst->vsi);
	if (inst == NULL || inst->vsi == NULL)
		return -EINVAL;

	if (bs_buf == NULL)
		inst->vsi->venc.venc_bs_va = 0;
	else
		inst->vsi->venc.venc_bs_va = (u64)(uintptr_t)bs_buf;
	if (frm_buf == NULL)
		inst->vsi->venc.venc_fb_va = 0;
	else
		inst->vsi->venc.venc_fb_va = (u64)(uintptr_t)frm_buf;

	ret = vcu_enc_encode(&inst->vcu_inst, VENC_BS_MODE_FRAME_FINAL, frm_buf,
						 bs_buf, bs_size);
	if (ret)
		return ret;

	*bs_size = inst->vcu_inst.bs_size;
	mtk_vcodec_debug(inst, "bs size %d <-", *bs_size);

	return ret;
}


/*
 * op6893: the 4.19 vpud registers one IPI receiver per encoder codec and derives
 * the codec from the channel a message arrives on, so an H264 session's INIT has
 * to travel on IPI_VENC_H264, not on IPI_VENC_COMMON -- vpud answers the latter
 * with "unknown codec id 14" and never initialises the instance.
 *
 * The dev_ctx instance is the exception: probe creates it before any codec is
 * known and mtk_vcodec_enc_set_default_params() runs its capability queries
 * through it, so it stays on COMMON.
 */
static enum ipi_id venc_ipi_id_for_ctx(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	if (!ctx || ctx->dev_ctx == ctx)
		return IPI_VENC_COMMON;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	if (q_data->fmt == NULL)
		return IPI_VENC_COMMON;

	switch (q_data->fmt->fourcc) {
	case V4L2_PIX_FMT_H264:
		return IPI_VENC_H264;
	case V4L2_PIX_FMT_HEVC:
		return IPI_VENC_H265;
	case V4L2_PIX_FMT_HEIF:
		return IPI_VENC_HEIF;
	case V4L2_PIX_FMT_VP8:
		return IPI_VENC_VP8;
	case V4L2_PIX_FMT_MPEG4:
		return IPI_VENC_MPEG4;
	case V4L2_PIX_FMT_H263:
		return IPI_VENC_H263;
	default:
		return IPI_VENC_COMMON;
	}
}

static int venc_init(struct mtk_vcodec_ctx *ctx, unsigned long *handle)
{
	int ret = 0;
	struct venc_inst *inst;
	struct vcu_v4l2_callback_func cb;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst) {
		*handle = (unsigned long)NULL;
		return -ENOMEM;
	}

	inst->ctx = ctx;
	inst->vcu_inst.ctx = ctx;
	inst->vcu_inst.dev = VCU_FPTR(vcu_get_plat_device)(ctx->dev->plat_dev);
	inst->vcu_inst.id = venc_ipi_id_for_ctx(ctx);
	inst->hw_base = mtk_vcodec_get_enc_reg_addr(inst->ctx, VENC_SYS);
	inst->vcu_inst.handler = vcu_enc_ipi_handler;
	(*handle) = (unsigned long)inst;

	mtk_vcodec_debug_enter(inst);

	mtk_vcodec_add_ctx_list(ctx);

	ret = vcu_enc_init(&inst->vcu_inst);

	inst->vsi = (struct venc_vsi *)inst->vcu_inst.vsi;

	memset(&cb, 0, sizeof(struct vcu_v4l2_callback_func));
	cb.enc_prepare = venc_encode_prepare;
	cb.enc_unprepare = venc_encode_unprepare;
	cb.enc_pmqos_gce_begin = venc_encode_pmqos_gce_begin;
	cb.enc_pmqos_gce_end = venc_encode_pmqos_gce_end;
	cb.gce_timeout_dump = mtk_vcodec_gce_timeout_dump;
	cb.enc_lock = venc_lock;
	cb.enc_unlock = venc_unlock;
	VCU_FPTR(vcu_set_v4l2_callback)(inst->vcu_inst.dev, &cb);

	mtk_vcodec_debug_leave(inst);

	if (ret) {
		mtk_vcodec_del_ctx_list(ctx);
		kfree(inst);
		(*handle) = (unsigned long)NULL;
	}

	return ret;
}

static int venc_encode(unsigned long handle,
					   enum venc_start_opt opt,
					   struct venc_frm_buf *frm_buf,
					   struct mtk_vcodec_mem *bs_buf,
					   struct venc_done_result *result)
{
	int ret = 0;
	struct venc_inst *inst = (struct venc_inst *)handle;

	if (inst == NULL || inst->vsi == NULL)
		return -EINVAL;

	mtk_vcodec_debug(inst, "opt %d ->", opt);

	switch (opt) {
	case VENC_START_OPT_ENCODE_SEQUENCE_HEADER: {
		unsigned int bs_size_hdr = 0;

		ret = venc_encode_header(inst, bs_buf, &bs_size_hdr);
		if (ret)
			goto encode_err;

		result->bs_size = bs_size_hdr;
		result->is_key_frm = false;
		break;
	}

	case VENC_START_OPT_ENCODE_FRAME: {
		/* only run @ worker then send ipi
		 * VPU flush cmd binding ctx & handle
		 * or cause cmd calllback ctx error
		 */
		ret = venc_encode_frame(inst, frm_buf, bs_buf,
			&result->bs_size);
		if (ret)
			goto encode_err;
		result->is_key_frm = inst->vcu_inst.is_key_frm;
		break;
	}

	case VENC_START_OPT_ENCODE_FRAME_FINAL: {
		ret = venc_encode_frame_final(inst,
			frm_buf, bs_buf, &result->bs_size);
		if (ret)
			goto encode_err;
		result->is_key_frm = inst->vcu_inst.is_key_frm;
		break;
	}

	default:
		mtk_vcodec_err(inst, "venc_start_opt %d not supported", opt);
		ret = -EINVAL;
		break;
	}

encode_err:
	mtk_vcodec_debug(inst, "opt %d <-", opt);

	return ret;
}

static void venc_dump_vsi(struct venc_inst *inst)
{
	static int n;
	const u32 *w = (const u32 *)inst->vsi;
	char line[192];
	int i, len = 0;

	if (n++ > 3)
		return;

	pr_info("[VCUDBG] vsi=%p size=%zu off(list_free)=%zu off(venc)=%zu off(count)=%zu\n",
		inst->vsi, sizeof(struct venc_vsi),
		offsetof(struct venc_vsi, list_free),
		offsetof(struct venc_vsi, venc),
		offsetof(struct venc_vsi, list_free) +
			offsetof(struct ring_input_list, count));

	for (i = 0; i < (int)(sizeof(struct venc_vsi) / 4); i++) {
		if (w[i] == 0)
			continue;
		len += scnprintf(line + len, sizeof(line) - len, "%03d=%08x ", i, w[i]);
		if (len > 150 || i == (int)(sizeof(struct venc_vsi) / 4) - 1) {
			pr_info("[VCUDBG] vsi %s\n", line);
			len = 0;
		}
	}
	if (len)
		pr_info("[VCUDBG] vsi %s\n", line);
}

static void venc_get_free_buffers(struct venc_inst *inst,
			     struct ring_input_list *list,
			     struct venc_done_result *pResult)
{
	venc_dump_vsi(inst);

	if (list->count < 0 || list->count >= VENC_MAX_FB_NUM) {
		mtk_vcodec_err(inst, "list count %d invalid ! (write_idx %d, read_idx %d)",
			list->count, list->write_idx, list->read_idx);
		if (list->write_idx < 0 || list->write_idx >= VENC_MAX_FB_NUM ||
		    list->read_idx < 0  || list->read_idx >= VENC_MAX_FB_NUM)
			list->write_idx = list->read_idx = 0;
		if (list->write_idx >= list->read_idx)
			list->count = list->write_idx - list->read_idx;
		else
			list->count = list->write_idx + VENC_MAX_FB_NUM - list->read_idx;
	}
	if (list->count == 0) {
		mtk_vcodec_debug(inst, "[FB] there is no free buffers");
		pResult->bs_va = 0;
		pResult->frm_va = 0;
		pResult->is_key_frm = false;
		pResult->bs_size = 0;
		return;
	}

	/*
	 * op6893: vpud echoes back the raw va it was handed in vsi->venc --
	 * venc_encode_frame() put the mtk_vcodec_mem / venc_frm_buf pointer
	 * there, which is what 4.19 and 5.10 both read straight out of the ring.
	 * This tree had replaced the pointer with bs_buf->index + 1 and
	 * dereferenced ctx->bs_list[] / fb_list[] here, a convention that exists
	 * only in the 6.6 VCP path and that vpud has no way to know about.
	 */
	pResult->bs_size = list->bs_size[list->read_idx];
	pResult->is_key_frm = list->is_key_frm[list->read_idx];
	pResult->bs_va = list->venc_bs_va_list[list->read_idx];
	pResult->frm_va = list->venc_fb_va_list[list->read_idx];
	/*
	 * op6893: 4.19's struct venc_done_result has no is_last_slc and no flags
	 * -- its mtk_enc_put_buf() completes the source buffer unconditionally.
	 * 6.6 gates that on is_last_slc, but nothing here can ever set it: the
	 * field exists only on the AP side, while the ring this function drains
	 * is vpud's, and vpud's ring_input_list has no such member.  Left zero,
	 * the input buffer is never handed back to the HAL, which runs out of
	 * input slots and stops the encoder after ~5 s with "over 5000ms not
	 * request from VCodec" -- while every frame encodes fine in the daemon.
	 */
	pResult->is_last_slc = 1;
	pResult->flags = 0;

	mtk_vcodec_debug(inst, "read_idx=%d bsva %lx frva %lx bssize %d iskey %d",
		list->read_idx,
		pResult->bs_va,
		pResult->frm_va,
		pResult->bs_size,
		pResult->is_key_frm);

	list->read_idx = (list->read_idx == VENC_MAX_FB_NUM - 1U) ?
			 0U : list->read_idx + 1U;
	list->count--;
}

static void venc_get_resolution_change(struct venc_inst *inst,
			     struct venc_vcu_config *Config,
			     struct venc_resolution_change *pResChange)
{
	pResChange->width = Config->pic_w;
	pResChange->height = Config->pic_h;
	pResChange->framerate = Config->framerate;
	pResChange->resolutionchange = Config->resolutionChange;

	if (Config->resolutionChange)
		Config->resolutionChange = 0;

	mtk_vcodec_debug(inst, "get reschange %d %d %d %d\n",
		 pResChange->width,
		 pResChange->height,
		 pResChange->framerate,
		 pResChange->resolutionchange);
}


static int venc_get_param(unsigned long handle,
						  enum venc_get_param_type type,
						  void *out)
{
	int ret = 0;
	struct venc_inst *inst = (struct venc_inst *)handle;

	if (inst == NULL)
		return -EINVAL;

	mtk_vcodec_debug(inst, "%s: %d", __func__, type);
	inst->vcu_inst.ctx = inst->ctx;

	switch (type) {
	case GET_PARAM_VENC_CAP_FRAME_SIZES:
	case GET_PARAM_VENC_CAP_SUPPORTED_FORMATS:
		vcu_enc_query_cap(&inst->vcu_inst, type, out);
		break;
	case GET_PARAM_FREE_BUFFERS:
		if (inst->vsi == NULL)
			return -EINVAL;
		venc_get_free_buffers(inst, &inst->vsi->list_free, out);
		break;
	case GET_PARAM_ROI_RC_QP: {
		if (inst->vsi == NULL || out == NULL)
			return -EINVAL;
		*(int *)out = inst->vsi->config.roi_rc_qp;
		break;
	}
	case GET_PARAM_RESOLUTION_CHANGE:
		if (inst->vsi == NULL)
			return -EINVAL;
		venc_get_resolution_change(inst, &inst->vsi->config, out);
		break;
	case GET_PARAM_VENC_CAP_COMMON: {
		/*
		 * op6893: 4.19 has no CAP_COMMON query -- neither its
		 * venc_get_param() nor its vpud daemon knows the id, and the
		 * daemon carries no max-B / max-temporal-layer table to answer
		 * it with.  This tree asks it from
		 * mtk_vcodec_enc_set_default_params() on every open, so leaving
		 * it unhandled sprayed "Cannot get cap common" over every encode
		 * session.  Answer it locally with what this hardware actually
		 * does: the 4.19 encode path runs with no B frames and tsvc 0
		 * ("setting max input refernce count 0", "tsvc is 0").
		 */
		struct mtk_codec_capability *cap = out;

		if (out == NULL)
			return -EINVAL;
		cap->max_b = 0;
		cap->max_temporal_layer = 0;
		break;
	}
	case GET_PARAM_VENC_VCU_VPUD_LOG:
		VCU_FPTR(vcu_get_log)(out, LOG_PROPERTY_SIZE);
		break;
	default:
		mtk_vcodec_err(inst, "invalid get parameter type=%d", type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int venc_set_param(unsigned long handle,
	enum venc_set_param_type type,
	struct venc_enc_param *enc_prm)
{
	int i;
	int ret = 0;
	struct venc_inst *inst = (struct venc_inst *)handle;
	unsigned int fmt = 0;

	if (inst == NULL)
		return -EINVAL;

	mtk_vcodec_debug(inst, "->type=%d", type);

	switch (type) {
	case VENC_SET_PARAM_ENC:
		if (inst->vsi == NULL)
			return -EINVAL;
		inst->vsi->config.input_fourcc = enc_prm->input_yuv_fmt;
		inst->vsi->config.bitrate = enc_prm->bitrate;
		inst->vsi->config.pic_w = enc_prm->width;
		inst->vsi->config.pic_h = enc_prm->height;
		inst->vsi->config.buf_w = enc_prm->buf_width;
		inst->vsi->config.buf_h = enc_prm->buf_height;
		inst->vsi->config.gop_size = enc_prm->gop_size;
		inst->vsi->config.framerate = enc_prm->frm_rate;
		inst->vsi->config.intra_period = enc_prm->intra_period;
		inst->vsi->config.operationrate = enc_prm->operationrate;
		inst->vsi->config.bitratemode = enc_prm->bitratemode;
		inst->vsi->config.roion = enc_prm->roion;
		inst->vsi->config.scenario = enc_prm->scenario;
		inst->vsi->config.prependheader = enc_prm->prependheader;
		inst->vsi->config.heif_grid_size = enc_prm->heif_grid_size;
		inst->vsi->config.max_w = enc_prm->max_w;
		inst->vsi->config.max_h = enc_prm->max_h;
		inst->vsi->config.num_b_frame = enc_prm->num_b_frame;
		inst->vsi->config.slbc_ready = enc_prm->slbc_ready;
		inst->ext.slbc_addr = enc_prm->slbc_addr;
		inst->vsi->config.i_qp = enc_prm->i_qp;
		inst->vsi->config.p_qp = enc_prm->p_qp;
		inst->vsi->config.b_qp = enc_prm->b_qp;
		inst->vsi->config.svp_mode = enc_prm->svp_mode;
		if (inst->vsi->config.svp_mode)
#if (!(IS_ENABLED(CONFIG_DEVICE_MODULES_ARM_SMMU_V3)))
			inst->vsi->config.svp_is_hal_secure_handle = is_disable_map_sec();
#else
			inst->vsi->config.svp_is_hal_secure_handle = false;
#endif
		inst->ext.highquality = enc_prm->highquality;
		inst->vsi->config.max_qp = enc_prm->max_qp;
		inst->vsi->config.min_qp = enc_prm->min_qp;
		inst->vsi->config.i_p_qp_delta = enc_prm->ip_qpdelta;
		inst->vsi->config.qp_control_mode = enc_prm->qp_control_mode;
		inst->vsi->config.frame_level_qp = enc_prm->framelvl_qp;
		inst->vsi->config.dummynal = enc_prm->dummynal;
		inst->ext.lowlatencywfd = enc_prm->lowlatencywfd;
		inst->ext.slice_count = enc_prm->slice_count;
		inst->ext.hier_ref_layer = enc_prm->hier_ref_layer;
		inst->ext.hier_ref_type = enc_prm->hier_ref_type;
		inst->ext.temporal_layer_pcount = enc_prm->temporal_layer_pcount;
		inst->ext.temporal_layer_bcount = enc_prm->temporal_layer_bcount;
		inst->ext.max_ltr_num = enc_prm->max_ltr_num;
		inst->ext.qpvbr_upper_enable = enc_prm->qpvbr_upper_enable;
		inst->ext.qpvbr_qp_upper_threshold = enc_prm->qpvbr_qp_upper_threshold;
		inst->ext.qpvbr_qp_max_brratio = enc_prm->qpvbr_qp_max_brratio;
		inst->ext.qpvbr_lower_enable = enc_prm->qpvbr_lower_enable;
		inst->ext.qpvbr_qp_lower_threshold = enc_prm->qpvbr_qp_lower_threshold;
		inst->ext.qpvbr_qp_min_brratio = enc_prm->qpvbr_qp_min_brratio;
		inst->ext.cb_qp_offset = enc_prm->cb_qp_offset;
		inst->ext.cr_qp_offset = enc_prm->cr_qp_offset;
		inst->ext.mbrc_tk_spd = enc_prm->mbrc_tk_spd;
		inst->ext.ifrm_q_ltr = enc_prm->ifrm_q_ltr;
		inst->ext.pfrm_q_ltr = enc_prm->pfrm_q_ltr;
		inst->ext.bfrm_q_ltr = enc_prm->bfrm_q_ltr;
		inst->ext.use_clean_gop = enc_prm->use_clean_gop;

		if (enc_prm->visual_quality) {
			memcpy(&inst->ext.visual_quality,
				enc_prm->visual_quality,
				sizeof(struct mtk_venc_visual_quality));
		}

		if (enc_prm->init_qp) {
			memcpy(&inst->ext.init_qp,
				enc_prm->init_qp,
				sizeof(struct mtk_venc_init_qp));
		}

		if (enc_prm->frame_qp_range) {
			memcpy(&inst->ext.frame_qp_range,
				enc_prm->frame_qp_range,
				sizeof(struct mtk_venc_frame_qp_range));
		}

		if (enc_prm->nal_length) {
			memcpy(&inst->ext.nal_length,
				enc_prm->nal_length,
				sizeof(struct mtk_venc_nal_length));
		}

		if (enc_prm->color_desc) {
			memcpy(&inst->vsi->config.color_desc,
				enc_prm->color_desc,
				sizeof(struct mtk_color_desc));
		}

		if (enc_prm->multi_ref) {
			memcpy(&inst->ext.multi_ref,
				enc_prm->multi_ref,
				sizeof(struct mtk_venc_multi_ref));
		}

		if (enc_prm->vui_info) {
			memcpy(&inst->ext.vui_info,
				enc_prm->vui_info,
				sizeof(struct mtk_venc_vui_info));
		}

		inst->ext.slice_header_spacing =
			enc_prm->slice_header_spacing;

		inst->ext.mlvec_mode =
			enc_prm->mlvec_mode;

		fmt = inst->ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
		mtk_vcodec_debug(inst, "fmt:%u", fmt);

		if (fmt == V4L2_PIX_FMT_H264) {
			inst->vsi->config.profile = enc_prm->profile;
			inst->vsi->config.level = enc_prm->level;
		} else if (fmt == V4L2_PIX_FMT_HEVC ||
				fmt == V4L2_PIX_FMT_HEIF) {
			inst->vsi->config.profile =
				venc_h265_get_profile(inst, enc_prm->profile);
			inst->vsi->config.level =
				venc_h265_get_level(inst, enc_prm->level,
					enc_prm->tier);
		} else if (fmt == V4L2_PIX_FMT_MPEG4) {
			inst->vsi->config.profile =
				venc_mpeg4_get_profile(inst, enc_prm->profile);
			inst->vsi->config.level =
				venc_mpeg4_get_level(inst, enc_prm->level);
		}
		inst->vsi->config.wfd = 0;
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		if (ret)
			break;

		for (i = 0; i < MTK_VCODEC_MAX_PLANES; i++) {
			enc_prm->sizeimage[i] =
				inst->vsi->sizeimage[i];
			mtk_vcodec_debug(inst, "sizeimage[%d] size=0x%x", i,
							 enc_prm->sizeimage[i]);
		}
		inst->ctx->async_mode = !(inst->vsi->sync_mode);

		break;
	case VENC_SET_PARAM_PREPEND_HEADER:
		inst->prepend_hdr = 1;
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		inst->ctx->async_mode = !(inst->vsi->sync_mode);
		break;
	case VENC_SET_PARAM_COLOR_DESC:
		if (inst->vsi == NULL)
			return -EINVAL;
		memcpy(&inst->vsi->config.color_desc, enc_prm->color_desc,
			sizeof(struct mtk_color_desc));
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		break;
	case VENC_SET_PARAM_PROPERTY:
		mtk_vcodec_err(inst, "VCU not support SET_PARAM_VDEC_PROPERTY\n");
		break;
	case VENC_SET_PARAM_VCU_VPUD_LOG:
		ret = VCU_FPTR(vcu_set_log)(enc_prm->log);
		break;
	case VENC_SET_PARAM_VISUAL_QUALITY:
		if (inst->vsi == NULL)
			return -EINVAL;
		memcpy(&inst->ext.visual_quality, enc_prm->visual_quality,
			sizeof(struct mtk_venc_visual_quality));
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		break;
	case VENC_SET_PARAM_INIT_QP:
		if (inst->vsi == NULL)
			return -EINVAL;
		memcpy(&inst->ext.init_qp, enc_prm->init_qp,
			sizeof(struct mtk_venc_init_qp));
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		break;
	case VENC_SET_PARAM_FRAME_QP_RANGE:
		if (inst->vsi == NULL)
			return -EINVAL;
		memcpy(&inst->ext.frame_qp_range, enc_prm->frame_qp_range,
			sizeof(struct mtk_venc_frame_qp_range));
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		break;
	default:
		if (inst->vsi == NULL)
			return -EINVAL;
		ret = vcu_enc_set_param(&inst->vcu_inst, type, enc_prm);
		inst->ctx->async_mode = !(inst->vsi->sync_mode);
		break;
	}

	mtk_vcodec_debug_leave(inst);

	return ret;
}

static int venc_deinit(unsigned long handle)
{
	int ret = 0;
	struct venc_inst *inst = (struct venc_inst *)handle;

	mtk_vcodec_debug_enter(inst);

	ret = vcu_enc_deinit(&inst->vcu_inst);

	mtk_vcodec_del_ctx_list(inst->ctx);

	mtk_vcodec_debug_leave(inst);
	kfree(inst);

	return ret;
}

static const struct venc_common_if venc_if = {
	.init = venc_init,
	.encode = venc_encode,
	.get_param = venc_get_param,
	.set_param = venc_set_param,
	.deinit = venc_deinit,
};

const struct venc_common_if *get_enc_vcu_if(void);

const struct venc_common_if *get_enc_vcu_if(void)
{
	return &venc_if;
}

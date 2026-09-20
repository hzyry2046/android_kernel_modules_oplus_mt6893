/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Daniel Hsiao <daniel.hsiao@mediatek.com>
 *              Jungchang Tsao <jungchang.tsao@mediatek.com>
 *              Tiffany Lin <tiffany.lin@mediatek.com>
 */

#ifndef _VENC_DRV_IF_H_
#define _VENC_DRV_IF_H_

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"
#include "venc_vcu_if.h"
#include "venc_ipi_msg.h"

/*
 * op6893: the encoder state this tree has and the 4.19 vpud daemon does not.
 *
 * struct venc_vsi is mapped into VCU DMEM and read by vpud at fixed offsets
 * (its own private state begins immediately after the 2016-byte copy it maps),
 * so it may not carry anything 4.19 did not have.  Everything this tree added
 * since then lives here instead -- ordinary kernel memory, invisible to the
 * daemon -- and is referenced as inst->ext.<x> where the code used to say
 * inst->vsi-><x>.  Members are grouped by the shared struct they came from.
 */
struct venc_ext {
	/* from struct venc_vcu_config */
	__u32 lowlatencywfd;
	__u32 highquality;
	__u32 slbc_addr;
	__u32 wpp_mode;
	__u32 low_latency_mode;
	__u32 slice_count;
	__u32 hier_ref_layer;
	__u32 hier_ref_type;
	__u32 temporal_layer_pcount;
	__u32 temporal_layer_bcount;
	__u32 max_ltr_num;
	__u32 slice_header_spacing;
	__u32 sysram_enable;
	__u32 ctx_id;
	__s32 priority;
	__u32 codec_fmt;
	__s32 target_freq;
	__u32 target_bw_factor;
	__u8 cpu_hint;
	__u32 mlvec_mode;
	struct mtk_venc_multi_ref multi_ref;
	struct mtk_venc_vui_info vui_info;
	__s32 qpvbr_upper_enable;
	__s32 qpvbr_qp_upper_threshold;
	__s32 qpvbr_qp_max_brratio;
	__s32 qpvbr_lower_enable;
	__s32 qpvbr_qp_lower_threshold;
	__s32 qpvbr_qp_min_brratio;
	__s32 cb_qp_offset;
	__s32 cr_qp_offset;
	__s32 mbrc_tk_spd;
	__s32 ifrm_q_ltr;
	__s32 pfrm_q_ltr;
	__s32 bfrm_q_ltr;
	struct mtk_venc_visual_quality visual_quality;
	struct mtk_venc_init_qp init_qp;
	struct mtk_venc_frame_qp_range frame_qp_range;
	struct mtk_venc_nal_length nal_length;
	__u8 use_clean_gop;

	/* from struct venc_vsi */
	__u32 meta_offset;
	__u32 qpmap_size;
	__u64 qpmap_addr;
	__u64 dynamicparams_addr;
	__u32 dynamicparams_size;
	__u32 dynamicparams_offset;
};

/*
 * struct venc_inst - encoder AP driver instance
 * @hw_base: encoder hardware register base
 * @work_bufs: working buffer
 * @pps_buf: buffer to store the pps bitstream
 * @work_buf_allocated: working buffer allocated flag
 * @frm_cnt: encoded frame count
 * @prepend_hdr: when the v4l2 layer send VENC_SET_PARAM_PREPEND_HEADER cmd
 *  through venc_set_param interface, it will set this flag and prepend the
 *  sps/pps in venc_encode function.
 * @vcu_inst: VCU instance to exchange information between AP and VCU
 * @vsi: driver structure allocated by VCU side and shared to AP side for
 *       control and info share
 * @ext: driver state the VCU side does not have (see struct venc_ext)
 * @ctx: context for v4l2 layer integration
 */
struct venc_inst {
	void __iomem *hw_base;
	struct mtk_vcodec_mem pps_buf;
	bool work_buf_allocated;
	unsigned int frm_cnt;
	unsigned int prepend_hdr;
	struct venc_vcu_inst vcu_inst;
	struct venc_vsi *vsi;
	struct venc_ext ext;
	struct mtk_vcodec_ctx *ctx;
};

/*
 * enum venc_start_opt - encode frame option used in venc_if_encode()
 * @VENC_START_OPT_ENCODE_SEQUENCE_HEADER: encode SPS/PPS for H264
 * @VENC_START_OPT_ENCODE_FRAME: encode normal frame
 * @VENC_START_OPT_ENCODE_FRAME_FINAL: encode last frame for oal codec
 */
enum venc_start_opt {
	VENC_START_OPT_ENCODE_SEQUENCE_HEADER,
	VENC_START_OPT_ENCODE_FRAME,
	VENC_START_OPT_ENCODE_FRAME_FINAL
};

/*
 * struct venc_done_result - This is return information used in venc_if_encode()
 * @bs_size: output bitstream size
 * @is_key_frm: output is key frame or not
 */
struct venc_done_result {
	__u32 bs_size;
	__u32 is_key_frm;
	unsigned long bs_va;
	unsigned long frm_va;
	__u32 is_last_slc;
	__u32 flags;
};

/*
 * struct venc_resolution_change
 * @width: width resolution change to
 * @height: height resolution change to
 * @resolutionchange : if resolution change
 */
struct venc_resolution_change {
	__u32 width;
	__u32 height;
	__u32 framerate;
	__u32 resolutionchange;
};

extern struct mtk_video_fmt
	mtk_venc_formats[MTK_MAX_ENC_CODECS_SUPPORT];
extern struct mtk_codec_framesizes
	mtk_venc_framesizes[MTK_MAX_ENC_CODECS_SUPPORT];

/*
 * venc_if_init - Create the driver handle
 * @ctx: device context
 * @fourcc: encoder input format
 * Return: 0 if creating handle successfully, otherwise it is failed.
 */
int venc_if_init(struct mtk_vcodec_ctx *ctx, unsigned int fourcc);

/*
 * venc_if_deinit - Release the driver handle
 * @ctx: device context
 * Return: 0 if releasing handle successfully, otherwise it is failed.
 */
int venc_if_deinit(struct mtk_vcodec_ctx *ctx);

/**
 * venc_if_get_param() - get driver's parameter
 * @ctx : [in] v4l2 context
 * @type    : [in] input parameter type
 * @out : [out] buffer to store query result
 */
int venc_if_get_param(struct mtk_vcodec_ctx *ctx, enum venc_get_param_type type,
					  void *out);

/*
 * venc_if_set_param - Set parameter to driver
 * @ctx: device context
 * @type: parameter type
 * @in: input parameter
 * Return: 0 if setting param successfully, otherwise it is failed.
 */
int venc_if_set_param(struct mtk_vcodec_ctx *ctx,
					  enum venc_set_param_type type,
					  struct venc_enc_param *in);

/*
 * venc_if_encode - Encode one frame
 * @ctx: device context
 * @opt: encode frame option
 * @frm_buf: input frame buffer information
 * @bs_buf: output bitstream buffer infomraiton
 * @result: encode result
 * Return: 0 if encoding frame successfully, otherwise it is failed.
 */
int venc_if_encode(struct mtk_vcodec_ctx *ctx,
				   enum venc_start_opt opt,
				   struct venc_frm_buf *frm_buf,
				   struct mtk_vcodec_mem *bs_buf,
				   struct venc_done_result *result);


int venc_if_dev_ctx_init(struct mtk_vcodec_dev *dev);
void venc_if_dev_ctx_deinit(struct mtk_vcodec_dev *dev);

void venc_encode_prepare(void *ctx_prepare,
		unsigned int core_id, unsigned long *flags);
void venc_encode_unprepare(void *ctx_unprepare,
		unsigned int core_id, unsigned long *flags);
void venc_check_release_lock(void *ctx_check);
int venc_lock(void *ctx_lock, int core_id, bool sec);
void venc_unlock(void *ctx_unlock, int core_id);

#endif /* _VENC_DRV_IF_H_ */

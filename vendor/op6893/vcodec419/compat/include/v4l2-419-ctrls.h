/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 4.19 MTK patched v4l2-ctrls.c so v4l2_ctrl_get_name()/v4l2_ctrl_fill()
 * knew the V4L2_CID_MPEG_MTK_* ids.  The 6.6 GKI core does not, so
 * v4l2_ctrl_new_std() on them fails with -ERANGE (NULL name) and the whole
 * handler -- and fops_vcodec_open -- fails.  Route ids the core does not
 * know through v4l2_ctrl_new_custom() with the 4.19 type/flags.
 * Include after <media/v4l2-ctrls.h>.
 */
#ifndef _V4L2_419_CTRLS_H
#define _V4L2_419_CTRLS_H

#include <media/v4l2-ctrls.h>

static inline const char *c419_mtk_ctrl_fill(u32 id, enum v4l2_ctrl_type *type,
					     u32 *flags)
{
	*type = V4L2_CTRL_TYPE_INTEGER;
	*flags = 0;
	switch (id) {
	case V4L2_CID_MPEG_MTK_FRAME_INTERVAL:
		*flags = V4L2_CTRL_FLAG_READ_ONLY; return "Video frame interval";
	case V4L2_CID_MPEG_MTK_ERRORMB_MAP:
		*flags = V4L2_CTRL_FLAG_READ_ONLY; return "Video error map";
	case V4L2_CID_MPEG_MTK_ASPECT_RATIO:
		*flags = V4L2_CTRL_FLAG_READ_ONLY; return "Video aspect ratio";
	case V4L2_CID_MPEG_MTK_FIX_BUFFERS:
		*flags = V4L2_CTRL_FLAG_READ_ONLY; return "Video fix buffers";
	case V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP:
		*flags = V4L2_CTRL_FLAG_READ_ONLY; return "Video fix buffers for svp";
	case V4L2_CID_MPEG_MTK_DECODE_MODE:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video decode mode";
	case V4L2_CID_MPEG_MTK_FRAME_SIZE:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video frame size";
	case V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video fixed maximum frame size";
	case V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Wait key frame";
	case V4L2_CID_MPEG_MTK_OPERATING_RATE:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Vdec Operating Rate";
	case V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Vdec Realtime Priority";
	case V4L2_CID_MPEG_MTK_SEC_DECODE:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video Sec Decode path";
	case V4L2_CID_MPEG_MTK_SEC_ENCODE:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video Sec Encode path";
	case V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT:
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video queued frame buf count";
	case V4L2_CID_MPEG_MTK_CRC_PATH:
		*type = V4L2_CTRL_TYPE_STRING;
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video crc path";
	case V4L2_CID_MPEG_MTK_GOLDEN_PATH:
		*type = V4L2_CTRL_TYPE_STRING;
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video golden path";
	case V4L2_CID_MPEG_MTK_LOG:
		*type = V4L2_CTRL_TYPE_STRING;
		*flags = V4L2_CTRL_FLAG_WRITE_ONLY; return "Video Log";
	case V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP: return "I-Frame QP Value";
	case V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP: return "P-Frame QP Value";
	case V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP: return "B-Frame QP Value";
	default:
		/* 4.19 left these nameless too; any name satisfies 6.6 */
		return "MTK vcodec control";
	}
}

static inline struct v4l2_ctrl *c419_ctrl_new_std(struct v4l2_ctrl_handler *hdl,
		const struct v4l2_ctrl_ops *ops, u32 id,
		s64 min, s64 max, u64 step, s64 def)
{
	struct v4l2_ctrl_config cfg = {
		.ops = ops, .id = id, .min = min, .max = max,
		.step = step, .def = def,
	};

	if (v4l2_ctrl_get_name(id))
		return v4l2_ctrl_new_std(hdl, ops, id, min, max, step, def);
	cfg.name = c419_mtk_ctrl_fill(id, &cfg.type, &cfg.flags);
	return v4l2_ctrl_new_custom(hdl, &cfg, NULL);
}
#define v4l2_ctrl_new_std c419_ctrl_new_std

#endif

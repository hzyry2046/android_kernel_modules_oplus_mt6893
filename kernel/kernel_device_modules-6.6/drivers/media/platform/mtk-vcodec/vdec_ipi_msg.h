/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 */

#ifndef _VDEC_IPI_MSG_H_
#define _VDEC_IPI_MSG_H_
#include <linux/build_bug.h>
#include <linux/types.h>
#include "vcodec_ipi_msg.h"

#define MTK_MAX_DEC_CODECS_SUPPORT       (128)
#define DEC_MAX_FB_NUM              VIDEO_MAX_FRAME
#define DEC_MAX_BS_NUM              VIDEO_MAX_FRAME

/**
 * enum vdec_src_chg_type - decoder src change type
 * @VDEC_NO_CHANGE      : no change
 * @VDEC_RES_CHANGE     : resolution change
 * @VDEC_REALLOC_MV_BUF : realloc mv buf
 * @VDEC_HW_NOT_SUPPORT : hw not support
 * @VDEC_NEED_MORE_OUTPUT_BUF: bs need more fm buffer to decode
 *          kernel should send the same bs buffer again with new fm buffer
 * @VDEC_CROP_CHANGED: notification to update frame crop info
 */
enum vdec_src_chg_type {
	VDEC_NO_CHANGE              = (0 << 0),
	VDEC_RES_CHANGE             = (1 << 0),
	VDEC_REALLOC_MV_BUF         = (1 << 1),
	VDEC_HW_NOT_SUPPORT         = (1 << 2),
	VDEC_NEED_SEQ_HEADER        = (1 << 3),
	VDEC_NEED_MORE_OUTPUT_BUF   = (1 << 4),
	VDEC_CROP_CHANGED           = (1 << 5),
	VDEC_OUTPUT_NOT_GENERATED   = (1 << 6),
	VDEC_COLOR_ASPECT_CHANGED   = (1 << 7),
};

enum vdec_fb_flag_type {
	VDEC_FB_NO_FLAGS            = (0 << 0),
	VDEC_FB_EOS                 = (1 << 0),
	VDEC_FB_NO_GENERATED        = (1 << 1),
	VDEC_FB_CROP_CHANGED        = (1 << 2),
};

enum vdec_ipi_msg_status {
	VDEC_IPI_MSG_STATUS_OK      = 0,
	VDEC_IPI_MSG_STATUS_FAIL    = -1,
	VDEC_IPI_MSG_STATUS_MAX_INST    = -2,
	VDEC_IPI_MSG_STATUS_ILSEQ   = -3,
	VDEC_IPI_MSG_STATUS_INVALID_ID  = -4,
	VDEC_IPI_MSG_STATUS_DMA_FAIL    = -5,
};

/**
 * enum vdec_ipi_msg_id - message id between AP and VCU
 * @AP_IPIMSG_XXX       : AP to VCU cmd message id
 * @VCU_IPIMSG_XXX_ACK  : VCU ack AP cmd message id
 */
/*
 * op6893 bring-up: the numbers below are the 4.19 vendor driver's, because the
 * peer -- the userspace /vendor/bin/vpud daemon -- is a 4.19 binary that
 * dispatches on them.  This tree shipped a renumbered set: DEINIT sat on 0xA002
 * where 4.19 has END, SET_PARAM on 0xA004 where 4.19 has RESET, the whole
 * VCU->AP range was shifted by two, and the ACK range repeated the same
 * mistake.  Every decoder message was therefore delivered to the daemon, or
 * read back from it, as some other operation.
 *
 * Messages that exist only in this tree have no 4.19 counterpart; they are
 * parked above the daemon's range (0xA01x / 0xB01x / 0xC01x) and must never be
 * relied on for anything the daemon originates.
 */
enum vdec_ipi_msg_id {
	AP_IPIMSG_DEC_INIT = 0xA000,
	AP_IPIMSG_DEC_START = 0xA001,
	AP_IPIMSG_DEC_END = 0xA002,
	AP_IPIMSG_DEC_DEINIT = 0xA003,
	AP_IPIMSG_DEC_RESET = 0xA004,
	AP_IPIMSG_DEC_SET_PARAM = 0xA005,
	AP_IPIMSG_DEC_QUERY_CAP = 0xA006,
	/* 6.6-only, no 4.19 counterpart */
	AP_IPIMSG_DEC_FRAME_BUFFER = 0xA010,
	AP_IPIMSG_DEC_BACKUP = 0xA011,
	AP_IPIMSG_DEC_RESUME = 0xA012,
	AP_IPIMSG_DEC_PWR_CTRL = 0xA013,

	VCU_IPIMSG_DEC_INIT_DONE = 0xB000,
	VCU_IPIMSG_DEC_START_DONE = 0xB001,
	VCU_IPIMSG_DEC_END_DONE = 0xB002,
	VCU_IPIMSG_DEC_DEINIT_DONE = 0xB003,
	VCU_IPIMSG_DEC_RESET_DONE = 0xB004,
	VCU_IPIMSG_DEC_SET_PARAM_DONE = 0xB005,
	VCU_IPIMSG_DEC_QUERY_CAP_DONE = 0xB006,
	/* 6.6-only, no 4.19 counterpart */
	VCU_IPIMSG_DEC_BACKUP_DONE = 0xB010,
	VCU_IPIMSG_DEC_RESUME_DONE = 0xB011,
	VCU_IPIMSG_DEC_PWR_CTRL_DONE = 0xB012,

	VCU_IPIMSG_DEC_WAITISR = 0xC000,
	VCU_IPIMSG_DEC_GET_FRAME_BUFFER = 0xC001,
	VCU_IPIMSG_DEC_PUT_FRAME_BUFFER = 0xC002,
	VCU_IPIMSG_DEC_LOCK_CORE = 0xC003,
	VCU_IPIMSG_DEC_UNLOCK_CORE = 0xC004,
	VCU_IPIMSG_DEC_LOCK_LAT = 0xC005,
	VCU_IPIMSG_DEC_UNLOCK_LAT = 0xC006,
	/* 6.6-only, no 4.19 counterpart */
	VCU_IPIMSG_DEC_DONE = 0xC010,
	VCU_IPIMSG_DEC_MEM_ALLOC = 0xC011,
	VCU_IPIMSG_DEC_MEM_FREE = 0xC012,
	VCU_IPIMSG_DEC_CHECK_CODEC_ID = 0xC013,
	VCU_IPIMSG_DEC_GET_KERNEL_PARAM = 0xC014,
	VCU_IPIMSG_DEC_SMI_DBG_DUMP = 0xC015,
	VCU_IPIMSG_DEC_SLICE_DONE_ISR = 0xC016,

	AP_IPIMSG_DEC_PUT_FRAME_BUFFER_DONE = 0xD000,
	AP_IPIMSG_DEC_LOCK_CORE_DONE = 0xD001,
	AP_IPIMSG_DEC_UNLOCK_CORE_DONE = 0xD002,
	AP_IPIMSG_DEC_LOCK_LAT_DONE = 0xD003,
	AP_IPIMSG_DEC_UNLOCK_LAT_DONE = 0xD004,
	AP_IPIMSG_DEC_MEM_ALLOC_DONE = 0xD005,
	AP_IPIMSG_DEC_MEM_FREE_DONE = 0xD006,
	AP_IPIMSG_DEC_WAITISR_DONE = 0xD007,
	AP_IPIMSG_DEC_CHECK_CODEC_ID_DONE = 0xD008,
	AP_IPIMSG_DEC_GET_KERNEL_PARAM_DONE = 0xD009,
	AP_IPIMSG_DEC_SMI_DBG_DUMP_DONE = 0xD00A,

	VCU_ASYNCIPIMSG_DEC_PUT_FRAME_BUFFER = 0xE000,
};

enum vdec_flush_type {
	FLUSH_BITSTREAM = (1 << 0),
	FLUSH_FRAME     = (1 << 1),
};

/**
 * enum vdec_reset_type - decoder reset type
 * @VDEC_FLUSH      : flush, no need to cotinue decode and return all frame buffers
 * @VDEC_DRAIN      : drain, need to decode done all inputs and return all decoded frames
 * @VDEC_DRAIN_EOS  : drain for EOS, except for drain, need to free one more buffer for EOS
 */
enum vdec_reset_type {
	VDEC_FLUSH = 0,
	VDEC_DRAIN = 1,
	VDEC_DRAIN_EOS = 2,
};

/* For GET_PARAM_DISP_FRAME_BUFFER and GET_PARAM_FREE_FRAME_BUFFER,
 * the caller does not own the returned buffer. The buffer will not be
 *                              released before vdec_if_deinit.
 * GET_PARAM_DISP_FRAME_BUFFER  : get next displayable frame buffer,
 *                              struct vdec_fb**
 * GET_PARAM_FREE_FRAME_BUFFER  : get non-referenced framebuffer, vdec_fb**
 * GET_PARAM_PIC_INFO           : get picture info, struct vdec_pic_info*
 * GET_PARAM_CROP_INFO          : get crop info, struct v4l2_crop*
 * GET_PARAM_DPB_SIZE           : get dpb size, __s32*
 * GET_PARAM_FRAME_INTERVAL     : get frame interval info*
 * GET_PARAM_ERRORMB_MAP        : get error mocroblock when decode error*
 * GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS: get codec supported format capability
 * GET_PARAM_VDEC_CAP_FRAME_SIZES:
 *                       get codec supported frame size & alignment info
 */
enum vdec_get_param_type {
	GET_PARAM_DISP_FRAME_BUFFER,
	GET_PARAM_FREE_FRAME_BUFFER,
	GET_PARAM_FREE_BITSTREAM_BUFFER,
	GET_PARAM_PIC_INFO,
	GET_PARAM_CROP_INFO,
	GET_PARAM_DPB_SIZE,
	GET_PARAM_FRAME_INTERVAL,
	GET_PARAM_ERRORMB_MAP,
	GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS,
	GET_PARAM_VDEC_CAP_FRAME_SIZES,
	GET_PARAM_COLOR_DESC,
	GET_PARAM_ASPECT_RATIO,
	GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS,
	GET_PARAM_INTERLACING,
	GET_PARAM_INPUT_DRIVEN,
	GET_PARAM_OUTPUT_ASYNC,
	GET_PARAM_LOW_POWER_MODE,
	GET_PARAM_INTERLACING_FIELD_SEQ,
	GET_PARAM_VDEC_CAP_FRAMEINTERVALS,
	GET_PARAM_RES_INFO,
	GET_PARAM_VDEC_CAP_MAX_BUF_INFO,
	GET_PARAM_BANDWIDTH_INFO,
	GET_PARAM_TRICK_MODE,
	GET_PARAM_VDEC_VCU_VPUD_LOG,

	GET_PARAM_MAX = 0xFFFFFFFF
};

/*
 * enum vdec_set_param_type -
 *                  The type of set parameter used in vdec_if_set_param()
 * (VCU related: If you change the order, you must also update the VCU codes.)
 * SET_PARAM_DECODE_MODE: set decoder mode
 * SET_PARAM_FRAME_SIZE: set container frame size
 * SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER: set fixed maximum buffer size
 * SET_PARAM_COMPRESSED_MODE: set compressed mode
 * SET_PARAM_CRC_PATH: set CRC path used for UT
 * SET_PARAM_GOLDEN_PATH: set Golden YUV path used for UT
 * SET_PARAM_FB_NUM_PLANES                      : frame buffer plane count
 */
enum vdec_set_param_type {
	SET_PARAM_DECODE_MODE,
	SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER,
	SET_PARAM_COMPRESSED_MODE,
	SET_PARAM_CRC_PATH,
	SET_PARAM_GOLDEN_PATH,
	SET_PARAM_FB_NUM_PLANES,
	SET_PARAM_WAIT_KEY_FRAME,
	SET_PARAM_OPERATING_RATE,
	SET_PARAM_TOTAL_BITSTREAM_BUFQ_COUNT,
	SET_PARAM_TOTAL_FRAME_BUFQ_COUNT,
	SET_PARAM_FRAME_BUFFER,
	SET_PARAM_VDEC_PROPERTY,
	SET_PARAM_VDEC_VCP_LOG_INFO,
	SET_PARAM_SET_DV,
	SET_PARAM_PUT_FB,
	SET_PARAM_CROP_INFO,
	SET_PARAM_HDR10_INFO,
	SET_PARAM_TRICK_MODE,
	SET_PARAM_NO_REORDER,
	SET_PARAM_DECODE_ERROR_HANDLE_MODE,
	SET_PARAM_DEC_PARAMS,
	SET_PARAM_MMDVFS,
	SET_PARAM_PER_FRAME_SUBSAMPLE_MODE,
	SET_PARAM_ACQUIRE_RESOURCE,
	SET_PARAM_VPEEK_MODE,
	SET_PARAM_VDEC_PLUS_DROP_RATIO,
	SET_PARAM_CONTAINER_FRAMERATE,
	SET_PARAM_DISABLE_DEBLOCK,
	SET_PARAM_LOW_LATENCY,
	SET_PARAM_VDEC_LINECOUNT_THRESHOLD,
	/** only for kernel **/
	SET_PARAM_VDEC_PWR_CTRL,
	SET_PARAM_VDEC_VCU_VPUD_LOG,
	SET_PARAM_VDEC_IN_GROUP,
	/*
	 * op6893: re-added at the end so no existing value shifts.  4.19 sends the
	 * container frame size right after vdec_if_init(), and the 4.19 daemon
	 * needs it before it can open its codec instance -- without it the daemon
	 * answers "[H264_GetInstByID] There is no any H264 instance" and every
	 * AP_IPIMSG_DEC_START comes back status -1.  Forwarded as
	 * SET_PARAM_419_FRAME_SIZE.
	 */
	SET_PARAM_FRAME_SIZE,
	SET_PARAM_MAX = 0xFFFFFFFF
};

/*
 * op6893: the ids the 4.19 daemon dispatches on.
 *
 * This tree renumbered and reordered enum vdec_set_param_type, and it also
 * changed *when* parameters are pushed: it collects the individual settings
 * into struct mtk_dec_params and hands the whole blob over as
 * SET_PARAM_DEC_PARAMS.  The daemon has neither that message nor those ids --
 * it expects each setting as its own AP_IPIMSG_DEC_SET_PARAM carrying one of
 * the numbers below, exactly as 4.19's vdec_set_param() did.
 *
 * Sending this tree's ids instead is not silently ignored: the daemon answers
 * "[VPUD] unknown param type (20)!" and its H.264 layer then fails to open the
 * instance, which comes back as AP_IPIMSG_DEC_START status -1.
 *
 * Everything the daemon has no id for must simply not be sent.
 */
enum vdec_419_set_param_type {
	SET_PARAM_419_DECODE_MODE = 0,			/* 1 word */
	SET_PARAM_419_FRAME_SIZE = 1,			/* 2 words */
	SET_PARAM_419_SET_FIXED_MAX_OUTPUT_BUFFER = 2,	/* 2 words */
	SET_PARAM_419_UFO_MODE = 3,			/* local only */
	SET_PARAM_419_CRC_PATH = 4,			/* local only */
	SET_PARAM_419_GOLDEN_PATH = 5,			/* local only */
	SET_PARAM_419_FB_NUM_PLANES = 6,		/* local only */
	SET_PARAM_419_WAIT_KEY_FRAME = 7,		/* 1 word */
	SET_PARAM_419_NAL_SIZE_LENGTH = 8,		/* 1 word */
	SET_PARAM_419_OPERATING_RATE = 9,		/* 1 word */
	SET_PARAM_419_TOTAL_FRAME_BUFQ_COUNT = 10,	/* 1 word */
	SET_PARAM_419_DEC_LOG = 11,
};

enum vdec_get_kernel_param_type {
	GET_KPARAM_VP_MODE_BUF,
	GET_KPARAM_MAX = 0xFFFFFFFF
};

/*
 * op6893 bring-up: every struct below that actually crosses the AP<->VCU
 * boundary is laid out exactly the way the 4.19 vendor driver has it, because
 * the peer is the 4.19 /vendor/bin/vpud binary and it reads these buffers at
 * fixed offsets.  Fields this tree added are appended *after* the 4.19 ones.
 *
 * Getting this wrong is silent, and that is what made hardware decode
 * unfixable-looking: the message is delivered, the daemon answers correctly,
 * and the kernel simply reads the wrong field out of the reply.  In the
 * QUERY_CAP case the daemon returned a valid 40-byte 0xB006 with id=8, the
 * driver's generic-ack prefix put that id where it expected `status`, the
 * `msg->status == 0` guard therefore failed, and the payload was dropped --
 * leaving mtk_vdec_formats[]/mtk_vdec_framesizes[] empty and configure()
 * failing with "Resolution not supported".
 *
 * The VCP path (vdec_vcp_if.c) has its own, newer protocol and is not built on
 * this platform (CONFIG_MTK_TINYSYS_VCP_SUPPORT is off); only the structs it
 * alone uses keep the old prefix.
 */
#define VDEC_MSG_VCP_PREFIX	\
	__u32 msg_id;	\
	__u32 ctx_id;	\
	__u64 ap_inst_addr;	\
	__s32 status;	\
	__u32 reserved

/**
 * struct vdec_ap_ipi_cmd - generic AP to VCU ipi command format
 * @msg_id        : vdec_ipi_msg_id
 * @vcu_inst_addr : VCU decoder instance address (32-bit DMEM offset)
 * @drain_type    : 6.6-only; the 4.19 daemon never reads it
 */
struct vdec_ap_ipi_cmd {
	__u32 msg_id;
	__u32 vcu_inst_addr;
	__u32 drain_type;
	__u32 reserved;
};

/*
 * op6893: the 4.19 struct ends where the trailing fields begin, and the daemon
 * is handed exactly that many bytes -- this tree's additions are not part of
 * its view of the message and must not be transmitted.
 */
#define VDEC_AP_IPI_CMD_LEN		8	/* through vcu_inst_addr */

/**
 * struct vdec_ap_ipi_cmd_indp - generic AP to VCU ipi command format for instance independent
 * @msg_id      : vdec_ipi_msg_id
 * @ap_inst_addr        : AP video decoder instance address
 */
struct vdec_ap_ipi_cmd_indp {
	VDEC_MSG_VCP_PREFIX;
};

/**
 * struct vdec_vcu_ipi_ack - generic VCU to AP ipi command format
 * @msg_id       : vdec_ipi_msg_id
 * @status       : VCU execution result, carries hw id when lock/unlock
 * @ap_inst_addr : AP video decoder instance address
 * @id           : 6.6-only
 * @data         : 6.6-only
 * @payload      : 6.6-only
 *
 * The three trailing fields are this tree's additions.  They sit past the end
 * of anything the 4.19 daemon writes, so they must not be consulted for a
 * message the daemon can originate -- for those only msg_id/status/
 * ap_inst_addr are meaningful.
 */
struct vdec_vcu_ipi_ack {
	__u32 msg_id;
	__s32 status;
	__u64 ap_inst_addr;
	__s32 id;
	__u32 data;
	__u64 payload;
};

/**
 * struct vdec_vcu_ipi_mem_op -VCU/AP bi-direction memory operation cmd structure
 * @msg_id:   message id (VCU_IPIMSG_XXX_ENC_DEINIT_DONE)
 * @status:   cmd status (venc_ipi_msg_status)
 * @ap_inst_addr:	AP decoder instance (struct vdec_inst*)
 * @struct vcodec_mem_obj: encoder memories
 */
struct vdec_vcu_ipi_mem_op {
	VDEC_MSG_VCP_PREFIX;
	struct vcodec_mem_obj mem;
	__u32 vcp_addr[2];
};

/**
 * struct vdec_ap_ipi_pwr_ctrl -VCU/AP bi-direction smi power contrl operation cmd structure
 * @msg_id:   message id (VCU_IPIMSG_XXX_ENC_DEINIT_DONE)
 * @status:   cmd status (venc_ipi_msg_status)
 * @ap_inst_addr:	AP decoder instance (struct vdec_inst*)
 * @struct vcodec_mem_obj: encoder memories
 */
struct vdec_ap_ipi_pwr_ctrl {
	VDEC_MSG_VCP_PREFIX;
#ifndef CONFIG_64BIT
	union {
		__u64 ap_data_addr_64;
		__u32 ap_data_addr;
	};
#else
	__u64 ap_data_addr;
#endif
	struct mtk_smi_pwr_ctrl_info info;
};

/**
 * struct vdec_ap_ipi_init - for AP_IPIMSG_DEC_INIT
 * @msg_id      : AP_IPIMSG_DEC_INIT
 * @reserved    : Reserved field
 * @ap_inst_addr        : AP video decoder instance address
 */
struct vdec_ap_ipi_init {
	__u32 msg_id;
	__u32 reserved;		/* 4.19 carries svp_mode here */
	__u64 ap_inst_addr;
};

/**
 * struct vdec_vcu_ipi_init_ack - for VCU_IPIMSG_DEC_INIT_ACK
 * @msg_id        : VCU_IPIMSG_DEC_INIT_ACK
 * @status        : VCU execution result
 * @ap_inst_addr        : AP vcodec_vcu_inst instance address
 * @vcu_inst_addr : VCU decoder instance address (32-bit DMEM offset)
 */
struct vdec_vcu_ipi_init_ack {
	__u32 msg_id;
	__s32 status;
	__u64 ap_inst_addr;
	__u32 vcu_inst_addr;
};

/**
 * struct vdec_ap_ipi_dec_start - for AP_IPIMSG_DEC_START
 * @msg_id      : AP_IPIMSG_DEC_START
 * @vcu_inst_addr       : VCU decoder instance address
 * @data        : Header info
 * @reserved    : Reserved field
 * @ack msg use vdec_vcu_ipi_ack
 */
struct vdec_ap_ipi_dec_start {
	__u32 msg_id;
	__u32 vcu_inst_addr;
	__u32 data[3];
	__u32 reserved;
	/*
	 * 6.6-only overflow.  This tree passes the fixed max frame size in the
	 * start message; 4.19 sends it through SET_PARAM instead.  Only the
	 * first three words exist as far as the daemon's struct goes, so these
	 * three are invisible to it and the feature is lost on this peer.
	 */
	__u32 data_ext[3];
};
#define VDEC_AP_IPI_DEC_START_LEN	24	/* through reserved */

/**
 * struct vdec_ap_ipi_set_param - for AP_IPIMSG_DEC_SET_PARAM
 * @msg_id        : AP_IPIMSG_DEC_SET_PARAM
 * @vcu_inst_addr : VCU decoder instance address
 * @id            : set param  type
 * @data          : param data
 */
struct vdec_ap_ipi_set_param {
	__u32 msg_id;
	__u32 vcu_inst_addr;
	__u32 id;
	/*
	 * The daemon reads eight words here.  This tree pushes a whole
	 * struct vdec_ipi_fb (40 bytes) through the same slot for its private
	 * AP_IPIMSG_DEC_FRAME_BUFFER, and the 4.19 daemon has no message for
	 * that -- so the feature is lost either way.  `raw' exists so the copy
	 * has somewhere legal to land instead of running off the field; the
	 * daemon only ever sees the first 32 bytes.
	 */
	union {
		__u32 data[8];
		__u8 raw[40];
	};
};
#define VDEC_AP_IPI_SET_PARAM_LEN	44	/* through data[8] */

/**
 * struct vdec_ap_ipi_query_cap - for AP_IPIMSG_DEC_QUERY_CAP
 * @msg_id        : AP_IPIMSG_DEC_QUERY_CAP
 * @id      : query capability type
 * @vdec_inst     : AP query data address
 */
/*
 * op6893 bring-up: keep the 4.19 vpud wire layout {msg_id, id,
 * ap_inst_addr, ap_data_addr} -- the vendor daemon parses this struct
 * directly and does not know this tree's ctx_id/status/reserved prefix.
 */
struct vdec_ap_ipi_query_cap {
	__u32 msg_id;
	__u32 id;
#ifndef CONFIG_64BIT
	union {
		__u64 ap_inst_addr_64;
		__u32 ap_inst_addr;
	};
	union {
		__u64 ap_data_addr_64;
		__u32 ap_data_addr;
	};
#else
	__u64 ap_inst_addr;
	__u64 ap_data_addr;
#endif
};

/**
 * struct vdec_vcu_ipi_query_cap_ack - for VCU_IPIMSG_DEC_QUERY_CAP_ACK
 * @msg_id      : VCU_IPIMSG_DEC_QUERY_CAP_ACK
 * @status      : VCU execution result
 * @ap_data_addr   : AP query data address
 * @vcu_data_addr  : VCU query data address
 */
/*
 * op6893 bring-up: keep the 4.19 vpud wire layout {msg_id, status,
 * ap_inst_addr, id, ap_data_addr, vcu_data_addr(u32)} -- see the
 * send-side note above.
 */
struct vdec_vcu_ipi_query_cap_ack {
	__u32 msg_id;
	__s32 status;
#ifndef CONFIG_64BIT
	union {
		__u64 ap_inst_addr_64;
		__u32 ap_inst_addr;
	};
	__u32 id;
	union {
		__u64 ap_data_addr_64;
		__u32 ap_data_addr;
	};
#else
	__u64 ap_inst_addr;
	__u32 id;
	__u64 ap_data_addr;
#endif
	__u32 vcu_data_addr;
};

/*
 * struct vdec_ipi_fb - decoder frame buffer information
 * @vdec_fb_va  : virtual address of struct vdec_fb
 * @y_fb_dma    : dma address of Y frame buffer
 * @c_fb_dma    : dma address of C frame buffer
 * @poc         : picture order count of frame buffer
 * @timestamp   : timestamp of frame buffer
 * @reserved    : for 8 bytes alignment
 *
 * op6893 bring-up: this is the 4.19 element type, and it is what the vpud
 * daemon reads out of @list_free / @list_disp.  This tree had replaced it with
 * a 24-byte `vdec_fb_entry', which made every ring element address wrong for
 * the daemon *and* left two bytes of `field' where the daemon expects the low
 * half of @poc.  The element must stay 48 bytes.
 */
struct vdec_ipi_fb {
	__u64 vdec_fb_va;
	__u64 y_fb_dma;
	__u64 c_fb_dma;
	__s32 poc;
	__u64 timestamp;
	__u32 reserved;
};

/*
 * struct vdec_frame_buf_info - this tree's frame buffer blob for its private
 * AP_IPIMSG_DEC_FRAME_BUFFER message.
 *
 * The 4.19 daemon has no such message, so this shape never reaches the wire in
 * a form it understands; it is kept only so the (dead on this platform) send
 * path still compiles.  Do not put it in shared memory.
 */
struct vdec_frame_buf_info {
	__u64 vdec_fb_va;
	__u64 y_fb_dma;
	__u64 c_fb_dma;
	__u64 dma_general_addr;
	__s32 general_size;
	__u32 reserved;
};

/*
 * struct vdec_fb_entry - decoder frame buffer information for free/disp list
 * @vdec_fb_va  : virtual address of struct vdec_fb
 * @timestamp : timestamp of frame buffer
 * @field       : enum v4l2_field, field type of frame buffer
 * @frame_type  : enum mtk_frame_type, I/P/B frame type
 * @flags       : flags
 *
 * Only the VCP path still uses this; it is not built on this platform.
 */
struct vdec_fb_entry {
	__u64 vdec_fb_va;
	__u64 timestamp;
	__u16 field;
	__u16 frame_type;
	__u32 flags;
};

/**
 * struct ring_bs_list - ring bitstream buffer list
 * @vdec_bs_va_list   : bitstream buffer arrary
 * @read_idx  : read index
 * @write_idx : write index
 * @count     : buffer count in list
 */
struct ring_bs_list {
	__u64 vdec_bs_va_list[DEC_MAX_BS_NUM];
	__u32 read_idx;
	__u32 write_idx;
	__u32 count;
	__u32 reserved;
};

/**
 * struct ring_fb_list - ring frame buffer list
 * @fb_list   : frame buffer arrary
 * @read_idx  : read index
 * @write_idx : write index
 * @count     : buffer count in list
 *
 * op6893 bring-up: element type is @vdec_ipi_fb (48 bytes), not
 * `vdec_fb_entry' (24 bytes), so the whole list is 3088 bytes -- that is the
 * size the 4.19 daemon walks.
 */
struct ring_fb_list {
	struct vdec_ipi_fb fb_list[DEC_MAX_FB_NUM];
	__u32 read_idx;
	__u32 write_idx;
	__u32 count;
	__u32 reserved;
};

struct vdec_vp_mode_buf_info {
	__u8 enable_smmu;
	__u8 alloc_src_buf[2];
	__u8 reserved; // 32 bit align padding for cross compiler safe
	__u64 src_buf[2][3]; // [0] for 8 bit, [1] for 10 bit, [3] = {y dat, c dat, len}
};

/**
 * struct vdec_vsi - shared memory for decode information exchange
 *                        between VCU and Host.
 *                        The memory is allocated by VCU and mapping to Host
 *                        in vcu_dec_init()
 * @list_free_bs: free bitstream buffer ring list
 * @list_free   : free frame buffer ring list
 * @list_disp   : display frame buffer ring list
 * @dec         : decode information
 * @pic         : picture information
 * @color_desc  : color description
 * @crop        : crop information
 * @video_formats        : codec supported format info
 * @vdec_framesizes    : codec supported resolution info
 * @input_driven: whether the daemon is driving input buffers
 * @general_buf_fd/dma/size: general (metadata) buffer
 *
 * op6893 bring-up: this is byte for byte the 4.19 vendor layout, because the
 * peer -- the 4.19 /vendor/bin/vpud daemon -- allocated this block and reads it
 * at fixed offsets.  It is *not* merely a struct in kernel memory: vpud hands
 * back the DMEM address in the INIT ack, vcu_dec_init() maps it, and from then
 * on every field the kernel touches must land where the daemon looks.
 *
 * Measured with pahole against the 4.19 vmlinux built from this device's
 * vendor tree:
 *
 *	list_free_bs      0     528
 *	list_free       528    3088      (vdec_ipi_fb[64], 48-byte elements)
 *	list_disp      3616    3088
 *	dec            6704     200
 *	pic            6904      60
 *	color_desc     6964      68
 *	crop           7032      16
 *	video_formats  7048    1536
 *	vdec_framesizes 8584   4608
 *	aspect_ratio  13192  ... general_buf_size 13744
 *	sizeof(struct vdec_vsi) = 13752
 *
 * The daemon's own instance block continues past 13752 with a 144-byte private
 * tail (it reads its own state at 0x35b8..0x3648), so nothing may be appended
 * here.  This tree's extra per-instance state lives in struct vdec_inst
 * instead -- see vdec_vsi_priv there.
 *
 * Getting this wrong is silent and total: the daemon would read `dec.bs_fd' as
 * zero (it reads offset 0x1a40; this tree used to write `dec' at 3712), then
 * ion_import() on fd 0 fails and every AP_IPIMSG_DEC_START is answered with
 * status -1.
 */
struct vdec_vsi {
	struct ring_bs_list list_free_bs;
	struct ring_fb_list list_free;
	struct ring_fb_list list_disp;
	struct vdec_dec_info dec;
	struct vdec_pic_info pic;
	struct mtk_color_desc color_desc;
	struct v4l2_rect crop;
	struct mtk_video_fmt video_formats[MTK_MAX_DEC_CODECS_SUPPORT];
	struct mtk_codec_framesizes vdec_framesizes[MTK_MAX_DEC_CODECS_SUPPORT];
	__u32 aspect_ratio;
	__u32 fix_buffers;
	__u32 fix_buffers_svp;
	__u32 interlacing;
	__u32 codec_type;
	__u8 crc_path[256];
	__u8 golden_path[256];
	__u8 input_driven;
	__s32 general_buf_fd;
	__u64 general_buf_dma;
	__u32 general_buf_size;
};

#define VDEC_VSI_4_19_SIZE	13752
static_assert(sizeof(struct vdec_vsi) == VDEC_VSI_4_19_SIZE,
	      "vdec_vsi must stay the size the 4.19 vpud maps");

/*
 * 6.6-only per-instance decoder state.
 *
 * None of this exists in the 4.19 daemon's view of the world, and none of it
 * may live in struct vdec_vsi: everything past 13752 is the daemon's private
 * tail.  It is kept here, inside kernel memory, and reached as `inst->priv.X'.
 */

/* Slots in struct vdec_vsi_priv.published_fds. */
#define VDEC_PUB_BS		0
#define VDEC_PUB_FB(plane)	(1 + (plane))
#define VDEC_PUB_GENERAL	(VIDEO_MAX_PLANES + 1)
#define VDEC_PUB_COUNT		(VIDEO_MAX_PLANES + 2)

struct vdec_vsi_priv {
	struct mtk_dec_params dec_params;
	struct v4l2_fract time_per_frame;
	__u64 meta_buf_dma;
	__s32 meta_buf_fd;
	__u32 meta_buf_size;
	__u32 ipi_blocked;
	__u32 interlacing_fieldseq;
	struct hdr10plus_info hdr10plus_buf;
	struct v4l2_vdec_hdr10_info hdr10_info;
	__u32 error_code[MTK_VDEC_HW_NUM];
	__u64 bs_non_acp_dma;
	__u8 output_async;
	__u8 low_pw_mode;
	__u8 in_group;
	__u8 cpu_hint;
	__u8 hdr10_info_valid;
	__u8 trick_mode;
	__u8 flush_type;
	__u8 pic_field;
	__u32 ctx_id;
	__s32 op_rate;
	__s32 op_rate_adaptive;
	__s32 priority;
	__u32 codec_fmt;
	__s32 target_freq;
	__u32 is_active;
	struct vdec_resource_info res_info;
	struct vdec_bandwidth_info bandwidth_info;
	/*
	 * Synthetic descriptors currently published for the daemon: [0] for the
	 * bitstream, [1 .. VIDEO_MAX_PLANES] for frame plane i - 1, and
	 * [VDEC_PUB_GENERAL] for the general (metadata) buffer.  Zero means
	 * "none", which is unambiguous because a published number is never
	 * zero.  See mtk_ion_publish_dmabuf().
	 */
	int published_fds[VDEC_PUB_COUNT];
};

struct vdec_common_vsi {
	struct mtk_tf_info tf_info;
	struct vdec_vp_mode_buf_info vp_mode_info;
};

#endif

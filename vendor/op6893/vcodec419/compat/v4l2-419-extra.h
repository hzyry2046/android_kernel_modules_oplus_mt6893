/* SPDX-License-Identifier: GPL-2.0 */
/* Verbatim 4.19 MTK-private V4L2 controls and pixel formats, for the forward
 * port of the 4.19 mt6885 vcodec.  Everything here is #ifndef-guarded so it can
 * never conflict with the 6.6 headers. */
#ifndef _COMPAT419_V4L2_EXTRA_H
#define _COMPAT419_V4L2_EXTRA_H

/* 6.6 hides the MPEG->CODEC aliases from kernel code (#ifndef __KERNEL__). */
#ifndef V4L2_CTRL_CLASS_MPEG
#define V4L2_CTRL_CLASS_MPEG V4L2_CTRL_CLASS_CODEC
#define V4L2_CID_MPEG_CLASS V4L2_CID_CODEC_CLASS
#define V4L2_CID_MPEG_BASE V4L2_CID_CODEC_BASE
#endif

/* VFL_TYPE_GRABBER became VFL_TYPE_VIDEO in 5.10; same value. */
#ifndef VFL_TYPE_GRABBER
#define VFL_TYPE_GRABBER VFL_TYPE_VIDEO
#endif

#ifndef V4L2_PIX_FMT_RGB332
#define V4L2_PIX_FMT_RGB332  v4l2_fourcc('R', 'G', 'B', '1') /*  8  RGB-3-3-2     */
#endif
#ifndef V4L2_PIX_FMT_RGB444
#define V4L2_PIX_FMT_RGB444  v4l2_fourcc('R', '4', '4', '4') /* 16  xxxxrrrr ggggbbbb */
#endif
#ifndef V4L2_PIX_FMT_ARGB444
#define V4L2_PIX_FMT_ARGB444 v4l2_fourcc('A', 'R', '1', '2') /* 16  aaaarrrr ggggbbbb */
#endif
#ifndef V4L2_PIX_FMT_XRGB444
#define V4L2_PIX_FMT_XRGB444 v4l2_fourcc('X', 'R', '1', '2') /* 16  xxxxrrrr ggggbbbb */
#endif
#ifndef V4L2_PIX_FMT_RGB555
#define V4L2_PIX_FMT_RGB555  v4l2_fourcc('R', 'G', 'B', 'O') /* 16  RGB-5-5-5     */
#endif
#ifndef V4L2_PIX_FMT_ARGB555
#define V4L2_PIX_FMT_ARGB555 v4l2_fourcc('A', 'R', '1', '5') /* 16  ARGB-1-5-5-5  */
#endif
#ifndef V4L2_PIX_FMT_XRGB555
#define V4L2_PIX_FMT_XRGB555 v4l2_fourcc('X', 'R', '1', '5') /* 16  XRGB-1-5-5-5  */
#endif
#ifndef V4L2_PIX_FMT_RGB565
#define V4L2_PIX_FMT_RGB565  v4l2_fourcc('R', 'G', 'B', 'P') /* 16  RGB-5-6-5     */
#endif
#ifndef V4L2_PIX_FMT_RGB555X
#define V4L2_PIX_FMT_RGB555X v4l2_fourcc('R', 'G', 'B', 'Q') /* 16  RGB-5-5-5 BE  */
#endif
#ifndef V4L2_PIX_FMT_ARGB555X
#define V4L2_PIX_FMT_ARGB555X v4l2_fourcc_be('A', 'R', '1', '5') /* 16  ARGB-5-5-5 BE */
#endif
#ifndef V4L2_PIX_FMT_XRGB555X
#define V4L2_PIX_FMT_XRGB555X v4l2_fourcc_be('X', 'R', '1', '5') /* 16  XRGB-5-5-5 BE */
#endif
#ifndef V4L2_PIX_FMT_RGB565X
#define V4L2_PIX_FMT_RGB565X v4l2_fourcc('R', 'G', 'B', 'R') /* 16  RGB-5-6-5 BE  */
#endif
#ifndef V4L2_PIX_FMT_BGR666
#define V4L2_PIX_FMT_BGR666  v4l2_fourcc('B', 'G', 'R', 'H') /* 18  BGR-6-6-6	  */
#endif
#ifndef V4L2_PIX_FMT_BGR24
#define V4L2_PIX_FMT_BGR24   v4l2_fourcc('B', 'G', 'R', '3') /* 24  BGR-8-8-8     */
#endif
#ifndef V4L2_PIX_FMT_RGB24
#define V4L2_PIX_FMT_RGB24   v4l2_fourcc('R', 'G', 'B', '3') /* 24  RGB-8-8-8     */
#endif
#ifndef V4L2_PIX_FMT_BGR32
#define V4L2_PIX_FMT_BGR32   v4l2_fourcc('B', 'G', 'R', '4') /* 32  BGR-8-8-8-8   */
#endif
#ifndef V4L2_PIX_FMT_ABGR32
#define V4L2_PIX_FMT_ABGR32  v4l2_fourcc('A', 'R', '2', '4') /* 32  BGRA-8-8-8-8  */
#endif
#ifndef V4L2_PIX_FMT_XBGR32
#define V4L2_PIX_FMT_XBGR32  v4l2_fourcc('X', 'R', '2', '4') /* 32  BGRX-8-8-8-8  */
#endif
#ifndef V4L2_PIX_FMT_RGB32
#define V4L2_PIX_FMT_RGB32   v4l2_fourcc('R', 'G', 'B', '4') /* 32  RGB-8-8-8-8   */
#endif
#ifndef V4L2_PIX_FMT_ARGB32
#define V4L2_PIX_FMT_ARGB32  v4l2_fourcc('B', 'A', '2', '4') /* 32  ARGB-8-8-8-8  */
#endif
#ifndef V4L2_PIX_FMT_XRGB32
#define V4L2_PIX_FMT_XRGB32  v4l2_fourcc('B', 'X', '2', '4') /* 32  XRGB-8-8-8-8  */
#endif
#ifndef V4L2_PIX_FMT_ARGB1010102
#define V4L2_PIX_FMT_ARGB1010102  v4l2_fourcc('A', 'B', '3', '0')
#endif
#ifndef V4L2_PIX_FMT_ABGR1010102
#define V4L2_PIX_FMT_ABGR1010102  v4l2_fourcc('A', 'R', '3', '0')
#endif
#ifndef V4L2_PIX_FMT_RGBA1010102
#define V4L2_PIX_FMT_RGBA1010102  v4l2_fourcc('R', 'A', '3', '0')
#endif
#ifndef V4L2_PIX_FMT_BGRA1010102
#define V4L2_PIX_FMT_BGRA1010102  v4l2_fourcc('B', 'A', '3', '0')
#endif
#ifndef V4L2_PIX_FMT_NV12
#define V4L2_PIX_FMT_NV12    v4l2_fourcc('N', 'V', '1', '2') /* 12  Y/CbCr 4:2:0  */
#endif
#ifndef V4L2_PIX_FMT_NV12_512
#define V4L2_PIX_FMT_NV12_512         v4l2_fourcc('Q', '5', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_NV12_UBWC
#define V4L2_PIX_FMT_NV12_UBWC        v4l2_fourcc('Q', '1', '2', '8')
#endif
#ifndef V4L2_PIX_FMT_NV12_TP10_UBWC
#define V4L2_PIX_FMT_NV12_TP10_UBWC   v4l2_fourcc('Q', '1', '2', 'A')
#endif
#ifndef V4L2_PIX_FMT_NV12M
#define V4L2_PIX_FMT_NV12M   v4l2_fourcc('N', 'M', '1', '2') /* 12  Y/CbCr 4:2:0  */
#endif
#ifndef V4L2_PIX_FMT_NV12MT
#define V4L2_PIX_FMT_NV12MT  v4l2_fourcc('T', 'M', '1', '2') /* 12  Y/CbCr 4:2:0 64x32 macroblocks */
#endif
#ifndef V4L2_PIX_FMT_NV12MT_16X16
#define V4L2_PIX_FMT_NV12MT_16X16 v4l2_fourcc('V', 'M', '1', '2') /* 12  Y/CbCr 4:2:0 16x16 macroblocks */
#endif
#ifndef V4L2_PIX_FMT_H265
#define V4L2_PIX_FMT_H265     v4l2_fourcc('H', '2', '6', '5')
#endif
#ifndef V4L2_PIX_FMT_HEIF
#define V4L2_PIX_FMT_HEIF     v4l2_fourcc('H', 'E', 'I', 'F') /* HEIF */
#endif
#ifndef V4L2_PIX_FMT_H263
#define V4L2_PIX_FMT_H263     v4l2_fourcc('H', '2', '6', '3') /* H263 */
#endif
#ifndef V4L2_PIX_FMT_MPEG1
#define V4L2_PIX_FMT_MPEG1    v4l2_fourcc('M', 'P', 'G', '1') /* MPEG-1 ES */
#endif
#ifndef V4L2_PIX_FMT_MPEG2
#define V4L2_PIX_FMT_MPEG2    v4l2_fourcc('M', 'P', 'G', '2') /* MPEG-2 ES */
#endif
#ifndef V4L2_PIX_FMT_MPEG4
#define V4L2_PIX_FMT_MPEG4    v4l2_fourcc('M', 'P', 'G', '4') /* MPEG-4 part 2 ES */
#endif
#ifndef V4L2_PIX_FMT_WMV1
#define V4L2_PIX_FMT_WMV1      v4l2_fourcc('W', 'M', 'V', '1') /* WMV7 */
#endif
#ifndef V4L2_PIX_FMT_WMV2
#define V4L2_PIX_FMT_WMV2      v4l2_fourcc('W', 'M', 'V', '2') /* WMV8 */
#endif
#ifndef V4L2_PIX_FMT_WMV3
#define V4L2_PIX_FMT_WMV3      v4l2_fourcc('W', 'M', 'V', '3') /* WMV9 */
#endif
#ifndef V4L2_PIX_FMT_WMVA
#define V4L2_PIX_FMT_WMVA      v4l2_fourcc('W', 'M', 'V', 'A') /* WMVA */
#endif
#ifndef V4L2_PIX_FMT_WVC1
#define V4L2_PIX_FMT_WVC1      v4l2_fourcc('W', 'V', 'C', '1') /* VC1 */
#endif
#ifndef V4L2_PIX_FMT_RV30
#define V4L2_PIX_FMT_RV30      v4l2_fourcc('R', 'V', '3', '0') /* RealVideo 8 */
#endif
#ifndef V4L2_PIX_FMT_RV40
#define V4L2_PIX_FMT_RV40     v4l2_fourcc('R', 'V', '4', '0')
#endif
#ifndef V4L2_PIX_FMT_AV1
#define V4L2_PIX_FMT_AV1      v4l2_fourcc('A', 'V', '1', '0') /* AV1 */
#endif
#ifndef V4L2_PIX_FMT_MT21
#define V4L2_PIX_FMT_MT21    v4l2_fourcc('M', 'M', '2', '1')
#endif
#ifndef V4L2_PIX_FMT_MT2110T
#define V4L2_PIX_FMT_MT2110T    v4l2_fourcc('M', 'T', '2', 'T')
#endif
#ifndef V4L2_PIX_FMT_MT2110R
#define V4L2_PIX_FMT_MT2110R    v4l2_fourcc('M', 'T', '2', 'R')
#endif
#ifndef V4L2_PIX_FMT_MT21C10T
#define V4L2_PIX_FMT_MT21C10T    v4l2_fourcc('M', 'T', 'C', 'T')
#endif
#ifndef V4L2_PIX_FMT_MT21C10R
#define V4L2_PIX_FMT_MT21C10R    v4l2_fourcc('M', 'T', 'C', 'R')
#endif
#ifndef V4L2_PIX_FMT_MT21CS
#define V4L2_PIX_FMT_MT21CS    v4l2_fourcc('M', '2', 'C', 'S')
#endif
#ifndef V4L2_PIX_FMT_MT21S
#define V4L2_PIX_FMT_MT21S    v4l2_fourcc('M', '2', '1', 'S')
#endif
#ifndef V4L2_PIX_FMT_MT21S10T
#define V4L2_PIX_FMT_MT21S10T    v4l2_fourcc('M', 'T', 'S', 'T')
#endif
#ifndef V4L2_PIX_FMT_MT21S10R
#define V4L2_PIX_FMT_MT21S10R    v4l2_fourcc('M', 'T', 'S', 'R')
#endif
#ifndef V4L2_PIX_FMT_MT21CS10T
#define V4L2_PIX_FMT_MT21CS10T    v4l2_fourcc('M', 'C', 'S', 'T')
#endif
#ifndef V4L2_PIX_FMT_MT21CS10R
#define V4L2_PIX_FMT_MT21CS10R    v4l2_fourcc('M', 'C', 'S', 'R')
#endif
#ifndef V4L2_PIX_FMT_MT21CSA
#define V4L2_PIX_FMT_MT21CSA    v4l2_fourcc('M', 'A', 'C', 'S')
#endif
#ifndef V4L2_PIX_FMT_MT21S10TJ
#define V4L2_PIX_FMT_MT21S10TJ    v4l2_fourcc('M', 'J', 'S', 'T')
#endif
#ifndef V4L2_PIX_FMT_MT21S10RJ
#define V4L2_PIX_FMT_MT21S10RJ    v4l2_fourcc('M', 'J', 'S', 'R')
#endif
#ifndef V4L2_PIX_FMT_MT21CS10TJ
#define V4L2_PIX_FMT_MT21CS10TJ    v4l2_fourcc('J', 'C', 'S', 'T')
#endif
#ifndef V4L2_PIX_FMT_MT21CS10RJ
#define V4L2_PIX_FMT_MT21CS10RJ    v4l2_fourcc('J', 'C', 'S', 'R')
#endif
#ifndef V4L2_PIX_FMT_MT21C
#define V4L2_PIX_FMT_MT21C    v4l2_fourcc('M', 'T', '2', '1') /* Mediatek compressed block mode  */
#endif
#ifndef V4L2_PIX_FMT_RGB32_AFBC
#define V4L2_PIX_FMT_RGB32_AFBC         v4l2_fourcc('M', 'C', 'R', '8')
#endif
#ifndef V4L2_PIX_FMT_BGR32_AFBC
#define V4L2_PIX_FMT_BGR32_AFBC         v4l2_fourcc('M', 'C', 'B', '8')
#endif
#ifndef V4L2_PIX_FMT_RGBA1010102_AFBC
#define V4L2_PIX_FMT_RGBA1010102_AFBC   v4l2_fourcc('M', 'C', 'R', 'X')
#endif
#ifndef V4L2_PIX_FMT_BGRA1010102_AFBC
#define V4L2_PIX_FMT_BGRA1010102_AFBC   v4l2_fourcc('M', 'C', 'B', 'X')
#endif
#ifndef V4L2_PIX_FMT_NV12_AFBC
#define V4L2_PIX_FMT_NV12_AFBC          v4l2_fourcc('M', 'C', 'N', '8')
#endif
#ifndef V4L2_PIX_FMT_NV12_10B_AFBC
#define V4L2_PIX_FMT_NV12_10B_AFBC      v4l2_fourcc('M', 'C', 'N', 'X')
#endif

/* Mediatek control IDs */
#ifndef V4L2_CID_MPEG_MTK_BASE
#define V4L2_CID_MPEG_MTK_BASE (V4L2_CTRL_CLASS_MPEG | 0x2000)
#endif
#ifndef V4L2_CID_MPEG_MTK_FRAME_INTERVAL
#define V4L2_CID_MPEG_MTK_FRAME_INTERVAL (V4L2_CID_MPEG_MTK_BASE+0)
#endif
#ifndef V4L2_CID_MPEG_MTK_ERRORMB_MAP
#define V4L2_CID_MPEG_MTK_ERRORMB_MAP (V4L2_CID_MPEG_MTK_BASE+1)
#endif
#ifndef V4L2_CID_MPEG_MTK_DECODE_MODE
#define V4L2_CID_MPEG_MTK_DECODE_MODE (V4L2_CID_MPEG_MTK_BASE+2)
#endif
#ifndef V4L2_CID_MPEG_MTK_FRAME_SIZE
#define V4L2_CID_MPEG_MTK_FRAME_SIZE (V4L2_CID_MPEG_MTK_BASE+3)
#endif
#ifndef V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER
#define V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER (V4L2_CID_MPEG_MTK_BASE+4)
#endif
#ifndef V4L2_CID_MPEG_MTK_CRC_PATH
#define V4L2_CID_MPEG_MTK_CRC_PATH (V4L2_CID_MPEG_MTK_BASE+5)
#endif
#ifndef V4L2_CID_MPEG_MTK_GOLDEN_PATH
#define V4L2_CID_MPEG_MTK_GOLDEN_PATH (V4L2_CID_MPEG_MTK_BASE+6)
#endif
#ifndef V4L2_CID_MPEG_MTK_COLOR_DESC
#define V4L2_CID_MPEG_MTK_COLOR_DESC (V4L2_CID_MPEG_MTK_BASE+7)
#endif
#ifndef V4L2_CID_MPEG_MTK_ASPECT_RATIO
#define V4L2_CID_MPEG_MTK_ASPECT_RATIO (V4L2_CID_MPEG_MTK_BASE+8)
#endif
#ifndef V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME
#define V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME (V4L2_CID_MPEG_MTK_BASE+9)
#endif
#ifndef V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH
#define V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH (V4L2_CID_MPEG_MTK_BASE+10)
#endif
#ifndef V4L2_CID_MPEG_MTK_SEC_DECODE
#define V4L2_CID_MPEG_MTK_SEC_DECODE (V4L2_CID_MPEG_MTK_BASE+11)
#endif
#ifndef V4L2_CID_MPEG_MTK_FIX_BUFFERS
#define V4L2_CID_MPEG_MTK_FIX_BUFFERS (V4L2_CID_MPEG_MTK_BASE+12)
#endif
#ifndef V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP
#define V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP (V4L2_CID_MPEG_MTK_BASE+13)
#endif
#ifndef V4L2_CID_MPEG_MTK_INTERLACING
#define V4L2_CID_MPEG_MTK_INTERLACING (V4L2_CID_MPEG_MTK_BASE+14)
#endif
#ifndef V4L2_CID_MPEG_MTK_CODEC_TYPE
#define V4L2_CID_MPEG_MTK_CODEC_TYPE (V4L2_CID_MPEG_MTK_BASE+15)
#endif
#ifndef V4L2_CID_MPEG_MTK_OPERATING_RATE
#define V4L2_CID_MPEG_MTK_OPERATING_RATE (V4L2_CID_MPEG_MTK_BASE+16)
#endif
#ifndef V4L2_CID_MPEG_MTK_SEC_ENCODE
#define V4L2_CID_MPEG_MTK_SEC_ENCODE (V4L2_CID_MPEG_MTK_BASE+17)
#endif
#ifndef V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT
#define V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT (V4L2_CID_MPEG_MTK_BASE+18)
#endif
#ifndef V4L2_CID_MPEG_MTK_UFO_MODE
#define V4L2_CID_MPEG_MTK_UFO_MODE (V4L2_CID_MPEG_MTK_BASE+19)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_SCENARIO
#define V4L2_CID_MPEG_MTK_ENCODE_SCENARIO (V4L2_CID_MPEG_MTK_BASE+20)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_NONREFP
#define V4L2_CID_MPEG_MTK_ENCODE_NONREFP (V4L2_CID_MPEG_MTK_BASE+21)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE
#define V4L2_CID_MPEG_MTK_ENCODE_DETECTED_FRAMERATE (V4L2_CID_MPEG_MTK_BASE+22)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RFS_ON
#define V4L2_CID_MPEG_MTK_ENCODE_RFS_ON (V4L2_CID_MPEG_MTK_BASE+23)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE
#define V4L2_CID_MPEG_MTK_ENCODE_OPERATION_RATE (V4L2_CID_MPEG_MTK_BASE+24)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP
#define V4L2_CID_MPEG_MTK_ENCODE_ROI_RC_QP (V4L2_CID_MPEG_MTK_BASE+25)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_ROI_ON
#define V4L2_CID_MPEG_MTK_ENCODE_ROI_ON (V4L2_CID_MPEG_MTK_BASE+26)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE
#define V4L2_CID_MPEG_MTK_ENCODE_GRID_SIZE (V4L2_CID_MPEG_MTK_BASE+27)
#endif
#ifndef V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE
#define V4L2_CID_MPEG_MTK_RESOLUTION_CHANGE (V4L2_CID_MPEG_MTK_BASE+28)
#endif
#ifndef V4L2_CID_MPEG_MTK_MAX_WIDTH
#define V4L2_CID_MPEG_MTK_MAX_WIDTH (V4L2_CID_MPEG_MTK_BASE+29)
#endif
#ifndef V4L2_CID_MPEG_MTK_MAX_HEIGHT
#define V4L2_CID_MPEG_MTK_MAX_HEIGHT (V4L2_CID_MPEG_MTK_BASE+30)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP
#define V4L2_CID_MPEG_MTK_ENCODE_RC_I_FRAME_QP (V4L2_CID_MPEG_MTK_BASE+31)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP
#define V4L2_CID_MPEG_MTK_ENCODE_RC_P_FRAME_QP (V4L2_CID_MPEG_MTK_BASE+32)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP
#define V4L2_CID_MPEG_MTK_ENCODE_RC_B_FRAME_QP (V4L2_CID_MPEG_MTK_BASE+33)
#endif

#ifndef V4L2_CID_MPEG_VIDEO_ENABLE_TSVC
#define V4L2_CID_MPEG_VIDEO_ENABLE_TSVC (V4L2_CID_MPEG_MTK_BASE+34)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ
#define V4L2_CID_MPEG_MTK_ENCODE_NONREFP_FREQ (V4L2_CID_MPEG_MTK_BASE+35)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP
#define V4L2_CID_MPEG_MTK_ENCODE_RC_MAX_QP (V4L2_CID_MPEG_MTK_BASE+36)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP
#define V4L2_CID_MPEG_MTK_ENCODE_RC_MIN_QP (V4L2_CID_MPEG_MTK_BASE+37)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA
#define V4L2_CID_MPEG_MTK_ENCODE_RC_I_P_QP_DELTA (V4L2_CID_MPEG_MTK_BASE+38)
#endif
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE
#define V4L2_CID_MPEG_MTK_ENCODE_RC_QP_CONTROL_MODE (V4L2_CID_MPEG_MTK_BASE+39)
#endif
enum v4l2_mpeg_video_qp_control_mode {
	V4L2_MPEG_VIDEO_QP_CONTROL_MODE_DEF     = 0,
	V4L2_MPEG_VIDEO_QP_CONTROL_MODE_FRAME   = 1,
	V4L2_MPEG_VIDEO_QP_CONTROL_MODE_MB      = 2,
};
#ifndef V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP
#define V4L2_CID_MPEG_MTK_ENCODE_RC_FRAME_LEVEL_QP (V4L2_CID_MPEG_MTK_BASE+40)
#endif

#ifndef V4L2_CID_MPEG_MTK_ENCODE_MAX_REFP_NUM
#define V4L2_CID_MPEG_MTK_ENCODE_MAX_REFP_NUM (V4L2_CID_MPEG_MTK_BASE+41)
#endif

#ifndef V4L2_CID_MPEG_MTK_ENCODE_REFP_DISTANCE
#define V4L2_CID_MPEG_MTK_ENCODE_REFP_DISTANCE (V4L2_CID_MPEG_MTK_BASE+42)
#endif

#ifndef V4L2_CID_MPEG_MTK_ENCODE_REFP_MAX_FRAME_NUM
#define V4L2_CID_MPEG_MTK_ENCODE_REFP_MAX_FRAME_NUM (V4L2_CID_MPEG_MTK_BASE+43)
#endif

#ifndef V4L2_CID_MPEG_MTK_ENCODE_REFP_FRAME_NUM
#define V4L2_CID_MPEG_MTK_ENCODE_REFP_FRAME_NUM (V4L2_CID_MPEG_MTK_BASE+44)
#endif

#ifndef V4L2_CID_MPEG_MTK_LOG
#define V4L2_CID_MPEG_MTK_LOG (V4L2_CID_MPEG_MTK_BASE+46)
#endif

#ifndef V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL
#define V4L2_CID_MPEG_MTK_ENCODE_ENABLE_DUMMY_NAL (V4L2_CID_MPEG_MTK_BASE+47)
#endif

#ifndef V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY
#define V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY (V4L2_CID_MPEG_MTK_BASE+48)
#endif


/* 4.19-only fourccs, buffer flags and MTK events (absent from 6.6 uapi) */
#ifndef V4L2_PIX_FMT_P010M
#define V4L2_PIX_FMT_P010M   v4l2_fourcc('P', '0', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_MT10S
#define V4L2_PIX_FMT_MT10S     v4l2_fourcc('M', '1', '0', 'S')
#endif
#ifndef V4L2_PIX_FMT_MT10
#define V4L2_PIX_FMT_MT10     v4l2_fourcc('M', 'T', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_P010S
#define V4L2_PIX_FMT_P010S   v4l2_fourcc('P', '0', '1', 'S')
#endif
#ifndef V4L2_PIX_FMT_MTISP_B8
#define V4L2_PIX_FMT_MTISP_B8	v4l2_fourcc('M', 'T', 'B', '8') /* 8 bit */
#endif
#ifndef V4L2_PIX_FMT_SDE_Y_CBCR_H2V2_P010_VENUS
#define V4L2_PIX_FMT_SDE_Y_CBCR_H2V2_P010_VENUS v4l2_fourcc('Q', 'P', '1', '0') /* Y/CbCr 4:2:0 P10 Venus*/
#endif
#ifndef V4L2_BUF_FLAG_REF_FREED
#define V4L2_BUF_FLAG_REF_FREED			0x00000200
#endif
#ifndef V4L2_BUF_FLAG_CROP_CHANGED
#define V4L2_BUF_FLAG_CROP_CHANGED		0x00008000
#endif
#ifndef V4L2_BUF_FLAG_CSD
#define V4L2_BUF_FLAG_CSD			0x00200000
#endif
#ifndef V4L2_BUF_FLAG_ROI
#define V4L2_BUF_FLAG_ROI			0x00400000
#endif
#ifndef V4L2_BUF_FLAG_HDR_META
#define V4L2_BUF_FLAG_HDR_META			0x01000000
#endif
#ifndef V4L2_BUF_FLAG_QP_META
#define V4L2_BUF_FLAG_QP_META			0x02000000
#endif
#ifndef V4L2_EVENT_MTK_VCODEC_START
#define V4L2_EVENT_MTK_VCODEC_START	(V4L2_EVENT_PRIVATE_START + 0x00002000)
#endif
#ifndef V4L2_EVENT_MTK_VDEC_ERROR
#define V4L2_EVENT_MTK_VDEC_ERROR	(V4L2_EVENT_MTK_VCODEC_START + 1)
#endif
#ifndef V4L2_EVENT_MTK_VDEC_NOHEADER
#define V4L2_EVENT_MTK_VDEC_NOHEADER	(V4L2_EVENT_MTK_VCODEC_START + 2)
#endif
#ifndef V4L2_EVENT_MTK_VENC_ERROR
#define V4L2_EVENT_MTK_VENC_ERROR	(V4L2_EVENT_MTK_VCODEC_START + 3)
#endif

#endif /* _COMPAT419_V4L2_EXTRA_H */

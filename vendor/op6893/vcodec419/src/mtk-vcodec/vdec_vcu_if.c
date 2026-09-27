// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/fdtable.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <media/v4l2-mem2mem.h>
#include <uapi/linux/mtk_vcu_controls.h>
#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "vdec_ipi_msg.h"
#include "vdec_vcu_if.h"
#include "mtk_ion_compat.h"
#include "vdec_drv_if.h"
#include "smi_public.h"

static void handle_init_ack_msg(struct vdec_vcu_ipi_init_ack *msg)
{
	struct vdec_vcu_inst *vcu = (struct vdec_vcu_inst *)
		(unsigned long)msg->ap_inst_addr;

	if (vcu == NULL)
		return;
	mtk_vcodec_debug(vcu, "+ ap_inst_addr = 0x%lx",
		(uintptr_t)msg->ap_inst_addr);

	/* mapping VCU address to kernel virtual address */
	/* the content in vsi is initialized to 0 in VCU */
	vcu->vsi = vcu_mapping_dm_addr(vcu->dev, msg->vcu_inst_addr);
	vcu->inst_addr = msg->vcu_inst_addr;
	mtk_vcodec_debug(vcu, "- vcu_inst_addr = 0x%x", vcu->inst_addr);
}

static void handle_query_cap_ack_msg(struct vdec_vcu_ipi_query_cap_ack *msg)
{
	struct vdec_vcu_inst *vcu = (struct vdec_vcu_inst *)msg->ap_inst_addr;
	void *data;
	int size = 0;

	if (vcu == NULL)
		return;
	mtk_vcodec_debug(vcu, "+ ap_inst_addr = 0x%lx, vcu_data_addr = 0x%x, id = %d",
		(uintptr_t)msg->ap_inst_addr, msg->vcu_data_addr, msg->id);
	/* mapping VCU address to kernel virtual address */
	data = vcu_mapping_dm_addr(vcu->dev, msg->vcu_data_addr);
	if (data == NULL)
		return;
	switch (msg->id) {
	case GET_PARAM_CAPABILITY_SUPPORTED_FORMATS:
		size = sizeof(struct mtk_video_fmt);
		memcpy((void *)msg->ap_data_addr, data,
			 size * MTK_MAX_DEC_CODECS_SUPPORT);
		break;
	case GET_PARAM_CAPABILITY_FRAME_SIZES:
		size = sizeof(struct mtk_codec_framesizes);
		memcpy((void *)msg->ap_data_addr, data,
			size * MTK_MAX_DEC_CODECS_SUPPORT);
		break;
	default:
		break;
	}
	mtk_vcodec_debug(vcu, "- vcu_inst_addr = 0x%x", vcu->inst_addr);
}

/*
 * 6.6: the 4.19 body installed the dma-buf into vpud's own fd table from the
 * codec's context (get_files_struct/__alloc_fd/__fd_install), none of which is
 * exported any more.  Publish it through mtk_ion_compat instead: the number we
 * hand out cannot be a real descriptor, vpud's ION_IOC_IMPORT resolves it, and
 * the following ION_IOC_SHARE installs a real fd from vpud's own context.
 * Callers keep the 4.19 contract: publish -> send msg -> wait ack -> close.
 */
int get_mapped_fd(struct dma_buf *dmabuf)
{
	int fd;

	if (mtk_ion_publish_dmabuf(dmabuf, &fd))
		return -EMFILE;
	return fd;
}

/*
 * As get_mapped_fd(), and tell mtk_ion_compat our own device address for the
 * buffer so vpud's GET_IOVA gets that back: vpud frees the ION handle right
 * after GET_IOVA but keeps using the address (reference frames), so it must
 * stay valid for the buffer's life, not the handle's.
 */
int get_mapped_fd_iova(struct dma_buf *dmabuf, dma_addr_t iova)
{
	int fd;

	if (mtk_ion_publish_dmabuf_iova(dmabuf, iova, &fd))
		return -EMFILE;
	return fd;
}

int get_mapped_fd_s16_iova(struct dma_buf *dmabuf, dma_addr_t iova)
{
	int fd;

	if (mtk_ion_publish_dmabuf_s16_iova(dmabuf, iova, &fd))
		return -EMFILE;
	return fd;
}

/* For the encoder's __s16 descriptor fields (venc_ap_ipi_msg_enc). */
int get_mapped_fd_s16(struct dma_buf *dmabuf)
{
	int fd;

	if (mtk_ion_publish_dmabuf_s16(dmabuf, &fd))
		return -EMFILE;
	return fd;
}

void close_mapped_fd(unsigned int target_fd)
{
	mtk_ion_unpublish_dmabuf((int)target_fd);
}

/*
 * This function runs in interrupt context and it means there's a IPI MSG
 * from VCU.
 */
int vcu_dec_ipi_handler(void *data, unsigned int len, void *priv)
{
	struct vdec_vcu_ipi_ack *msg = data;
	struct vdec_vcu_inst *vcu = NULL;
	struct vdec_fb *pfb;
	struct timeval t_s, t_e;
	struct task_struct *task = NULL;
	struct files_struct *f = NULL;
	struct vdec_vsi *vsi;
	uint64_t vdec_fb_va;
	long timeout_jiff;
	int ret = 0;
	int i = 0;

	BUILD_BUG_ON(sizeof(struct vdec_ap_ipi_cmd) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct vdec_ap_ipi_init) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct vdec_ap_ipi_dec_start) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct vdec_ap_ipi_set_param) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct vdec_ap_ipi_query_cap) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct vdec_vcu_ipi_ack) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(sizeof(struct vdec_vcu_ipi_init_ack) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(
		sizeof(struct vdec_vcu_ipi_query_cap_ack) > SHARE_BUF_SIZE);
	BUILD_BUG_ON(
		sizeof(struct vdec_vcu_ipi_query_cap_ack) > SHARE_BUF_SIZE);

	vcu_get_file_lock();
	vcu_get_task(&task, &f, 0);
	vcu_put_file_lock();
	if (msg == NULL || task == NULL ||
	   task->tgid != current->tgid ||
	   (struct vdec_vcu_inst *)msg->ap_inst_addr == NULL) {
		ret = -EINVAL;
		return ret;
	}

	vcu = (struct vdec_vcu_inst *)(unsigned long)msg->ap_inst_addr;
	if ((vcu != priv) && msg->msg_id < VCU_IPIMSG_DEC_WAITISR) {
		pr_info("%s, vcu:%p != priv:%p\n", __func__, vcu, priv);
		return 1;
	}

	if (vcu->daemon_pid != current->tgid) {
		pr_info("%s, vcu->daemon_pid:%d != current %d\n",
			__func__, vcu->daemon_pid, current->tgid);
		return 1;
	}

	vsi = (struct vdec_vsi *)vcu->vsi;
	mtk_vcodec_debug(vcu, "+ id=%X status = %d\n",
		msg->msg_id, msg->status);

	if (vcu->abort)
		return -EINVAL;

	if (msg->msg_id == VCU_IPIMSG_DEC_WAITISR) {
		/* wait decoder done interrupt */
		do_gettimeofday(&t_s);
		ret = mtk_vcodec_wait_for_done_ctx(vcu->ctx,
			msg->status,
			MTK_INST_IRQ_RECEIVED,
			WAIT_INTR_TIMEOUT_MS);
		do_gettimeofday(&t_e);
		mtk_vcodec_perf_log("irq:%ld",
			(t_e.tv_sec - t_s.tv_sec) * 1000000 +
			(t_e.tv_usec - t_s.tv_usec));
		//if (ret == -1 && msg->status == MTK_VDEC_CORE) {
			///* dump smi when vdec core timeout */
			//smi_debug_bus_hang_detect(0, "VCODEC");
		//}
		msg->status = ret;
		ret = 1;
	} else if (msg->status == 0) {
		switch (msg->msg_id) {
		case VCU_IPIMSG_DEC_INIT_ACK:
			handle_init_ack_msg(data);
			break;
		case VCU_IPIMSG_DEC_QUERY_CAP_ACK:
			handle_query_cap_ack_msg(data);
			break;
		case VCU_IPIMSG_DEC_START_ACK:
		case VCU_IPIMSG_DEC_END_ACK:
		case VCU_IPIMSG_DEC_DEINIT_ACK:
		case VCU_IPIMSG_DEC_RESET_ACK:
		case VCU_IPIMSG_DEC_SET_PARAM_ACK:
			break;
		case VCU_IPIMSG_DEC_LOCK_LAT:
			vdec_decode_prepare(vcu->ctx, MTK_VDEC_LAT);
			ret = 1;
			break;
		case VCU_IPIMSG_DEC_UNLOCK_LAT:
			vdec_decode_unprepare(vcu->ctx, MTK_VDEC_LAT);
			ret = 1;
			break;
		case VCU_IPIMSG_DEC_LOCK_CORE:
			vdec_decode_prepare(vcu->ctx, MTK_VDEC_CORE);
			ret = 1;
			break;
		case VCU_IPIMSG_DEC_UNLOCK_CORE:
			vdec_decode_unprepare(vcu->ctx, MTK_VDEC_CORE);
			ret = 1;
			break;
		case VCU_IPIMSG_DEC_GET_FRAME_BUFFER:
			mtk_vcodec_debug(vcu, "+ try get fm form rdy q size=%d\n",
				v4l2_m2m_num_dst_bufs_ready(vcu->ctx->m2m_ctx));

			pfb = mtk_vcodec_get_fb(vcu->ctx);
			timeout_jiff = msecs_to_jiffies(1000);
			/* 1s timeout */
			while (pfb == NULL) {
				ret = wait_event_interruptible_timeout(
					vcu->ctx->fm_wq,
					 v4l2_m2m_num_dst_bufs_ready(
						 vcu->ctx->m2m_ctx) > 0 ||
					 vcu->ctx->state == MTK_STATE_FLUSH,
					 timeout_jiff);
				pfb = mtk_vcodec_get_fb(vcu->ctx);
				if (vcu->ctx->state == MTK_STATE_FLUSH)
					mtk_vcodec_debug(vcu, "get fm fail: state == FLUSH (pfb=0x%p)\n",
						pfb);
				else if (ret == 0)
					mtk_vcodec_debug(vcu, "get fm fail: timeout (pfb=0x%p)\n",
						pfb);
				else if (pfb == NULL)
					mtk_vcodec_debug(vcu, "get fm fail: unknown (ret = %d)\n",
						ret);

				if (vcu->ctx->state == MTK_STATE_FLUSH ||
					ret != 0)
					break;
			}
			mtk_vcodec_debug(vcu, "- wait get fm pfb=0x%p\n", pfb);

			vdec_fb_va = (u64)pfb;
			vsi->dec.vdec_fb_va = vdec_fb_va;
			if (pfb != NULL) {
				vsi->dec.index = pfb->index;
				for (i = 0; i < pfb->num_planes; i++) {
					vsi->dec.fb_dma[i] = (u64)
						pfb->fb_base[i].dma_addr;
					vsi->dec.fb_fd[i] = (uint64_t)
						get_mapped_fd_iova(
							pfb->fb_base[i].dmabuf,
							pfb->fb_base[i].dma_addr);
					pfb->fb_base[i].buf_fd = (s64)
						vsi->dec.fb_fd[i];
					mtk_vcodec_debug(vcu, "+ vsi->dec.fb_fd[%d]:%llx\n",
						i, vsi->dec.fb_fd[i]);
				}
				if (pfb->dma_general_buf != 0) {
					vsi->general_buf_dma =
						pfb->dma_general_addr;
					pfb->general_buf_fd =
						(uint32_t)get_mapped_fd_iova(
							pfb->dma_general_buf,
							pfb->dma_general_addr);
					vsi->general_buf_fd =
						pfb->general_buf_fd;
					vsi->general_buf_size =
						pfb->dma_general_buf->size;
					mtk_vcodec_debug(vcu, "get_mapped_fd fb->dma_general_buf = %p, mapped fd = %d, size = %lu",
						pfb->dma_general_buf,
						vsi->general_buf_fd,
						pfb->dma_general_buf->size);
				} else {
					pfb->general_buf_fd = -1;
					vsi->general_buf_fd = -1;
					mtk_vcodec_debug(vcu, "no general buf dmabuf");
				}
			} else {
				vsi->dec.index = 0xFF;
				mtk_vcodec_err(vcu, "Cannot get frame buffer from V4l2\n");
			}
			mtk_vcodec_debug(vcu, "+ FB y_fd=%llx c_fd=%llx BS fd=0x%llx index:%d vdec_fb_va:%llx,dma_addr(%llx,%llx)",
				vsi->dec.fb_fd[0], vsi->dec.fb_fd[1],
				vsi->dec.bs_fd, vsi->dec.index,
				vsi->dec.vdec_fb_va,
				vsi->dec.fb_dma[0], vsi->dec.fb_dma[1]);

#if 0
			if (vcu->ctx->dec_params.svp_mode == 0 &&
				vcu->ctx->dev->dec_irq != 0) {
				ret = devm_request_irq(
					&vcu->ctx->dev->plat_dev->dev,
					vcu->ctx->dev->dec_irq,
					mtk_vcodec_dec_irq_handler, 0,
					vcu->ctx->dev->plat_dev->name,
					vcu->ctx->dev);
				vcu->ctx->dev->reqst_irq = true;
				mtk_vcodec_debug(vcu, "Requset irq:%d ok,rqst_irq:%d",
					vcu->ctx->dev->dec_irq,
					vcu->ctx->dev->reqst_irq);
				if (ret) {
					mtk_vcodec_err(vcu, "Failed to install dev->dec_irq %d (%d)",
					vcu->ctx->dev->dec_irq,
					ret);
					return -EINVAL;
				}
			}
#endif
			ret = 1;
			break;
		case VCU_IPIMSG_DEC_PUT_FRAME_BUFFER:
			mtk_vdec_put_fb(vcu->ctx, msg->msg_id);
			ret = 1;
			break;
		default:
			mtk_vcodec_err(vcu, "invalid msg=%X", msg->msg_id);
			ret = 1;
			break;
		}
	}

	mtk_vcodec_debug(vcu, "- id=%X", msg->msg_id);

	/* deinit ack timeout case handling do not touch vdec_vcu_inst
	 * or memory used after freed
	 */
	if (msg->msg_id != VCU_IPIMSG_DEC_DEINIT_ACK) {
		vcu->signaled = 1;
		vcu->failure = msg->status;
	}

	return ret;
}

static int vcodec_vcu_send_msg(struct vdec_vcu_inst *vcu, void *msg, int len)
{
	int err;
	struct task_struct *task = NULL;
	struct files_struct *f = NULL;

	mtk_vcodec_debug(vcu, "id=%X", *(uint32_t *)msg);
	if (vcu->abort)
		return -EIO;

	vcu_get_file_lock();
	vcu_get_task(&task, &f, 0);
	vcu_put_file_lock();
	if (task == NULL ||
		vcu->daemon_pid != task->tgid) {
		if (task)
			mtk_vcodec_err(vcu, "send fail pid: inst %d curr %d",
				vcu->daemon_pid, task->tgid);
		vcu->abort = 1;
		return -EIO;
	}

	vcu->failure = 0;
	vcu->signaled = 0;

	err = vcu_ipi_send(vcu->dev, vcu->id, msg, len, vcu);
	if (err) {
		mtk_vcodec_err(vcu, "send fail vcu_id=%d msg_id=%X status=%d",
					   vcu->id, *(uint32_t *)msg, err);
		if (err == -EIO)
			vcu->abort = 1;
		return err;
	}
	mtk_vcodec_debug(vcu, "- ret=%d", err);
	return vcu->failure;
}

static int vcodec_send_ap_ipi(struct vdec_vcu_inst *vcu, unsigned int msg_id)
{
	struct vdec_ap_ipi_cmd msg;
	int err = 0;

	mtk_vcodec_debug(vcu, "+ id=%X", msg_id);

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = msg_id;
	msg.vcu_inst_addr = vcu->inst_addr;

	err = vcodec_vcu_send_msg(vcu, &msg, sizeof(msg));
	mtk_vcodec_debug(vcu, "- id=%X ret=%d", msg_id, err);
	return err;
}

void vcu_dec_set_pid(struct vdec_vcu_inst *vcu)
{
	struct task_struct *task = NULL;
	struct files_struct *f = NULL;

	vcu_get_file_lock();
	vcu_get_task(&task, &f, 0);
	vcu_put_file_lock();
	if (task != NULL)
		vcu->daemon_pid = task->tgid;
	else
		vcu->daemon_pid = -1;
}

int vcu_dec_set_ctx(struct vdec_vcu_inst *vcu,
	struct mtk_vcodec_mem *bs,
	struct vdec_fb *fb)
{
	int err = 0;
	struct vb2_buffer *src_vb = NULL;
	struct vb2_buffer *dst_vb = NULL;
	struct mtk_video_dec_buf *temp;

	if (bs != NULL) {
		temp = container_of(bs, struct mtk_video_dec_buf, bs_buffer);
		src_vb = &temp->vb.vb2_buf;
	}
	if (fb != NULL) {
		temp = container_of(fb, struct mtk_video_dec_buf, frame_buffer);
		dst_vb = &temp->vb.vb2_buf;
	}
	vcu_set_codec_ctx(vcu->dev,
		(void *)vcu->ctx,
		src_vb, dst_vb,
		VCU_VDEC);

	return err;
}

int vcu_dec_clear_ctx(struct vdec_vcu_inst *vcu)
{
	int err = 0;

	vcu_clear_codec_ctx(vcu->dev,
		(void *)vcu->ctx, VCU_VDEC);

	return err;
}

int vcu_dec_init(struct vdec_vcu_inst *vcu)
{
	struct vdec_ap_ipi_init msg;
	int err;

	mtk_vcodec_debug_enter(vcu);

	init_waitqueue_head(&vcu->wq);
	vcu->signaled = 0;
	vcu->failure = 0;
	vcu_get_ctx_ipi_binding_lock(vcu->dev, &vcu->ctx_ipi_binding, VCU_VDEC);

	err = vcu_ipi_register(vcu->dev, vcu->id, vcu->handler, NULL, vcu);
	if (err != 0) {
		mtk_vcodec_err(vcu, "vcu_ipi_register fail status=%d", err);
		return err;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_DEC_INIT;
	msg.ap_inst_addr = (unsigned long)vcu;

	if (vcu->ctx->dec_params.svp_mode)
		msg.reserved = vcu->ctx->dec_params.svp_mode;

	mtk_vcodec_debug(vcu, "vdec_inst=%p svp_mode=%d", vcu, msg.reserved);

	vcu_dec_set_pid(vcu);

	err = vcodec_vcu_send_msg(vcu, (void *)&msg, sizeof(msg));

	mtk_vcodec_debug(vcu, "- ret=%d", err);
	return err;
}

int vcu_dec_start(struct vdec_vcu_inst *vcu,
	unsigned int *data, unsigned int len,
	struct mtk_vcodec_mem *bs, struct vdec_fb *fb)
{
	struct vdec_ap_ipi_dec_start msg;
	int i;
	int err = 0;

	mtk_vcodec_debug_enter(vcu);

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_DEC_START;
	msg.vcu_inst_addr = vcu->inst_addr;

	for (i = 0; i < len; i++)
		msg.data[i] = data[i];

	mutex_lock(vcu->ctx_ipi_binding);
	vcu_dec_set_ctx(vcu, bs, fb);
	err = vcodec_vcu_send_msg(vcu, (void *)&msg, sizeof(msg));
	mutex_unlock(vcu->ctx_ipi_binding);

	mtk_vcodec_debug(vcu, "- ret=%d", err);
	return err;
}

int vcu_dec_end(struct vdec_vcu_inst *vcu)
{
	return vcodec_send_ap_ipi(vcu, AP_IPIMSG_DEC_END);
}

int vcu_dec_deinit(struct vdec_vcu_inst *vcu)
{
	int err = 0;

	mutex_lock(vcu->ctx_ipi_binding);
	err = vcodec_send_ap_ipi(vcu, AP_IPIMSG_DEC_DEINIT);
	vcu_dec_clear_ctx(vcu);
	mutex_unlock(vcu->ctx_ipi_binding);

	return err;
}

int vcu_dec_reset(struct vdec_vcu_inst *vcu)
{
	return vcodec_send_ap_ipi(vcu, AP_IPIMSG_DEC_RESET);
}

int vcu_dec_query_cap(struct vdec_vcu_inst *vcu, unsigned int id, void *out)
{
	struct vdec_ap_ipi_query_cap msg;
	int err = 0;

	mtk_vcodec_debug(vcu, "+ id=%X", AP_IPIMSG_DEC_QUERY_CAP);
	vcu->dev = vcu_get_plat_device(vcu->ctx->dev->plat_dev);
	if (vcu->dev  == NULL) {
		mtk_vcodec_err(vcu, "vcu device in not ready");
		return -EPROBE_DEFER;
	}

	vcu->id = (vcu->id == IPI_VCU_INIT) ? IPI_VDEC_COMMON : vcu->id;
	vcu->handler = vcu_dec_ipi_handler;

	err = vcu_ipi_register(vcu->dev, vcu->id, vcu->handler, NULL, vcu);
	if (err != 0) {
		mtk_vcodec_err(vcu, "vcu_ipi_register fail status=%d", err);
		return err;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_DEC_QUERY_CAP;
	msg.id = id;
	msg.ap_inst_addr = (uintptr_t)vcu;
	msg.ap_data_addr = (uintptr_t)out;

	vcu_dec_set_pid(vcu);
	err = vcodec_vcu_send_msg(vcu, &msg, sizeof(msg));
	mtk_vcodec_debug(vcu, "- id=%X ret=%d", msg.msg_id, err);
	return err;
}

int vcu_dec_set_param(struct vdec_vcu_inst *vcu, unsigned int id, void *param,
					  unsigned int size)
{
	struct vdec_ap_ipi_set_param msg;
	unsigned long *param_ptr = (unsigned long *)param;
	int err = 0;
	int i = 0;

	mtk_vcodec_debug(vcu, "+ id=%X", AP_IPIMSG_DEC_SET_PARAM);

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_DEC_SET_PARAM;
	msg.id = id;
	msg.vcu_inst_addr = vcu->inst_addr;
	for (i = 0; i < size; i++) {
		msg.data[i] = (__u32)(*(param_ptr + i));
		mtk_vcodec_debug(vcu, "msg.id = 0x%X, msg.data[%d]=%d",
			msg.id, i, msg.data[i]);
	}

	err = vcodec_vcu_send_msg(vcu, &msg, sizeof(msg));
	mtk_vcodec_debug(vcu, "- id=%X ret=%d", AP_IPIMSG_DEC_SET_PARAM, err);

	return err;
}


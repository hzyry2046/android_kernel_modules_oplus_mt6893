/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 MediaTek Inc.
 */


#ifndef __APUSYS_REVISER_PLAT_H__
#define __APUSYS_REVISER_PLAT_H__

struct reviser_plat {
	int (*init)(struct platform_device *pdev, void *rplat);
	int (*uninit)(struct platform_device *pdev);

	unsigned int bank_size;
	unsigned int mdla_max;
	unsigned int vpu_max;
	unsigned int edma_max;
	unsigned int up_max;
	unsigned int slb_wait_time;

	/*
	 * Fallbacks for DTs written before these became properties (the 4.19
	 * ABI has neither "boundary" nor "default-dram"): 4.19 took the
	 * per-SoC BOUNDARY_APUSYS constant and REMAP_DRAM_BASE instead.
	 * Zero means "the DT must carry it".
	 */
	unsigned int boundary_def;
	unsigned int default_dram_def;

	/* Migration */
	unsigned int vlm_remap_table_base;
	unsigned int vlm_CTX_base;
	unsigned int vlm_remap_ctx_src;
	unsigned int vlm_remap_ctx_src_ofst;
	unsigned int vlm_remap_ctx_dst;
	unsigned int vlm_remap_ctx_dst_ofst;
	unsigned int vlm_remap_tlb_src_max;
	unsigned int vlm_remap_tlb_dst_max;

	unsigned int mva_base;

	unsigned int MDLA_ctxt_0;
	unsigned int MDLA_ctxt_1;
	unsigned int VPU_ctxt_0;
	unsigned int VPU_ctxt_1;
	unsigned int VPU_ctxt_2;
	unsigned int EDMA_ctxt_0;
	unsigned int EDMA_ctxt_1;
};

int reviser_plat_init(struct platform_device *pdev);
int reviser_plat_uninit(struct platform_device *pdev);

int reviser_v1_0_init(struct platform_device *pdev, void *rplat);
int reviser_v1_0_uninit(struct platform_device *pdev);

int reviser_vrv_init(struct platform_device *pdev, void *rplat);
int reviser_vrv_uninit(struct platform_device *pdev);
#endif

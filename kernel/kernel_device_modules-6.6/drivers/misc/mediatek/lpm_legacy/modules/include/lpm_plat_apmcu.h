/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#ifndef __LPM_PLAT_APMCU_H__
#define __LPM_PLAT_APMCU_H__

#include <linux/types.h>

#define nr_cluster_ids                  (1)
#define get_physical_cluster_id(cpu)    (0)

#define LP_PM_SYSRAM_INFO_OFS  0x8
#define LP_PM_SYSRAM_SIZE      0x500

extern void __iomem *cpu_pm_syssram_base;
/* 0x11B000 */
#define MT_CPU_PM_KERNEL_SRAMBASE	(cpu_pm_syssram_base + 0x100)
#define MT_CPU_PM_KERNEL_SRAM(slot)	(MT_CPU_PM_KERNEL_SRAMBASE + (slot<<2))


#define cpu_pm_sync_writel(v, a)				\
do {								\
	__raw_writel((v), (void __force __iomem *)((a)));	\
	mb(); /*make sure register access in order */		\
} while (0)

#define MT_CPU_PM_KERNEL_SRAM_WR(slot, val) ({\
		unsigned int uval = 0;\
		if (cpu_pm_syssram_base)\
			cpu_pm_sync_writel(val,\
				MT_CPU_PM_KERNEL_SRAM(slot)); uval; })


#define MT_CPU_PM_KERNEL_SRAM_RD(slot) ({\
		unsigned int uval = 0;\
		if (cpu_pm_syssram_base)\
			uval = __raw_readl(MT_CPU_PM_KERNEL_SRAM(slot));\
		uval; })

void lpm_plat_set_mcusys_off(int cpu);
void lpm_plat_clr_mcusys_off(int cpu);

void lpm_plat_set_cluster_off(int cpu);
void lpm_plat_clr_cluster_off(int cpu);

bool lpm_plat_is_mcusys_off(void);
bool lpm_plat_is_cluster_off(int cpu);
/*
 * op6893 6.6 bring-up: the raw MCUSYS power-on count, for diagnostics only.
 * lpm_plat_is_mcusys_off() is just "this is 0", and when it never becomes 0
 * there is no way from outside to tell a count stuck high (nothing is
 * decrementing) from one that simply never reaches the floor (the eight cores
 * are never all in mcusysoff at the same instant).
 */
unsigned int lpm_plat_mcusys_pwr_cnt(void);

int lpm_plat_apmcu_init(void);
int lpm_plat_apmcu_early_init(void);

#endif

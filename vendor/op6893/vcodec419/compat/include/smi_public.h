/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 4.19 smi_public.h surface.  smi_bus_prepare_enable() is real: it resumes the
 * 6.6 mtk-smi larb device bound to the legacy "mediatek,smi_larb" node with the
 * matching mediatek,larb-id (see compat419_smi.c).  The larb resume is what
 * powers the larb and re-applies the per-port MMU enables set up by the IOMMU.
 */
#ifndef _C419_SMI_PUBLIC_H
#define _C419_SMI_PUBLIC_H

#include <linux/types.h>
#include "smi_port.h"

enum {
	SMI_LARB0, SMI_LARB1, SMI_LARB2, SMI_LARB3, SMI_LARB4,
	SMI_LARB5, SMI_LARB6, SMI_LARB7, SMI_LARB8, SMI_LARB9,
	SMI_LARB10, SMI_LARB11, SMI_LARB12, SMI_LARB13, SMI_LARB14,
	SMI_LARB15, SMI_LARB16, SMI_LARB17, SMI_LARB18, SMI_LARB19,
	SMI_LARB20,
};

s32 compat419_smi_bus_prepare_enable(u32 id, const char *user);
s32 compat419_smi_bus_disable_unprepare(u32 id, const char *user);

#define smi_bus_prepare_enable(id, user) compat419_smi_bus_prepare_enable(id, user)
#define smi_bus_disable_unprepare(id, user) compat419_smi_bus_disable_unprepare(id, user)
/* SLBC/SRAM is not used on this board; bus-hang dump is debug only. */
#define smi_sysram_enable(master_id, enable, user) (0)
#define smi_debug_bus_hang_detect(gce, user) (0)

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/* mt6885 smi_port.h from 4.19 (port counts), with the non-IOMMU_V2 MTK_M4U_ID. */
#ifndef _C419_SMI_PORT_H
#define _C419_SMI_PORT_H

#include <dt-bindings/memory/mt6885-larb-port.h>

#ifndef MTK_M4U_ID
#define MTK_M4U_ID(larb, port)	(((larb) << 5) | (port))
#endif

#define SMI_LARB_NUM		(21)
#define SMI_LARB4_PORT_NUM	(11)	/* SYS_VDE */
#define SMI_LARB5_PORT_NUM	(8)	/* SYS_VDE */
#define SMI_LARB7_PORT_NUM	(27)	/* SYS_VEN */
#define SMI_LARB8_PORT_NUM	(27)	/* SYS_VEN */

#endif

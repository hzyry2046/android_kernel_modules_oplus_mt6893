// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 *
 * MNOC QoS: explicitly not implemented.
 *
 * The middleware's scheduler calls apu_cmd_qos_start() before handing a command
 * to an engine and apu_cmd_qos_end() after it completes, to account bandwidth
 * and to program MNOC.  Neither the shared-buffer layout nor the DT binding of
 * the MT6893 MNOC QoS provider exists in this port: mnoc/1.0 carries only the
 * checked firmware layout/window helpers (mt6893_qos_snapshot.h), and enabling
 * the real provider also clears SRAM and sends initialization IPIs.
 *
 * So this is the vendor's own MNOC_QOS_ENABLE=0 shape: the scheduler proceeds,
 * no bandwidth is accounted, and nothing is programmed.  It is deliberately not
 * dressed up as QoS support -- apu_cmd_qos_end() returns 0 (zero bandwidth, not
 * an error) so a command is never failed because of a vote that was never made.
 * Do not read sc->bw from this build as a real bandwidth measurement.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#include "mnoc_api.h"

int apu_cmd_qos_start(uint64_t cmd_id, uint64_t sub_cmd_id,
	int dev_type, int dev_core, uint32_t boost_val)
{
	pr_info_once("apusys-legacy: MNOC QoS not implemented; bandwidth votes are not sent\n");
	return 0;
}

int apu_cmd_qos_suspend(uint64_t cmd_id, uint64_t sub_cmd_id,
	int dev_type, int dev_core)
{
	return 0;
}

int apu_cmd_qos_end(uint64_t cmd_id, uint64_t sub_cmd_id,
	int dev_type, int dev_core)
{
	/* Zero bandwidth, not an error: a negative return would fail the
	 * command in mdw_sched, which is not what "not implemented" means.
	 */
	return 0;
}

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT6893_LEGACY_APU_PORTS_H
#define MT6893_LEGACY_APU_PORTS_H
#include <linux/errno.h>
#include <dt-bindings/memory/mt6893-larb-port.h>

/* Stock DT uses mt6885 CODE=21/0, DATA=21/1, VLM=21/2 without domain bits.
 * 6.6 changes both their domain encoding and their pseudo-port ordering.
 */
static inline int mt6893_legacy_apu_port(unsigned int cell, unsigned int old_id,
				       unsigned int *new_id)
{
	if (!new_id)
		return -EINVAL;
	if (cell == 2 && old_id == MTK_M4U_ID(21, 0))
		*new_id = M4U_PORT_L21_APU_FAKE_CODE;
	else if (cell == 3 && old_id == MTK_M4U_ID(21, 1))
		*new_id = M4U_PORT_L21_APU_FAKE_DATA;
	else if (cell == 2 && old_id == MTK_M4U_ID(21, 2))
		*new_id = M4U_PORT_L21_APU_FAKE_VLM;
	else
		return -EINVAL;
	return 0;
}
#endif

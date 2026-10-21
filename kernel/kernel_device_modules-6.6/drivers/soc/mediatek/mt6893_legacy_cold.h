/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT6893_LEGACY_COLD_H
#define MT6893_LEGACY_COLD_H

#include "mt6893_legacy_vcore.h"
#include "mt6893_legacy_init_table.h"

#define MT6893_COLD_HIGH_REQUEST 0x7040U
#define MT6893_COLD_REQUEST_MASK 0x7070U
#define MT6893_COLD_SPM_DVS_ONLY 0x410U /* common scenario; DDR DFS disabled */
#define MT6893_COLD_SPM_DISABLED 0x418U /* common scenario; DVS/DFS disabled */

struct mt6893_legacy_cold_ops {
	void *context;
	unsigned int (*read)(void *context, unsigned int offset);
	void (*write)(void *context, unsigned int offset, unsigned int value);
	int (*smc)(void *context, unsigned int cmd, unsigned int arg1, unsigned int arg2);
	int (*voltage)(void *context);
	int (*wait_high)(void *context);
};

static inline int mt6893_legacy_cold_quiesce(const struct mt6893_legacy_cold_ops *ops,
		const unsigned int uv[5], int spm_touched)
{
	unsigned int value = ops->read(ops->context, 0xc);
	int ret = 0;

	ops->write(ops->context, 0xc,
		(value & ~MT6893_COLD_REQUEST_MASK) | MT6893_COLD_HIGH_REQUEST);
	if (spm_touched)
		ret = ops->smc(ops->context, 1, MT6893_COLD_SPM_DISABLED, 0);
	ops->write(ops->context, 0, 8);
	ops->write(ops->context, 0xd6c, 0);
	if (!ret)
		ret = mt6893_legacy_voltage_check(ops->voltage(ops->context), 4, uv);
	if (!ret && ops->read(ops->context, 0) != 8)
		ret = -EIO;
	return ret;
}

/* One-way cold initialization from the observed reset state. Successful
 * startup pins highest VCORE and DDR requests and never enables DDR DFS.
 * Failure containment is not restoration of the original firmware state.
 */
static inline int mt6893_legacy_cold_start(const struct mt6893_legacy_cold_ops *ops,
		const unsigned int uv[5], unsigned int rsrv, int *containment_result)
{
	unsigned int i, value, selector;
	int ret, spm_touched = 0;

	if (!ops || !ops->read || !ops->write || !ops->smc || !ops->voltage ||
	    !ops->wait_high || !uv || !containment_result)
		return -EINVAL;
	*containment_result = 0;
	for (i = 0; i < 5; i++) {
		if (uv[i] < 400000 || uv[i] > 1193750 || uv[i] % 6250 ||
		    (i && uv[i] < uv[i - 1]))
			return -EINVAL;
	}
	/* Reject a running, forced or otherwise unknown controller. Require the
	 * highest calibrated rail already present before any voltage-related SMC.
	 */
	if (ops->read(ops->context, 0) != 8 || ops->read(ops->context, 0xd44) ||
	    ops->read(ops->context, 0xd48) || ops->read(ops->context, 0xd6c))
		return -EBUSY;
	ret = mt6893_legacy_voltage_check(ops->voltage(ops->context), 4, uv);
	if (ret)
		return ret;
	value = ops->read(ops->context, 0xc);
	/* Only the observed empty AP vote can be adopted; leave other fields. */
	if (value & MT6893_COLD_REQUEST_MASK)
		return -EBUSY;
	ops->write(ops->context, 0xc, value | MT6893_COLD_HIGH_REQUEST);

	/* Old OPP0 is highest and uses PWRAP command16; OPP4 uses command20. */
	for (i = 0; i < 5; i++) {
		selector = (uv[4 - i] - 400000) / 6250;
		ret = ops->smc(ops->context, 3, 16 + i, selector);
		if (ret)
			goto contain;
	}
	spm_touched = 1;
	ret = ops->smc(ops->context, 0, 0, 0);
	if (ret)
		goto contain;
	ret = ops->smc(ops->context, 1, MT6893_COLD_SPM_DVS_ONLY, 0);
	if (ret)
		goto contain;

	/* Preserve old SoC setup order. No timeout interrupt is enabled before
	 * an IRQ owner exists; use highest combined OPP during the force stage.
	 */
	ops->write(ops->context, 0x310, rsrv & (1U << 28) ? 0xe : 6);
	for (i = 0; i < sizeof(mt6893_legacy_init_table) / sizeof(mt6893_legacy_init_table[0]); i++) {
		value = mt6893_legacy_init_table[i].value;
		if (mt6893_legacy_init_table[i].offset == 0xc8)
			value = 0;
		if (mt6893_legacy_init_table[i].offset == 0xd6c && value)
			value = 1;
		ops->write(ops->context, mt6893_legacy_init_table[i].offset, value);
	}
	ret = ops->wait_high(ops->context);
	if (ret)
		goto contain;
	if (ops->read(ops->context, 0) != 0x66f8014b ||
	    ops->read(ops->context, 0xd6c) ||
	    (ops->read(ops->context, 0xc) & MT6893_COLD_REQUEST_MASK) != MT6893_COLD_HIGH_REQUEST) {
		ret = -EIO;
		goto contain;
	}
	ret = mt6893_legacy_voltage_check(ops->voltage(ops->context), 4, uv);
	if (!ret)
		return 0;
contain:
	/* Retain the high vote. Stop dynamic changes and gate DVFSRC output.
	 * Report both original failure and containment failure to the caller.
	 * PWRAP entries/SPM initialization cannot be undone by restoring MMIO.
	 */
	*containment_result = mt6893_legacy_cold_quiesce(ops, uv, spm_touched);
	return ret;
}
#endif

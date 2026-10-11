/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __OPLUS_MTK_LEGACY_GAUGE_H__
#define __OPLUS_MTK_LEGACY_GAUGE_H__

#include <linux/of.h>

#define OPLUS_MTK_LEGACY_GAUGE_NAME "mt6893-platform-gauge"

/* The original MT6893 DT has native gauge data but no v2 gauge binding. */
static inline bool oplus_mtk_legacy_gauge_needed(void)
{
	struct device_node *node;
	bool legacy;

	if (!of_machine_is_compatible("mediatek,MT6893"))
		return false;
	node = of_find_compatible_node(NULL, NULL, "oplus,mms_gauge");
	if (node) {
		of_node_put(node);
		return false;
	}
	node = of_find_compatible_node(NULL, NULL, "oplus,hal_mt6375_gauge");
	if (node) {
		of_node_put(node);
		return false;
	}
	node = of_find_node_by_path("/charger");
	legacy = node && of_device_is_compatible(node, "mediatek,charger") &&
		!of_find_property(node, "oplus,ic_type", NULL);
	of_node_put(node);
	if (!legacy)
		return false;
	node = of_find_node_by_path("/battery");
	legacy = node && of_device_is_compatible(node, "mediatek,bat_gm30");
	of_node_put(node);
	return legacy;
}

#endif

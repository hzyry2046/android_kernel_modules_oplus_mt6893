// SPDX-License-Identifier: GPL-2.0
/* MT6893 resources from the stock DT. All mappings follow the device lifetime. */
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include "hal_config_power.h"

static void __iomem *map_shared_node(struct device *dev, const char *compatible)
{
	struct device_node *np;
	struct resource res;
	void __iomem *base;
	int ret;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np)
		return ERR_PTR(-ENODEV);
	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret)
		return ERR_PTR(ret);
	/* These SPM/clock register ranges also have existing framework owners.
	 * Map the range without claiming a competing exclusive resource.
	 */
	base = devm_ioremap(dev, res.start, resource_size(&res));
	return base ? base : ERR_PTR(-ENOMEM);
}

#define MAP_OWN(field, name) do { \
	data->field = devm_platform_ioremap_resource_byname(pdev, name); \
	if (IS_ERR(data->field)) return PTR_ERR(data->field); \
} while (0)
#define MAP_SHARED(field, compatible) do { \
	data->field = map_shared_node(&pdev->dev, compatible); \
	if (IS_ERR(data->field)) return PTR_ERR(data->field); \
} while (0)

int init_platform_resource(struct platform_device *pdev,
			   struct hal_param_init_power *data)
{
	memset(data, 0, sizeof(*data));
	data->dev = &pdev->dev;
	MAP_OWN(rpc_base_addr, "apusys_rpc");
	MAP_OWN(pcu_base_addr, "apusys_pcu");
	MAP_OWN(vcore_base_addr, "apusys_vcore");
	MAP_SHARED(spm_base_addr, "mediatek,sleep");
	MAP_SHARED(infracfg_ao_base_addr, "mediatek,infracfg_ao");
	MAP_SHARED(infra_bcrm_base_addr, "mediatek,infra_bcrm");
	MAP_SHARED(conn_base_addr, "mediatek,mt6893-apu_conn");
	MAP_SHARED(vpu0_base_addr, "mediatek,mt6893-apu0");
	MAP_SHARED(vpu1_base_addr, "mediatek,mt6893-apu1");
	MAP_SHARED(vpu2_base_addr, "mediatek,mt6893-apu2");
	MAP_SHARED(mdla0_base_addr, "mediatek,mt6893-apu_mdla0");
	MAP_SHARED(mdla1_base_addr, "mediatek,mt6893-apu_mdla1");
	return 0;
}

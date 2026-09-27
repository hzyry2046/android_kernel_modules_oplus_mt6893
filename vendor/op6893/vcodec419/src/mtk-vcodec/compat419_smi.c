// SPDX-License-Identifier: GPL-2.0
/*
 * smi_bus_prepare_enable()/smi_bus_disable_unprepare() for the 4.19 vcodec,
 * on top of the 6.6 mtk-smi larb driver.
 *
 * The frozen 4.19 DTB has one node per larb, compatible "mediatek,smi_larb",
 * carrying "mediatek,larb-id".  The 6.6 mtk-smi driver binds those nodes (see the
 * display DT-ABI shims) and has runtime PM enabled on them, so powering a larb
 * is a runtime-PM get on that device.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include "smi_public.h"

static struct device *larb_dev[SMI_LARB_NUM];
static DEFINE_MUTEX(larb_lock);

static struct device *larb_lookup(u32 id)
{
	struct device_node *np;
	struct platform_device *pdev;
	u32 larb_id;

	if (id >= SMI_LARB_NUM)
		return NULL;

	mutex_lock(&larb_lock);
	if (larb_dev[id])
		goto out;

	for_each_compatible_node(np, NULL, "mediatek,smi_larb") {
		if (of_property_read_u32(np, "mediatek,larb-id", &larb_id) ||
		    larb_id != id)
			continue;
		pdev = of_find_device_by_node(np);
		of_node_put(np);
		if (!pdev)
			break;
		if (!pdev->dev.driver) {
			/* Not bound (yet): don't cache, retry next time. */
			put_device(&pdev->dev);
			break;
		}
		larb_dev[id] = &pdev->dev;	/* reference kept for module life */
		break;
	}
out:
	mutex_unlock(&larb_lock);
	return larb_dev[id];
}

s32 compat419_smi_bus_prepare_enable(u32 id, const char *user)
{
	struct device *dev = larb_lookup(id);
	int ret;

	if (!dev) {
		pr_err("compat419-smi: %s: larb%u not bound\n", user, id);
		return -ENODEV;
	}
	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		pr_err("compat419-smi: %s: larb%u resume failed %d\n", user, id, ret);
	return ret;
}


s32 compat419_smi_bus_disable_unprepare(u32 id, const char *user)
{
	struct device *dev = larb_lookup(id);

	if (!dev)
		return -ENODEV;
	return pm_runtime_put_sync(dev);
}


void compat419_smi_release(void)
{
	int i;

	mutex_lock(&larb_lock);
	for (i = 0; i < SMI_LARB_NUM; i++) {
		if (larb_dev[i])
			put_device(larb_dev[i]);
		larb_dev[i] = NULL;
	}
	mutex_unlock(&larb_lock);
}


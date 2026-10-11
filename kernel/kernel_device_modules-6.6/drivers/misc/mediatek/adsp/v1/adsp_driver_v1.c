// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "adsp_mbox.h"
#include "adsp_platform.h"
#include "adsp_platform_driver.h"
#include "adsp_core.h"
#include "adsp_clk_tinysys.h"
#include "adsp_driver_v1.h"
#include "mtk-afe-external.h"

/* semaphore */
#define SEMA_TIMEOUT        5000
#define SEMA_WAY_BITS       3
#define SEMA_CTRL_BIT       2

static struct adspsys_priv *v1_adspsys;

const struct adspsys_description mt6893_adspsys_desc = {
	.platform_name = "mt6893",
	.version = 1,
	.semaphore_ways = 3,
	.semaphore_ctrl = 2,
	.semaphore_retry = 5000,
	.axibus_idle_val = 0x100, //AP read pending counter = 1
};

const struct adspsys_description mt6853_adspsys_desc = {
	.platform_name = "mt6853",
	.version = 1,
	.semaphore_ways = 3,
	.semaphore_ctrl = 2,
	.semaphore_retry = 5000,
	.axibus_idle_val = 0x100, //AP read pending counter = 1
};

const struct adsp_core_description mt6893_adsp_c0_desc = {
	.id = 0,
	.name = "adsp_0",
	.sharedmems = {
		[ADSP_SHAREDMEM_BOOTUP_MARK] = {0x0004, 0x0004},
		[ADSP_SHAREDMEM_SYS_STATUS] = {0x0008, 0x0004},
		[ADSP_SHAREDMEM_MPUINFO] = {0x0028, 0x0020},
		[ADSP_SHAREDMEM_WAKELOCK] = {0x002C, 0x0004},
		[ADSP_SHAREDMEM_IPCBUF] = {0x0300, 0x0200},
		[ADSP_SHAREDMEM_C2C_0_BUF] = {0x2508, 0x2208}, //common begin
		[ADSP_SHAREDMEM_C2C_BUFINFO] = {0x2510, 0x0008},
		[ADSP_SHAREDMEM_TIMESYNC] = {0x2530, 0x0020},
		[ADSP_SHAREDMEM_DVFSSYNC] = {0x253C, 0x000C},
		[ADSP_SHAREDMEM_SLEEPSYNC] = {0x2540, 0x0004},
		[ADSP_SHAREDMEM_BUS_MON_DUMP] = {0x25FC, 0x00BC},
		[ADSP_SHAREDMEM_INFRA_BUS_DUMP] = {0x269C, 0x00A0},
		[ADSP_SHAREDMEM_LATMON_DUMP] = {0x26B8, 0x001C},
	},
	.ops = {
		.initialize = adsp_core0_init,
		.after_bootup = adsp_after_bootup,
	}
};

const struct adsp_core_description mt6893_adsp_c1_desc = {
	.id = 1,
	.name = "adsp_1",
	.sharedmems = {
		[ADSP_SHAREDMEM_BOOTUP_MARK] = {0x0004, 0x0004},
		[ADSP_SHAREDMEM_SYS_STATUS] = {0x0008, 0x0004},
		[ADSP_SHAREDMEM_MPUINFO] = {0x0028, 0x0020},
		[ADSP_SHAREDMEM_WAKELOCK] = {0x002C, 0x0004},
		[ADSP_SHAREDMEM_IPCBUF] = {0x0300, 0x0200},
		[ADSP_SHAREDMEM_C2C_1_BUF] = {0x2508, 0x2208}, //common begin
	},
	.ops = {
		.initialize = adsp_core1_init,
		.after_bootup = adsp_after_bootup,
	}
};

const struct adsp_core_description mt6853_adsp_c0_desc = {
	.id = 0,
	.name = "adsp_0",
	.sharedmems = {
		[ADSP_SHAREDMEM_BOOTUP_MARK] = {0x0004, 0x0004},
		[ADSP_SHAREDMEM_SYS_STATUS] = {0x0008, 0x0004},
		[ADSP_SHAREDMEM_MPUINFO] = {0x0028, 0x0020},
		[ADSP_SHAREDMEM_WAKELOCK] = {0x002C, 0x0004},
		[ADSP_SHAREDMEM_IPCBUF] = {0x0300, 0x0200},
		[ADSP_SHAREDMEM_TIMESYNC] = {0x2530, 0x0020},
		[ADSP_SHAREDMEM_DVFSSYNC] = {0x253C, 0x000C},
		[ADSP_SHAREDMEM_SLEEPSYNC] = {0x2540, 0x0004},
		[ADSP_SHAREDMEM_BUS_MON_DUMP] = {0x25FC, 0x00BC},
		[ADSP_SHAREDMEM_INFRA_BUS_DUMP] = {0x269C, 0x00A0},
		[ADSP_SHAREDMEM_LATMON_DUMP] = {0x26B8, 0x001C},
	},
	.ops = {
		.initialize = adsp_core0_init,
		.after_bootup = adsp_after_bootup,
	}
};

static const struct of_device_id adspsys_of_ids[] = {
	{ .compatible = "mediatek,adsp_common", .data = &mt6893_adspsys_desc},
	{ .compatible = "mediatek,mt6853-adspsys", .data = &mt6853_adspsys_desc},
	{}
};

static const struct of_device_id adsp_core_of_ids[] = {
	{ .compatible = "mediatek,adsp_core_0", .data = &mt6893_adsp_c0_desc},
	{ .compatible = "mediatek,adsp_core_1", .data = &mt6893_adsp_c1_desc},
	{ .compatible = "mediatek,mt6853-adsp_core_0", .data = &mt6853_adsp_c0_desc},
	{}
};

static int adspsys_drv_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct adspsys_priv *adspsys;
	struct device_node *core;
	struct resource cfg;
	u32 loaded;

	/* create private data */
	adspsys = devm_kzalloc(dev, sizeof(*adspsys), GFP_KERNEL);
	if (!adspsys)
		return -ENOMEM;

	match = of_match_node(adspsys_of_ids, dev->of_node);
	if (!match)
		return -ENODEV;

	adspsys->desc = (struct adspsys_description *)match->data;
	adspsys->dev = dev;

	/* The original MT6893 DT puts shared CFG in each core node. */
	adspsys->legacy_mt6893 = !strcmp(adspsys->desc->platform_name, "mt6893") &&
		!of_find_property(dev->of_node, "core-num", NULL) &&
		of_property_match_string(dev->of_node, "reg-names", "cfg_secure") == 0;
	if (adspsys->legacy_mt6893) {
		ret = of_property_read_u32(dev->of_node, "load", &loaded);
		if (ret || !loaded)
			return -ENODEV;
		core = of_find_compatible_node(NULL, NULL, "mediatek,adsp_core_0");
		if (!core)
			return -ENODEV;
		ret = of_address_to_resource(core, 0, &cfg);
		of_node_put(core);
		if (ret)
			return ret;
		res = &cfg;
		adspsys->num_cores = 2;
	} else {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
		ret = of_property_read_u32(dev->of_node, "core-num", &adspsys->num_cores);
		if (ret || !adspsys->num_cores || adspsys->num_cores > ADSP_CORE_TOTAL)
			return -EINVAL;
	}
	adspsys->cfg = devm_ioremap_resource(dev, res);
	if (IS_ERR(adspsys->cfg))
		return PTR_ERR(adspsys->cfg);
	adspsys->cfg_size = resource_size(res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg_secure");
	adspsys->cfg_secure = devm_ioremap_resource(dev, res);
	if (IS_ERR(adspsys->cfg_secure))
		return PTR_ERR(adspsys->cfg_secure);
	adspsys->cfg_secure_size = resource_size(res);

	ret = adsp_clk_probe(pdev, &adspsys->clk_ops);
	if (ret) {
		pr_warn("%s(), clk probe fail, %d\n", __func__, ret);
		goto ERROR;
	}

	ret = adsp_mem_device_probe(pdev);
	if (ret) {
		pr_info("%s(), memory probe fail, %d\n", __func__, ret);
		goto ERROR;
	}

	ret = adsp_mbox_probe(pdev);
	if (ret) {
		pr_warn("%s(), mbox probe fail, %d\n", __func__, ret);
		goto ERROR;
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev))
		pr_warn("%s(), pm_runtime_enable fail, %d\n", __func__, ret);

	/* register as syscore_device, not to be turned off when suspend */
	dev_pm_syscore_device(&pdev->dev, true);

	/* register syscore if adsp on infra */
	//register_syscore_ops(&adsp_syscore_ops);

	register_adspsys(adspsys);
	v1_adspsys = adspsys;
	switch_adsp_power(true);

	pr_info("%s, success cores=%u legacy=%d\n", __func__,
		adspsys->num_cores, adspsys->legacy_mt6893);
ERROR:
	return ret;
}

static int adspsys_drv_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int adsp_core_drv_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct adsp_priv *pdata;
	const struct adsp_core_description *desc;
	const struct of_device_id *match;
	struct of_phandle_args spec;
	u64 system_info[2];
	u32 legacy_features;
	int tcm_index;
	int irq;
	struct adspsys_priv *adspsys = v1_adspsys;

	if (!adspsys)
		return -EPROBE_DEFER;

	/* create private data */
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	match = of_match_node(adsp_core_of_ids, dev->of_node);
	if (!match)
		return -ENODEV;

	desc = (struct adsp_core_description *)match->data;

	pdata->id = desc->id;
	pdata->name = desc->name;
	pdata->ops = &desc->ops;
	pdata->mapping_table = desc->sharedmems;

	pdata->dev = dev;
	init_completion(&pdata->done);

	/* get resource from platform_device */
	tcm_index = adspsys->legacy_mt6893 ? 1 : 0;
	res = platform_get_resource(pdev, IORESOURCE_MEM, tcm_index);
	pdata->itcm = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->itcm))
		return PTR_ERR(pdata->itcm);
	pdata->itcm_size = resource_size(res);

	res = platform_get_resource(pdev, IORESOURCE_MEM, tcm_index + 1);
	pdata->dtcm = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->dtcm))
		return PTR_ERR(pdata->dtcm);
	pdata->dtcm_size = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	pdata->irq[ADSP_IRQ_IPC_ID].seq = irq;
	pdata->irq[ADSP_IRQ_IPC_ID].clear_irq = adsp_mt_clr_sysirq;
	irq = platform_get_irq(pdev, 1);
	if (irq < 0)
		return irq;
	pdata->irq[ADSP_IRQ_WDT_ID].seq = irq;
	pdata->irq[ADSP_IRQ_WDT_ID].clear_irq = adsp_mt_disable_wdt;
	irq = platform_get_irq(pdev, 2);
	if (irq < 0)
		return irq;
	pdata->irq[ADSP_IRQ_AUDIO_ID].seq = irq;
	pdata->irq[ADSP_IRQ_AUDIO_ID].clear_irq = adsp_mt_clr_auidoirq;

	ret = of_property_read_u64_array(dev->of_node, "system", system_info, 2);
	if (ret)
		return ret;
	pdata->sysram_phys = (phys_addr_t)system_info[0];
	pdata->sysram_size = (size_t)system_info[1];

	if (pdata->sysram_phys == 0 || pdata->sysram_size == 0)
		return -ENODEV;
	pdata->sysram = devm_ioremap_wc(dev, pdata->sysram_phys, pdata->sysram_size);
	if (!pdata->sysram)
		return -ENOMEM;

	ret = of_property_read_u64(dev->of_node, "feature-control-bits",
			     &pdata->feature_set);
	if (ret && adspsys->legacy_mt6893) {
		ret = of_property_read_u32(dev->of_node, "feature_control_bits", &legacy_features);
		if (!ret)
			pdata->feature_set = legacy_features;
	}
	if (ret || !pdata->feature_set)
		return -EINVAL;

	/* mailbox channel parsing */
	if (of_parse_phandle_with_args(dev->of_node, "mboxes",
				       "#mbox-cells", 0, &spec)) {
		dev_dbg(dev, "%s: can't parse \"mboxes\" property\n", __func__);
		return -ENODEV;
	}
	pdata->send_mbox = get_adsp_mbox_pin_send(spec.args[0]);
	of_node_put(spec.np);
	if (!pdata->send_mbox)
		return -EINVAL;

	if (of_parse_phandle_with_args(dev->of_node, "mboxes",
				       "#mbox-cells", 1, &spec)) {
		dev_dbg(dev, "%s: can't parse \"mboxes\" property\n", __func__);
		return -ENODEV;
	}
	pdata->recv_mbox = get_adsp_mbox_pin_recv(spec.args[0]);
	of_node_put(spec.np);
	if (!pdata->recv_mbox)
		return -EINVAL;

	/* add to adsp_core list */
	register_adsp_core(pdata);

	pr_info("%s, id:%d success\n", __func__, pdata->id);
	return ret;
}

static int adsp_core_drv_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver adspsys_driver = {
	.probe = adspsys_drv_probe,
	.remove = adspsys_drv_remove,
	.driver = {
		.name = "adspsys",
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = adspsys_of_ids,
#endif
	},
};

static struct platform_driver adsp_core0_driver = {
	.probe = adsp_core_drv_probe,
	.remove = adsp_core_drv_remove,
	.driver = {
		.name = "adsp-core0",
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = adsp_core_of_ids,
#endif
	},
};

static struct platform_driver adsp_core1_driver = {
	.probe = adsp_core_drv_probe,
	.remove = adsp_core_drv_remove,
	.driver = {
		.name = "adsp-core1",
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = adsp_core_of_ids,
#endif
	},
};

static struct platform_driver * const drivers[] = {
	&adspsys_driver,
	&adsp_core0_driver,
	&adsp_core1_driver,
};

int notify_adsp_semaphore_event(struct notifier_block *nb,
				unsigned long event, void *v)
{
	int status = NOTIFY_DONE;

	if (event == NOTIFIER_ADSP_3WAY_SEMAPHORE_GET) {
		status = (get_adsp_semaphore(SEMA_AUDIOREG) == ADSP_OK) ?
			 NOTIFY_STOP : NOTIFY_BAD;
	} else if (event == NOTIFIER_ADSP_3WAY_SEMAPHORE_RELEASE) {
		release_adsp_semaphore(SEMA_AUDIOREG);
		status = NOTIFY_STOP;
	}

	return status;
}

static struct notifier_block adsp_semaphore_init_notifier = {
	.notifier_call = notify_adsp_semaphore_event,
};

/*
 * driver initialization entry point
 */
static int __init platform_adsp_init(void)
{
	int ret = 0;

	ret = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (ret)
		return ret;

	register_3way_semaphore_notifier(&adsp_semaphore_init_notifier);

	ret = adsp_system_bootup();
	if (ret && v1_adspsys && v1_adspsys->legacy_mt6893) {
		/* Keep callbacks and mappings alive for diagnostics after a failed
		 * legacy boot. Returning an init error here unloads this module
		 * while its registered platform drivers and IRQs still exist.
		 */
		pr_err("ADSP legacy boot failed (%d); driver remains registered, DSP offline\n", ret);
		return 0;
	}
	return ret;
}

static void __exit platform_adsp_exit(void)
{
	unregister_3way_semaphore_notifier(&adsp_semaphore_init_notifier);
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
	pr_info("[ADSP] platform-adsp Exit.\n");
}

module_init(platform_adsp_init);
module_exit(platform_adsp_exit);

MODULE_AUTHOR("Chien-Wei Hsu <Chien-Wei.Hsu@mediatek.com>");
MODULE_DESCRIPTION("MTK AUDIO DSP PLATFORM Device Driver");
MODULE_LICENSE("GPL v2");


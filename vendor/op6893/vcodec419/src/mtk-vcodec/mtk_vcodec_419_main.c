// SPDX-License-Identifier: GPL-2.0
/*
 * Module entry for the forward-ported 4.19 mt6885 VCU + decoder + encoder.
 * 4.19 built vcu, decoder and encoder in (three module_platform_driver()s); as one module
 * they need a single init/exit.
 */
#include <linux/module.h>
#include <linux/platform_device.h>

extern struct platform_driver mtk_vcu_driver;
extern struct platform_driver mtk_vcodec_dec_driver;
extern struct platform_driver mtk_vcodec_enc_driver;
void compat419_smi_release(void);

static int __init mtk_vcodec_419_init(void)
{
	int ret;

	/* VCU first: the codec probes look it up through mediatek,vcu. */
	ret = platform_driver_register(&mtk_vcu_driver);
	if (ret)
		return ret;
	ret = platform_driver_register(&mtk_vcodec_dec_driver);
	if (ret)
		goto err_dec;
	ret = platform_driver_register(&mtk_vcodec_enc_driver);
	if (ret)
		goto err_enc;
	return 0;

err_enc:
	platform_driver_unregister(&mtk_vcodec_dec_driver);
err_dec:
	platform_driver_unregister(&mtk_vcu_driver);
	return ret;
}

static void __exit mtk_vcodec_419_exit(void)
{
	platform_driver_unregister(&mtk_vcodec_enc_driver);
	platform_driver_unregister(&mtk_vcodec_dec_driver);
	platform_driver_unregister(&mtk_vcu_driver);
	compat419_smi_release();
}

module_init(mtk_vcodec_419_init);
module_exit(mtk_vcodec_419_exit);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_DESCRIPTION("Mediatek video codec V4L2 driver (4.19 mt6885, forward-ported)");

// SPDX-License-Identifier: GPL-2.0
/*
 * op6893 bring-up: read-only probe of the ISP/ISP2 MTCMOS state and, only
 * when the domain reports powered, of the imgsys CG and a few DIP-A regs.
 * Touching imgsys with the domain off can hang the bus, hence the gate.
 * Loads, prints, and refuses to stay loaded (-EAGAIN).
 */
#include <linux/module.h>
#include <linux/io.h>

#define SPM_BASE	0x10006000
#define PWR_STATUS	0x16c
#define PWR_STATUS_2ND	0x170
#define ISP_PWR_CON	0x330
#define ISP2_PWR_CON	0x334
#define ISP_BIT		BIT(12)
#define ISP2_BIT	BIT(13)

static void dump_block(const char *tag, phys_addr_t pa, const u32 *offs, int n)
{
	void __iomem *b = ioremap(pa, 0x3000);
	int i;

	if (!b) {
		pr_info("pwrprobe: ioremap %s fail\n", tag);
		return;
	}
	for (i = 0; i < n; i++)
		pr_info("pwrprobe: %s+0x%04x = 0x%08x\n", tag, offs[i],
			readl(b + offs[i]));
	iounmap(b);
}

static int __init pwrprobe_init(void)
{
	static const u32 cg_offs[] = { 0x0 };
	static const u32 dip_offs[] = { 0x1000, 0x1004, 0x1010, 0x1014,
					0x1020, 0x1030, 0x1040, 0x1050 };
	void __iomem *spm = ioremap(SPM_BASE, 0x1000);
	u32 st, st2;

	if (!spm)
		return -ENOMEM;
	st = readl(spm + PWR_STATUS);
	st2 = readl(spm + PWR_STATUS_2ND);
	pr_info("pwrprobe: PWR_STATUS=0x%08x 2ND=0x%08x ISP_PWR_CON=0x%08x ISP2_PWR_CON=0x%08x\n",
		st, st2, readl(spm + ISP_PWR_CON), readl(spm + ISP2_PWR_CON));
	iounmap(spm);

	pr_info("pwrprobe: ISP  domain %s\n",
		(st & ISP_BIT) && (st2 & ISP_BIT) ? "ON" : "OFF");
	pr_info("pwrprobe: ISP2 domain %s\n",
		(st & ISP2_BIT) && (st2 & ISP2_BIT) ? "ON" : "OFF");

	if ((st & ISP_BIT) && (st2 & ISP_BIT)) {
		dump_block("imgsys1_cg", 0x15020000, cg_offs, 1);
		dump_block("dip_a", 0x15021000, dip_offs, ARRAY_SIZE(dip_offs));
	}
	if ((st & ISP2_BIT) && (st2 & ISP2_BIT))
		dump_block("imgsys2_cg", 0x15820000, cg_offs, 1);

	return -EAGAIN;
}
module_init(pwrprobe_init);
MODULE_LICENSE("GPL");

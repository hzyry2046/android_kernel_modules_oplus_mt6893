// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 The op6893 6.6 port
 *
 * MM QoS / SMI BWC front end for the 4.19 board contract.
 *
 * The op6893 board DT is the 4.19 one.  It describes multimedia QoS with a
 * single "mediatek,mmdvfs_pmqos" node: per-larb port ratio tables, the
 * max-OSTD larbs, the VCORE OPP steps, and a clock-mux frequency step table
 * for each multimedia class (disp/mdp/vdec/venc/img/cam/dpe/ipe/ccu/img2).
 *
 * The 6.6 stack expects the opposite shape -- "mediatek,mt6893-mmqos" with
 * larbs-supply / commons-supply phandles, plus an "interconnects" property on
 * every consumer.  The board DT has neither, so on this port nothing consumes
 * the QoS contract: of_mtk_icc_get() returns NULL for every multimedia
 * consumer and mtk_icc_set_bw() silently drops the request.
 *
 * Stage 1 (this file) parses and validates the contract the board DT actually
 * provides, and exports it through debugfs.  It maps no registers, enables no
 * clock, votes on no regulator and takes no reference on the rails: the only
 * thing it proves is that the live contract is complete and self-consistent.
 * Later stages add the bandwidth aggregation and the hardware application.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MMQOS_MAX_STEP		6
#define MMQOS_MAX_CLASS		10
#define MMQOS_MAX_CLOCK		32
#define MMQOS_MAX_LARB		32
#define MMQOS_MAX_PORT		32
#define MMQOS_MAX_CAM_LARB	16
#define MMQOS_MAX_OSTD_LARB	8
#define MMQOS_MAX_FMETER	9
#define MMQOS_MAX_COMM		2

/*
 * 4.19's mt6885/mt6893 SMI has 21 physical larbs.  mmdvfs_pmqos then appends
 * four synthetic ones for traffic that has no larb of its own:
 *
 *   21 VIRTUAL_DISP   22 VIRTUAL_MD   23 VIRTUAL_CCU_COMMON   24 VIRTUAL_CCU_COMMON2
 *
 * They have no "larbN" property (they are not real larbs), but the board DT
 * does name 23 and 24 in "cam_larb", because the CCU's two virtual ports are
 * camera ports.  Treating them as unknown was the parser's mistake, not the
 * contract's.
 */
#define MMQOS_SMI_LARB_NUM	21
#define MMQOS_VIRTUAL_DISP	21
#define MMQOS_VIRTUAL_MD	22
#define MMQOS_VIRTUAL_CCU	23
#define MMQOS_VIRTUAL_CCU2	24

enum mmqos_clk_type {
	MMQOS_CLK_NONE = 0,
	MMQOS_CLK_MUX = 1,
	MMQOS_CLK_PLL = 2,
};

struct mmqos_step {
	u32 freq_mhz;
	u32 clk_type;
	u32 clk_param1;		/* mux index (MUX) or PLL id (PLL) */
	u32 clk_param2;		/* source index (MUX) or PLL value (PLL) */
	struct clk *mux;
	struct clk *src;
};

struct mmqos_class {
	const char *name;
	int nsteps;
	struct mmqos_step step[MMQOS_MAX_STEP];
};

struct mmqos_larb {
	bool present;
	u32 nports;
	u32 ratio[MMQOS_MAX_PORT];
	bool is_cam;
	bool is_max_ostd;
};

struct mmqos_contract {
	struct device *dev;

	int nclocks;
	const char *clock_name[MMQOS_MAX_CLOCK];
	struct clk *clock[MMQOS_MAX_CLOCK];
	int nclocks_missing;

	int nvopp;
	u32 vopp[MMQOS_MAX_STEP];

	struct mmqos_class cls[MMQOS_MAX_CLASS];
	int ncls;

	struct mmqos_larb larb[MMQOS_MAX_LARB];
	bool virtual_larb[MMQOS_MAX_LARB];
	u32 larb_groups[MMQOS_MAX_LARB];
	int nlarb_groups;

	u32 cam_larb[MMQOS_MAX_CAM_LARB];
	int ncam_larb;

	u32 max_ostd_larb[MMQOS_MAX_OSTD_LARB];
	int nmax_ostd_larb;
	s32 max_ostd;
	u32 max_bw_bound;

	const char *comm_freq[MMQOS_MAX_COMM];
	int ncomm;

	u32 fmeter_mux[MMQOS_MAX_FMETER];
	int nfmeter;

	struct regulator *vcore;
	int vcore_err;

	int nerrors;
	int nwarnings;
};

static const char *const mmqos_class_names[MMQOS_MAX_CLASS] = {
	"disp", "mdp", "vdec", "venc", "img",
	"cam", "dpe", "ipe", "ccu", "img2",
};

static void mmqos_error(struct mmqos_contract *c, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	dev_err(c->dev, "contract: %pV", &vaf);
	va_end(args);
	c->nerrors++;
}

static void mmqos_warn(struct mmqos_contract *c, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	dev_warn(c->dev, "contract: %pV", &vaf);
	va_end(args);
	c->nwarnings++;
}

static int mmqos_parse_clocks(struct mmqos_contract *c, struct device_node *np)
{
	int n, i, ret;

	n = of_property_count_strings(np, "clock-names");
	if (n <= 0) {
		mmqos_error(c, "clock-names missing or empty (%d)\n", n);
		return -EINVAL;
	}
	if (n > MMQOS_MAX_CLOCK) {
		mmqos_error(c, "clock-names has %d entries, max %d\n",
			    n, MMQOS_MAX_CLOCK);
		return -EINVAL;
	}

	c->nclocks = n;
	for (i = 0; i < n; i++) {
		ret = of_property_read_string_index(np, "clock-names", i,
						    &c->clock_name[i]);
		if (ret) {
			mmqos_error(c, "clock-names[%d] unreadable (%d)\n",
				    i, ret);
			return ret;
		}
		c->clock[i] = devm_clk_get(c->dev, c->clock_name[i]);
		if (IS_ERR(c->clock[i])) {
			ret = PTR_ERR(c->clock[i]);
			c->clock[i] = NULL;
			c->nclocks_missing++;
			mmqos_warn(c, "clock[%d] %s unresolved (%d)\n",
				   i, c->clock_name[i], ret);
		}
	}

	return 0;
}

static int mmqos_parse_vopp(struct mmqos_contract *c, struct device_node *np)
{
	int n;

	n = of_property_count_u32_elems(np, "vopp_steps");
	if (n <= 0) {
		mmqos_error(c, "vopp_steps missing or empty (%d)\n", n);
		return -EINVAL;
	}
	if (n > MMQOS_MAX_STEP) {
		mmqos_error(c, "vopp_steps has %d entries, max %d\n",
			    n, MMQOS_MAX_STEP);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "vopp_steps", c->vopp, n)) {
		mmqos_error(c, "vopp_steps unreadable\n");
		return -EINVAL;
	}
	c->nvopp = n;
	return 0;
}

static int mmqos_parse_step(struct mmqos_contract *c, struct device_node *np,
			    const char *prop, struct mmqos_step *step)
{
	u32 cell[4];
	int n, ret;

	n = of_property_count_u32_elems(np, prop);
	if (n != ARRAY_SIZE(cell)) {
		mmqos_error(c, "%s has %d cells, expected %zu\n",
			    prop, n, ARRAY_SIZE(cell));
		return -EINVAL;
	}
	ret = of_property_read_u32_array(np, prop, cell, ARRAY_SIZE(cell));
	if (ret) {
		mmqos_error(c, "%s unreadable (%d)\n", prop, ret);
		return ret;
	}

	step->freq_mhz = cell[0];
	step->clk_type = cell[1];
	step->clk_param1 = cell[2];
	step->clk_param2 = cell[3];

	switch (step->clk_type) {
	case MMQOS_CLK_NONE:
		break;
	case MMQOS_CLK_MUX:
		step->mux = c->clock[cell[2] < c->nclocks ? cell[2] : 0];
		step->src = c->clock[cell[3] < c->nclocks ? cell[3] : 0];
		if (cell[2] >= c->nclocks) {
			mmqos_error(c, "%s mux index %u out of %d clocks\n",
				    prop, cell[2], c->nclocks);
			return -EINVAL;
		}
		if (cell[3] >= c->nclocks) {
			mmqos_error(c, "%s source index %u out of %d clocks\n",
				    prop, cell[3], c->nclocks);
			return -EINVAL;
		}
		if (!step->mux) {
			mmqos_error(c, "%s mux %s unresolved\n",
				    prop, c->clock_name[cell[2]]);
			return -EINVAL;
		}
		if (!step->src) {
			mmqos_error(c, "%s source %s unresolved\n",
				    prop, c->clock_name[cell[3]]);
			return -EINVAL;
		}
		break;
	case MMQOS_CLK_PLL:
		mmqos_warn(c, "%s uses PLL hopping (id %u, value %#x); the port does not program it\n",
			   prop, cell[2], cell[3]);
		break;
	default:
		mmqos_error(c, "%s has unknown clk_type %u\n",
			    prop, step->clk_type);
		return -EINVAL;
	}

	return 0;
}

static int mmqos_parse_class(struct mmqos_contract *c, struct device_node *np,
			     const char *name)
{
	struct mmqos_class *cls = &c->cls[c->ncls];
	struct property *prop;
	const char *step_name;
	char freq_prop[32];
	int ret;

	snprintf(freq_prop, sizeof(freq_prop), "%s_freq", name);

	if (!of_find_property(np, freq_prop, NULL)) {
		mmqos_warn(c, "%s: no %s property, class skipped\n",
			   name, freq_prop);
		return 0;
	}

	cls->name = name;
	of_property_for_each_string(np, freq_prop, prop, step_name) {
		if (cls->nsteps >= MMQOS_MAX_STEP) {
			mmqos_error(c, "%s: more than %d steps\n",
				    freq_prop, MMQOS_MAX_STEP);
			break;
		}
		ret = mmqos_parse_step(c, np, step_name,
				       &cls->step[cls->nsteps]);
		if (ret)
			return ret;
		cls->nsteps++;
	}

	if (!cls->nsteps) {
		mmqos_error(c, "%s: empty step list\n", freq_prop);
		return -EINVAL;
	}
	if (c->nvopp && cls->nsteps != c->nvopp)
		mmqos_warn(c, "%s: %d steps but vopp_steps has %d\n",
			   freq_prop, cls->nsteps, c->nvopp);

	c->ncls++;
	return 0;
}

static int mmqos_parse_larb(struct mmqos_contract *c, struct device_node *np,
			    u32 id)
{
	struct mmqos_larb *larb;
	struct property *prop;
	char name[16];
	const __be32 *p;
	u32 value;

	if (id >= MMQOS_MAX_LARB) {
		mmqos_error(c, "larb%u exceeds the %d-larb table\n",
			    id, MMQOS_MAX_LARB);
		return -EINVAL;
	}

	larb = &c->larb[id];
	if (larb->present) {
		mmqos_error(c, "larb%u listed twice\n", id);
		return -EINVAL;
	}
	larb->present = true;

	snprintf(name, sizeof(name), "larb%u", id);
	of_property_for_each_u32(np, name, prop, p, value) {
		if (larb->nports >= MMQOS_MAX_PORT) {
			mmqos_error(c, "%s has more than %d ports\n",
				    name, MMQOS_MAX_PORT);
			return -EINVAL;
		}
		larb->ratio[larb->nports++] = value;
	}

	if (!larb->nports) {
		mmqos_error(c, "%s is empty\n", name);
		return -EINVAL;
	}
	return 0;
}

static int mmqos_parse_larbs(struct mmqos_contract *c, struct device_node *np)
{
	struct property *prop;
	const __be32 *p;
	u32 value, i;
	int ret;

	of_property_for_each_u32(np, "larb_groups", prop, p, value) {
		if (c->nlarb_groups >= MMQOS_MAX_LARB) {
			mmqos_error(c, "larb_groups exceeds %d entries\n",
				    MMQOS_MAX_LARB);
			return -EINVAL;
		}
		c->larb_groups[c->nlarb_groups++] = value;
	}
	if (!c->nlarb_groups) {
		mmqos_error(c, "larb_groups missing or empty\n");
		return -EINVAL;
	}

	for (i = 0; i < c->nlarb_groups; i++) {
		ret = mmqos_parse_larb(c, np, c->larb_groups[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static void mmqos_parse_aux(struct mmqos_contract *c, struct device_node *np)
{
	struct property *prop;
	const __be32 *p;
	const char *s;
	char class_prop[32];
	u32 value;
	int i;

	of_property_for_each_u32(np, "cam_larb", prop, p, value) {
		if (c->ncam_larb >= MMQOS_MAX_CAM_LARB)
			break;
		if (value >= MMQOS_MAX_LARB ||
		    (!c->larb[value].present && !c->virtual_larb[value])) {
			mmqos_error(c, "cam_larb %u is neither a real larb nor a virtual one\n",
				    value);
			continue;
		}
		c->cam_larb[c->ncam_larb++] = value;
		c->larb[value].is_cam = true;
	}

	of_property_for_each_u32(np, "max_ostd_larb", prop, p, value) {
		if (c->nmax_ostd_larb >= MMQOS_MAX_OSTD_LARB)
			break;
		if (value >= MMQOS_MAX_LARB ||
		    (!c->larb[value].present && !c->virtual_larb[value])) {
			mmqos_error(c, "max_ostd_larb %u is neither a real larb nor a virtual one\n",
				    value);
			continue;
		}
		c->max_ostd_larb[c->nmax_ostd_larb++] = value;
		c->larb[value].is_max_ostd = true;
	}

	c->max_ostd = -1;
	if (!of_property_read_s32(np, "max_ostd", &c->max_ostd)) {
		if (c->max_ostd < 0)
			mmqos_error(c, "max_ostd is negative (%d)\n",
				    c->max_ostd);
		else
			c->max_bw_bound = c->max_ostd * 256 * 2;
	} else {
		mmqos_error(c, "max_ostd missing\n");
	}

	/*
	 * "comm_freq" names the classes whose mux is shared by both SMI
	 * commons.  The board DT spells them in full, i.e. "disp_freq" and
	 * "mdp_freq", so compare against the property name and not the short
	 * class name.
	 */
	of_property_for_each_string(np, "comm_freq", prop, s) {
		if (c->ncomm >= MMQOS_MAX_COMM)
			break;
		for (i = 0; i < c->ncls; i++) {
			snprintf(class_prop, sizeof(class_prop), "%s_freq",
				 c->cls[i].name);
			if (!strcmp(s, class_prop))
				break;
		}
		if (i == c->ncls) {
			mmqos_error(c, "comm_freq names unknown class \"%s\"\n",
				    s);
			continue;
		}
		c->comm_freq[c->ncomm++] = s;
	}

	of_property_for_each_u32(np, "fmeter_mux_ids", prop, p, value) {
		if (c->nfmeter >= MMQOS_MAX_FMETER)
			break;
		c->fmeter_mux[c->nfmeter++] = value;
	}
}

static void mmqos_parse_vcore(struct mmqos_contract *c)
{
	if (!of_find_property(c->dev->of_node, "vcore-supply", NULL)) {
		mmqos_error(c, "vcore-supply missing\n");
		return;
	}

	c->vcore = devm_regulator_get_optional(c->dev, "vcore");
	if (IS_ERR(c->vcore)) {
		c->vcore_err = PTR_ERR(c->vcore);
		c->vcore = NULL;
		if (c->vcore_err == -EPROBE_DEFER)
			mmqos_warn(c, "vcore regulator defers\n");
		else
			mmqos_error(c, "vcore regulator unavailable (%d)\n",
				    c->vcore_err);
	}
}

static int mmqos_contract_parse(struct mmqos_contract *c,
				struct device_node *np)
{
	int i, ret;

	ret = mmqos_parse_clocks(c, np);
	if (ret)
		return ret;

	ret = mmqos_parse_vopp(c, np);
	if (ret)
		return ret;

	for (i = 0; i < MMQOS_MAX_CLASS; i++) {
		ret = mmqos_parse_class(c, np, mmqos_class_names[i]);
		if (ret)
			return ret;
	}
	if (!c->ncls) {
		mmqos_error(c, "no frequency class parsed\n");
		return -EINVAL;
	}

	ret = mmqos_parse_larbs(c, np);
	if (ret)
		return ret;

	c->virtual_larb[MMQOS_VIRTUAL_DISP] = true;
	c->virtual_larb[MMQOS_VIRTUAL_MD] = true;
	c->virtual_larb[MMQOS_VIRTUAL_CCU] = true;
	c->virtual_larb[MMQOS_VIRTUAL_CCU2] = true;

	mmqos_parse_aux(c, np);
	mmqos_parse_vcore(c);

	return 0;
}

/* ------------------------------------------------------------------ */
/* debugfs: read-only view of the contract                             */
/* ------------------------------------------------------------------ */

static void mmqos_dump_clock(struct seq_file *s, struct mmqos_contract *c,
			     u32 idx)
{
	if (idx >= c->nclocks) {
		seq_printf(s, "clock%u <out of range>\n", idx);
		return;
	}
	seq_printf(s, "%s", c->clock_name[idx]);
	if (!c->clock[idx])
		seq_puts(s, " <unresolved>");
}

static void mmqos_dump_step(struct seq_file *s, struct mmqos_contract *c,
			    struct mmqos_step *step)
{
	seq_printf(s, "    %4u MHz  ", step->freq_mhz);
	switch (step->clk_type) {
	case MMQOS_CLK_MUX:
		seq_puts(s, "mux ");
		mmqos_dump_clock(s, c, step->clk_param1);
		seq_puts(s, " <- ");
		mmqos_dump_clock(s, c, step->clk_param2);
		break;
	case MMQOS_CLK_PLL:
		seq_printf(s, "pll id=%u value=%#x",
			   step->clk_param1, step->clk_param2);
		break;
	default:
		seq_puts(s, "no clock change");
		break;
	}
	seq_puts(s, "\n");
}

static int mmqos_contract_show(struct seq_file *s, void *unused)
{
	struct mmqos_contract *c = s->private;
	int i, j;

	seq_printf(s, "node: %pOF\n", c->dev->of_node);
	seq_printf(s, "clocks: %d (%d unresolved)\n",
		   c->nclocks, c->nclocks_missing);
	for (i = 0; i < c->nclocks; i++)
		seq_printf(s, "  [%2d] %s%s\n", i, c->clock_name[i],
			   c->clock[i] ? "" : "  <unresolved>");

	seq_printf(s, "vopp_steps: %d\n  ", c->nvopp);
	for (i = 0; i < c->nvopp; i++)
		seq_printf(s, "%u ", c->vopp[i]);
	seq_puts(s, "\n");

	seq_printf(s, "classes: %d\n", c->ncls);
	for (i = 0; i < c->ncls; i++) {
		seq_printf(s, "  %s: %d steps\n",
			   c->cls[i].name, c->cls[i].nsteps);
		for (j = 0; j < c->cls[i].nsteps; j++)
			mmqos_dump_step(s, c, &c->cls[i].step[j]);
	}

	seq_printf(s, "larb_groups: %d\n", c->nlarb_groups);
	for (i = 0; i < c->nlarb_groups; i++) {
		struct mmqos_larb *larb = &c->larb[c->larb_groups[i]];

		seq_printf(s, "  larb%-2u %2u ports  ratio",
			   c->larb_groups[i], larb->nports);
		for (j = 0; j < larb->nports; j++)
			seq_printf(s, " %u", larb->ratio[j]);
		if (larb->is_cam)
			seq_puts(s, "  [cam]");
		if (larb->is_max_ostd)
			seq_puts(s, "  [max-ostd]");
		seq_puts(s, "\n");
	}

	seq_printf(s, "cam_larb: %d\n ", c->ncam_larb);
	for (i = 0; i < c->ncam_larb; i++) {
		seq_printf(s, " %u", c->cam_larb[i]);
		if (c->virtual_larb[c->cam_larb[i]])
			seq_puts(s, "(virt)");
	}
	seq_puts(s, "\n");

	seq_printf(s, "virtual larbs: disp=%u md=%u ccu=%u ccu2=%u (SMI_LARB_NUM=%u)\n",
		   MMQOS_VIRTUAL_DISP, MMQOS_VIRTUAL_MD, MMQOS_VIRTUAL_CCU,
		   MMQOS_VIRTUAL_CCU2, MMQOS_SMI_LARB_NUM);

	seq_printf(s, "max_ostd_larb: %d\n ", c->nmax_ostd_larb);
	for (i = 0; i < c->nmax_ostd_larb; i++)
		seq_printf(s, " %u", c->max_ostd_larb[i]);
	seq_printf(s, "\nmax_ostd: %d  bound: %u MB/s\n",
		   c->max_ostd, c->max_bw_bound);

	seq_printf(s, "comm_freq: %d\n ", c->ncomm);
	for (i = 0; i < c->ncomm; i++)
		seq_printf(s, " %s", c->comm_freq[i]);
	seq_puts(s, "\n");

	seq_printf(s, "fmeter_mux_ids: %d\n ", c->nfmeter);
	for (i = 0; i < c->nfmeter; i++)
		seq_printf(s, " %u", c->fmeter_mux[i]);
	seq_puts(s, "\n");

	if (c->vcore) {
		int uv = regulator_get_voltage(c->vcore);

		if (uv >= 0)
			seq_printf(s, "vcore: owned, %d uV\n", uv);
		else
			seq_printf(s, "vcore: owned, voltage unreadable (%d)\n",
				   uv);
	} else {
		seq_printf(s, "vcore: unavailable (%d)\n", c->vcore_err);
	}

	seq_printf(s, "verdict: %s (%d errors, %d warnings)\n",
		   c->nerrors ? "NOT OK" : "OK", c->nerrors, c->nwarnings);

	return 0;
}

static int mmqos_contract_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmqos_contract_show, inode->i_private);
}

static const struct file_operations mmqos_contract_fops = {
	.owner = THIS_MODULE,
	.open = mmqos_contract_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *mmqos_debugfs_root;
static struct dentry *mmqos_debugfs_contract;

static void mmqos_debugfs_init(struct mmqos_contract *c)
{
	mmqos_debugfs_root = debugfs_create_dir("mmqos-pmqos", NULL);
	if (IS_ERR_OR_NULL(mmqos_debugfs_root)) {
		mmqos_debugfs_root = NULL;
		return;
	}
	mmqos_debugfs_contract = debugfs_create_file("contract", 0444,
						     mmqos_debugfs_root, c,
						     &mmqos_contract_fops);
}

static void mmqos_debugfs_exit(void)
{
	debugfs_remove(mmqos_debugfs_contract);
	debugfs_remove(mmqos_debugfs_root);
	mmqos_debugfs_contract = NULL;
	mmqos_debugfs_root = NULL;
}

static int mmqos_pmqos_probe(struct platform_device *pdev)
{
	struct mmqos_contract *c;
	int ret, i;

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = &pdev->dev;

	ret = mmqos_contract_parse(c, pdev->dev.of_node);
	if (ret) {
		dev_err(&pdev->dev,
			"board QoS contract is incomplete, driver stays out of the way\n");
		return ret;
	}

	dev_info(&pdev->dev,
		 "contract: %d clocks (%d unresolved), %d classes, %d larbs, %d cam larbs, max_ostd %d, vcore %s\n",
		 c->nclocks, c->nclocks_missing, c->ncls, c->nlarb_groups,
		 c->ncam_larb, c->max_ostd,
		 c->vcore ? "owned" : "unavailable");

	for (i = 0; i < c->ncls; i++)
		dev_info(&pdev->dev, "  %-4s %d steps, top %u MHz\n",
			 c->cls[i].name, c->cls[i].nsteps,
			 c->cls[i].step[0].freq_mhz);

	if (c->nerrors)
		dev_warn(&pdev->dev,
			 "contract parsed with %d errors and %d warnings; nothing will be applied\n",
			 c->nerrors, c->nwarnings);

	platform_set_drvdata(pdev, c);
	mmqos_debugfs_init(c);

	return 0;
}

static int mmqos_pmqos_remove(struct platform_device *pdev)
{
	mmqos_debugfs_exit();
	return 0;
}

static const struct of_device_id mmqos_pmqos_of_ids[] = {
	{ .compatible = "mediatek,mmdvfs_pmqos" },
	{ }
};
MODULE_DEVICE_TABLE(of, mmqos_pmqos_of_ids);

static struct platform_driver mmqos_pmqos_driver = {
	.probe = mmqos_pmqos_probe,
	.remove = mmqos_pmqos_remove,
	.driver = {
		.name = "mtk-mmqos-pmqos",
		.of_match_table = mmqos_pmqos_of_ids,
	},
};
module_platform_driver(mmqos_pmqos_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MM QoS / SMI BWC front end for the 4.19 mmdvfs_pmqos board contract");

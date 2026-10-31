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

#include "mtk-interconnect-provider.h"
#include "mmqos-port-names.h"

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

struct mmqos_icc;

struct mmqos_contract {
	struct device *dev;

	struct mmqos_icc *icc;

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
/* Stage 2: an icc provider over the parsed contract                   */
/* ------------------------------------------------------------------ */

#define MMQOS_COMMON_NUM	2

/*
 * Node ids are private to this provider.  The board DT has no "interconnects"
 * phandle pointing at us, so nothing depends on the vendor encoding; all that
 * matters is that they are unique and positive.
 */
#define MMQOS_NODE_MASTER(port)	(0x40000000 | ((port) & 0x1fffff))
#define MMQOS_NODE_LARB(larb)	(0x30000000 | ((larb) & 0xff))
#define MMQOS_NODE_COMMON(c)	(0x10000000 | ((c) & 0xff))

/*
 * Which SMI common each larb hangs off, from the vendor node table
 * (drivers/interconnect/mediatek/mmqos-mt6893.c).  Larbs 6/10/12/15 do not
 * exist on this SoC and never appear in "larb_groups".
 */
static const s8 mmqos_larb_common[MMQOS_MAX_LARB] = {
	[0] = 0, [1] = 0, [2] = 1, [3] = 1, [4] = 1, [5] = 0, [6] = -1,
	[7] = 0, [8] = 1, [9] = 1, [10] = -1, [11] = 0, [12] = -1,
	[13] = 1, [14] = 0, [15] = -1, [16] = 1, [17] = 0, [18] = 1,
	[19] = 0, [20] = 0,
	/*
	 * The synthetic larbs.  These numbers are the vendor node table's,
	 * not 4.19's VIRTUAL_*_LARB_ID enum: mmqos-mt6893.c puts Virt-CCU at
	 * 22 and Virt-Disp at 23, and the 6.6 dtsi agrees (disp_hrt_qos is a
	 * path from SLAVE_LARB(23) to SLAVE_COMMON(0), ccu_g from
	 * SLAVE_LARB(21) to SLAVE_COMMON(1)).  larb21 is the CCU's write
	 * port, which is why the vendor marks it is_write.
	 */
	[21] = 1, [22] = 0, [23] = 0, [24] = 0,
};

/*
 * A few consumer names resolve to a synthetic larb rather than to a port, so
 * the path starts at the larb node itself.  Display's HRT vote is one of them
 * and it is the main display QoS request, so leaving it out kept
 * priv->hrt_bw_request NULL and the whole display bandwidth path dead.
 */
struct mmqos_larb_name {
	const char *name;
	u32 larb;
};

static const struct mmqos_larb_name mmqos_larb_names[] = {
	{ "disp_hrt_qos", 23 },	/* Virt-Disp */
	{ "ccu_g", 21 },	/* Virt-CCU write port */
};

static const char *const mmqos_common_names[MMQOS_COMMON_NUM] = {
	"smi-disp-common", "smi-mdp-common",
};

struct mmqos_icc_node {
	bool is_write;
};

struct mmqos_icc {
	struct icc_provider prov;
	struct icc_node *common[MMQOS_COMMON_NUM];
	struct icc_node *larb[MMQOS_MAX_LARB];
	struct icc_node *master[ARRAY_SIZE(mmqos_port_names)];
	int nmaster;
	char larb_name[MMQOS_MAX_LARB][12];

	/* observability only: Stage 2 programs nothing */
	u64 set_calls;
	u64 set_last_avg;
	u64 set_last_peak;
	/*
	 * Requests are set and cleared inside a single frame, so sampling the
	 * debugfs file almost always catches the cleared state.  Keep the high
	 * water mark instead.
	 */
	u32 max_avg;
	u32 max_peak;
	const char *max_avg_node;
	const char *max_peak_node;
};

static int mmqos_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
			       u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	*agg_avg += avg_bw;
	if (peak_bw > *agg_peak)
		*agg_peak = peak_bw;
	return 0;
}

static bool mmqos_icc_path_is_write(struct icc_node *node)
{
	struct mmqos_icc_node *n = node->data;

	return n ? n->is_write : false;
}

static int mmqos_icc_comm_chn_info(struct icc_node *src, struct icc_node *dst)
{
	return 0;
}

static int mmqos_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct mmqos_icc *icc = dst->provider->data;

	/*
	 * Stage 2 proves that the requests arrive and that they aggregate per
	 * larb and per common.  Nothing is programmed here on purpose: the mux
	 * step is driven by the MM pm_qos frequency classes in 4.19, and this
	 * tree dropped them entirely, so there is no frequency input to act
	 * on yet; the SMI OSTD/ratio write is Stage 4.
	 */
	icc->set_calls++;
	icc->set_last_avg = dst->avg_bw;
	icc->set_last_peak = dst->peak_bw;
	if (dst->avg_bw > icc->max_avg) {
		icc->max_avg = dst->avg_bw;
		icc->max_avg_node = dst->name;
	}
	if (dst->peak_bw > icc->max_peak) {
		icc->max_peak = dst->peak_bw;
		icc->max_peak_node = dst->name;
	}
	return 0;
}

static struct icc_node *mmqos_icc_xlate(struct of_phandle_args *spec,
					void *data)
{
	/* No board DT phandle points here; consumers resolve by name. */
	return ERR_PTR(-EINVAL);
}

static struct icc_node *mmqos_icc_master_by_port(struct mmqos_icc *icc,
						 u32 port)
{
	int i;

	for (i = 0; i < icc->nmaster; i++)
		if (icc->master[i]->id == MMQOS_NODE_MASTER(port))
			return icc->master[i];
	return NULL;
}

static int mmqos_icc_add_common(struct mmqos_contract *c, struct mmqos_icc *icc,
				int idx)
{
	struct icc_node *node;

	node = mtk_icc_node_create(MMQOS_NODE_COMMON(idx));
	if (IS_ERR(node))
		return PTR_ERR(node);
	node->name = mmqos_common_names[idx];
	mtk_icc_node_add(node, &icc->prov);
	icc->common[idx] = node;
	return 0;
}

static int mmqos_icc_add_larb(struct mmqos_contract *c, struct mmqos_icc *icc,
			      u32 larb)
{
	struct mmqos_icc_node *priv;
	struct icc_node *node;
	int ret;

	if (icc->larb[larb])
		return 0;
	if (larb >= MMQOS_MAX_LARB || mmqos_larb_common[larb] < 0)
		return -EINVAL;

	node = mtk_icc_node_create(MMQOS_NODE_LARB(larb));
	if (IS_ERR(node))
		return PTR_ERR(node);

	priv = devm_kzalloc(c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->is_write = (larb == 21);	/* Virt-CCU write port */
	node->data = priv;

	snprintf(icc->larb_name[larb], sizeof(icc->larb_name[larb]),
		 "larb%u", larb);
	node->name = icc->larb_name[larb];
	mtk_icc_node_add(node, &icc->prov);

	ret = mtk_icc_link_create(node, MMQOS_NODE_COMMON(mmqos_larb_common[larb]));
	if (ret)
		return ret;

	icc->larb[larb] = node;
	return 0;
}

static int mmqos_icc_add_master(struct mmqos_contract *c, struct mmqos_icc *icc,
				const struct mmqos_port_name *p)
{
	struct icc_node *node;
	struct mmqos_icc_node *priv;
	u32 larb = MTK_M4U_TO_LARB(p->port);
	int ret;

	node = mmqos_icc_master_by_port(icc, p->port);
	if (node) {
		/* Same hardware port, another QoS channel name. */
		return mtk_icc_node_add_alias(node, p->name);
	}

	if (larb >= MMQOS_MAX_LARB) {
		mmqos_error(c, "%s maps to larb%u, out of range\n", p->name, larb);
		return 0;
	}
	if (!c->larb[larb].present) {
		/*
		 * A synthetic larb (21..24).  The board's larb_groups does not
		 * list them because they have no SMI port table, but they are
		 * real path endpoints: disp_hrt_qos starts at Virt-Disp.
		 */
		ret = mmqos_icc_add_larb(c, icc, larb);
		if (ret) {
			mmqos_warn(c, "%s: synthetic larb%u unavailable (%d), node skipped\n",
				   p->name, larb, ret);
			return 0;
		}
	}

	node = mtk_icc_node_create(MMQOS_NODE_MASTER(p->port));
	if (IS_ERR(node))
		return PTR_ERR(node);

	priv = devm_kzalloc(c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->is_write = p->is_write;
	node->data = priv;

	node->name = p->name;
	mtk_icc_node_add(node, &icc->prov);

	ret = mtk_icc_link_create(node, MMQOS_NODE_LARB(larb));
	if (ret)
		return ret;

	icc->master[icc->nmaster++] = node;
	return 0;
}

static void mmqos_icc_teardown(struct mmqos_icc *icc);

static int mmqos_icc_build(struct mmqos_contract *c, struct mmqos_icc *icc)
{
	int i, ret;

	icc->prov.dev = c->dev;
	icc->prov.set = mmqos_icc_set;
	icc->prov.aggregate = mmqos_icc_aggregate;
	icc->prov.path_is_write = mmqos_icc_path_is_write;
	icc->prov.comm_chn_info = mmqos_icc_comm_chn_info;
	icc->prov.xlate = mmqos_icc_xlate;
	icc->prov.data = icc;

	ret = mtk_icc_provider_add(&icc->prov);
	if (ret) {
		mmqos_error(c, "mtk_icc_provider_add failed (%d)\n", ret);
		return ret;
	}

	for (i = 0; i < MMQOS_COMMON_NUM; i++) {
		ret = mmqos_icc_add_common(c, icc, i);
		if (ret)
			goto err;
	}

	for (i = 0; i < c->nlarb_groups; i++) {
		ret = mmqos_icc_add_larb(c, icc, c->larb_groups[i]);
		if (ret)
			goto err;
	}

	for (i = 0; i < ARRAY_SIZE(mmqos_larb_names); i++) {
		const struct mmqos_larb_name *ln = &mmqos_larb_names[i];

		if (ln->larb >= MMQOS_MAX_LARB ||
		    mmqos_larb_common[ln->larb] < 0) {
			mmqos_error(c, "%s maps to larb%u, which has no common\n",
				    ln->name, ln->larb);
			ret = -EINVAL;
			goto err;
		}
		ret = mmqos_icc_add_larb(c, icc, ln->larb);
		if (ret)
			goto err;
		ret = mtk_icc_node_add_alias(icc->larb[ln->larb], ln->name);
		if (ret)
			goto err;
	}

	for (i = 0; i < ARRAY_SIZE(mmqos_port_names); i++) {
		ret = mmqos_icc_add_master(c, icc, &mmqos_port_names[i]);
		if (ret)
			goto err;
	}

	dev_info(c->dev,
		 "icc provider up: %d larbs, %d ports, %d names, 2 commons\n",
		 c->nlarb_groups, icc->nmaster,
		 (int)ARRAY_SIZE(mmqos_port_names));
	return 0;

err:
	mmqos_error(c, "icc provider build failed at entry %d (%d)\n", i, ret);
	mmqos_icc_teardown(icc);
	return ret;
}

static void mmqos_icc_teardown(struct mmqos_icc *icc)
{
	int i;

	for (i = 0; i < icc->nmaster; i++) {
		struct icc_node *node = icc->master[i];
		u32 larb = MTK_M4U_TO_LARB(node->id & 0x1fffff);

		if (larb < MMQOS_MAX_LARB && icc->larb[larb])
			mtk_icc_link_destroy(node, icc->larb[larb]);
		mtk_icc_node_del_alias(node);
		mtk_icc_node_del(node);
		mtk_icc_node_destroy(node->id);
	}
	icc->nmaster = 0;

	for (i = 0; i < MMQOS_MAX_LARB; i++) {
		struct icc_node *node = icc->larb[i];

		if (!node)
			continue;
		mtk_icc_link_destroy(node,
				     icc->common[mmqos_larb_common[i]]);
		mtk_icc_node_del(node);
		mtk_icc_node_destroy(node->id);
		icc->larb[i] = NULL;
	}

	for (i = 0; i < MMQOS_COMMON_NUM; i++) {
		if (!icc->common[i])
			continue;
		mtk_icc_node_del(icc->common[i]);
		mtk_icc_node_destroy(icc->common[i]->id);
		icc->common[i] = NULL;
	}

	mtk_icc_provider_del(&icc->prov);
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

static int mmqos_topology_show(struct seq_file *s, void *unused)
{
	struct mmqos_contract *c = s->private;
	struct mmqos_icc *icc = c->icc;
	int i;

	if (!icc) {
		seq_puts(s, "no icc provider registered\n");
		return 0;
	}

	seq_printf(s, "provider: %d ports, %d larbs, set() calls %llu (last avg %llu peak %llu)\n",
		   icc->nmaster, c->nlarb_groups, icc->set_calls,
		   icc->set_last_avg, icc->set_last_peak);
	seq_printf(s, "peak seen: avg %u on %s, peak %u on %s\n",
		   icc->max_avg, icc->max_avg_node ? icc->max_avg_node : "-",
		   icc->max_peak, icc->max_peak_node ? icc->max_peak_node : "-");
	seq_puts(s, "node                              avg         peak\n");
	seq_puts(s, "--------------------------------------------------\n");

	for (i = 0; i < MMQOS_COMMON_NUM; i++) {
		if (!icc->common[i])
			continue;
		seq_printf(s, "%-30s %12u %12u\n", icc->common[i]->name,
			   icc->common[i]->avg_bw, icc->common[i]->peak_bw);
	}

	for (i = 0; i < MMQOS_MAX_LARB; i++) {
		if (!icc->larb[i])
			continue;
		seq_printf(s, "%-30s %12u %12u\n", icc->larb[i]->name,
			   icc->larb[i]->avg_bw, icc->larb[i]->peak_bw);
	}

	return 0;
}

static int mmqos_topology_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmqos_topology_show, inode->i_private);
}

static const struct file_operations mmqos_topology_fops = {
	.owner = THIS_MODULE,
	.open = mmqos_topology_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * Stage 2 harness.  This exercises exactly the path the multimedia drivers
 * use -- of_mtk_icc_get(dev, name) on a device whose node has no
 * "interconnects" -- so a success here is the same resolution MDP would get.
 *
 *   echo "mdp_rdma0 1000 2000" > selftest
 *
 * The bandwidth is released again before returning, so the aggregates this
 * prints are the ones produced by that single request.
 */
static ssize_t mmqos_selftest_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *ppos)
{
	struct mmqos_contract *c = file->private_data;
	struct icc_path *path;
	char *buf, name[64];
	u32 avg = 0, peak = 0;
	int ret, i;

	if (*ppos != 0 || len == 0 || len > 128)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = sscanf(buf, "%63s %u %u", name, &avg, &peak);
	kfree(buf);
	if (ret < 1) {
		dev_info(c->dev, "selftest: expected \"<name> [avg] [peak]\"\n");
		return -EINVAL;
	}

	path = of_mtk_icc_get(c->dev, name);
	if (!path) {
		dev_info(c->dev, "selftest: %s did not resolve (NULL path)\n",
			 name);
		return len;
	}
	if (IS_ERR(path)) {
		dev_info(c->dev, "selftest: %s failed (%ld)\n", name,
			 PTR_ERR(path));
		return len;
	}

	ret = mtk_icc_set_bw(path, avg, peak);
	dev_info(c->dev, "selftest: %s -> avg %u peak %u, set_bw %d\n",
		 name, avg, peak, ret);

	for (i = 0; i < MMQOS_MAX_LARB; i++) {
		struct icc_node *n = c->icc->larb[i];

		if (n && (n->avg_bw || n->peak_bw))
			dev_info(c->dev, "selftest:   %-8s avg %u peak %u\n",
				 n->name, n->avg_bw, n->peak_bw);
	}
	for (i = 0; i < MMQOS_COMMON_NUM; i++) {
		struct icc_node *n = c->icc->common[i];

		if (n && (n->avg_bw || n->peak_bw))
			dev_info(c->dev, "selftest:   %-8s avg %u peak %u\n",
				 n->name, n->avg_bw, n->peak_bw);
	}

	mtk_icc_set_bw(path, 0, 0);
	mtk_icc_put(path);
	return len;
}

static const struct file_operations mmqos_selftest_fops = {
	.owner = THIS_MODULE,
	.write = mmqos_selftest_write,
	.open = simple_open,
	.llseek = noop_llseek,
};

static struct dentry *mmqos_debugfs_root;
static struct dentry *mmqos_debugfs_contract;
static struct dentry *mmqos_debugfs_topology;
static struct dentry *mmqos_debugfs_selftest;

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
	mmqos_debugfs_topology = debugfs_create_file("topology", 0444,
						     mmqos_debugfs_root, c,
						     &mmqos_topology_fops);
	mmqos_debugfs_selftest = debugfs_create_file("selftest", 0200,
						     mmqos_debugfs_root, c,
						     &mmqos_selftest_fops);
}

static void mmqos_debugfs_exit(void)
{
	debugfs_remove(mmqos_debugfs_selftest);
	debugfs_remove(mmqos_debugfs_topology);
	debugfs_remove(mmqos_debugfs_contract);
	debugfs_remove(mmqos_debugfs_root);
	mmqos_debugfs_selftest = NULL;
	mmqos_debugfs_topology = NULL;
	mmqos_debugfs_contract = NULL;
	mmqos_debugfs_root = NULL;
}

static bool provide = true;
module_param(provide, bool, 0444);
MODULE_PARM_DESC(provide,
		 "register the icc provider over the parsed contract (default: 1)");

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

	if (c->nerrors) {
		dev_warn(&pdev->dev,
			 "contract parsed with %d errors and %d warnings; the provider will not be registered\n",
			 c->nerrors, c->nwarnings);
	} else if (provide) {
		c->icc = devm_kzalloc(&pdev->dev, sizeof(*c->icc), GFP_KERNEL);
		if (!c->icc)
			return -ENOMEM;
		ret = mmqos_icc_build(c, c->icc);
		if (ret) {
			c->icc = NULL;
			return ret;
		}
	}

	platform_set_drvdata(pdev, c);
	mmqos_debugfs_init(c);

	return 0;
}

static int mmqos_pmqos_remove(struct platform_device *pdev)
{
	struct mmqos_contract *c = platform_get_drvdata(pdev);

	mmqos_debugfs_exit();

	if (c && c->icc) {
		if (c->icc->prov.users) {
			dev_warn(&pdev->dev,
				 "provider still has %d active paths; refusing to unregister\n",
				 c->icc->prov.users);
			return -EBUSY;
		}
		mmqos_icc_teardown(c->icc);
		c->icc = NULL;
	}

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

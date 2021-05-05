// SPDX-License-Identifier: GPL-2.0
/*
 * RZG2L Clock Pulse Generator
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/psci.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <dt-bindings/clock/renesas-cpg-mssr.h>

#include "renesas-rzg2l-cpg.h"

#ifdef DEBUG
#define WARN_DEBUG(x)	WARN_ON(x)
#else
#define WARN_DEBUG(x)	do { } while (0)
#endif

/*register value parser*/
/*Right shift and apply mask*/
#define DIV_RSMASK(v, s, m)	((v >> s) & m)
#define GET_REG(val)		((val >> 20) & 0xfff)
#define GET_SHIFT(val)		((val >> 12) & 0xff)
#define GET_WIDTH(val)		((val >> 8) & 0xf)

#define KDIV(val)		DIV_RSMASK(val, 16, 0xffff)
#define MDIV(val)		DIV_RSMASK(val, 6, 0x3ff)
#define PDIV(val)		DIV_RSMASK(val, 0, 0x3f)
#define SDIV(val)		DIV_RSMASK(val, 0, 0x7)

/**
 * Clock Pulse Generator Private Data
 *
 * @dev: CPG/MSSR device
 * @base: CPG/MSSR register block base address
 * @rmw_lock: protects RMW register accesses
 * @clks: Array containing all Core and Module Clocks
 * @num_core_clks: Number of Core Clocks in clks[]
 * @num_mod_clks: Number of Module Clocks in clks[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @notifiers: Notifier chain to save/restore clock state for system resume
 */
struct cpg_mssr_priv {
#ifdef CONFIG_RESET_CONTROLLER
	struct reset_controller_dev rcdev;
#endif
	struct device *dev;
	void __iomem *base;
	spinlock_t rmw_lock;

	struct clk **clks;
	unsigned int num_core_clks;
	unsigned int num_mod_clks;
	unsigned int last_dt_core_clk;

	struct raw_notifier_head notifiers;
	const struct cpg_mssr_info *info;
};

static const struct of_device_id rzg2l_cpg_match[] = {
#ifdef CONFIG_CLK_R9A07G044L
	{
		.compatible = "renesas,r9a07g044l-cpg",
		.data = &r9a07g044l_cpg_info,
	},
#endif
#ifdef CONFIG_CLK_R9A07G054L
	{
		.compatible = "renesas,r9a07g054l-cpg",
		.data = &r9a07g054l_cpg_info,
	},
#endif
	{ /* sentinel */ }
};

static void rzg2l_cpg_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

struct clk * __init rzg2l_cpg_div_clk_register(const struct cpg_core_clk *core,
						struct clk **clks,
						void __iomem *base,
						struct cpg_mssr_priv *priv)
{
	const struct clk *parent;
	struct clk_hw *clk_hw;
	struct device *dev = priv->dev;
	const char *parent_name;

	parent = clks[core->parent & 0xffff];   /* some types use high bits */
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	if (core->dtable)
		clk_hw = clk_hw_register_divider_table(dev,
			core->name, parent_name, 0,
			base + GET_REG(core->conf),
			GET_SHIFT(core->conf), GET_WIDTH(core->conf),
			core->flag, core->dtable, &priv->rmw_lock);
	else
		clk_hw = clk_hw_register_divider(dev, core->name,
			parent_name, 0, base + GET_REG(core->conf),
			GET_SHIFT(core->conf), GET_WIDTH(core->conf),
			core->flag, &priv->rmw_lock);

	if (IS_ERR(clk_hw))
		return NULL;

	return clk_hw->clk;
}

struct clk * __init rzg2l_cpg_sell_clk_register(const struct cpg_core_clk *core,
		void __iomem *base, struct cpg_mssr_priv *priv)
{
	const struct clk_hw *clk_hw;

	clk_hw = clk_hw_register_mux(priv->dev, core->name,
			core->parent_names, core->num_parents,
			core->flag == CLK_MUX_READ_ONLY ?
					CLK_SET_RATE_PARENT : 0,
			base + GET_REG(core->conf),
			GET_SHIFT(core->conf), GET_WIDTH(core->conf),
			core->flag, &priv->rmw_lock);

	if (IS_ERR(clk_hw))
		return NULL;

	return clk_hw->clk;
}

struct div2_clk {
	struct clk_hw hw;
	unsigned int conf;
	struct clk_div_table *dtable;
	unsigned int confs;
	struct clk_div_table *dtables;
	void __iomem *base;
	struct cpg_mssr_priv *priv;
};

#define to_d2clk(_hw)	container_of(_hw, struct div2_clk, hw)
unsigned int div2_clock_get_div(unsigned int val,
			struct clk_div_table *t,
			int length)
{
	int i;

	for (i = 0; i <= length; i++)
		if (val == t[i].val)
			return t[i].div;

	/*return div as 1 if failed*/
	return 1;
}

static unsigned long rzg2l_cpg_div2_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct div2_clk *d2clk = to_d2clk(hw);
	u32 div, divs, val, vals;

	val = readl(d2clk->base + GET_REG(d2clk->conf));
	div = DIV_RSMASK(val, GET_SHIFT(d2clk->conf),
			(BIT(GET_WIDTH(d2clk->conf)) - 1));
	div = div2_clock_get_div(div, d2clk->dtable,
			(BIT(GET_WIDTH(d2clk->conf)) - 1));

	vals = readl(d2clk->base + GET_REG(d2clk->confs));
	divs = DIV_RSMASK(val, GET_SHIFT(d2clk->confs),
			(BIT(GET_WIDTH(d2clk->confs)) - 1));
	divs = div2_clock_get_div(divs, d2clk->dtables,
			(BIT(GET_WIDTH(d2clk->confs)) - 1));

	div = div * divs;

	return DIV_ROUND_CLOSEST_ULL((u64)parent_rate, div);
}

static long rzg2l_cpg_div2_clk_round_rate(struct clk_hw *hw,
					unsigned long rate,
					unsigned long *parent_rate)
{
	struct div2_clk *d2clk = to_d2clk(hw);
	unsigned long best_diff = (unsigned long)-1;
	unsigned long diff;
	unsigned int best_div, best_divs, div, divs;
	int i, j, n, ns;

	n = BIT(GET_WIDTH(d2clk->conf)) - 1;
	ns = BIT(GET_WIDTH(d2clk->confs)) - 1;
	for (i = 0; i <= n; i++) {
		for (j = 0; j <= ns; j++) {
			div = div2_clock_get_div(i, d2clk->dtable, n);
			divs = div2_clock_get_div(j, d2clk->dtables, ns);
			diff = abs(*parent_rate - (rate * div * divs));
			if (best_diff > diff) {
				best_diff = diff;
				best_div = div;
				best_divs = divs;
			}
		}
	}

	return DIV_ROUND_CLOSEST_ULL((u64)*parent_rate, best_div * best_divs);
}

static int rzg2l_cpg_div2_clk_set_rate(struct clk_hw *hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	struct div2_clk *d2clk = to_d2clk(hw);
	struct cpg_mssr_priv *priv = d2clk->priv;
	unsigned long best_diff = (unsigned long)-1;
	unsigned long diff, flags;
	unsigned int div, divs, val, vals, n, ns;
	int i, j;

	n = BIT(GET_WIDTH(d2clk->conf)) - 1;
	ns = BIT(GET_WIDTH(d2clk->confs)) - 1;
	for (i = 0; i <= n; i++) {
		for (j = 0; j <= ns; j++) {
			div = div2_clock_get_div(i, d2clk->dtable, n);
			divs = div2_clock_get_div(j, d2clk->dtables, ns);
			diff = abs(parent_rate - (rate * div * divs));
			if (best_diff > diff) {
				best_diff = diff;
				val = i;
				vals = j;
			}
		}
	}

	spin_lock_irqsave(&priv->rmw_lock, flags);

	val = (val << GET_SHIFT(d2clk->conf))
	    | (0x1 << (GET_SHIFT(d2clk->conf) + 16));
	writel(val, d2clk->base + GET_REG(d2clk->conf));

	vals = (vals << GET_SHIFT(d2clk->confs))
	    | (0x1 << (GET_SHIFT(d2clk->confs) + 16));
	writel(vals, d2clk->base + GET_REG(d2clk->confs));

	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	return 0;
}

static const struct clk_ops rzg2l_cpg_div2_ops = {
	.recalc_rate = rzg2l_cpg_div2_clk_recalc_rate,
	.round_rate = rzg2l_cpg_div2_clk_round_rate,
	.set_rate = rzg2l_cpg_div2_clk_set_rate,
};

struct clk * __init rzg2l_cpg_div2_clk_register(const struct cpg_core_clk *core,
						struct clk **clks,
						void __iomem *base,
						struct cpg_mssr_priv *priv)
{
	const struct clk *parent;
	struct clk_init_data init;
	struct div2_clk *d2clk;
	struct device *dev = priv->dev;
	struct clk *clk;
	const char *parent_name;

	parent = clks[core->parent & 0xffff];   /* some types use high bits */
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	d2clk = devm_kzalloc(dev, sizeof(*d2clk), GFP_KERNEL);
	if (!d2clk) {
		clk = ERR_PTR(-ENOMEM);
		return NULL;
	}

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &rzg2l_cpg_div2_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	d2clk->hw.init = &init;
	d2clk->conf = core->conf;
	d2clk->confs = core->confs;
	d2clk->dtable = core->dtable;
	d2clk->dtables = core->dtables;
	d2clk->base = base;
	d2clk->priv = priv;

	clk = clk_register(NULL, &d2clk->hw);
	if (IS_ERR(clk))
		kfree(d2clk);

	return clk;
}

struct pll_clk {
	struct clk_hw hw;
	unsigned int stby_conf;
	unsigned int mon_conf;
	unsigned int conf;
	void __iomem *base;
	struct cpg_mssr_priv *priv;
	unsigned int type;
};

#define to_pll(_hw)	container_of(_hw, struct pll_clk, hw)

static unsigned long rzg2l_cpg_pll_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct pll_clk *pll_clk = to_pll(hw);
	struct cpg_mssr_priv *priv = pll_clk->priv;
	unsigned int mult = 1;
	unsigned int div = 1;
	unsigned int val1, val2;

	if (pll_clk->type == CLK_TYPE_SAM_PLL) {
		val1 = readl(priv->base + GET_REG1(pll_clk->conf));
		val2 = readl(priv->base + GET_REG2(pll_clk->conf));
		mult = MDIV(val1) + KDIV(val1)/65536;
		div = PDIV(val1) * (1 << SDIV(val2));
	} else {
		return 0;
	}

	return DIV_ROUND_CLOSEST_ULL((u64)parent_rate * mult, div);
}

static const struct clk_ops rzg2l_cpg_pll_ops = {
	.recalc_rate = rzg2l_cpg_pll_clk_recalc_rate,
};

struct clk * __init rzg2l_cpg_pll_clk_register(const struct cpg_core_clk *core,
						struct clk **clks,
						void __iomem *base,
						struct cpg_mssr_priv *priv)
{
	const struct clk *parent;
	struct clk_init_data init;
	struct pll_clk *pll_clk;
	struct device *dev = priv->dev;
	struct clk *clk;
	const char *parent_name;

	parent = clks[core->parent & 0xffff];   /* some types use high bits */
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk) {
		clk = ERR_PTR(-ENOMEM);
		return NULL;
	}

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &rzg2l_cpg_pll_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll_clk->hw.init = &init;
	pll_clk->stby_conf = core->stby_conf;
	pll_clk->mon_conf = core->mon_conf;
	pll_clk->conf = core->conf;
	pll_clk->base = base;
	pll_clk->priv = priv;
	pll_clk->type = core->type;

	clk = clk_register(NULL, &pll_clk->hw);
	if (IS_ERR(clk))
		kfree(pll_clk);

	return clk;
}

static struct clk *rzg2l_cpg_clk_src_twocell_get(
				struct of_phandle_args *clkspec,
				void *data)
{
	unsigned int clkidx = clkspec->args[1];
	struct cpg_mssr_priv *priv = data;
	struct device *dev = priv->dev;
	const char *type;
	struct clk *clk;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		type = "core";
		if (clkidx > priv->last_dt_core_clk) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
			       clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[clkidx];
		break;

	case CPG_MOD:
		type = "module";
		if (clkidx > priv->num_core_clks + priv->num_mod_clks) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
				clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[priv->num_core_clks + clkidx];
		break;

	default:
		dev_err(dev, "Invalid CPG clock type %u\n", clkspec->args[0]);
		return ERR_PTR(-EINVAL);
	}

	if (IS_ERR(clk))
		dev_err(dev, "Cannot get %s clock %u: %ld", type, clkidx,
			PTR_ERR(clk));
	else
		dev_dbg(dev, "clock (%u, %u) is %pC at %lu Hz\n",
			clkspec->args[0], clkspec->args[1], clk,
			clk_get_rate(clk));
	return clk;
}

static void __init rzg2l_cpg_register_core_clk(const struct cpg_core_clk *core,
			const struct cpg_mssr_info *info,
			struct cpg_mssr_priv *priv)
{
	struct clk *clk = ERR_PTR(-ENOTSUPP), *parent;
	struct device *dev = priv->dev;
	unsigned int id = core->id, div = core->div;
	const char *parent_name;

	WARN_DEBUG(id >= priv->num_core_clks);
	WARN_DEBUG(PTR_ERR(priv->clks[id]) != -ENOENT);

	if (!core->name) {
		/* Skip NULLified clock */
		return;
	}

	switch (core->type) {
	case CLK_TYPE_IN:
		clk = of_clk_get_by_name(priv->dev->of_node, core->name);
		break;
	case CLK_TYPE_FF:
		WARN_DEBUG(core->parent >= priv->num_core_clks);
		parent = priv->clks[core->parent];
		if (IS_ERR(parent)) {
			clk = parent;
			goto fail;
		}

		parent_name = __clk_get_name(parent);
		clk = clk_register_fixed_factor(NULL, core->name,
				parent_name, CLK_SET_RATE_PARENT,
				core->mult, div);
		break;
	case CLK_TYPE_SAM_PLL:
		clk = rzg2l_cpg_pll_clk_register(core, priv->clks,
						 priv->base, priv);
		break;
	case CLK_TYPE_DIV:
		clk = rzg2l_cpg_div_clk_register(core, priv->clks,
						 priv->base, priv);
		break;
	case CLK_TYPE_2DIV:
		clk = rzg2l_cpg_div2_clk_register(core, priv->clks,
						  priv->base, priv);
		break;
	case CLK_TYPE_MUX:
		clk = rzg2l_cpg_sell_clk_register(core, priv->base, priv);
		break;
	default:
		goto fail;
	};

	if (IS_ERR_OR_NULL(clk))
		goto fail;

	dev_dbg(dev, "Core clock %pC at %lu Hz\n", clk, clk_get_rate(clk));
	priv->clks[id] = clk;
	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "core",
		core->name, PTR_ERR(clk));
}

/**
 * struct mstp_clock - MSTP gating clock
 * @hw: handle between common and hardware-specific interfaces
 * @bit: 16bits register offset, 8bits ON/MON, 8bits RESET
 * @priv: CPG/MSSR private data
 */
struct mstp_clock {
	struct clk_hw hw;
	u32 bit;
	struct cpg_mssr_priv *priv;
};

#define to_mod_clock(_hw) container_of(_hw, struct mstp_clock, hw)

static int rzg2l_mod_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct mstp_clock *clock = to_mod_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	unsigned int reg = MSSR_OFF(clock->bit) * 4;
	struct device *dev = priv->dev;
	unsigned long flags;
	unsigned int i;
	u32 value;

	if (clock->bit == 0) {
		dev_dbg(dev, "%pC does not support ON/OFF\n",  hw->clk);
		return 0;
	}

	dev_dbg(dev, "CLK_ON %u@%u/%pC %s\n",
			CLK_ON_R(reg), clock->bit, hw->clk,
			enable ? "ON" : "OFF");
	spin_lock_irqsave(&priv->rmw_lock, flags);

	if (enable)
		value = (MSSR_ON(clock->bit) << 16) | MSSR_ON(clock->bit);
	else
		value = MSSR_ON(clock->bit) << 16;
	writel(value, priv->base + CLK_ON_R(reg));

	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	if (!enable)
		return 0;

	for (i = 1000; i > 0; --i) {
		if (((readl(priv->base + CLK_MON_R(reg)))
			& MSSR_ON(clock->bit)))
			break;
		cpu_relax();
	}

	if (!i) {
		dev_err(dev, "Failed to enable CLK_ON %p\n",
			priv->base + CLK_ON_R(reg));
		return -ETIMEDOUT;
	}

	return 0;
}

static int rzg2l_mod_clock_enable(struct clk_hw *hw)
{
	return rzg2l_mod_clock_endisable(hw, true);
}

static void rzg2l_mod_clock_disable(struct clk_hw *hw)
{
	rzg2l_mod_clock_endisable(hw, false);
}

static int rzg2l_mod_clock_is_enabled(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mod_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	u32 value;

	if (clock->bit == 0) {
		dev_dbg(priv->dev, "%pC does not support ON/OFF\n",  hw->clk);
		return 1;
	}

	value = readl(priv->base + CLK_MON_R(MSSR_OFF(clock->bit) * 4));

	return !(value & (MSSR_ON(clock->bit)));
}

static const struct clk_ops rzg2l_mod_clock_ops = {
	.enable = rzg2l_mod_clock_enable,
	.disable = rzg2l_mod_clock_disable,
	.is_enabled = rzg2l_mod_clock_is_enabled,
};

static void __init rzg2l_cpg_register_mod_clk(const struct mssr_mod_clk *mod,
				     const struct cpg_mssr_info *info,
				     struct cpg_mssr_priv *priv)
{
	struct mstp_clock *clock = NULL;
	struct device *dev = priv->dev;
	unsigned int id = mod->id;
	struct clk_init_data init;
	struct clk *parent, *clk;
	const char *parent_name;
	unsigned int i;

	WARN_DEBUG(id < priv->num_core_clks);
	WARN_DEBUG(id >= priv->num_core_clks + priv->num_mod_clks);
	WARN_DEBUG(mod->parent >= priv->num_core_clks + priv->num_mod_clks);
	WARN_DEBUG(PTR_ERR(priv->clks[id]) != -ENOENT);

	if (!mod->name) {
		/* Skip NULLified clock */
		return;
	}

	parent = priv->clks[mod->parent];
	if (IS_ERR(parent)) {
		clk = parent;
		goto fail;
	}

	clock = devm_kzalloc(dev, sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		clk = ERR_PTR(-ENOMEM);
		goto fail;
	}

	init.name = mod->name;
	init.ops = &rzg2l_mod_clock_ops;
	init.flags = CLK_SET_RATE_PARENT;
	for (i = 0; i < info->num_crit_mod_clks; i++)
		if (id == info->crit_mod_clks[i]) {
			dev_dbg(dev, "CPG %s setting CLK_IS_CRITICAL\n",
				mod->name);
			init.flags |= CLK_IS_CRITICAL;
			break;
		}

	parent_name = __clk_get_name(parent);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->bit = mod->bit;
	clock->priv = priv;
	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto fail;

	dev_dbg(dev, "Module clock %pC at %lu Hz\n", clk, clk_get_rate(clk));
	priv->clks[id] = clk;
	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "module",
		mod->name, PTR_ERR(clk));
	kfree(clock);
}

#ifdef CONFIG_RESET_CONTROLLER

#define rcdev_to_priv(x)	container_of(x, struct cpg_mssr_priv, rcdev)

static int rzg2l_cpg_reset(struct reset_controller_dev *rcdev,
			  unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	const struct cpg_mssr_info *info = priv->info;
	unsigned int reg = MSSR_OFF(info->mod_clks[id].bit) * 0x4;
	u32 dis = MSSR_RES(info->mod_clks[id].bit);
	u32 we = dis << 16;

	dev_dbg(rcdev->dev, "reset %u%02u\n", reg, info->mod_clks[id].bit);

	/* Reset module */
	writel(we, priv->base + CLK_RST_R(reg));

	/* Wait for at least one cycle of the RCLK clock (@ ca. 32 kHz) */
	udelay(35);

	/* Release module from reset state */
	writel(we | dis, priv->base + CLK_RST_R(reg));

	return 0;
}

static int rzg2l_cpg_assert(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	const struct cpg_mssr_info *info = priv->info;
	unsigned int reg = MSSR_OFF(info->mod_clks[id].bit) * 0x4;
	u32 value = MSSR_RES(info->mod_clks[id].bit) << 16;

	dev_dbg(rcdev->dev, "assert %u%02u\n", reg, info->mod_clks[id].bit);

	writel(value, priv->base + CLK_RST_R(reg));
	return 0;
}

static int rzg2l_cpg_deassert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	const struct cpg_mssr_info *info = priv->info;
	unsigned int reg = MSSR_OFF(info->mod_clks[id].bit) * 0x4;
	u32 dis = MSSR_RES(info->mod_clks[id].bit);
	u32 value = (dis << 16) | dis;

	dev_dbg(rcdev->dev, "deassert %u%02u\n", reg, info->mod_clks[id].bit);

	writel(value, priv->base + CLK_RST_R(reg));
	return 0;
}

static int rzg2l_cpg_status(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	const struct cpg_mssr_info *info = priv->info;
	unsigned int reg = MSSR_OFF(info->mod_clks[id].bit) * 0x4;
	u32 bitmask = MSSR_RES(info->mod_clks[id].bit);

	return !(readl(priv->base + CLK_MRST_R(reg)) & bitmask);
}

static const struct reset_control_ops rzg2l_cpg_reset_ops = {
	.reset = rzg2l_cpg_reset,
	.assert = rzg2l_cpg_assert,
	.deassert = rzg2l_cpg_deassert,
	.status = rzg2l_cpg_status,
};

static int rzg2l_cpg_reset_xlate(struct reset_controller_dev *rcdev,
				const struct of_phandle_args *reset_spec)
{
	unsigned int id = reset_spec->args[0];

	if (id >= rcdev->nr_resets) {
		dev_err(rcdev->dev, "Invalid reset index %u\n", id);
		return -EINVAL;
	}

	return id;
}

static int rzg2l_cpg_reset_controller_register(struct cpg_mssr_priv *priv)
{
	priv->rcdev.ops = &rzg2l_cpg_reset_ops;
	priv->rcdev.of_node = priv->dev->of_node;
	priv->rcdev.dev = priv->dev;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.of_xlate = rzg2l_cpg_reset_xlate;
	priv->rcdev.nr_resets = priv->num_mod_clks;

	return devm_reset_controller_register(priv->dev, &priv->rcdev);
}

#else /* !CONFIG_RESET_CONTROLLER */
static int rzg2l_cpg_reset_controller_register(struct cpg_mssr_priv *priv)
{
	return 0;
}
#endif /* !CONFIG_RESET_CONTROLLER */

static bool rzg2l_cpg_is_pm_clk(const struct of_phandle_args *clkspec)
{
	if (clkspec->args_count != 2)
		return false;

	switch (clkspec->args[0]) {
	case CPG_MOD:
		return true;

	default:
		return false;
	}
}

int rzg2l_cpg_attach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	struct clk *clk;
	int i = 0;
	int error;

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {
		if (rzg2l_cpg_is_pm_clk(&clkspec))
			goto found;

		of_node_put(clkspec.np);
		i++;
	}

	return 0;

found:
	clk = of_clk_get_from_provider(&clkspec);
	of_node_put(clkspec.np);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	error = pm_clk_create(dev);
	if (error)
		goto fail_put;

	error = pm_clk_add_clk(dev, clk);
	if (error)
		goto fail_destroy;

	return 0;

fail_destroy:
	pm_clk_destroy(dev);
fail_put:
	clk_put(clk);
	return error;
}

void rzg2l_cpg_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	if (!pm_clk_no_clocks(dev))
		pm_clk_destroy(dev);
}

static int __init rzg2l_cpg_add_clk_domain(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct generic_pm_domain *genpd;

	genpd = devm_kzalloc(dev, sizeof(*genpd), GFP_KERNEL);
	if (!genpd)
		return -ENOMEM;

	genpd->name = np->name;
	genpd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ALWAYS_ON |
		       GENPD_FLAG_ACTIVE_WAKEUP;
	genpd->attach_dev = rzg2l_cpg_attach_dev;
	genpd->detach_dev = rzg2l_cpg_detach_dev;
	pm_genpd_init(genpd, &pm_domain_always_on_gov, false);

	of_genpd_add_provider_simple(np, genpd);
	return 0;
}

static int __init rzg2l_cpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct cpg_mssr_info *info;
	struct cpg_mssr_priv *priv;
	unsigned int nclks, i;
	struct resource *res;
	struct clk **clks;
	int error;

	info = of_device_get_match_data(dev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->info = info;
	spin_lock_init(&priv->rmw_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	nclks = info->num_total_core_clks + info->num_hw_mod_clks;
	clks = devm_kmalloc_array(dev, nclks, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->clks = clks;
	priv->num_core_clks = info->num_total_core_clks;
	priv->num_mod_clks = info->num_hw_mod_clks;
	priv->last_dt_core_clk = info->last_dt_core_clk;

	for (i = 0; i < nclks; i++)
		clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < info->num_core_clks; i++)
		rzg2l_cpg_register_core_clk(&info->core_clks[i], info, priv);

	for (i = 0; i < info->num_mod_clks; i++)
		rzg2l_cpg_register_mod_clk(&info->mod_clks[i], info, priv);

	error = of_clk_add_provider(np, rzg2l_cpg_clk_src_twocell_get, priv);
	if (error)
		return error;

	error = devm_add_action_or_reset(dev,
					 rzg2l_cpg_del_clk_provider,
					 np);
	if (error)
		return error;

	error = rzg2l_cpg_add_clk_domain(dev);
	if (error)
		return error;

	error = rzg2l_cpg_reset_controller_register(priv);
	if (error)
		return error;
	{

		/*
		 * Workaround: For all IPs that can use generic DMAC,
		 * interrupt signals come to CA55 via DMAC. This Workaround
		 * enable DMAC clock, required while DMAC was not available yet.
		 */

		writel(0x00030003, priv->base + 0x082C);
		udelay(10);
		writel(0x00030003, priv->base + 0x052C);
	}

	return 0;
}

static struct platform_driver rzg2l_cpg_driver = {
	.driver		= {
		.name	= "rzg2l-cpg",
		.of_match_table = rzg2l_cpg_match,
	},
};

static int __init rzg2l_cpg_init(void)
{
	return platform_driver_probe(&rzg2l_cpg_driver, rzg2l_cpg_probe);
}

subsys_initcall(rzg2l_cpg_init);

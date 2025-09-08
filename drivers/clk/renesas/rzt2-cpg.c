// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/T2 Clock Pulse Generator
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 *
 * Based on renesas-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/units.h>

#include <dt-bindings/clock/renesas-cpg-mssr.h>

#include "rzt2-cpg.h"

#ifdef DEBUG
#define WARN_DEBUG(x)	WARN_ON(x)
#else
#define WARN_DEBUG(x)	do { } while (0)
#endif

#define GET_SHIFT(val)		((val >> 12) & 0xff)
#define GET_WIDTH(val)		((val >> 8) & 0xf)

#define GET_REG_OFFSET(val)		((val >> 20) & 0xfff)
#define GET_REG_SAMPLL_CLK1(val)	((val >> 22) & 0xfff)
#define GET_REG_SAMPLL_CLK2(val)	((val >> 12) & 0xfff)

#define PLL3P_MIN		1
#define PLL3P_MAX		0x3F
#define PLL3M_MIN		0x40
#define PLL3M_MAX		0x3FF
#define PLL3S_MIN		0
#define PLL3S_MAX		6
#define PLL3K_MIN		0
#define PLL3K_MAX		0x7FFF
#define PLL3_OSC		(48000)
#define PLL3_FFREF_MIN		(6000)
#define PLL3_FFREF_MAX		(25000)
#define PLL3_FFVCO_MIN		(1600000)
#define PLL3_FFVCO_MAX		(3200000)
#define PLL3_FFOUT_MIN		(25000)
#define PLL3_FFOUT_MAX		(430000)

/**
 * struct rzt2_cpg_priv - Clock Pulse Generator Private Data
 *
 * @rcdev: Reset controller entity
 * @dev: CPG device
 * @base: CPG register block base address
 * @rmw_lock: protects register accesses
 * @clks: Array containing all Core and Module Clocks
 * @num_core_clks: Number of Core Clocks in clks[]
 * @num_mod_clks: Number of Module Clocks in clks[]
 * @num_resets: Number of Module Resets in info->resets[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @info: Pointer to platform data
 */
struct rzt2_cpg_priv {
	struct reset_controller_dev rcdev;
	struct device *dev;
	void __iomem *cpg_base0, *cpg_base1, *slave_stop;
	spinlock_t rmw_lock;

	struct clk **clks;
	struct clk_hw hw;
	unsigned int num_core_clks;
	unsigned int num_mod_clks;
	unsigned int num_resets;
	unsigned int last_dt_core_clk;

	const struct rzt2_cpg_info *info;
};

static void rzt2_cpg_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

static struct clk * __init
rzt2_cpg_div_clk_register(const struct cpg_core_clk *core,
			   struct clk **clks,
			   void __iomem *base,
			   struct rzt2_cpg_priv *priv)
{
	struct device *dev = priv->dev;
	const struct clk *parent;
	const char *parent_name;
	struct clk_hw *clk_hw;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	if (core->dtable) {
		clk_hw = clk_hw_register_divider_table(dev, core->name,
						       parent_name, 0,
						       base + GET_REG_OFFSET(core->conf),
						       GET_SHIFT(core->conf),
						       GET_WIDTH(core->conf),
						       core->flag,
						       core->dtable,
						       &priv->rmw_lock);
	} else {
		clk_hw = clk_hw_register_divider(dev, core->name,
						 parent_name, 0,
						 base + GET_REG_OFFSET(core->conf),
						 GET_SHIFT(core->conf),
						 GET_WIDTH(core->conf),
						 core->flag, &priv->rmw_lock);

	}
	if (IS_ERR(clk_hw))
		return ERR_CAST(clk_hw);

	return clk_hw->clk;
}

static struct clk * __init
rzt2_cpg_mux_clk_register(const struct cpg_core_clk *core,
			   void __iomem *base,
			   struct rzt2_cpg_priv *priv)
{
	const struct clk_hw *clk_hw;

	clk_hw = devm_clk_hw_register_mux(priv->dev, core->name,
					  core->parent_names, core->num_parents,
					  core->flag,
					  base + GET_REG_OFFSET(core->conf),
					  GET_SHIFT(core->conf),
					  GET_WIDTH(core->conf),
					  core->mux_flags, &priv->rmw_lock);
	if (IS_ERR(clk_hw))
		return ERR_CAST(clk_hw);

	return clk_hw->clk;
}

struct pll_clk {
	struct clk_hw hw;
	unsigned int conf;
	unsigned int type;
	void __iomem *base;
	struct rzt2_cpg_priv *priv;
};

#define to_pll(_hw)	container_of(_hw, struct pll_clk, hw)

static unsigned long rzt2h_cpg_pll_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct pll_clk *pll_clk = to_pll(hw);
	struct rzt2_cpg_priv *priv = pll_clk->priv;
	unsigned int val1, val2;
	unsigned int mult = 1;
	unsigned int div = 1;

	if (pll_clk->type != CLK_TYPE_SAM_PLL)
		return parent_rate;

	val1 = readl(priv->cpg_base1 + GET_REG_SAMPLL_CLK1(pll_clk->conf));
	val2 = readl(priv->cpg_base1 + GET_REG_SAMPLL_CLK2(pll_clk->conf));
	mult = MDIV(val1) + KDIV(val2) / 65536;
	div = PDIV(val1) << SDIV(val2);

	return DIV_ROUND_CLOSEST_ULL((u64)parent_rate * mult, div);
}

static const struct clk_ops rzt2_cpg_pll_ops = {
	.recalc_rate = rzt2h_cpg_pll_clk_recalc_rate,
};

static struct clk * __init
rzt2_cpg_pll_clk_register(const struct cpg_core_clk *core,
			   struct clk **clks,
			   void __iomem *base,
			   struct rzt2_cpg_priv *priv)
{
	struct device *dev = priv->dev;
	const struct clk *parent;
	struct clk_init_data init;
	const char *parent_name;
	struct pll_clk *pll_clk;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return ERR_PTR(-ENOMEM);

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &rzt2_cpg_pll_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll_clk->hw.init = &init;
	pll_clk->conf = core->conf;
	pll_clk->base = base;
	pll_clk->priv = priv;
	pll_clk->type = core->type;

	return clk_register(NULL, &pll_clk->hw);
}

struct rzt2_pll3_param {
	unsigned int csdiv;
	unsigned int pll_p;
	unsigned int pll_m;
	unsigned int pll_s;
	short pll_k;
};

struct pll3_clk {
	struct clk_hw hw;
	unsigned long rate;
	struct rzt2_cpg_priv *priv;
};
#define to_pll3(_hw)	container_of(_hw, struct pll3_clk, hw)

static int
rzt2_cpg_get_pll3_setting(struct rzt2_pll3_param *params,
			unsigned long rate)
{
	long long div, res, mult;
	unsigned long vclk_rate = rate / 1000;
	unsigned long freq, fvco, fout;
	int ret = -EINVAL;

	for (params->csdiv = 2; params->csdiv <= 32; params->csdiv = params->csdiv + 2) {
		fout = vclk_rate * params->csdiv;
		if ((fout > PLL3_FFOUT_MAX) || (fout < PLL3_FFOUT_MIN))
			continue;

		for (params->pll_s = PLL3S_MIN; params->pll_s <= PLL3S_MAX; params->pll_s++) {
			fvco = fout * (1 << params->pll_s);
			if ((fvco > PLL3_FFVCO_MAX) || (fvco < PLL3_FFVCO_MIN))
				continue;

			for (params->pll_p = PLL3P_MIN; params->pll_p <= PLL3P_MAX; params->pll_p++) {
				freq = PLL3_OSC / params->pll_p;
				if ((freq > PLL3_FFREF_MAX) || (freq < PLL3_FFREF_MIN))
					continue;

				mult = fvco * params->pll_p;
				div = mult / PLL3_OSC;
				if ((div < PLL3M_MIN) || (div > PLL3M_MAX))
					continue;

				res = mult % PLL3_OSC;
				if (res >= (PLL3_OSC / 2))
					continue;

				params->pll_m = div;
				params->pll_k = res * 65536 / PLL3_OSC;
				params->csdiv = (params->csdiv / 2) - 1;
				ret = 0;
				goto found;
			}
		}
	}

found:
	return ret;

}

static unsigned long rzt2_cpg_lcdc_div_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct pll3_clk *pll3_clk = to_pll3(hw);
	unsigned long rate = pll3_clk->rate;

	if (!rate)
		rate = parent_rate;
	return rate;
}

static int rzt2_cpg_lcdc_div_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	/* To ensure lcdc clk rate falls within the supported range of 5MHz to 100MHz */
	req->rate = clamp(req->rate, 5000000UL, 100000000UL);
	req->best_parent_rate = clamp(req->rate * 2,
				25000000UL, 430000000UL);

	return 0;
}

static int rzt2_cpg_lcdc_div_set_rate(struct clk_hw *hw,
                                     unsigned long rate,
                                     unsigned long parent_rate)
{
	struct pll3_clk *pll3_clk = to_pll3(hw);
	struct rzt2_cpg_priv *priv = pll3_clk->priv;
	struct rzt2_pll3_param params;
	int ret;
	u32 val;

	ret = rzt2_cpg_get_pll3_setting(&params, rate);
	if (ret) {
		dev_err(priv->dev, "failed to set pll3 rate");
		return ret;
	}

	/* SCKCR3: LCDCDIVSEL */
	writel((params.csdiv << 20) | (readl(priv->cpg_base0 + SCKCR3) &
		~(LCDCDIVSEL)), priv->cpg_base0 + SCKCR3);

	/* PLL3EN: PLL3EN = 0 */
	writel(0, priv->cpg_base1 + PLL3EN_REG);

	ret = readl_poll_timeout(priv->cpg_base1 + PLL3MON, val,
		!(val & BIT(0)), 100, 250000);
	if (ret) {
		dev_err(priv->dev, "failed to lock pll3");
		return ret;
	}

	/* PLL3_VCO_CTR0: PLL3M and PLL3P */
	writel((params.pll_p << 16) | params.pll_m, priv->cpg_base1 + PLL3_VCO_CTR0);

	/* PLL3_VCO_CTR0: PLL3K and PLL3S */
	writel((params.pll_k << 16) | params.pll_s, priv->cpg_base1 + PLL3_VCO_CTR1);

	/* PLL3EN: PLL3EN = 1 */
	writel(PLL3EN, priv->cpg_base1 + PLL3EN_REG);

	ret = readl_poll_timeout(priv->cpg_base1 + PLL3MON, val,
		(val & BIT(0)), 100, 250000);
	if (ret) {
		dev_err(priv->dev, "failed to lock pll3");
		return ret;
	}

	return 0;
}

static const struct clk_ops rzt2_cpg_lcdc_div_ops = {
	.recalc_rate = rzt2_cpg_lcdc_div_recalc_rate,
	.determine_rate = rzt2_cpg_lcdc_div_determine_rate,
	.set_rate = rzt2_cpg_lcdc_div_set_rate,
};

static struct clk * __init
		rzt2_cpg_lcdc_div_clk_register(const struct cpg_core_clk *core,
				struct rzt2_cpg_priv *priv)
{
	struct pll3_clk *pll3_clk;
	const struct clk *parent;
	const char *parent_name;
	struct clk_init_data init;
	struct clk_hw *clk_hw;
	int ret;

	parent = priv->clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	pll3_clk = devm_kzalloc(priv->dev, sizeof(*pll3_clk), GFP_KERNEL);
	pll3_clk->priv = priv;
	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &rzt2_cpg_lcdc_div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk_hw = &pll3_clk->hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(priv->dev, clk_hw);
	if (ret)
		return ERR_PTR(ret);

	return clk_hw->clk;
}

static struct clk
*rzt2_cpg_clk_src_twocell_get(struct of_phandle_args *clkspec,
			       void *data)
{
	unsigned int clkidx = clkspec->args[1];
	struct rzt2_cpg_priv *priv = data;
	struct device *dev = priv->dev;
	const char *type;
	struct clk *clk;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		type = "core";
		if (clkidx > priv->last_dt_core_clk) {
			dev_err(dev, "Invalid %s clock index %u\n", type, clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[clkidx];
		break;

	case CPG_MOD:
		type = "module";
		if (clkidx >= priv->num_mod_clks) {
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

static void __init
rzt2_cpg_register_core_clk(const struct cpg_core_clk *core,
			    const struct rzt2_cpg_info *info,
			    struct rzt2_cpg_priv *priv)
{
	struct clk *clk = ERR_PTR(-EOPNOTSUPP), *parent;
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
		clk = rzt2_cpg_pll_clk_register(core, priv->clks,
						 priv->cpg_base1, priv);
		break;
	case CLK_TYPE_LCDC_DIV:
		clk = rzt2_cpg_lcdc_div_clk_register(core, priv);
		break;
	case CLK_TYPE_DIV:
		if (core->sel_base)
			clk = rzt2_cpg_div_clk_register(core, priv->clks,
						priv->cpg_base1, priv);
		else
			clk = rzt2_cpg_div_clk_register(core, priv->clks,
						priv->cpg_base0, priv);
		break;
	case CLK_TYPE_MUX:
		clk = rzt2_cpg_mux_clk_register(core, priv->cpg_base0, priv);
		break;
	default:
		goto fail;
	}

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
 *
 * @hw: handle between common and hardware-specific interfaces
 * @addr: register address
 * @bit: ON/MON bit
 * @enabled: soft state of the clock, if it is coupled with another clock
 * @priv: CPG/MSTP private data
 */
struct mstp_clock {
	struct clk_hw hw;
	u32 addr;
	u8 bit;
	int sel_base;
	bool enabled;
	struct rzt2_cpg_priv *priv;
	struct mstp_clock *sibling;
};

#define to_mod_clock(_hw)	container_of(_hw, struct mstp_clock, hw)

static int rzt2_mod_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct mstp_clock *clock = to_mod_clock(hw);
	const struct rzt2_cpg_priv *priv = clock->priv;
	unsigned int reg = clock->addr;
	struct device *dev = priv->dev;
	u32 bitmask = BIT(clock->bit);
	unsigned long flags;
	u32 value;

	if (!reg) {
		dev_dbg(dev, "\ndoes not support mstop\n");
		return 0;
	}

	spin_lock_irqsave((struct spinlock *)&priv->rmw_lock, flags);
	if (clock->sel_base)
		value = readl(priv->cpg_base1 + reg);
	else
		value = readl(priv->cpg_base0 + reg);
	if (enable)
		value &= ~bitmask;
	else
		value |= bitmask;

	if (clock->sel_base)
		writel(value, priv->cpg_base1 + reg);
	else
		writel(value, priv->cpg_base0 + reg);

	udelay(50);

	spin_unlock_irqrestore((struct spinlock *)&priv->rmw_lock, flags);

	return 0;
}

static int rzt2_mod_clock_enable(struct clk_hw *hw)
{
	return rzt2_mod_clock_endisable(hw, true);
}

static void rzt2_mod_clock_disable(struct clk_hw *hw)
{
	rzt2_mod_clock_endisable(hw, false);
}

static int rzt2_mod_clock_is_enabled(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mod_clock(hw);
	struct rzt2_cpg_priv *priv = clock->priv;
	u32 bitmask = BIT(clock->bit);
	u32 value;

	if (!clock->addr) {
		dev_dbg(priv->dev, "%pC does not support ON/OFF\n",  hw->clk);
		return 1;
	}

	if (clock->sel_base)
		value = readl(priv->cpg_base1 + clock->addr);
	else
		value = readl(priv->cpg_base0 + clock->addr);

	return (value & bitmask);
}

static const struct clk_ops rzt2_mod_clock_ops = {
	.enable = rzt2_mod_clock_enable,
	.disable = rzt2_mod_clock_disable,
	.is_enabled = rzt2_mod_clock_is_enabled,
};

static void __init
rzt2_cpg_register_mod_clk(const struct rzt2_mod_clk *mod,
			   const struct rzt2_cpg_info *info,
			   struct rzt2_cpg_priv *priv)
{
	struct mstp_clock *clock = NULL;
	struct device *dev = priv->dev;
	unsigned int id = mod->id;
	struct clk_init_data init;
	struct clk *parent, *clk;
	const char *parent_name;

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
	init.ops = &rzt2_mod_clock_ops;
	init.flags = CLK_SET_RATE_PARENT;
	parent_name = __clk_get_name(parent);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->addr = mod->addr;
	clock->bit = mod->bit;
	clock->sel_base = mod->sel_base;
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
}

#define rcdev_to_priv(x)       container_of(x, struct rzt2_cpg_priv, rcdev)

static int rzt2h_cpg_reset(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct rzt2_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzt2_cpg_info *info = priv->info;
	unsigned int reg = info->resets[id].off;
	u32 dis = info->resets[id].bit;
	int sel_base = info->resets[id].sel_base;
	u32 value;

       dev_dbg(rcdev->dev, "reset id:%ld offset:0x%x\n", id, reg);

       if (sel_base) {
		/* Reset module */
		value = readl(priv->cpg_base1 + reg);
		writel(value | BIT(dis), priv->cpg_base1 + reg);
		udelay(35);
		/* Release module from reset state */
		writel(value & ~BIT(dis), priv->cpg_base1 + reg);
       } else {
		value = readl(priv->cpg_base0 + reg);
		writel(value | BIT(dis), priv->cpg_base0 + reg);
		udelay(35);
		writel(value & ~BIT(dis), priv->cpg_base0 + reg);
       }

       return 0;
}

static int rzt2h_cpg_assert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct rzt2_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzt2_cpg_info *info = priv->info;
	unsigned int reg = info->resets[id].off;
	u32 bit = info->resets[id].bit;
	int sel_base = info->resets[id].sel_base;
	u32 value;

	dev_dbg(rcdev->dev, "assert id:%ld offset:0x%x\n", id, reg);

	if (sel_base) {
		value = readl(priv->cpg_base1 + reg);
		writel(value | BIT(bit), priv->cpg_base1 + reg);
	} else {
		value = readl(priv->cpg_base0 + reg);
		writel(value | BIT(bit), priv->cpg_base0 + reg);
	}

	return 0;
}

static int rzt2h_cpg_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct rzt2_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzt2_cpg_info *info = priv->info;
	unsigned int reg = info->resets[id].off;
	u32 dis = info->resets[id].bit;
	int sel_base = info->resets[id].sel_base;
	u32 value;

	dev_dbg(rcdev->dev, "deassert id:%ld offset:0x%x\n", id, reg);
	if (sel_base) {
		value = readl(priv->cpg_base1 + reg);
		writel(value & ~BIT(dis), priv->cpg_base1 + reg);
	} else {
		value = readl(priv->cpg_base0 + reg);
		writel(value & ~BIT(dis), priv->cpg_base0 + reg);
	}

	return 0;
}

static const struct reset_control_ops rzt2h_cpg_reset_ops = {
	.reset = rzt2h_cpg_reset,
	.assert = rzt2h_cpg_assert,
	.deassert = rzt2h_cpg_deassert,
};

static int rzt2h_cpg_reset_xlate(struct reset_controller_dev *rcdev,
				const struct of_phandle_args *reset_spec)
{
	struct rzt2_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzt2_cpg_info *info = priv->info;
	unsigned int id = reset_spec->args[0];

	if (id >= rcdev->nr_resets || !info->resets[id].off) {
		dev_err(rcdev->dev, "Invalid reset index %u\n", id);
		return -EINVAL;
	}

	return id;
}

static int rzt2h_cpg_reset_controller_register(struct rzt2_cpg_priv *priv)
{
	priv->rcdev.ops = &rzt2h_cpg_reset_ops;
	priv->rcdev.of_node = priv->dev->of_node;
	priv->rcdev.dev = priv->dev;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.of_xlate = rzt2h_cpg_reset_xlate;
	priv->rcdev.nr_resets = priv->num_resets;

	return devm_reset_controller_register(priv->dev, &priv->rcdev);
}

static bool rzt2_cpg_is_pm_clk(const struct of_phandle_args *clkspec)
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

static int rzt2_cpg_attach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	bool once = true;
	struct clk *clk;
	int error;
	int i = 0;

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {
		if (rzt2_cpg_is_pm_clk(&clkspec)) {
			if (once) {
				once = false;
				error = pm_clk_create(dev);
				if (error) {
					of_node_put(clkspec.np);
					goto err;
				}
			}
			clk = of_clk_get_from_provider(&clkspec);
			of_node_put(clkspec.np);
			if (IS_ERR(clk)) {
				error = PTR_ERR(clk);
				goto fail_destroy;
			}

			error = pm_clk_add_clk(dev, clk);
			if (error) {
				dev_err(dev, "pm_clk_add_clk failed %d\n",
					error);
				goto fail_put;
			}
		} else {
			of_node_put(clkspec.np);
		}
		i++;
	}

	return 0;

fail_put:
	clk_put(clk);

fail_destroy:
	pm_clk_destroy(dev);
err:
	return error;
}

static void rzt2_cpg_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	if (!pm_clk_no_clocks(dev))
		pm_clk_destroy(dev);
}

static void rzt2_cpg_genpd_remove(void *data)
{
	pm_genpd_remove(data);
}

static int __init rzt2_cpg_add_clk_domain(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct generic_pm_domain *genpd;
	int ret;

	genpd = devm_kzalloc(dev, sizeof(*genpd), GFP_KERNEL);
	if (!genpd)
		return -ENOMEM;

	genpd->name = np->name;
	genpd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ALWAYS_ON |
		       GENPD_FLAG_ACTIVE_WAKEUP;
	genpd->attach_dev = rzt2_cpg_attach_dev;
	genpd->detach_dev = rzt2_cpg_detach_dev;
	ret = pm_genpd_init(genpd, &pm_domain_always_on_gov, false);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, rzt2_cpg_genpd_remove, genpd);
	if (ret)
		return ret;

	return of_genpd_add_provider_simple(np, genpd);
}

static int __init rzt2_cpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct rzt2_cpg_info *info;
	struct rzt2_cpg_priv *priv;
	unsigned int nclks, i;
	struct clk **clks;
	int error;

	info = of_device_get_match_data(dev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->info = info;
	spin_lock_init(&priv->rmw_lock);


	priv->cpg_base0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->cpg_base1))
		return PTR_ERR(priv->cpg_base0);

	priv->cpg_base1 = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->cpg_base1))
		return PTR_ERR(priv->cpg_base1);

	priv->slave_stop = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(priv->slave_stop))
		return PTR_ERR(priv->slave_stop);

	writel(0x0, priv->slave_stop + 0x4);

	nclks = info->num_total_core_clks + info->num_hw_mod_clks;
	clks = devm_kmalloc_array(dev, nclks, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->clks = clks;
	priv->num_core_clks = info->num_total_core_clks;
	priv->num_mod_clks = info->num_hw_mod_clks;
	priv->num_resets = info->num_resets;
	priv->last_dt_core_clk = info->last_dt_core_clk;

	for (i = 0; i < nclks; i++)
		clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < info->num_core_clks; i++)
		rzt2_cpg_register_core_clk(&info->core_clks[i], info, priv);

	for (i = 0; i < info->num_mod_clks; i++)
		rzt2_cpg_register_mod_clk(&info->mod_clks[i], info, priv);

	/* Enable PLL register */
	writel(PLL0EN, priv->cpg_base1 + PLL0EN_REG);
	writel(PLL2EN, priv->cpg_base1 + PLL2EN_REG);
	writel(PLL3EN, priv->cpg_base1 + PLL3EN_REG);

	error = of_clk_add_provider(np, rzt2_cpg_clk_src_twocell_get, priv);
	if (error)
		return error;

	error = devm_add_action_or_reset(dev, rzt2_cpg_del_clk_provider, np);
	if (error)
		return error;

	error = rzt2_cpg_add_clk_domain(dev);
	if (error)
		return error;
	error = rzt2h_cpg_reset_controller_register(priv);
	if (error)
		return error;

	return 0;
}

static const struct of_device_id rzt2_cpg_match[] = {
#ifdef CONFIG_CLK_R9A09G077
	{
		.compatible = "renesas,r9a09g077-cpg",
		.data = &r9a09g077_cpg_info,
	},
#endif
	{ /* sentinel */ }
};

static struct platform_driver rzt2_cpg_driver = {
	.driver		= {
		.name	= "rzt2-cpg",
		.of_match_table = rzt2_cpg_match,
	},
};

static int __init rzt2_cpg_init(void)
{
	return platform_driver_probe(&rzt2_cpg_driver, rzt2_cpg_probe);
}

subsys_initcall(rzt2_cpg_init);

MODULE_DESCRIPTION("Renesas RZ/T2 CPG Driver");
MODULE_LICENSE("GPL v2");

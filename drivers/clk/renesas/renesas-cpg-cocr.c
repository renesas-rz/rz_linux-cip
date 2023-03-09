/*
 * Driver for the Renesas Clock ON/OFF Control Register(COCR)
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include <dt-bindings/clock/r9a09g011gbg-cpg-cocr.h>

#include "renesas-cpg-cocr.h"

#ifdef DEBUG
#define WARN_DEBUG(x)  WARN_ON(x)
#else
#define WARN_DEBUG(x)  do { } while (0)
#endif


/**
 * Clock Pulse Generator Private Data
 *
 * @rcdev: Optional reset controller entity
 * @dev: CPG device
 * @base: CPG register block base address
 * @rmw_lock: protects RMW register accesses
 * @clks: Array containing all Core and Module Clocks
 * @num_core_clks: Number of Core Clocks in clks[]
 * @num_mod_clks: Number of Module Clocks in clks[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @notifiers: Notifier chain to save/restore clock state for system resume
 */
struct cpg_cocr_priv {
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

	struct rcr_reset *resets;
	unsigned int num_resets;
	unsigned int num_hw_resets;

	struct raw_notifier_head notifiers;
	struct cocr_mod_clk *mod_clk;
	struct cpg_core_clk *core_clk;
	bool no_hw_clk_rate;
};


/**
 * struct cocr_clock - COCR gating clock
 * @hw: handle between common and hardware-specific interfaces
 * @index: COCR clock number
 * @priv: CPG private data
 */
struct cocr_clock {
	struct clk_hw hw;
	u32 index;
	struct cpg_cocr_priv *priv;
};

#define to_cocr_clock(_hw) container_of(_hw, struct cocr_clock, hw)

static void cpg_clkonoff_ctrl(struct cpg_cocr_priv *priv, unsigned char reg_num, 
				unsigned short target, unsigned short set_value)
{
	u32 offset, value;
	unsigned long flags;

	offset = CPG_CLK_ON1 + ((reg_num - 1) * sizeof(u32));
	value = ((u32)target << CPG_REG_WEN_SHIFT)
			| (set_value & CPG_SET_DATA_MASK);

	spin_lock_irqsave(&priv->rmw_lock, flags);
	writel(value,priv->base + offset);
	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static int is_cpg_clk_on_sts(struct cpg_cocr_priv *priv, unsigned char reg_num,
				unsigned short target)
{
	u32 offset;
	int clk_onoff_sts;

	offset = CPG_CLK_ON1 + ((reg_num - 1) * sizeof(u32));
	clk_onoff_sts = ((readl(priv->base + offset) & target) >> target);
	return clk_onoff_sts;
}

static int __cpg_cocr_clock_is_enabled(struct cpg_cocr_priv *priv, unsigned index)
{
       unsigned int no = index / 32;
       unsigned int bit =index % 32;

       return is_cpg_clk_on_sts(priv,no,BIT(bit));
}

static int cpg_cocr_clock_is_enabled(struct clk_hw *hw)
{
	struct cocr_clock *clock = to_cocr_clock(hw);
	struct cpg_cocr_priv *priv = clock->priv;

	return __cpg_cocr_clock_is_enabled(priv,clock->index);

}

static int __cpg_cocr_clock_endisable(struct cpg_cocr_priv *priv, unsigned index,bool enable)
{
	unsigned int no = index / 32;
	unsigned int bit = index % 32;
	int clk_status;

	/* Check the CLK ON/OFF status with corresponding the clock id */
	clk_status = __cpg_cocr_clock_is_enabled(priv,index);

	/* If the current status and the request match, skip the process */
	if (clk_status == (int)enable)
		return 0;
	
	/* Set the clock on/off control register */
	if (enable)
		cpg_clkonoff_ctrl(priv,no,BIT(bit),BIT(bit));
	else
		cpg_clkonoff_ctrl(priv,no,BIT(bit),0);

	return 0;
}

static int cpg_cocr_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct cocr_clock *clock = to_cocr_clock(hw);
	struct cpg_cocr_priv *priv = clock->priv;

	__cpg_cocr_clock_endisable(priv,clock->index, enable);

	return 0;
}

static int cpg_cocr_clock_enable(struct clk_hw *hw)
{
	return cpg_cocr_clock_endisable(hw, true);
}

static void cpg_cocr_clock_disable(struct clk_hw *hw)
{
	(void)cpg_cocr_clock_endisable(hw, false);
}

static const struct clk_ops cpg_cocr_clock_ops = {
	.enable		= cpg_cocr_clock_enable,
	.disable	= cpg_cocr_clock_disable,
	.is_enabled	= cpg_cocr_clock_is_enabled,
};

static struct clk *cpg_cocr_clk_src_twocell_get(
		struct of_phandle_args *clkspec, void *data)
{
	unsigned int clkidx = clkspec->args[1];
	struct cpg_cocr_priv *priv = data;
	struct device *dev = priv->dev;
	const char *type;
	struct clk *clk;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		type = "core";
		if (clkidx > priv->last_dt_core_clk) {
			dev_err(dev, "Invalid %s clock index %u\n", 
				type, clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[clkidx];
		break;
	case CPG_MOD:
		type = "module";
		clk = priv->clks[priv->num_core_clks + MOD_CLK_PACK(clkidx)];
		break;

	default:
		dev_err(dev, "Invalid CPG clock type %u\n", clkspec->args[0]);
		return ERR_PTR(-EINVAL);
	}

	if (IS_ERR(clk))
		dev_err(dev, "Cannot get %s clock %u: %ld", type, clkidx,PTR_ERR(clk));
	else
		dev_dbg(dev, "clock (%u, %u) is %pC at %lu Hz\n",
			clkspec->args[0], clkspec->args[1], clk,
			clk_get_rate(clk));
	
	return clk;
}

static void __init cpg_cocr_register_core_clk(const struct cpg_core_clk *core,
						const struct cpg_cocr_info *info,
						struct cpg_cocr_priv *priv)
{
	struct clk *clk = ERR_PTR(-ENOTSUPP), *parent;
	struct device *dev = priv->dev;
	unsigned int id = core->id, div = core->div;
	const char *parent_name;
	unsigned long t;
	unsigned int i;

	WARN_DEBUG(id >= priv->num_core_clks);
	WARN_DEBUG(PTR_ERR(priv->clks[id]) != -ENOENT);

	if (!core->name) {
		/* Skip NULLified clock */
		return;
	}

	switch (core->type) {
	case CLK_TYPE_FR:
		clk = clk_register_fixed_rate(NULL, core->name, NULL, 0,
						core->val);
		break;

	default:
		parent = priv->clks[core->parent];
		if (IS_ERR(parent)) {
			clk = parent;
			goto fail;
		}

		if (!priv->no_hw_clk_rate) {
			if (core->type == CLK_TYP_STATIC) {
				for (i = 0; i < info->num_mod_clks; i++) {
					if (core->id == info->mod_clks[i].parent) {
						//children Clock off
						cpg_clkonoff_ctrl(priv,
						(info->mod_clks[i].id/32) ,(BIT(info->mod_clks[i].id%32)),0);
					}
				}
			}

			if (core->type == CLK_TYPE_DIV) {
				t = 0;
				while (10000000 > t++) {
					if (0 == (readl(priv->base + CPG_CLKSTATUS) & core->status)) 
						break;
					udelay(1);
				}
			}
			
			writel(core->msk | core->val, priv->base + core->offset);

			if(core->type == CLK_TYPE_DIV){
				t = 0;
				while (10000000 > t++) {
					if (0 == (readl(priv->base + CPG_CLKSTATUS) & core->status))
						break;
					udelay(1);
				}
			}
		}

		parent_name = __clk_get_name(parent);

		clk = clk_register_fixed_factor(NULL, core->name,
						parent_name, 0, 1, div);

		break;
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


static void __init cpg_cocr_register_mod_clk(const struct cocr_mod_clk *mod,
						const struct cpg_cocr_info *info,
						struct cpg_cocr_priv *priv)
{
	struct cocr_clock *clock = NULL;
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

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		clk = ERR_PTR(-ENOMEM);
		goto fail;
	}
	
	init.name = mod->name;
	init.ops = &cpg_cocr_clock_ops;
	
	//init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	init.flags = CLK_SET_RATE_PARENT;
	for (i = 0; i < info->num_crit_mod_clks; i++) {
		if (id == info->crit_mod_clks[i]) {
			dev_dbg(dev, "COCR %s setting CLK_IS_CRITICAL\n",mod->name);
			init.flags |= CLK_IS_CRITICAL;
			break;
		}
	}

	parent_name = __clk_get_name(parent);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->index = id - priv->num_core_clks;
	clock->priv = priv;
	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto fail;

	dev_dbg(dev, "Module clock %pC[%ld] at %lu Hz\n", clk, clock->index, clk_get_rate(clk));

	clk_prepare(clk);
	clk_enable(clk);

	priv->clks[id] = clk;
	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "module",mod->name, PTR_ERR(clk));
	kfree(clock);
}

#ifdef CONFIG_RESET_CONTROLLER

#define rcdev_to_priv(x)       container_of(x, struct cpg_cocr_priv, rcdev)

static void cpg_reset_ctrl(struct reset_controller_dev *rcdev, 
			unsigned char reg_num, unsigned short target, unsigned short set_value)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	u32 offset, value;
	unsigned long flags;

	offset = CPG_RST1 + ((reg_num - 1) * sizeof(u32));
	value = ((u32)target << CPG_REG_WEN_SHIFT) | (set_value & CPG_SET_DATA_MASK);
	
	dev_dbg(priv->dev, "%s: reg offset:0x%08x write value:0x%08x\n",
		__func__, offset, value);
	spin_lock_irqsave(&priv->rmw_lock, flags);
	writel(value, (priv->base+ offset));
	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static int cpg_get_reset_status(struct reset_controller_dev *rcdev,
			unsigned char reg_num, unsigned short target)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	u32 offset, value;
	unsigned long flags;

	offset = CPG_RST1 + ((reg_num - 1) * sizeof(u32));
	
	spin_lock_irqsave(&priv->rmw_lock, flags);
	value = readl(priv->base + offset) & target;
	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	dev_dbg(priv->dev, "%s: reg offset:0x%08x read value:0x%08x status:0x%08x\n",
		__func__, offset, value, !value);
	return !value;
}

static int cpg_wait_reset_monitor(struct reset_controller_dev *rcdev,
			u32 timeout, u32 msk, u32 status)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	u32 count = timeout;

	while (true)
	{
		if (status == ((readl(priv->base + CPG_RST_MON) & msk) >> msk) ) {
			break;
		}
		if (0 < count) {
			udelay(1);
			count--;
		}
		else
			return -ETIMEDOUT;
	}
	return 0;
}


static int cpg_cocr_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	int i;
	int clk_state[9] = {0};

	if ( id <= CPG_MIN_CLKID ) {
		dev_err(priv->dev, "Invalid reset id %ld\n", id);
		return -EINVAL;
	}
	if ( (reg < CPG_RST_REG_MIN || CPG_RST_REG_MAX < reg) || (bit > 15) ) {
		dev_err(priv->dev, "Invalid CPG register info %u:%u\n", reg, bit);
		return -EINVAL;
	}
	
	if (priv->resets[id].type == RST_TYPEA){
		/*Reset assert*/
		cpg_reset_ctrl(rcdev,reg,BIT(bit),0);
	} else {
		/*Type B*/
		for (i = 0;i < priv->resets[id].clk_num; i++){
			clk_state[i] = __cpg_cocr_clock_is_enabled(priv,priv->resets[id].clk_id[i]);
			if(0 == clk_state[i]){
				__cpg_cocr_clock_endisable(priv,priv->resets[id].clk_id[i],true);
				udelay(10);
			}
		}

		/* Reset assert */
		cpg_reset_ctrl(rcdev,reg,BIT(bit),0);

		/* Check the monitor */
		if (0 != cpg_wait_reset_monitor(rcdev, RST_MON_TIMEOUT, BIT(priv->resets[id].reset_msk), RST_MON_ASSERT) ){
			dev_err(priv->dev, "Reset assert was time out:id %d\n", priv->resets[id].clk_id[i]);
		}
		
		for (i=0; i < priv->resets[id].clk_num; i++){
			if (0 == clk_state[i]){
				__cpg_cocr_clock_endisable(priv,priv->resets[id].clk_id[i],false);
			}
			udelay(10);
		}
	}
	return 0;
}

static int cpg_cocr_deassert(struct reset_controller_dev *rcdev,
                            unsigned long id)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	int i;
	int clk_state[9] = {0};

	if ( id <= CPG_MIN_CLKID ) {
		dev_err(priv->dev, "Invalid reset id %ld\n", id);
		return -EINVAL;
	}
	if ( (reg < CPG_RST_REG_MIN || CPG_RST_REG_MAX < reg) || (bit > 15) ) {
		dev_err(priv->dev, "Invalid CPG register info %u:%u\n", reg, bit);
		return -EINVAL;
	}
	
	if (priv->resets[id].type == RST_TYPEA){
		/*Reset deassert*/
		for (i = 0;i < priv->resets[id].clk_num; i++){
			clk_state[i] = __cpg_cocr_clock_is_enabled(priv,priv->resets[id].clk_id[i]);
			if(0 != clk_state[i]){
				__cpg_cocr_clock_endisable(priv,priv->resets[id].clk_id[i],false);
				udelay(10);
			}
		}

		cpg_reset_ctrl(rcdev,reg,BIT(bit),BIT(bit));
		udelay(10);

		for (i=0; i < priv->resets[id].clk_num; i++){
			if (0 != clk_state[i]){
				__cpg_cocr_clock_endisable(priv,priv->resets[id].clk_id[i],true);
				udelay(10);
			}
		}
	} else {
		/*Type B*/
		for (i = 0;i < priv->resets[id].clk_num; i++){
			clk_state[i] = __cpg_cocr_clock_is_enabled(priv,priv->resets[id].clk_id[i]);
			if (0 == clk_state[i]){
				__cpg_cocr_clock_endisable(priv,priv->resets[id].clk_id[i],true);
				udelay(10);
			}
		}
	
		/*Reset deassert*/
		cpg_reset_ctrl(rcdev,reg,BIT(bit),BIT(bit));

		/* Check the monitor */
		if (0 != cpg_wait_reset_monitor(rcdev, RST_MON_TIMEOUT, BIT(priv->resets[id].reset_msk), RST_MON_DEASSERT) ){
			dev_err(priv->dev, "Reset deassert was time out:id %d\n", priv->resets[id].clk_id[i]);
		}

		for (i=0; i < priv->resets[id].clk_num; i++){
			if (0 == clk_state[i]){
				__cpg_cocr_clock_endisable(priv,priv->resets[id].clk_id[i],false);
				udelay(10);
			}
		}
	}
	return 0;
}

static int cpg_cocr_reset(struct reset_controller_dev *rcdev,
                            unsigned long id)
{
	cpg_cocr_assert(rcdev,id);
	cpg_cocr_deassert(rcdev,id);
	return 0;
}

static int cpg_cocr_status(struct reset_controller_dev *rcdev,
                          unsigned long id)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	int status;
	
	if ( id <= CPG_MIN_CLKID ) {
		dev_err(priv->dev, "Invalid reset id %ld\n", id);
		return -EINVAL;
	}
	if ( (reg < CPG_RST_REG_MIN || CPG_RST_REG_MAX < reg) || (bit > 15) ) {
		dev_err(priv->dev, "Invalid CPG register info %d:%d\n", reg, bit);
		return -EINVAL;
	}
	
	status = cpg_get_reset_status(priv->base,reg,BIT(bit));
	dev_dbg(priv->dev, "status is %d\n", status);
	
	return status;
}

static const struct reset_control_ops cpg_cocr_reset_ops = {
	.reset = cpg_cocr_reset,
	.assert = cpg_cocr_assert,
	.deassert = cpg_cocr_deassert,
	.status = cpg_cocr_status,
};

static int cpg_cocr_reset_xlate(struct reset_controller_dev *rcdev,
				const struct of_phandle_args *reset_spec)
{
	struct cpg_cocr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int unpacked = reset_spec->args[0];
	unsigned int idx = MOD_CLK_PACK(unpacked);

	dev_dbg(priv->dev, "reset unpacked%u index %u\n", unpacked,idx);

	if (unpacked % 100 > 31 || idx >= rcdev->nr_resets) {
		dev_err(priv->dev, "Invalid reset index %u\n", unpacked);
		return -EINVAL;
	}

	return idx;
}

static int cpg_cocr_reset_controller_register(struct cpg_cocr_priv *priv)
{
	priv->rcdev.ops = &cpg_cocr_reset_ops;
	priv->rcdev.of_node = priv->dev->of_node;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.of_xlate = cpg_cocr_reset_xlate;
	priv->rcdev.nr_resets = priv->num_resets;
	return devm_reset_controller_register(priv->dev, &priv->rcdev);
}

#else /* !CONFIG_RESET_CONTROLLER */
static inline int cpg_cocr_reset_controller_register(struct cpg_cocr_priv *priv)
{
	return 0;
}
#endif /* !CONFIG_RESET_CONTROLLER */


static const struct of_device_id cpg_cocr_match[] = {
#if 0 //[TODO]
	{
		.compatible = "renesas,r9a09g011gbg-cpg-cocr",
		.data = &r9a09g011gbg_cpg_cocr_info,
	},
#endif
	{
		.compatible = "renesas,r9a09g055ma3gbg-cpg-cocr",
		.data = &r9a09g055ma3gbg_cpg_cocr_info,
	},
	{ /* sentinel */ }
};

static void cpg_cocr_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

static int __init cpg_cocr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct cpg_cocr_info *info;
	struct cpg_cocr_priv *priv;
	unsigned int nclks, i;
	struct resource *res;
	struct clk **clks;
	struct rcr_reset *resets;
	int error;

	info = of_device_get_match_data(dev);
	if (info->init) {
		error = info->init(dev);
		if (error)
			return error;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	spin_lock_init(&priv->rmw_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	nclks = info->num_total_core_clks + info->num_hw_mod_clks;
	clks = devm_kmalloc_array(dev, nclks, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	resets = devm_kmalloc_array(dev, info->num_hw_resets, sizeof(*resets), GFP_KERNEL);
	if (!resets)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->clks = clks;
	priv->num_core_clks = info->num_total_core_clks;
	priv->num_mod_clks = info->num_hw_mod_clks;
	priv->last_dt_core_clk = info->last_dt_core_clk;
	priv->mod_clk = info->mod_clks;
	priv->core_clk = info->core_clks;
	priv->no_hw_clk_rate = device_property_read_bool(dev, "cpg-non-set-div");
	RAW_INIT_NOTIFIER_HEAD(&priv->notifiers);

	priv->resets = resets;
	priv->num_resets = info->num_hw_resets;

	for (i = 0; i < nclks; i++)
		clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < info->num_core_clks; i++)
		cpg_cocr_register_core_clk(&info->core_clks[i], info, priv);

	for (i = 0; i < info->num_mod_clks; i++)
		cpg_cocr_register_mod_clk(&info->mod_clks[i], info, priv);

	for (i = 0; i < info->num_resets; i++){
		memcpy(&priv->resets[MOD_CLK_PACK(info->resets[i].id)],&info->resets[i],sizeof(struct rcr_reset));
	}
	error = of_clk_add_provider(np, cpg_cocr_clk_src_twocell_get, priv);
	if (error)
		return error;

	error = devm_add_action_or_reset(dev,cpg_cocr_del_clk_provider,np);
	if (error)
		return error;

	error = cpg_cocr_reset_controller_register(priv);
	if (error)
		return error;

	return 0;
}

static struct platform_driver cpg_cocr_driver = {
	.driver = {
		.name   = "renesas-cpg-cocr",
		.of_match_table = cpg_cocr_match,
	},
};

static int __init cpg_cocr_init(void)
{
	return platform_driver_probe(&cpg_cocr_driver, cpg_cocr_probe);
}

subsys_initcall(cpg_cocr_init);

MODULE_DESCRIPTION("Renesas CPG COCR Driver");
MODULE_LICENSE("GPL v2");

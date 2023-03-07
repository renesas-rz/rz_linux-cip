/*
 * Renesas Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2015 Glider bvba
 *
 * Based on clk-mstp.c, clk-rcar-gen2.c, and clk-rcar-gen3.c
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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

#include "renesas-cpg-clkon.h"
#include "r9a09g011-cpg-mssr.h"
#include "clk-div6.h"

#ifdef DEBUG
#define WARN_DEBUG(x)  WARN_ON(x)
#else
#define WARN_DEBUG(x)  do { } while (0)
#endif

static const u16 smstpcr[] = {
};

/**
 * Clock Pulse Generator / Module Standby and Software Reset Private Data
 *
 * @rcdev: Optional reset controller entity
 * @dev: CPG/MSSR device
 * @base: CPG/MSSR register block base address
 * @rmw_lock: protects RMW register accesses
 * @clks: Array containing all Core and Module Clocks
 * @num_core_clks: Number of Core Clocks in clks[]
 * @num_mod_clks: Number of Module Clocks in clks[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @notifiers: Notifier chain to save/restore clock state for system resume
 * @smstpcr_saved[].mask: Mask of SMSTPCR[] bits under our control
 * @smstpcr_saved[].val: Saved values of SMSTPCR[]
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

       struct raw_notifier_head notifiers; //[TODO]
       struct {
               u32 mask;
               u32 val;
       } smstpcr_saved[ARRAY_SIZE(smstpcr)];
       struct mssr_mod_clk *mod_clk;
       struct cpg_core_clk *core_clk;

};


/**
 * struct mstp_clock - MSTP gating clock
 * @hw: handle between common and hardware-specific interfaces
 * @index: MSTP clock number
 * @priv: CPG/MSSR private data
 */
struct mstp_clock {
       struct clk_hw hw;
       u32 index;
       u32 type;
       u32 reset_no;
       u32 reset_bit;
       u32 reset_msk;
       struct cpg_mssr_priv *priv;
};

#define to_mstp_clock(_hw) container_of(_hw, struct mstp_clock, hw)

static int cpg_mstp_clock_is_enabled(struct clk_hw *hw)
{
       struct mstp_clock *clock = to_mstp_clock(hw);
       struct cpg_mssr_priv *priv = clock->priv;
       unsigned int no = clock->index / 100;
       unsigned int bit = clock->index % 100;
       u32 value;

       value = r8arzv2m_cpg_getClockCtrl(priv->base,no,BIT(bit));

       if(value == 0xFFFFFFFF){
               return 0;
       }

       value = value >> bit;

       return value;
}

static int cpg_mstp_clock_endisable(struct clk_hw *hw, bool enable)
{
       struct mstp_clock *clock = to_mstp_clock(hw);
       struct cpg_mssr_priv *priv = clock->priv;
       unsigned int no = clock->index / 100;
       unsigned int bit = clock->index % 100;
       struct device *dev = priv->dev;
       u32 bitmask = BIT(bit);
       unsigned long flags;
       unsigned int i;
       u32 value;

       dev_dbg(dev, "CLOCKON %u%02u/%pC %s\n", no, bit, hw->clk,
               enable ? "ON" : "OFF");
       value = cpg_mstp_clock_is_enabled(hw);
       if((0 != value) && enable){

               dev_dbg(dev, "%s is enabled",hw->init->name);
               return 0;
       }
       spin_lock_irqsave(&priv->rmw_lock, flags);

       dev_info(dev, "CLOCK SET %u%02u/%pC %s\n", no, bit, hw->clk,
                      enable ? "is enabled" : "is disabled");

       if(clock->type == RST_TYPEA){
               CPG_SetResetCtrl(priv->base,clock->reset_no,BIT(clock->reset_bit),0);
               r8arzv2m_cpg_setClockCtrl(priv->base,no,BIT(bit),0);
               udelay(1);
               CPG_SetResetCtrl(priv->base,clock->reset_no,BIT(clock->reset_bit),BIT(clock->reset_bit));
       }

       if (enable)
               value = bitmask;
       else
               value = 0;

       r8arzv2m_cpg_setClockCtrl(priv->base,no,BIT(bit),value);

       if(clock->type == RST_TYPEB){
               CPG_SetResetCtrl(priv->base,clock->reset_no,BIT(clock->reset_bit),0);
               CPG_SetResetCtrl(priv->base,clock->reset_no,BIT(clock->reset_bit),BIT(clock->reset_bit));
               CPG_WaitResetMon(priv->base,100,clock->reset_msk,clock->reset_msk);
       }

       spin_unlock_irqrestore(&priv->rmw_lock, flags);

       return 0;
}


static int cpg_mstp_clock_enable(struct clk_hw *hw)
{
       struct mstp_clock *clock = to_mstp_clock(hw);//add for debug
       struct cpg_mssr_priv *priv = clock->priv;//add for debug

       return cpg_mstp_clock_endisable(hw, true);
}

static void cpg_mstp_clock_disable(struct clk_hw *hw)
{
       struct mstp_clock *clock = to_mstp_clock(hw);//add for debug
       struct cpg_mssr_priv *priv = clock->priv;//add for debug

       cpg_mstp_clock_endisable(hw, false);
}

static const struct clk_ops cpg_mstp_clock_ops = {
       .enable = cpg_mstp_clock_enable,
       .disable = cpg_mstp_clock_disable,
       .is_enabled = cpg_mstp_clock_is_enabled,
};

static
struct clk *cpg_mssr_clk_src_twocell_get(struct of_phandle_args *clkspec,
                                        void *data)
{
       unsigned int clkidx = clkspec->args[1];
       struct cpg_mssr_priv *priv = data;
       struct device *dev = priv->dev;
       unsigned int idx;
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

static void __init cpg_mssr_register_core_clk(const struct cpg_core_clk *core,
                                             const struct cpg_mssr_info *info,
                                             struct cpg_mssr_priv *priv)
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

               if(core->type == CLK_TYP_STATIC){
                       for (i = 0; i < info->num_mod_clks; i++){
                               if (core->id == info->mod_clks[i].parent) {
                                       //children Cllock off
                                       if(info->mod_clks[i].type == RST_TYPEA){
                                               //Reset A reset
                                               CPG_SetResetCtrl(priv->base,info->mod_clks[i].reset_no,
                                                       BIT(info->mod_clks[i].reset_bit),0);
                                       }
                                       r8arzv2m_cpg_setClockCtrl(priv->base,
                                       info->mod_clks[i].id/100 ,BIT(info->mod_clks[i].id%100),0);
                               }
                       }
               }

               if(core->type == CLK_TYPE_DIV){
                       t = 0;
                       while (10000000 > t++) //Time out in 1sec
                       {
                               if (0 == (readl(priv->base + CPG_CLKSTATUS) & core->status))
                               {
                                       break;
                               }
                               udelay(1);
                       }
               }
               writel(core->msk | core->val, priv->base + core->offset);

               if(core->type == CLK_TYPE_DIV){
                       t = 0;
                       while (10000000 > t++) //Time out in 1sec
                       {
                               if (0 == (readl(priv->base + CPG_CLKSTATUS) & core->status))
                               {
                                       break;
                               }
                               udelay(1);
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


static void __init cpg_mssr_register_mod_clk(const struct mssr_mod_clk *mod,
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

       clock = kzalloc(sizeof(*clock), GFP_KERNEL);
       if (!clock) {
               clk = ERR_PTR(-ENOMEM);
               goto fail;
       }
       init.name = mod->name;
       init.ops = &cpg_mstp_clock_ops;
       //init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
       init.flags = CLK_SET_RATE_PARENT;
       for (i = 0; i < info->num_crit_mod_clks; i++)
               if (id == info->crit_mod_clks[i]) {
                       dev_dbg(dev, "MSTP %s setting CLK_IS_CRITICAL\n",
                               mod->name);
                       init.flags |= CLK_IS_CRITICAL;
                       break;
               }

       parent_name = __clk_get_name(parent);
       init.parent_names = &parent_name;
       init.num_parents = 1;

       clock->index = id - priv->num_core_clks;
       clock->priv = priv;
       clock->hw.init = &init;
       clock->type = mod->type;
       clock->reset_no = mod->reset_no;
       clock->reset_bit = mod->reset_bit;
       clock->reset_msk = mod->reset_msk;

       clk = clk_register(NULL, &clock->hw);
       if (IS_ERR(clk))
               goto fail;

       dev_dbg(dev, "Module clock %pC at %lu Hz\n", clk, clk_get_rate(clk));

       clk_prepare(clk);
       clk_enable(clk);

       priv->clks[id] = clk;
       priv->smstpcr_saved[clock->index / 100].mask |= BIT(clock->index % 100); //[TODO:saved module stop manage]
       return;

fail:
       dev_err(dev, "Failed to register %s clock %s: %ld\n", "module",
               mod->name, PTR_ERR(clk));
       kfree(clock);
}

struct cpg_mssr_clk_domain {
       struct generic_pm_domain genpd;
       struct device_node *np;
       unsigned int num_core_pm_clks;
       unsigned int core_pm_clks[0];
};

static struct cpg_mssr_clk_domain *cpg_mssr_clk_domain;

static bool cpg_mssr_is_pm_clk(const struct of_phandle_args *clkspec,
                              struct cpg_mssr_clk_domain *pd)
{
       unsigned int i;

       if (clkspec->np != pd->np || clkspec->args_count != 2)
               return false;

       switch (clkspec->args[0]) {
       case CPG_CORE:
               for (i = 0; i < pd->num_core_pm_clks; i++)
                       if (clkspec->args[1] == pd->core_pm_clks[i])
                               return true;
               return false;

       case CPG_MOD:
               return true;

       default:
               return false;
       }
}

int cpg_mssr_attach_dev(struct generic_pm_domain *unused, struct device *dev)
{
       struct cpg_mssr_clk_domain *pd = cpg_mssr_clk_domain;
       struct device_node *np = dev->of_node;
       struct of_phandle_args clkspec;
       struct clk *clk;
       int i = 0;
       int error;

       if (!pd) {
               dev_dbg(dev, "CPG/MSSR clock domain not yet available\n");
               return -EPROBE_DEFER;
       }

       while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
                                          &clkspec)) {
               if (cpg_mssr_is_pm_clk(&clkspec, pd))
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
       if (error) {
               dev_err(dev, "pm_clk_create failed %d\n", error);
               goto fail_put;
       }

       error = pm_clk_add_clk(dev, clk);
       if (error) {
               dev_err(dev, "pm_clk_add_clk %pC failed %d\n", clk, error);
               goto fail_destroy;
       }

       return 0;

fail_destroy:
       pm_clk_destroy(dev);
fail_put:
       clk_put(clk);
       return error;
}

void cpg_mssr_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
       if (!pm_clk_no_clocks(dev))
               pm_clk_destroy(dev);
}

static int __init cpg_mssr_add_clk_domain(struct device *dev,
                                         const unsigned int *core_pm_clks,
                                         unsigned int num_core_pm_clks)
{
       struct device_node *np = dev->of_node;
       struct generic_pm_domain *genpd;
       struct cpg_mssr_clk_domain *pd;
       size_t pm_size = num_core_pm_clks * sizeof(core_pm_clks[0]);

       pd = devm_kzalloc(dev, sizeof(*pd) + pm_size, GFP_KERNEL);
       if (!pd)
               return -ENOMEM;

       pd->np = np;
       pd->num_core_pm_clks = num_core_pm_clks;
       memcpy(pd->core_pm_clks, core_pm_clks, pm_size);

       genpd = &pd->genpd;
       genpd->name = np->name;
       genpd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ACTIVE_WAKEUP;
       genpd->attach_dev = cpg_mssr_attach_dev;
       genpd->detach_dev = cpg_mssr_detach_dev;
       pm_genpd_init(genpd, &pm_domain_always_on_gov, false);
       cpg_mssr_clk_domain = pd;

       of_genpd_add_provider_simple(np, genpd);
       return 0;
}


#ifdef CONFIG_RESET_CONTROLLER

#define rcdev_to_priv(x)       container_of(x, struct cpg_mssr_priv, rcdev)

static int cpg_mssr_reset(struct reset_controller_dev *rcdev,
                         unsigned long id)
{
       struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
       unsigned int reg = id / 100;
       unsigned int bit = id % 100;
       u32 bitmask = BIT(bit);
       unsigned long flags;
       u32 value;
       u32 idx;

       /* Reset module */
       spin_lock_irqsave(&priv->rmw_lock, flags);


       for( idx = 0; idx < priv->num_mod_clks; idx++ )
       {
               if((priv->mod_clk[idx].reset_no == reg) && (priv->mod_clk[idx].reset_bit == bit) )
               {
                       dev_info(NULL, "deassert reset[%u] bit[%02u]\n", priv->mod_clk[idx].reset_no, priv->mod_clk[idx].reset_bit);
                       break;

               }
       }

       if(priv->mod_clk->type == RST_TYPEA){
               dev_info(priv->dev,"Not support type A Reset.\n");
       }

       if(priv->mod_clk->type == RST_TYPEB){
               CPG_SetResetCtrl(priv->base,reg,BIT(bit),0);
               CPG_SetResetCtrl(priv->base,reg,BIT(bit),BIT(bit));
               CPG_WaitResetMon(priv->base,100,priv->mod_clk[idx].reset_msk,priv->mod_clk[idx].reset_msk);
       }

       spin_unlock_irqrestore(&priv->rmw_lock, flags);

       return 0;
}

static int cpg_mssr_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
       struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
       unsigned int reg = id / 100;
       unsigned int bit = id % 100;
       u32 bitmask = BIT(bit);
       unsigned long flags;
       u32 value;
       u32 idx;

       dev_dbg(priv->dev, "reset %u%02u\n", reg, bit);

       /* Reset module */
       spin_lock_irqsave(&priv->rmw_lock, flags);


       for( idx = 0; idx < priv->num_mod_clks; idx++ )
       {
               if((priv->mod_clk[idx].reset_no == reg) && (priv->mod_clk[idx].reset_bit == bit) )
               {
                       dev_dbg(NULL, "deassert reset[%u] bit[%02u]\n", priv->mod_clk[idx].reset_no, priv->mod_clk[idx].reset_bit);
                       break;

               }
       }

       if(priv->mod_clk->type == RST_TYPEA){
               dev_info(priv->dev,"Not support type A Reset.\n");//[DEBUG]
       }

       if(priv->mod_clk->type == RST_TYPEB){
               //CPG_SetResetCtrl(priv->base,reg,BIT(bit),0);
               CPG_SetResetCtrl(priv->base,reg,BIT(bit),BIT(bit));
               CPG_WaitResetMon(priv->base,100,priv->mod_clk[idx].reset_msk,priv->mod_clk[idx].reset_msk);
       }

       spin_unlock_irqrestore(&priv->rmw_lock, flags);

       return 0;
}

static int cpg_mssr_deassert(struct reset_controller_dev *rcdev,
                            unsigned long id)
{
       struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
       unsigned int reg = id / 100;
       unsigned int bit = id % 100;
       u32 bitmask = BIT(bit);
       unsigned long flags;
       u32 value;
       u32 idx;

       dev_dbg(priv->dev, "reset %u%02u\n", reg, bit);

       /* Reset module */
       spin_lock_irqsave(&priv->rmw_lock, flags);


       for( idx = 0; idx < priv->num_mod_clks; idx++ )
       {
               if((priv->mod_clk[idx].reset_no == reg) && (priv->mod_clk[idx].reset_bit == bit) )
               {
                       dev_dbg(NULL, "deassert reset[%u] bit[%02u]\n", priv->mod_clk[idx].reset_no, priv->mod_clk[idx].reset_bit);
                       break;

               }
       }

       if(priv->mod_clk->type == RST_TYPEA){
               dev_info(priv->dev,"Not support type A Reset.\n");//[DEBUG]
       }

       if(priv->mod_clk->type == RST_TYPEB){
               CPG_SetResetCtrl(priv->base,reg,BIT(bit),0);
               CPG_WaitResetMon(priv->base,100,priv->mod_clk[idx].reset_msk,0);
       }

       spin_unlock_irqrestore(&priv->rmw_lock, flags);

       return 0;
}

static int cpg_mssr_status(struct reset_controller_dev *rcdev,
                          unsigned long id)
{
       struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
       unsigned int reg = id / 32;
       unsigned int bit = id % 32;
       u32 bitmask = BIT(bit);

       dev_dbg(priv->dev, "status %u%02u\n", reg, bit);

       return 0;
}

static const struct reset_control_ops cpg_mssr_reset_ops = {
       .reset = cpg_mssr_reset,
       .assert = cpg_mssr_assert,
       .deassert = cpg_mssr_deassert,
       .status = cpg_mssr_status,
};

static int cpg_mssr_reset_xlate(struct reset_controller_dev *rcdev,
                               const struct of_phandle_args *reset_spec)
{
       struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
       unsigned int unpacked = reset_spec->args[0];
       unsigned int idx = unpacked;//MOD_CLK_PACK(unpacked);

       if (unpacked % 100 > 31 || idx >= rcdev->nr_resets) {
               dev_err(priv->dev, "Invalid reset index %u\n", unpacked);
               return -EINVAL;
       }

       return idx;
}

static int cpg_mssr_reset_controller_register(struct cpg_mssr_priv *priv)
{
       priv->rcdev.ops = &cpg_mssr_reset_ops;
       priv->rcdev.of_node = priv->dev->of_node;
       priv->rcdev.of_reset_n_cells = 1;
       priv->rcdev.of_xlate = cpg_mssr_reset_xlate;
       priv->rcdev.nr_resets = priv->num_mod_clks;
       return devm_reset_controller_register(priv->dev, &priv->rcdev);
}

#else /* !CONFIG_RESET_CONTROLLER */
static inline int cpg_mssr_reset_controller_register(struct cpg_mssr_priv *priv)
{
       return 0;
}
#endif /* !CONFIG_RESET_CONTROLLER */


static const struct of_device_id cpg_mssr_match[] = {
#ifdef CONFIG_CLK_R9A09G011GBG
       {
               .compatible = "renesas,r8arzv2m-cpg-mssr",
               .data = &r8arzv2m_cpg_mssr_info,
       },
#endif
       { /* sentinel */ }
};

static void cpg_mssr_del_clk_provider(void *data)
{
       of_clk_del_provider(data);
}

static int __init cpg_mssr_probe(struct platform_device *pdev)
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

//     nclks = info->num_total_core_clks + info->num_hw_mod_clks;
       nclks = 20*1000;//[TODO]:Need to adujst the table size.
       clks = devm_kmalloc_array(dev, nclks, sizeof(*clks), GFP_KERNEL);
       if (!clks)
               return -ENOMEM;

       dev_set_drvdata(dev, priv);
       priv->clks = clks;
       priv->num_core_clks = info->num_total_core_clks;
       priv->num_mod_clks = info->num_hw_mod_clks;
       priv->last_dt_core_clk = info->last_dt_core_clk;
       priv->mod_clk = info->mod_clks;
       priv->core_clk = info->core_clks;
       RAW_INIT_NOTIFIER_HEAD(&priv->notifiers);

       for (i = 0; i < nclks; i++)
               clks[i] = ERR_PTR(-ENOENT);

       for (i = 0; i < info->num_core_clks; i++)
               cpg_mssr_register_core_clk(&info->core_clks[i], info, priv);

       for (i = 0; i < info->num_mod_clks; i++)
               cpg_mssr_register_mod_clk(&info->mod_clks[i], info, priv);

       error = of_clk_add_provider(np, cpg_mssr_clk_src_twocell_get, priv);
       if (error)
               return error;

       error = devm_add_action_or_reset(dev,
                                        cpg_mssr_del_clk_provider,
                                        np);
       if (error)
               return error;

       error = cpg_mssr_add_clk_domain(dev, info->core_pm_clks,
                                       info->num_core_pm_clks);
       if (error)
               return error;

       error = cpg_mssr_reset_controller_register(priv);
       if (error)
               return error;

       return 0;
}

static struct platform_driver cpg_mssr_driver = {
       .driver         = {
               .name   = "renesas-cpg-mssr",
               .of_match_table = cpg_mssr_match,

       },
};

static int __init cpg_mssr_init(void)
{
       return platform_driver_probe(&cpg_mssr_driver, cpg_mssr_probe);
}

subsys_initcall(cpg_mssr_init);

MODULE_DESCRIPTION("Renesas CPG/CLKON Driver");
MODULE_LICENSE("GPL v2");

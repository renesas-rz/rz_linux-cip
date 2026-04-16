// SPDX-License-Identifier: GPL-2.0
/*
 * VBATTB clock driver
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/cleanup.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <dt-bindings/clock/renesas,r9a08g045-vbattb.h>

#define VBATTB_BKSCCR			0x1c
#define VBATTB_BKSCCR_SOSEL		6
#define VBATTB_SOSCCR			0x20
#define VBATTB_SOSCCR_SOSTP		0
#define VBATTB_SOSCCR2			0x24
#define VBATTB_SOSCCR2_SOSTP2		0
#define VBATTB_XOSCCR			0x30
#define VBATTB_XOSCCR_OUTEN		16
#define VBATTB_XOSCCR_XSEL		GENMASK(1, 0)
#define VBATTB_XOSCCR_XSEL_4_PF		0x0
#define VBATTB_XOSCCR_XSEL_7_PF		0x1
#define VBATTB_XOSCCR_XSEL_9_PF		0x2
#define VBATTB_XOSCCR_XSEL_12_5_PF	0x3

enum rz_vbattb_type {
	VBATTB_RZG3S,
	VBATTB_RZG3L,
};

/**
 * struct vbattb_clk - VBATTB clock data structure
 * @base: base address
 * @lock: lock
 */
struct vbattb_clk {
	void __iomem *base;
	spinlock_t lock;
	enum rz_vbattb_type devtype;
};

static int vbattb_clk_validate_load_capacitance(u32 *reg_lc, u32 of_lc)
{
	switch (of_lc) {
	case 4000:
		*reg_lc = VBATTB_XOSCCR_XSEL_4_PF;
		break;
	case 7000:
		*reg_lc = VBATTB_XOSCCR_XSEL_7_PF;
		break;
	case 9000:
		*reg_lc = VBATTB_XOSCCR_XSEL_9_PF;
		break;
	case 12500:
		*reg_lc = VBATTB_XOSCCR_XSEL_12_5_PF;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void vbattb_clk_action(void *data)
{
	struct device *dev = data;
	struct reset_control *rstc = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_assert(rstc);
	if (ret)
		dev_err(dev, "Failed to de-assert reset!");

	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_err(dev, "Failed to runtime suspend!");

	of_clk_del_provider(dev->of_node);
}

/*
 * On RZ/G3L, the 32kHz oscillator is stopped by default
 * (VBATTB_SOSCCR.SOSTP = 1).
 *
 * The current implementation only updates SOSCCR2.SOSTP2
 * for the XC clock.
 * However, the hardware requires SOSCCR.SOSTP to be cleard
 * as well for the 32kHz oscillator to run.
 *
 * According to hardware requirements, both:
 *   - SOSCCR.SOSTP
 *   - SOSCCR2.SOSTP2
 * must be cleared to enable the oscillator clock (XC).
 *
 * Update SOSTP based on the currently selected parent XC or XBYP.
 */
static void vbattb_set_sostp_with_parent(struct device_node *node, struct vbattb_clk *vbclk)
{
	struct of_phandle_args clkspec;
	int ret;
	u32 val;

	val = readl_relaxed(vbclk->base + VBATTB_SOSCCR);

	ret = of_parse_phandle_with_args(node, "assigned-clock-parents",
					       "#clock-cells", 0, &clkspec);

	if (ret)
		return;

	if (clkspec.args[0] == VBATTB_XC)
		val &= ~BIT(VBATTB_SOSCCR_SOSTP);
	else if (clkspec.args[0] == VBATTB_XBYP)
		val |= BIT(VBATTB_SOSCCR_SOSTP);

	writel_relaxed(val, vbclk->base + VBATTB_SOSCCR);

	of_node_put(clkspec.np);
}

static int vbattb_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk_parent_data parent_data = {};
	struct clk_hw_onecell_data *clk_data;
	const struct clk_hw *parent_hws[2];
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	struct vbattb_clk *vbclk;
	u32 of_lc, reg_lc;
	struct clk_hw *hw;
	/* 4 clocks are exported: VBATTB_XC, VBATTB_XBYP, VBATTB_MUX, VBATTB_VBATTCLK. */
	u8 num_clks = 4;
	int ret;

	/* Default to 4pF as this is not needed if external clock device is connected. */
	of_lc = 4000;
	of_property_read_u32(np, "quartz-load-femtofarads", &of_lc);

	ret = vbattb_clk_validate_load_capacitance(&reg_lc, of_lc);
	if (ret)
		return ret;

	vbclk = devm_kzalloc(dev, sizeof(*vbclk), GFP_KERNEL);
	if (!vbclk)
		return -ENOMEM;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, num_clks), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	clk_data->num = num_clks;

	vbclk->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vbclk->base))
		return PTR_ERR(vbclk->base);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	rstc = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(rstc))
		return PTR_ERR(rstc);

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = reset_control_deassert(rstc);
	if (ret) {
		pm_runtime_put_sync(dev);
		return ret;
	}

	dev_set_drvdata(dev, rstc);
	ret = devm_add_action_or_reset(dev, vbattb_clk_action, dev);
	if (ret)
		return ret;

	vbclk->devtype = (uintptr_t)of_device_get_match_data(dev);
	if (!vbclk->devtype)
		return -ENODEV;

	spin_lock_init(&vbclk->lock);

	parent_data.fw_name = "rtx";

	hw = devm_clk_hw_register_gate_parent_data(dev, "xc", &parent_data, 0,
						   vbclk->base + VBATTB_SOSCCR2,
						   VBATTB_SOSCCR2_SOSTP2,
						   CLK_GATE_SET_TO_DISABLE, &vbclk->lock);
	/* Update SOSTP bit on RZ/G3L */
	if (vbclk->devtype == VBATTB_RZG3L)
		vbattb_set_sostp_with_parent(np, vbclk);

	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clk_data->hws[VBATTB_XC] = hw;

	hw = devm_clk_hw_register_fixed_factor_fwname(dev, np, "xbyp", "rtx", 0, 1, 1);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clk_data->hws[VBATTB_XBYP] = hw;

	parent_hws[0] = clk_data->hws[VBATTB_XC];
	parent_hws[1] = clk_data->hws[VBATTB_XBYP];
	hw = devm_clk_hw_register_mux_parent_hws(dev, "mux", parent_hws, 2, 0,
						 vbclk->base + VBATTB_BKSCCR,
						 VBATTB_BKSCCR_SOSEL,
						 1, 0, &vbclk->lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clk_data->hws[VBATTB_MUX] = hw;

	/* Set load capacitance before registering the VBATTCLK clock. */
	scoped_guard(spinlock, &vbclk->lock) {
		u32 val = readl_relaxed(vbclk->base + VBATTB_XOSCCR);

		val &= ~VBATTB_XOSCCR_XSEL;
		val |= reg_lc;
		writel_relaxed(val, vbclk->base + VBATTB_XOSCCR);
	}

	/* This feeds the RTC counter clock and it needs to stay on. */
	hw = devm_clk_hw_register_gate_parent_hw(dev, "vbattclk", hw, CLK_IS_CRITICAL,
						 vbclk->base + VBATTB_XOSCCR,
						 VBATTB_XOSCCR_OUTEN, 0,
						 &vbclk->lock);

	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clk_data->hws[VBATTB_VBATTCLK] = hw;

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

static const struct of_device_id vbattb_clk_match[] = {
	{ .compatible = "renesas,r9a08g045-vbattb", .data = (void *)VBATTB_RZG3S },
	{ .compatible = "renesas,r9a08g046-vbattb", .data = (void *)VBATTB_RZG3L },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vbattb_clk_match);

static struct platform_driver vbattb_clk_driver = {
	.driver		= {
		.name	= "renesas-vbattb-clk",
		.of_match_table = vbattb_clk_match,
	},
	.probe = vbattb_clk_probe,
};
module_platform_driver(vbattb_clk_driver);

MODULE_DESCRIPTION("Renesas VBATTB Clock Driver");
MODULE_AUTHOR("Claudiu Beznea <claudiu.beznea.uj@bp.renesas.com>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/* Renesas RZ/G2L General PWM Timer (GPT) MFD core driver */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/units.h>
#include <linux/mfd/rzg2l-gpt.h>

struct rzg2l_gpt_priv {
	struct rzg2l_gpt gpt;
};

u32 rzg2l_gpt_read(struct rzg2l_gpt *gpt, u32 reg)
{
	return readl(gpt->mmio + reg);
}
EXPORT_SYMBOL_GPL(rzg2l_gpt_read);

void rzg2l_gpt_write(struct rzg2l_gpt *gpt, u32 reg, u32 val)
{
	writel(val, gpt->mmio + reg);
}
EXPORT_SYMBOL_GPL(rzg2l_gpt_write);

void rzg2l_gpt_modify(struct rzg2l_gpt *gpt, u32 reg, u32 mask, u32 val)
{
	rzg2l_gpt_write(gpt, reg, (rzg2l_gpt_read(gpt, reg) & ~mask) | val);
}
EXPORT_SYMBOL_GPL(rzg2l_gpt_modify);

static u8 rzg2l_gpt_calculate_prescale(u64 period_ticks)
{
	u32 prescaled_period_ticks = period_ticks >> 32;

	if (prescaled_period_ticks >= 256)
		return 5;

	return (fls(prescaled_period_ticks) + 1) / 2;
}

static u8 rzg3e_gpt_calculate_prescale(u64 period_ticks)
{
	u32 prescaled_period_ticks = period_ticks >> 32;
	u8 prescale;

	if (prescaled_period_ticks >= 64 && prescaled_period_ticks < 256)
		prescale = 6;
	else if (prescaled_period_ticks >= 256 && prescaled_period_ticks < 1024)
		prescale = 8;
	else if (prescaled_period_ticks >= 1024)
		prescale = 10;
	else {
		prescale = fls(prescaled_period_ticks);
		if (prescale > 1)
			prescale -= 1;
	}

	return prescale;
}

static const struct mfd_cell rzg2l_gpt_devs[] = {
	{ .name = "pwm-rzg2l-gpt" },
	{ .name = "rzg2l-gpt-counter" },
};

static int rzg2l_gpt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg2l_gpt_priv *priv;
	struct rzg2l_gpt *gpt;
	const struct rzg2l_gpt_info *info;
	unsigned long rate;
	struct device_node *icu_np __free(device_node) = NULL;
	u32 val;
	int ret;

	info = of_device_get_match_data(dev);
	if (!info)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	gpt = &priv->gpt;
	gpt->dev = dev;
	gpt->pdev = pdev;
	gpt->info = info;
	mutex_init(&gpt->mutex);

	ret = device_property_read_u32(dev, "gpt,max-channels", &val);
	gpt->max_hw_channels = ret ? info->max_hw_channels : val;

	gpt->id = of_alias_get_id(dev->of_node, "gpt");
	if ((int)gpt->id < 0)
		gpt->id = pdev->id >= 0 ? pdev->id : 0;

	gpt->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpt->mmio))
		return PTR_ERR(gpt->mmio);

	gpt->rstc = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(gpt->rstc))
		return dev_err_probe(dev, PTR_ERR(gpt->rstc), "Cannot get reset control\n");

	gpt->rstc_s = devm_reset_control_get_optional_shared(dev, "rst_s");
	if (IS_ERR(gpt->rstc_s))
		return dev_err_probe(dev, PTR_ERR(gpt->rstc_s), "Cannot get rst_s reset\n");

	gpt->clk = devm_clk_get_optional(dev, "bus");
	if (IS_ERR(gpt->clk))
		return dev_err_probe(dev, PTR_ERR(gpt->clk), "Cannot get bus clock\n");

	gpt->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(gpt->clk))
		return dev_err_probe(dev, PTR_ERR(gpt->clk), "Cannot get clock\n");

	ret = devm_clk_rate_exclusive_get(dev, gpt->clk);
	if (ret)
		return ret;

	rate = clk_get_rate(gpt->clk);
	if (!rate)
		return dev_err_probe(dev, -EINVAL, "The GPT clock rate is 0\n");

	/*
	 * Refuse clk rates > 1 GHz to prevent overflow later for computing
	 * period and duty cycle.
	 */
	if (rate > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL, "The GPT clock rate is > 1GHz\n");

	/*
	 * Rate is in MHz and is always integer for peripheral clk
	 * 2^32 * 2^10 (prescalar) * 10^6 (rate_khz) < 2^64
	 * So make sure rate is multiple of 1000.
	 */
	gpt->rate_khz = rate / KILO;
	if (gpt->rate_khz * KILO != rate)
		return dev_err_probe(dev, -EINVAL, "Rate is not multiple of 1000\n");

	if (gpt->info->flags & RZT2H_GPT_HAS_ICU_INTSEL) {
		struct platform_device *icu_pdev;
		icu_np = of_parse_phandle(dev->of_node, "renesas,icu", 0);
		if (!icu_np)
			return dev_err_probe(dev, -ENODEV,
					     "renesas,icu not found\n");

		icu_pdev = of_find_device_by_node(icu_np);
		of_node_put(icu_np);

		if (!icu_pdev)
			return dev_err_probe(dev, -ENODEV,
					     "ICU device not found\n");

		gpt->icu_dev = icu_pdev;
	}

	platform_set_drvdata(pdev, gpt);

	ret = devm_mfd_add_devices(dev, gpt->id, rzg2l_gpt_devs,
				ARRAY_SIZE(rzg2l_gpt_devs), NULL, 0, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add GPT child devices\n");

	return 0;
}

static const struct rzg2l_gpt_info rzg3e_data = {
	.calculate_prescale = rzg3e_gpt_calculate_prescale,
	.gtcr_tpcs = RZG3E_GTCR_TPCS,
	.gtcr_tpcs_value = RZG3E_GTCR_TPCS_P0_1024,
	.prescale_mult = 1,
	.max_hw_channels = 8,
};

static const struct rzg2l_gpt_info rzt2h_data = {
	.flags = RZG2L_GPT_HAS_OVF_IRQ | RZT2H_GPT_HAS_ICU_INTSEL,
	.calculate_prescale = rzg3e_gpt_calculate_prescale,
	.gtcr_tpcs = RZG3E_GTCR_TPCS,
	.prescale_mult = 1,
	.max_hw_channels = 5,
};

static const struct rzg2l_gpt_info rzg2l_data = {
	.flags = RZG2L_GPT_HAS_OVF_IRQ,
	.calculate_prescale = rzg2l_gpt_calculate_prescale,
	.gtcr_tpcs = RZG2L_GTCR_TPCS,
	.gtcr_tpcs_value = RZG2L_GTCR_TPCS_P0_1024,
	.prescale_mult = 2,
	.max_hw_channels = 8,
};

static const struct of_device_id rzg2l_gpt_of_table[] = {
	{ .compatible = "renesas,r9a09g047-gpt", .data = &rzg3e_data },
	{ .compatible = "renesas,r9a09g077-gpt", .data = &rzt2h_data },
	{ .compatible = "renesas,rzg2l-gpt", .data = &rzg2l_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_gpt_of_table);

static struct platform_driver rzg2l_gpt_driver = {
	.probe = rzg2l_gpt_probe,
	.driver = {
		.name = "rzg2l-gpt",
		.of_match_table = rzg2l_gpt_of_table,
	},
};
module_platform_driver(rzg2l_gpt_driver);

MODULE_AUTHOR("NghiaVo <nghia.vo.zn@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L GPT MFD core driver");
MODULE_LICENSE("GPL");

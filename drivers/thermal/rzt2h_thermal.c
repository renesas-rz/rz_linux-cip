// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZT2H TSU Temperature Sensor Unit
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <linux/units.h>
#include <linux/interrupt.h>
#include <linux/arm-smccc.h>

#include "thermal_hwmon.h"

#define RZ_SIP_SVC_GET_SYSTSU		0x82000022

#define CTEMP_MASK			GENMASK(11, 0)

/* Register offsets */
#define TSU_SSUSR		0x00
#define TSU_STRGR		0x04
#define	TSU_SOSR1		0x08
#define TSU_SCRR		0x10
#define TSU_SSR			0x14
#define TSU_CMSR		0x18
#define TSU_SISR		0x30
#define TSU_SIER		0x34
#define TSU_SICR		0x38

#define TSU_SSUSR_EN_TS			BIT(0)
#define TSU_SSUSR_ADC_PD_TS		BIT(1)
#define TSU_SSUSR_SOC_TS_EN		BIT(2)

#define TSU_SOSR1_ADCT_1		0x00
#define TSU_SOSR1_ADCT_2		0x01
#define TSU_SOSR1_ADCT_4		0x02
#define TSU_SOSR1_ADCT_8		0x03
#define TSU_SOSR1_TRGE			BIT(3)
#define TSU_SOSR1_ADCS			BIT(4)
#define TSU_SOSR1_OUTSEL_AVERAGE	BIT(9)
#define TSU_SOSR1_OUTSEL_LATEST		(0 << 9)

#define TSU_CMSR_CMPEN			BIT(0)
#define TSU_CMSR_CMPCOND		BIT(1)

#define TSU_SISR_ADF			BIT(0)

#define TSU_SIER_ADIE			BIT(0)
#define TSU_SIER_CMPIE			BIT(1)

#define TSU_STRGR_ADST			BIT(0)

#define TSU_SICR_ADCLR			BIT(0)
#define TSU_SICR_CMPCLR			BIT(1)

#define TSU_SSR_CONV_RUNNING		BIT(0)

#define TSU_D				40
#define	TSU_E				125

#define MCELSIUS(temp)			((temp) * MILLIDEGREE_PER_DEGREE)

#define RZT2H_TSU_SS_TIMEOUT_US		10000

#define OTP_TSU_REG_ADR_TEMPHI		0x01DC
#define OTP_TSU_REG_ADR_TEMPLO		0x01DD

struct rzt2h_thermal_priv {
	struct device *dev;
	void __iomem *base;
	u32 otp_tscode_temphi;
	u32 otp_tscode_templo;
	struct thermal_zone_device *zone;
	struct clk *clk;
};

static int rzt2h_thermal_get_temp(void *devdata, int *temp)
{
	struct rzt2h_thermal_priv *priv = devdata;
	u32 result, reg;
	s64 val, val1, val2;
	int ret;

	/* Check the conversion status */
	ret = readl_poll_timeout(priv->base + TSU_SSR, reg, !reg,
					50, RZT2H_TSU_SS_TIMEOUT_US);
	if (ret)
		return ret;

	/* Start conversion */
	writel(TSU_STRGR_ADST, priv->base + TSU_STRGR);

	/* The conversion complete interrupt is generated.
	 * Check the interrupt status.
	 */
	ret = readl_poll_timeout(priv->base + TSU_SISR, reg,
					reg == TSU_SISR_ADF, 50,
					RZT2H_TSU_SS_TIMEOUT_US);
	if (ret)
		return ret;

	/* Check the conversion status */
	ret = readl_poll_timeout(priv->base + TSU_SSR, reg, !reg,
					50, RZT2H_TSU_SS_TIMEOUT_US);
	if (ret)
		return ret;

	/* Read the converted code */
	result = readl(priv->base + TSU_SCRR) & CTEMP_MASK;

	/* Compensation formula */
	val = (TSU_E + TSU_D) * (result - priv->otp_tscode_templo);

	val1 = (priv->otp_tscode_temphi - priv->otp_tscode_templo);

	val2 = (val / val1) - TSU_D;

	/* Clear the conversion complete status flag */
	writel(TSU_SICR_ADCLR, priv->base + TSU_SICR);

	/* Check that the conversion complete status flag was cleared */
	ret = readl_poll_timeout(priv->base + TSU_SISR, reg,
					reg == !TSU_SISR_ADF, 50,
					RZT2H_TSU_SS_TIMEOUT_US);
	if (ret)
		return ret;

	*temp = MCELSIUS(val2);

	return 0;
}

static const struct thermal_zone_of_device_ops rzt2h_tz_of_ops = {
	.get_temp = rzt2h_thermal_get_temp,
};

static void rzt2h_thermal_init(struct rzt2h_thermal_priv *priv)
{
	u32 reg;

	/* Release the temperature sensor IP from the power-down state */
	reg = TSU_SSUSR_SOC_TS_EN | TSU_SSUSR_EN_TS;
	writel(reg, priv->base + TSU_SSUSR);

	/* Set the operating mode
	 * - SOSR1: Average data output, Single scan mode, 8 samples.
	 * - CMSR: Default setting, Disable the comparison function.
	 * - SIER: Enable the conversion complete interrupt.
	 */
	reg = TSU_SOSR1_OUTSEL_AVERAGE | TSU_SOSR1_ADCT_8 | BIT(3);
	writel(reg, priv->base + TSU_SOSR1);

	reg = TSU_SIER_ADIE;
	writel(reg, priv->base + TSU_SIER);
}

static int rzt2h_otp_tsu_get_temp(struct platform_device *pdev)
{
	struct rzt2h_thermal_priv *priv = dev_get_drvdata(&pdev->dev);
	struct arm_smccc_res local_res;

	/* Get TSU TSCODE values from OTP registers */
	arm_smccc_smc(RZ_SIP_SVC_GET_SYSTSU, OTP_TSU_REG_ADR_TEMPHI,
					0, 0, 0, 0, 0, 0, &local_res);
	priv->otp_tscode_temphi = local_res.a0 & CTEMP_MASK;

	arm_smccc_smc(RZ_SIP_SVC_GET_SYSTSU, OTP_TSU_REG_ADR_TEMPLO,
					0, 0, 0, 0, 0, 0, &local_res);
	priv->otp_tscode_templo = local_res.a0 & CTEMP_MASK;

	if (priv->otp_tscode_temphi == 0 || priv->otp_tscode_templo == 0) {
		dev_err(priv->dev, "not found OTP value");
		return -EINVAL;
	}

	return 0;
}

static int rzt2h_thermal_remove(struct platform_device *pdev)
{
	struct rzt2h_thermal_priv *priv = dev_get_drvdata(&pdev->dev);

	thermal_remove_hwmon_sysfs(priv->zone);

	return 0;
}

static int rzt2h_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *zone;
	struct rzt2h_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->dev = dev;

	/* Get TSU clk clock */
	priv->clk = devm_clk_get(&pdev->dev, "tsu_clk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk), "no tsu clk");

	/* Enable TSU clk clock */
	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);
	rzt2h_thermal_init(priv);

	ret = rzt2h_otp_tsu_get_temp(pdev);
	if (ret)
		return ret;

	zone = devm_thermal_zone_of_sensor_register(dev, 0, priv,
						    &rzt2h_tz_of_ops);
	if (IS_ERR(zone)) {
		dev_err(dev, "Can't register thermal zone");
		ret = PTR_ERR(zone);
		return ret;
	}

	priv->zone = zone;
	priv->zone->tzp->no_hwmon = false;
	ret = thermal_add_hwmon_sysfs(priv->zone);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id rzt2h_thermal_dt_ids[] = {
	{ .compatible = "renesas,r9a09g077-tsu", },
	{ .compatible = "renesas,r9a09g087-tsu", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzt2h_thermal_dt_ids);

static struct platform_driver rzt2h_thermal_driver = {
	.driver = {
		.name = "rzt2h_thermal",
		.of_match_table = rzt2h_thermal_dt_ids,
	},
	.probe = rzt2h_thermal_probe,
	.remove = rzt2h_thermal_remove,
};
module_platform_driver(rzt2h_thermal_driver);

MODULE_DESCRIPTION("Renesas RZ/T2H TSU Thermal Sensor Driver");
MODULE_AUTHOR("Huy Nguyen <huy.nguyen.wh@renesas.com>");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 *	RZ/G2L TSU thermal sensor driver
 *
 * Copyright (C) 2016 Renesas Electronics Corporation.
 *
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>
#include <linux/reset.h>

#include "thermal_hwmon.h"

#define CTEMP_MASK	0xFFF
#define OTPTSUTRIM0_REG	3148	/* Dsensor at 125 Celsius */
#define OTPTSUTRIM1_REG	503	/* Dsensor at -40 Celsius */

/* Register offsets */
#define R_TSU_SM							0x00
#define R_TSU_ST							0x04
#define R_TSU_SCS							0x08
#define R_TSU_SAD							0x0C
#define R_TSU_SS							0x10
#define R_OTPTSU_REG					0x14
#define R_OTPTSUTRIM_REG(n)	(0x18 + (n * 0x4))

/* Sensor Mode Register(TSU_SM) */
#define EN_TS				BIT(0)
#define ADC_EN_TS		BIT(1)

/* TSU_ST bits */
#define TSU_START	BIT(0)

#define INT(x)	((x) * 1000000)
#define MCELSIUS(temp)	((temp) * 1000)	/* mili Celsius */
#define CAP_TIMES	8	/* Capture  times */

typedef enum
{
		THERMAL_1_POINT_CAL = 0,							 /* 1-Point calibration */
		THERMAL_2_POINT_CAL = 1									 /* 2-Point calibration */
} r_thermal_calibration_t;

struct rzg2l_thermal_tsc {
	void __iomem *base;
	struct thermal_zone_device *zone;
		r_thermal_calibration_t cal;
	int low;
	int high;
	int tj_t;
};

struct rzg2l_thermal_priv {
	struct reset_control *rstc;
	struct rzg2l_thermal_tsc *tsc;
	void (*thermal_init)(struct rzg2l_thermal_tsc *tsc);
};


static inline u32 rzg2l_thermal_read(struct rzg2l_thermal_tsc *tsc,
					 u32 reg)
{
	return ioread32(tsc->base + reg);
}

static inline void rzg2l_thermal_write(struct rzg2l_thermal_tsc *tsc,
						 u32 reg, u32 data)
{
	iowrite32(data, tsc->base + reg);
}

static int rzg2l_thermal_round(int temp)
{
		int result;

		result = temp / 10;

		return result + TEMP_25;
}

static int rzg2l_thermal_get_temp(void *devdata, int *temp)
{
	struct rzg2l_thermal_tsc *tsc = devdata;
	u32 TSCode[CAP_TIMES], result, D_sensor, TSCode_Ave;
	int val, i;

	result = 0;
	/*  AD conversion digital value capture CAP_TIMES */
	for (i = 0; i < CAP_TIMES ; i++)
	{
		TSCode[i] = rzg2l_thermal_read(tsc, R_TSU_SAD) & CTEMP_MASK;
		result = result + TSCode[i];

		udelay(20);
	}

	/* Calculate the average value */
	TSCode_Ave = result/CAP_TIMES;

	/* Curvature correction */
	D_sensor = INT(TSCode_Ave) / (INT(1) + (TSCode_Ave * 13));

	/* Temperature calculation */
	val = ((D_sensor - OTPTSUTRIM1_REG) * (MCELSIUS(165) /
			(OTPTSUTRIM0_REG - OTPTSUTRIM1_REG))) + MCELSIUS(-40);

	/* Round value to device granularity setting */
	*temp = rzg2l_thermal_round(val);

	/* Guaranteed operating range is -40C to 125C. */
	if ((*temp	< MCELSIUS(-40)) || (*temp > MCELSIUS(125)))
		return -EIO;

	return 0;
}

static int rzg2l_thermal_set_trips(void *devdata, int low, int high)
{
	struct rzg2l_thermal_tsc *tsc = devdata;

	low = clamp_val(low, -40000, 120000);
	high = clamp_val(high, -40000, 120000);

	tsc->low = low;
	tsc->high = high;

	return 0;
}

static const struct thermal_zone_of_device_ops rzg2l_tz_of_ops = {
				.get_temp		= rzg2l_thermal_get_temp,
		.set_trips	= rzg2l_thermal_set_trips,
};

static void rzg2l_thermal_init(struct rzg2l_thermal_tsc *tsc)
{
	u32 reg_val;

	rzg2l_thermal_write(tsc, R_TSU_SCS, 0x1);
	rzg2l_thermal_write(tsc, R_TSU_SM, 0x3);
	rzg2l_thermal_write(tsc, R_TSU_ST, 0);

		udelay(60);

	reg_val = rzg2l_thermal_read(tsc, R_TSU_ST);
	reg_val |= TSU_START;
	rzg2l_thermal_write(tsc, R_TSU_ST, reg_val);
	rzg2l_thermal_write(tsc, R_TSU_SS, 0x1);

		usleep_range(1000, 2000);

}

static int rzg2l_thermal_exit(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_zone_device *zone = platform_get_drvdata(pdev);

	devm_thermal_zone_of_sensor_unregister(dev, zone);

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static void rzg2l_hwmon_action(void *data)
{
	struct thermal_zone_device *zone = data;

	thermal_remove_hwmon_sysfs(zone);
}

static int rzg2l_thermal_probe(struct platform_device *pdev)
{
	struct rzg2l_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct thermal_zone_device *zone;
	int ret;

	/* default values if FUSEs are missing */
	/* TODO: Read values from hardware on supported platforms */

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->rstc)) {
	dev_err(&pdev->dev, "failed to get cpg reset\n");
	return PTR_ERR(priv->rstc);
	}

		reset_control_deassert(priv->rstc);

	priv->thermal_init = rzg2l_thermal_init;

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	{
		struct rzg2l_thermal_tsc *tsc;
		struct device_node *np = dev->of_node;
				u32 cal;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			goto error_unregister;

		tsc = devm_kzalloc(dev, sizeof(*tsc), GFP_KERNEL);
		if (!tsc) {
			ret = -ENOMEM;
			goto error_unregister;
		}

				of_property_read_u32(np, "thermal-cal", &cal);

		tsc->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(tsc->base)) {
			ret = PTR_ERR(tsc->base);
			goto error_unregister;
		}
				tsc->cal = cal;
		priv->tsc = tsc;

		priv->thermal_init(tsc);

		zone = devm_thermal_zone_of_sensor_register(dev, 0, tsc,
								&rzg2l_tz_of_ops);
		if (IS_ERR(zone)) {
			dev_err(dev, "Can't register thermal zone\n");
			ret = PTR_ERR(zone);
			goto error_unregister;
		}
		tsc->zone = zone;

		tsc->zone->tzp->no_hwmon = false;
		ret = thermal_add_hwmon_sysfs(tsc->zone);
		if (ret)
			goto error_unregister;

		ret = devm_add_action(dev, rzg2l_hwmon_action, zone);
		if (ret) {
			rzg2l_hwmon_action(zone);
			goto error_unregister;
		}

		return 0;
	}

error_unregister:
	rzg2l_thermal_exit(pdev);

	return ret;
}

static int __maybe_unused rzg2l_thermal_suspend(struct device *dev)
{
	struct rzg2l_thermal_priv *priv = dev_get_drvdata(dev);
		struct rzg2l_thermal_tsc *tsc = priv->tsc;
		u32 reg_val;

		reg_val = rzg2l_thermal_read(tsc, R_TSU_ST);
		reg_val &= ~TSU_START;
		rzg2l_thermal_write(tsc, R_TSU_ST, reg_val);
		rzg2l_thermal_write(tsc, R_TSU_SM, 0);

	return 0;
}

static int __maybe_unused rzg2l_thermal_resume(struct device *dev)
{
	struct rzg2l_thermal_priv *priv = dev_get_drvdata(dev);
	struct rzg2l_thermal_tsc *tsc = priv->tsc;

	priv->thermal_init(tsc);
	rzg2l_thermal_set_trips(tsc, tsc->low, tsc->high);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rzg2l_thermal_pm_ops, rzg2l_thermal_suspend,
			 rzg2l_thermal_resume);

static const struct of_device_id rzg2l_thermal_dt_ids[] = {
	{
		.compatible = "renesas,tsu-r9a07g044l",
	},
	{},
};
MODULE_DEVICE_TABLE(of, rzg2l_thermal_dt_ids);


static struct platform_driver rzg2l_thermal_driver = {
	.driver	= {
		.name	= "rzg2l_thermal",
		.of_match_table = rzg2l_thermal_dt_ids,
		.pm = &rzg2l_thermal_pm_ops,
	},
	.probe		= rzg2l_thermal_probe,
	.remove		= rzg2l_thermal_exit,
};
module_platform_driver(rzg2l_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RZ/G2L TSU thermal sensor driver");
MODULE_AUTHOR("Tien Le (tien.le.xw@renesas.com)");

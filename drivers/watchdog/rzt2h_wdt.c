// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/T2H WDT Watchdog Driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/units.h>
#include <linux/watchdog.h>

#define WDTRR			0x00	/* RW,8 */
#define WDTCR			0x02	/* RW,16 */
#define WDTSR			0x04	/* RW,16 */
#define WDTRCR			0x06	/* RW,8 */

#define WDTCR_TOPS_1024		0x00
#define WDTCR_TOPS_4096		0x01
#define WDTCR_TOPS_8192		0x02
#define WDTCR_TOPS_16384	0x03

#define WDTCR_CKS_CLK_4		0x10
#define WDTCR_CKS_CLK_64	0x40
#define WDTCR_CKS_CLK_128	0xF0
#define WDTCR_CKS_CLK_512	0x60
#define WDTCR_CKS_CLK_2048	0x70
#define WDTCR_CKS_CLK_8192	0x80

#define WDTCR_RPES_75		0x00
#define WDTCR_RPES_50		0x100
#define WDTCR_RPES_25		0x200
#define WDTCR_RPES_0		0x300

#define WDTCR_RPSS_25		0x00
#define WDTCR_RPSS_50		0x1000
#define WDTCR_RPSS_75		0x2000
#define WDTCR_RPSS_100		0x3000

#define WDTDCR_WDTSTOPCTRL	BIT(0)
#define WDTDCR_WDTSTOPMASK	BIT(16)

#define MAX_TIMEOUT_CYCLES	16384
#define CLOCK_DEV_BY_8192	8192

#define WDT_DEFAULT_TIMEOUT	60U

#define PERIERR_ERR_MASK	GENMASK(16, 13)
#define PERIERR_CLR_OFFSET	0xc8
#define PERIERR_RSTMSK_OFFSET	0xb0

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct rzt2h_wdt_priv {
	void __iomem *base;
	void __iomem *base_dbg;
	struct watchdog_device wdev;
	unsigned long pclkl_rate;
	struct clk *pclkl;
};

static u32 rzt2h_wdt_get_cycle_usec(struct rzt2h_wdt_priv *priv,
						unsigned long cycle,
						u16 wdttime)
{
	int clock_division_ratio;

	u64 timer_cycle_us;

	clock_division_ratio = CLOCK_DEV_BY_8192;

	timer_cycle_us = clock_division_ratio * (wdttime + 1) * MICRO;

	return div64_ul(timer_cycle_us, cycle);
}

static int rzt2h_wdt_ping(struct watchdog_device *wdev)
{
	struct rzt2h_wdt_priv *priv = watchdog_get_drvdata(wdev);
	unsigned long delay;

	/*
	 * The down-counter is refreshed and starts counting operation on
	 * a write of the values 00h and FFh to the WDTRR register.
	 */
	writeb(0x0, priv->base + WDTRR);
	writeb(0xFF, priv->base + WDTRR);

	/* Refreshing the down-counter requires up to 4 cycles
	 * of the signal for counting
	 */
	delay = 4 * rzt2h_wdt_get_cycle_usec(priv, priv->pclkl_rate, 0);
	udelay(delay);

	return 0;
}

static void rzt2h_wdt_setup(struct watchdog_device *wdev, u16 wdtcr)
{
	struct rzt2h_wdt_priv *priv = watchdog_get_drvdata(wdev);

	/* Configure the timeout, clock division ratio, and window start and end positions. */
	writew(wdtcr, priv->base + WDTCR);

	/* Enable interrupt output to the ICU. */
	writeb(0, priv->base + WDTRCR);

	/* Clear underflow flag and refresh error flag. */
	writew(0, priv->base + WDTSR);
}

static int rzt2h_wdt_start(struct watchdog_device *wdev)
{
	struct rzt2h_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u32 reg;
	int ret;

	ret = pm_runtime_resume_and_get(wdev->parent);
	if (ret)
		return ret;

	/* Start the count in case of wdt has been stopped by WDTSTOPCTRL */
	reg = readl(priv->base_dbg);
	reg &= ~WDTDCR_WDTSTOPCTRL;
	writel(reg, priv->base_dbg);

	/* Setup WDTCR
	 * - CKS[7:4] - Clock Division Ratio Select - 1000b: pclkl/8192
	 * - RPSS[13:12] - Window Start Position Select - 11b: 100%
	 * - RPES[9:8] - Window End Position Select - 11b: 0%
	 * - TOPS[1:0] - Timeout Period Select - 11b: 16384 cycles (3FFFh)
	 */
	rzt2h_wdt_setup(wdev, WDTCR_CKS_CLK_8192 | WDTCR_RPSS_100 |
			WDTCR_RPES_0 | WDTCR_TOPS_16384);

	/*
	 * Down counting starts after writing the sequence 00h -> FFh to the
	 * WDTRR register. Hence, call the ping operation after loading the counter.
	 */
	rzt2h_wdt_ping(wdev);

	return 0;
}

static int rzt2h_wdt_stop(struct watchdog_device *wdev)
{
	struct rzt2h_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u32 reg;
	int ret;

	/* Stop the count using WDTSTOPCTRL */
	reg = readl(priv->base_dbg);
	reg |= WDTDCR_WDTSTOPCTRL;
	writel(reg, priv->base_dbg);

	ret = pm_runtime_put(wdev->parent);
	if (ret < 0)
		return ret;

	return 0;
}

static int rzt2h_wdt_init_perierr(struct device *dev, struct device_node *np)
{
	struct regmap *syscon;
	int ret;

	syscon = syscon_regmap_lookup_by_phandle(np, "renesas,syscon-perierr-error");
	if (!IS_ERR(syscon)) {
		/* Clear Peripheral Error Event Reset status */
		ret = regmap_write(syscon, PERIERR_CLR_OFFSET, PERIERR_ERR_MASK);
		if (ret)
			return ret;

		/* Unmask Peripheral Error Event Reset */
		ret = regmap_update_bits(syscon, PERIERR_RSTMSK_OFFSET, PERIERR_ERR_MASK, 0);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct watchdog_info rzt2h_wdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
	.identity = "Renesas RZ/T2H WDT Watchdog",
};

static const struct watchdog_ops rzt2h_wdt_ops = {
	.owner = THIS_MODULE,
	.start = rzt2h_wdt_start,
	.stop = rzt2h_wdt_stop,
	.ping = rzt2h_wdt_ping,
};

static int rzt2h_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rzt2h_wdt_priv *priv;
	int ret;

	ret = rzt2h_wdt_init_perierr(dev, np);
	if (ret < 0)
		return ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource_byname(pdev, "wdt");
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->base_dbg = devm_platform_ioremap_resource_byname(pdev, "dbg");
	if (IS_ERR(priv->base_dbg))
		return PTR_ERR(priv->base_dbg);

	priv->pclkl = devm_clk_get_prepared(&pdev->dev, "pclkl");
	if (IS_ERR(priv->pclkl))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->pclkl), "no pclkl");

	priv->pclkl_rate = clk_get_rate(priv->pclkl);
	if (!priv->pclkl_rate)
		return dev_err_probe(&pdev->dev, -EINVAL, "pclkl rate is 0");

	priv->wdev.max_hw_heartbeat_ms = (MILLI * MAX_TIMEOUT_CYCLES * CLOCK_DEV_BY_8192) /
					 priv->pclkl_rate;
	dev_dbg(dev, "max hw timeout of %dms\n", priv->wdev.max_hw_heartbeat_ms);

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	priv->wdev.min_timeout = 1;
	priv->wdev.timeout = WDT_DEFAULT_TIMEOUT;
	priv->wdev.info = &rzt2h_wdt_ident;
	priv->wdev.ops = &rzt2h_wdt_ops;
	priv->wdev.parent = dev;
	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_set_nowayout(&priv->wdev, nowayout);
	watchdog_stop_on_unregister(&priv->wdev);

	ret = watchdog_init_timeout(&priv->wdev, 0, dev);
	if (ret)
		dev_warn(dev, "Specified timeout invalid, using default");

	return devm_watchdog_register_device(&pdev->dev, &priv->wdev);
}

static const struct of_device_id rzt2h_wdt_ids[] = {
	{ .compatible = "renesas,r9a09g077-wdt", },
	{ .compatible = "renesas,r9a09g087-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzt2h_wdt_ids);

static struct platform_driver rzt2h_wdt_driver = {
	.driver = {
		.name = "rzt2h_wdt",
		.of_match_table = rzt2h_wdt_ids,
	},
	.probe = rzt2h_wdt_probe,
};
module_platform_driver(rzt2h_wdt_driver);
MODULE_AUTHOR("Renesas");
MODULE_DESCRIPTION("Renesas RZ/T2H WDT Watchdog Driver");

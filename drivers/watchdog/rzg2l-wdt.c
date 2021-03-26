// SPDX-License-Identifier: GPL-2.0
/*
 * RZG2L Watch dog driver
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/smp.h>
#include <linux/sys_soc.h>
#include <linux/watchdog.h>
#include <linux/delay.h>
#include <linux/reset.h>

#define WDT_DEFAULT_TIMEOUT 60U

#define WDTCNT      0x00
#define WDTSET      0x04
#define WDTTIM      0x08
#define WDTINT      0x0C
#define WDTCNT_WDTEN    BIT(0)
#define WDTINT_INTDISP  BIT(0)

#define F2CYCLE_NSEC(f) (1000000000/f)
#define WDT_CYCLE_MSEC(f, wdttime) ((1024 * 1024 * ((u64)wdttime + 1)) \
				/ ((u64)(f) / 1000000))

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct rzg2l_wdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
	unsigned long	clk_rate;
	unsigned long	xinclk;
	u32	timeout_min, timeout_max;
	int	irq;
	struct reset_control *rstc;
};

static void rzg2l_wdt_wait_delay(struct rzg2l_wdt_priv *priv)
{
	u32 delay;

	/* delay timer when change the setting register */
	delay =  F2CYCLE_NSEC(priv->xinclk)*6 + F2CYCLE_NSEC(priv->clk_rate)*9;
	ndelay(delay);
}

static int rzg2l_wdt_write(struct rzg2l_wdt_priv *priv, u32 val,
				unsigned int reg)
{
	int i;

	if (reg == WDTSET)
		val &= 0xFFF00000;

	writel_relaxed(val, priv->base + reg);

	for (i = 0; i < 1000; i++) {
		if (readl_relaxed(priv->base + reg) == val)
			return 0;
		rzg2l_wdt_wait_delay(priv);
	}

	return -ETIMEDOUT;
}

static int rzg2l_wdt_init_timeout(struct watchdog_device *wdev)
{
	struct rzg2l_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u32 time_out;

	/* Clear Lapsed Time Register and clear Interrupt */
	rzg2l_wdt_write(priv, WDTINT_INTDISP, WDTINT);
	/* Initialize timeout value */
	time_out = ((wdev->timeout)/2) * (1000000)/(priv->timeout_min);
	/* Setting period time register only 12 bit set in WDTSET[31:20] */
	time_out <<= 20;
	/* Delay timer before setting watchdog counter*/
	rzg2l_wdt_wait_delay(priv);

	rzg2l_wdt_write(priv, time_out, WDTSET);

	return 0;
}

static irqreturn_t rzg2l_wdt_irq(int irq, void *devid)
{
	return IRQ_HANDLED;
}

static int rzg2l_wdt_start(struct watchdog_device *wdev)
{
	struct rzg2l_wdt_priv *priv = watchdog_get_drvdata(wdev);

	reset_control_deassert(priv->rstc);
	pm_runtime_get_sync(wdev->parent);

	/* Initialize time out */
	rzg2l_wdt_init_timeout(wdev);

	rzg2l_wdt_wait_delay(priv);
	/* Initialize watchdog counter register */
	rzg2l_wdt_write(priv, 0, WDTTIM);

	/* Enable watchdog timer*/
	rzg2l_wdt_write(priv, WDTCNT_WDTEN, WDTCNT);

	set_bit(WDOG_HW_RUNNING, &wdev->status);

	return 0;
}

static int rzg2l_wdt_stop(struct watchdog_device *wdev)
{
	struct rzg2l_wdt_priv *priv = watchdog_get_drvdata(wdev);

	pm_runtime_put(wdev->parent);
	reset_control_assert(priv->rstc);

	return 0;
}

static const struct watchdog_info rzg2l_wdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
		WDIOF_CARDRESET,
	.identity = "Renesas WDT Watchdog",
};

static int rzg2l_wdt_ping(struct watchdog_device *wdev)
{
	struct rzg2l_wdt_priv *priv = watchdog_get_drvdata(wdev);

	rzg2l_wdt_write(priv, WDTINT_INTDISP, WDTINT);

	return 0;
}
static const struct watchdog_ops rzg2l_wdt_ops = {
	.owner = THIS_MODULE,
	.start = rzg2l_wdt_start,
	.stop = rzg2l_wdt_stop,
	.ping = rzg2l_wdt_ping,
};

static int rzg2l_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg2l_wdt_priv *priv;
	struct resource *res;
	struct clk *clks[2];
	int ret;

	{
		/* FIXME: Hard code to access to CPG registers until can find
		 * another way. CPG_WDTRST_SEL register is belong to CPG used
		 * to mask/unmask WDT overflow system reset
		 */
		void __iomem *cpg_base = ioremap_nocache(0x11010000, 0x1000);

		iowrite32(0x00770077, cpg_base + 0xb14);
		iounmap(cpg_base);
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	clks[0] = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(clks[0]))
		return PTR_ERR(clks[0]);

	clks[1] = of_clk_get(pdev->dev.of_node, 1);
	if (IS_ERR(clks[1]))
		return PTR_ERR(clks[1]);

	priv->rstc = devm_reset_control_get(&pdev->dev, NULL);

	if (IS_ERR(priv->rstc)) {
		dev_err(&pdev->dev, "failed to get cpg reset\n");
		return PTR_ERR(priv->rstc);
	}

	reset_control_deassert(priv->rstc);

	/* Get watchdog clock */
	priv->xinclk = clk_get_rate(clks[0]);
	if (!priv->xinclk) {
		ret = -ENOENT;
		goto out_pm_disable;
	}

	/* Get Peripheral clock */
	priv->clk_rate = clk_get_rate(clks[1]);
	if (!priv->clk_rate) {
		ret = -ENOENT;
		goto out_pm_disable;
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq > 0) {
		ret = devm_request_irq(&pdev->dev, priv->irq,
			rzg2l_wdt_irq, 0, pdev->name, priv);
		if (ret < 0)
			dev_warn(&pdev->dev, "failed to request IRQ\n");
	} else
		dev_err(&pdev->dev, "IRQ index 0 not found\n");

	/* time out min microsecond */
	priv->timeout_min = WDT_CYCLE_MSEC(priv->xinclk, 0);
	/* time out max microsecond */
	priv->timeout_max = WDT_CYCLE_MSEC(priv->xinclk, 0xfff);

	priv->wdev.info = &rzg2l_wdt_ident;
	priv->wdev.ops = &rzg2l_wdt_ops;
	priv->wdev.parent = dev;
	priv->wdev.min_timeout = 1;
	priv->wdev.max_timeout = priv->timeout_max;
	priv->wdev.timeout = WDT_DEFAULT_TIMEOUT;

	platform_set_drvdata(pdev, priv);
	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_stop_on_unregister(&priv->wdev);

	ret = watchdog_init_timeout(&priv->wdev, 0, dev);
	if (ret)
		dev_warn(dev, "Specified timeout invalid, using default\n");

	ret = watchdog_register_device(&priv->wdev);

	if (ret < 0)
		goto out_pm_disable;

	return 0;

out_pm_disable:
	pm_runtime_disable(dev);
	return ret;
}

static int rzg2l_wdt_remove(struct platform_device *pdev)
{
	struct rzg2l_wdt_priv *priv = platform_get_drvdata(pdev);

	watchdog_unregister_device(&priv->wdev);

	return 0;
}

static const struct of_device_id rzg2l_wdt_ids[] = {
	{ .compatible = "renesas,rzg2l-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_wdt_ids);

static struct platform_driver rzg2l_wdt_driver = {
	.driver = {
		.name = "rzg2l_wdt",
		.of_match_table = rzg2l_wdt_ids,
	},
	.probe = rzg2l_wdt_probe,
	.remove = rzg2l_wdt_remove,
};

module_platform_driver(rzg2l_wdt_driver);

MODULE_DESCRIPTION("RZ/G2L WDT driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL v2");

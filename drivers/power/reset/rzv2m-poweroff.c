/*
 * Driver for the Renesas RZ/V2M series poweroff-reset driver
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */

#include <linux/kallsyms.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/delay.h>

#define WCTVAL	0x0048
#define RTCEN	0x004C
#define PWCCTL	0x0050
#define PUCFG	0x0058

#define PUCFG_BOOTFACT_BOOTTIMER	0x1
#define WCTVAL_BOOTTIME_FROM_PWOFF	0x5
#define RTCEN_KICK_BOOTTIMER		0x2
#define PWCCTL_OFF_SEQ			0x1

struct rzv2m_powercon {
	struct device	*dev;
	void __iomem	*base;
};
static struct rzv2m_powercon *rzv2m_poweroff;

static void rzv2m_power_off(void)
{
	struct rzv2m_powercon *pwcon = rzv2m_poweroff;

	dev_info(pwcon->dev, "Power-off system.\n");

	/* Power off sequence */
	writel(PWCCTL_OFF_SEQ, pwcon->base + PWCCTL);

	mdelay(150); //typ 70ms x2

	//Not reached here...
	panic("RZV2M System Power Off: operation not handled.\n");
}

static int rzv2m_poweroff_reset_handler(struct notifier_block *this,
				 unsigned long mode, void *cmd)
{
	struct rzv2m_powercon *pwcon = rzv2m_poweroff;
	u32 val;

	dev_info(pwcon->dev, "Restarting system.\n");

	/* Set the "boot timer" with boot facter */
	writel(PUCFG_BOOTFACT_BOOTTIMER, pwcon->base + PUCFG);

	/* Set the boot-timer interval */
	writel(WCTVAL_BOOTTIME_FROM_PWOFF, pwcon->base + WCTVAL);

	/* Kick the BootTimer */
	writel(RTCEN_KICK_BOOTTIMER, pwcon->base + RTCEN);

	/* Power off sequence */
	writel(PWCCTL_OFF_SEQ, pwcon->base + PWCCTL);
	mdelay(100);

	//Not reached here...
	panic("RZV2M System Reboot: operation not handled.\n");
	return NOTIFY_DONE;
}

static struct notifier_block rzv2m_poweroff_reset_nb = {
	.notifier_call = rzv2m_poweroff_reset_handler,
	.priority = 192,
};

static int rzv2m_poweroff_reset_probe(struct platform_device *pdev)
{
	char symname[KSYM_NAME_LEN];
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct rzv2m_powercon *pwcon;
	int ret;

	pwcon = devm_kzalloc(dev, sizeof(*pwcon), GFP_KERNEL);
	if (!pwcon)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwcon->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pwcon->base))
		return PTR_ERR(pwcon->base);

	pwcon->dev = dev;

	ret = register_restart_handler(&rzv2m_poweroff_reset_nb);
	if (ret) {
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", ret);
		return ret;
	}

	if (pm_power_off) {
		lookup_symbol_name((ulong)pm_power_off, symname);
		dev_err(&pdev->dev,
			"pm_power_off already claimed %p %s",
			pm_power_off, symname);
		return -EBUSY;
	}
	pm_power_off = rzv2m_power_off;
	rzv2m_poweroff = pwcon;

	dev_info(dev, "RZ/V2M poweroff-reset driver registered\n");

	return 0;
}

static int rzv2m_poweroff_reset_remove(struct platform_device *pdev)
{
	pm_power_off = NULL;
	unregister_restart_handler(&rzv2m_poweroff_reset_nb);
	return 0;
}

static const struct of_device_id rzv2m_poweroff_reset_of_match[] = {
	{ .compatible = "renesas,psc-rzv2m", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2m_poweroff_reset_of_match);

static struct platform_driver rzv2m_poweroff_reset_driver = {
	.probe = rzv2m_poweroff_reset_probe,
	.remove = rzv2m_poweroff_reset_remove,
	.driver = {
		.name = "rzv2m_poweroff_reset",
		.of_match_table = rzv2m_poweroff_reset_of_match,
	},
};

module_platform_driver(rzv2m_poweroff_reset_driver);

MODULE_AUTHOR("Shuichi Sueki <shuichi.sueki.zc@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2M Poweroff & Reset Driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ-G2L USBTEST phy reset and characteristic control driver
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define USBTEST_RESET		0x000
#define USBTEST_UDIRPD		0x01C

#define PHYRST			0x1133
#define PHYRST_RELEASE		0x1000

struct rz_g2l_usbtest {
	void __iomem *base;
	struct phy *phy;
};

static int rz_g2l_usbtest_init(struct phy *p)
{
	struct rz_g2l_usbtest *r = phy_get_drvdata(p);
	u32 val = readl(r->base + USBTEST_RESET);

	val |= PHYRST;
	writel(val, r->base + USBTEST_RESET);
	udelay(1);
	val &= PHYRST_RELEASE;
	writel(val, r->base + USBTEST_RESET);

	return 0;
}

static int rz_g2l_usbtest_exit(struct phy *p)
{
	struct rz_g2l_usbtest *r = phy_get_drvdata(p);
	u32 val = readl(r->base + USBTEST_RESET);

	val |= PHYRST;
	writel(val, r->base + USBTEST_RESET);
	return 0;
}

static const struct phy_ops rz_g2l_usbtest_ops = {
	.init		= rz_g2l_usbtest_init,
	.exit		= rz_g2l_usbtest_exit,
	.owner		= THIS_MODULE,
};

static const struct of_device_id rz_g2l_usbtest_match_table[] = {
	{ .compatible = "renesas,rz-g2l-usbtest" },
	{ }
};
MODULE_DEVICE_TABLE(of, rz_g2l_usbtest_match_table);

static int rz_g2l_usbtest_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct rz_g2l_usbtest *r;
	struct resource *res;
	int ret = 0;

	if (!dev->of_node) {
		dev_err(dev, "This driver needs device tree\n");
		return -EINVAL;
	}

	r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	r->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(r->base))
		return PTR_ERR(r->base);

	r->phy = devm_phy_create(dev, NULL, &rz_g2l_usbtest_ops);
	if (IS_ERR(r->phy)) {
		dev_err(dev, "Failed to create USBTEST PHY\n");
		ret = PTR_ERR(r->phy);
		goto error;
	}

	platform_set_drvdata(pdev, r);
	phy_set_drvdata(r->phy, r);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		ret = PTR_ERR(provider);
		goto error;
	}

	return 0;

error:
	return ret;
}

static struct platform_driver rz_g2l_usbtest_driver = {
	.driver = {
		.name		= "phy_rz_g2l_usbtest",
		.of_match_table	= rz_g2l_usbtest_match_table,
	},
	.probe	= rz_g2l_usbtest_probe,
};
module_platform_driver(rz_g2l_usbtest_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas RZ/G2L USBTEST");
MODULE_AUTHOR("biju.das.jz@bp.renesas.com>");

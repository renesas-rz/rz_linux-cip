// SPDX-License-Identifier: GPL-2.0+
/*
 * dwmac-renesas-gbeth.c - DWMAC Specific Glue layer for Renesas GBETH
 *
 * The Rx and Tx clocks are supplied as follows for the GBETH IP.
 *
 *                         Rx / Tx
 *   -------+------------- on / off -------
 *          |
 *          |            Rx-180 / Tx-180
 *          +---- not ---- on / off -------
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include "stmmac_platform.h"

#define SYS_GBETH_CFG_PHY_INTF_TYPE_MII		0
#define SYS_GBETH_CFG_PHY_INTF_TYPE_RGMII	1

struct renesas_sysc_function {
	u32 offset;
	u32 stride;
	const u32 mask;
};

enum renesas_sysc_func_id {
	SYSC_FUNC_ID_PHY_INTF_TYPE,
	SYSC_FUNC_ID_MAX,
};

struct renesas_gbeth_hw_info {
	const struct renesas_sysc_function functions[SYSC_FUNC_ID_MAX];
	u8 max_gbeth_channels;
};

struct renesas_gbeth {
	struct plat_stmmacenet_data *plat_dat;
	struct reset_control *rstc;
	struct device *dev;
	struct regmap *syscon;
	const struct renesas_gbeth_hw_info *info;
	phy_interface_t phy_mode;
	int id;
};

static int renesas_gbeth_sysc_config_func(struct renesas_gbeth *gbeth,
					  enum renesas_sysc_func_id fid,
					  u32 val)
{
	const struct renesas_sysc_function *func = &gbeth->info->functions[fid];
	u32 offset;

	if (fid >= SYSC_FUNC_ID_MAX)
		return -EINVAL;

	if (!func->mask)
		return 0;

	offset = func->offset + (gbeth->id ? func->stride : 0);
	val = (val << (ffs(func->mask) - 1)) & func->mask;

	return regmap_update_bits(gbeth->syscon, offset, func->mask, val);
}

static const char *const renesas_gbeth_clks[] = {
	"tx", "tx-180", "rx", "rx-180",
};

static int renesas_gbeth_init(struct platform_device *pdev, void *priv)
{
	struct plat_stmmacenet_data *plat_dat;
	struct renesas_gbeth *gbeth = priv;
	int ret;
	u32 val;

	plat_dat = gbeth->plat_dat;

	switch (gbeth->phy_mode) {
		case PHY_INTERFACE_MODE_MII:
			val = SYS_GBETH_CFG_PHY_INTF_TYPE_MII;
			break;
		case PHY_INTERFACE_MODE_RGMII:
		case PHY_INTERFACE_MODE_RGMII_ID:
		case PHY_INTERFACE_MODE_RGMII_RXID:
		case PHY_INTERFACE_MODE_RGMII_TXID:
			val = SYS_GBETH_CFG_PHY_INTF_TYPE_RGMII;
			break;
	default:
		dev_err(&pdev->dev, "Unsupported phy mode %s\n",
			phy_modes(gbeth->phy_mode));
		return -EINVAL;
	}

	ret = renesas_gbeth_sysc_config_func(gbeth,
					     SYSC_FUNC_ID_PHY_INTF_TYPE,
					     val);
	if (ret) {
		dev_err(gbeth->dev, "Config PHY interface mode failed\n");
		return ret;
	}

	ret = reset_control_deassert(gbeth->rstc);
	if (ret) {
		dev_err(gbeth->dev, "Reset deassert failed\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(plat_dat->num_clks,
				      plat_dat->clks);
	if (ret)
		reset_control_assert(gbeth->rstc);

	return ret;
}

static void renesas_gbeth_exit(struct platform_device *pdev, void *priv)
{
	struct plat_stmmacenet_data *plat_dat;
	struct renesas_gbeth *gbeth = priv;
	int ret;

	plat_dat = gbeth->plat_dat;

	clk_bulk_disable_unprepare(plat_dat->num_clks, plat_dat->clks);

	ret = reset_control_assert(gbeth->rstc);
	if (ret)
		dev_err(gbeth->dev, "Reset assert failed\n");
}

static int renesas_gbeth_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	struct renesas_gbeth *gbeth;
	const struct renesas_gbeth_hw_info *info;
	unsigned int i;
	int err;

	info = of_device_get_match_data(dev);
	if (!info)
		return -ENODEV;

	err = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to get resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(dev, PTR_ERR(plat_dat),
				     "dt configuration failed\n");

	gbeth = devm_kzalloc(dev, sizeof(*gbeth), GFP_KERNEL);
	if (!gbeth)
		return -ENOMEM;

	gbeth->syscon = syscon_regmap_lookup_by_phandle_args(dev->of_node,
					"renesas,eth-syscon", 1, &gbeth->id);
	if (IS_ERR(gbeth->syscon))
		return dev_err_probe(dev, PTR_ERR(gbeth->syscon),
				"Failed to get ethernet syscon");

	if (gbeth->id >= info->max_gbeth_channels) {
		dev_err(dev, "Invalid gbeth id: %d (max %d)\n",
			gbeth->id, info->max_gbeth_channels - 1);
		return -EINVAL;
	}

	plat_dat->num_clks = ARRAY_SIZE(renesas_gbeth_clks);
	plat_dat->clks = devm_kcalloc(dev, plat_dat->num_clks,
				      sizeof(*plat_dat->clks), GFP_KERNEL);
	if (!plat_dat->clks)
		return -ENOMEM;

	for (i = 0; i < plat_dat->num_clks; i++)
		plat_dat->clks[i].id = renesas_gbeth_clks[i];

	err = devm_clk_bulk_get(dev, plat_dat->num_clks, plat_dat->clks);
	if (err < 0)
		return err;

	plat_dat->clk_tx_i = stmmac_pltfr_find_clk(plat_dat, "tx");
	if (!plat_dat->clk_tx_i)
		return dev_err_probe(dev, -EINVAL,
				     "error finding tx clock\n");

	gbeth->rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(gbeth->rstc))
		return PTR_ERR(gbeth->rstc);

	err = of_get_phy_mode(dev->of_node, &gbeth->phy_mode);
	if (err)
		return dev_err_probe(dev, err, "Failed to get phy mode\n");

	/* Disable TSO and enable TBS on all queues */
	if (of_property_read_bool(node, "enable-time-based-scheduling")) {
		dev_info(dev, "TBS enabled: configuring TX queues for time-based scheduling\n");
		for (i = 0; i < plat_dat->tx_queues_to_use; i++) {
			plat_dat->tx_queues_cfg[i].tbs_en = 1;
			dev_dbg(dev, "TX queue %d: TBS enabled\n", i);
		}
	}

	gbeth->dev = dev;
	gbeth->info = info;
	gbeth->plat_dat = plat_dat;
	plat_dat->bsp_priv = gbeth;
	plat_dat->set_clk_tx_rate = stmmac_set_clk_tx_rate;
	plat_dat->init = renesas_gbeth_init;
	plat_dat->exit = renesas_gbeth_exit;
	plat_dat->flags |= STMMAC_FLAG_RX_CLK_RUNS_IN_LPI;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct renesas_gbeth_hw_info rzv2h_gbeth_info = {
	.max_gbeth_channels = 2,
	.functions = {
		[SYSC_FUNC_ID_PHY_INTF_TYPE] = {
			.offset = 0x0F00,
			.stride = 0x04,
			.mask = GENMASK(18, 16),
		},
	},
};

static const struct renesas_gbeth_hw_info rzg3l_gbeth_info = {
	.max_gbeth_channels = 2,
	.functions = {
		[SYSC_FUNC_ID_PHY_INTF_TYPE] = {
			.offset = 0x0380,
			.stride = 0x10,
			.mask = GENMASK(10, 8),
		},
	},
};

static const struct of_device_id renesas_gbeth_match[] = {
	{ .compatible = "renesas,rzv2h-gbeth", .data = &rzv2h_gbeth_info, },
	{ .compatible = "renesas,r9a08g046-gbeth", .data = &rzg3l_gbeth_info, },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, renesas_gbeth_match);

static struct platform_driver renesas_gbeth_driver = {
	.probe  = renesas_gbeth_probe,
	.driver = {
		.name		= "renesas-gbeth",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table	= renesas_gbeth_match,
	},
};
module_platform_driver(renesas_gbeth_driver);

MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas GBETH DWMAC Specific Glue layer");
MODULE_LICENSE("GPL");

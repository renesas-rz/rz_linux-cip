// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Schneider Electric
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mdio.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phylink.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <dt-bindings/net/rzt2h-ethss.h>
#include <linux/net/renesas/rzt2h-ethss.h>

#define ETHSS_PRCMD			0x0

#define ETHSS_MODCTRL			0x8
#define ETHSS_MODCTRL_SW_MODE		GENMASK(2, 0)

#define ETHSS_PHYLNK			0x14
#define ETHSS_PHYLNK_SWLINK_LOW(x)	BIT(x)
#define ETHSS_PHYLNK_SWLINK_MASK(x)	BIT(x)

#define ETHSS_PHYLNK_ESCLINK_HIGH(x)	(BIT(x) << 4)
#define ETHSS_PHYLNK_ESCLINK_MASK(x)	(BIT(x) << 4)

#define ETHSS_CONVCTRL(port)		(0x100 + (port) * 4)

#define ETHSS_CONVCTRL_CONV_SPEED	GENMASK(1, 0)
#define CONV_MODE_10MBPS		0
#define CONV_MODE_100MBPS		1
#define CONV_MODE_1000MBPS		2

#define ETHSS_CONVCTRL_CONV_MODE		GENMASK(3, 2)
#define CONV_MODE_MII			0
#define CONV_MODE_RMII			1
#define CONV_MODE_RGMII			2

#define ETHSS_CONVCTRL_FULLD		BIT(8)
#define ETHSS_CONVCTRL_RGMII_LINK	BIT(12)
#define ETHSS_CONVCTRL_RGMII_DUPLEX	BIT(13)
#define ETHSS_CONVCTRL_RGMII_SPEED	GENMASK(15, 14)

#define ETHSS_CONVRST			0x114
#define ETHSS_CONVRST_PHYIF_RST(port)	BIT(port)
#define ETHSS_CONVRST_PHYIF_RST_MASK	GENMASK(3, 0)

#define ETHSS_SWCTRL			0x304
#define ETHSS_SWCTRL_MPBS_10(x)		(((0 << 4) | (1 << 0)) << (x))
#define ETHSS_SWCTRL_MPBS_100(x)	(((0 << 4) | (0 << 0)) << (x))
#define ETHSS_SWCTRL_MPBS_1000(x)	(((1 << 4) | (0 << 0)) << (x))
#define ETHSS_SWCTRL_MPBS_MASK(x)	(((1 << 4) | (1 << 0)) << (x))
#define ETHSS_SWDUPC			0x308
#define ETHSS_SWDUPC_DUPLEX_MASK(x)	BIT(x)
#define ETHSS_SWDUPC_DUPLEX_FULL(x)	BIT(x)

#define ETHSS_PTPMCTRL			0x00C
#define ETHSS_PTPMCTRL_GMAC(gmac)	BIT(gmac)
#define ETHSS_PTPMCTRL_PLS_RST		BIT(16)
#define ETHSS_PTPMCTRL_PULSE_GEN	BIT(0)

#define ESC_ECATOFFADR			0x200
#define ESC_ECATOPMOD			0x204
#define ESC_ECATDBGC			0x208
#define ESC_ECATRESOUT			0x210
#define ESC_ECATRESOUT_RST_MASK		BIT(0)

#define ETHSS_MAX_NR_PORTS		4

#define ETHSS_MODCTRL_CONF_CONV_NUM	5
#define ETHSS_MODCTRL_CONF_NONE		-1

/**
 * struct modctrl_match - Matching table entry for  convctrl configuration
 * @mode_cfg: Configuration value for convctrl
 * @conv: Configuration of ethernet port muxes. First index is SWITCH_MANAGEMENT_PORT,
 *	  then index 0 - 3 are CONV0 - CONV3.
 */
struct modctrl_match {
	u32 mode_cfg;
	u8 conv[ETHSS_MODCTRL_CONF_CONV_NUM];
};

static struct modctrl_match modctrl_match_table[] = {
	{0x0, {ETHSS_GMAC0_PORT, ETHSS_SWITCH_PORT0, ETHSS_SWITCH_PORT1,
	       ETHSS_SWITCH_PORT2, ETHSS_GMAC1_PORT}},

	{0x1, {ETHSS_MODCTRL_CONF_NONE, ETHSS_ETHERCAT_PORT0, ETHSS_ETHERCAT_PORT1,
	       ETHSS_GMAC2_PORT, ETHSS_GMAC1_PORT}},

	{0x2, {ETHSS_GMAC0_PORT, ETHSS_ETHERCAT_PORT0, ETHSS_ETHERCAT_PORT1,
		ETHSS_SWITCH_PORT2, ETHSS_GMAC1_PORT}},

	{0x3, {ETHSS_MODCTRL_CONF_NONE, ETHSS_ETHERCAT_PORT0, ETHSS_ETHERCAT_PORT1,
	       ETHSS_ETHERCAT_PORT2, ETHSS_GMAC1_PORT}},

	{0x4, {ETHSS_GMAC0_PORT, ETHSS_SWITCH_PORT0, ETHSS_ETHERCAT_PORT1,
	       ETHSS_ETHERCAT_PORT2, ETHSS_GMAC1_PORT}},

	{0x5, {ETHSS_GMAC0_PORT, ETHSS_SWITCH_PORT0, ETHSS_ETHERCAT_PORT1,
	       ETHSS_SWITCH_PORT2, ETHSS_GMAC1_PORT}},

	{0x6, {ETHSS_GMAC0_PORT, ETHSS_SWITCH_PORT0, ETHSS_SWITCH_PORT1,
	       ETHSS_GMAC2_PORT, ETHSS_GMAC1_PORT}},

	{0x7, {ETHSS_MODCTRL_CONF_NONE, ETHSS_GMAC0_PORT, ETHSS_GMAC1_PORT,
		ETHSS_GMAC2_PORT, ETHSS_MODCTRL_CONF_NONE}}
};

static const char * const conf_to_string[] = {
	[ETHSS_GMAC0_PORT]      = "GMAC0_PORT",
	[ETHSS_GMAC1_PORT]	= "GMAC1_PORT",
	[ETHSS_GMAC2_PORT]	= "GMAC2_PORT",
	[ETHSS_ETHERCAT_PORT0]	= "ETHERCAT_PORT0",
	[ETHSS_ETHERCAT_PORT1]	= "ETHERCAT_PORT1",
	[ETHSS_ETHERCAT_PORT2]	= "ETHERCAT_PORT2",
	[ETHSS_SWITCH_PORT0]	= "SWITCH_PORT0",
	[ETHSS_SWITCH_PORT1]	= "SWITCH_PORT1",
	[ETHSS_SWITCH_PORT2]	= "SWITCH_PORT2",
};

static const char *index_to_string[ETHSS_MODCTRL_CONF_CONV_NUM] = {
	"SWITCH_MANAGEMENT_PORT",
	"CONV0",
	"CONV1",
	"CONV2",
	"CONV3",
};

static void ethss_reg_writel(struct ethss *ethss, int offset, u32 value)
{
	/* ETHSS: Unprotect register writes */
	writel(0x00A5, ethss->base + ETHSS_PRCMD);
	writel(0x0001, ethss->base + ETHSS_PRCMD);
	writel(0xFFFE, ethss->base + ETHSS_PRCMD);
	writel(0x0001, ethss->base + ETHSS_PRCMD);

	writel(value, ethss->base + offset);

	/* Enable protection */
	writel(0x0000, ethss->base + ETHSS_PRCMD);
}

static u32 ethss_reg_readl(struct ethss *ethss, int offset)
{
	return readl(ethss->base + offset);
}

static void ethss_reg_rmw(struct ethss *ethss, int offset, u32 mask, u32 val)
{
	u32 reg;

	spin_lock(&ethss->lock);

	reg = ethss_reg_readl(ethss, offset);
	reg &= ~mask;
	reg |= val;
	ethss_reg_writel(ethss, offset, reg);

	spin_unlock(&ethss->lock);
}

static void ethss_converter_enable(struct ethss *ethss, int port, int enable)
{
	u32 val = 0;

	if (enable)
		val = ETHSS_CONVRST_PHYIF_RST(port);

	ethss_reg_rmw(ethss, ETHSS_CONVRST, ETHSS_CONVRST_PHYIF_RST(port), val);
}

static int ethss_config(struct phylink_pcs *pcs, unsigned int neg_mode,
		       phy_interface_t interface,
		       const unsigned long *advertising, bool permit)
{
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);
	struct ethss *ethss = ethss_port->ethss;
	u32 speed, conv_mode, val, mask;
	int port = ethss_port->port;

	switch (interface) {
	case PHY_INTERFACE_MODE_RMII:
		conv_mode = CONV_MODE_RMII;
		speed = CONV_MODE_100MBPS;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		conv_mode = CONV_MODE_RGMII;
		speed = CONV_MODE_1000MBPS;
		break;
	case PHY_INTERFACE_MODE_MII:
		conv_mode = CONV_MODE_MII;
		/* When in MII mode, speed should be set to 0 (which is actually
		 * CONV_MODE_10MBPS)
		 */
		speed = CONV_MODE_10MBPS;
		break;
	default:
		return -EOPNOTSUPP;
	}

	val = FIELD_PREP(ETHSS_CONVCTRL_CONV_MODE, conv_mode);
	mask = ETHSS_CONVCTRL_CONV_MODE;

	/* Update speed only if we are going to change the interface because
	 * the link might already be up and it would break it if the speed is
	 * changed.
	 */
	if (interface != ethss_port->interface) {
		val |= FIELD_PREP(ETHSS_CONVCTRL_CONV_SPEED, speed);
		mask |= ETHSS_CONVCTRL_CONV_SPEED;
		ethss_port->interface = interface;
	}

	ethss_reg_rmw(ethss, ETHSS_CONVCTRL(port), mask, val);
	ethss_converter_enable(ethss, ethss_port->port, 1);

	return 0;
}

static void ethss_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			 phy_interface_t interface, int speed, int duplex)
{
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);
	struct ethss *ethss = ethss_port->ethss;
	u32 conv_speed = 0, val = 0;
	int port = ethss_port->port;

	if (duplex == DUPLEX_FULL)
		val |= ETHSS_CONVCTRL_FULLD;

	/* No speed in MII through-mode */
	if (interface != PHY_INTERFACE_MODE_MII) {
		switch (speed) {
		case SPEED_1000:
			conv_speed = CONV_MODE_1000MBPS;
			break;
		case SPEED_100:
			conv_speed = CONV_MODE_100MBPS;
			break;
		case SPEED_10:
			conv_speed = CONV_MODE_10MBPS;
			break;
		default:
			return;
		}
	}

	val |= FIELD_PREP(ETHSS_CONVCTRL_CONV_SPEED, conv_speed);
	ethss_reg_rmw(ethss, ETHSS_CONVCTRL(port),
			(ETHSS_CONVCTRL_CONV_SPEED | ETHSS_CONVCTRL_FULLD), val);
}

static int ethss_validate(struct phylink_pcs *pcs, unsigned long *supported,
			 const struct phylink_link_state *state)
{
	if (phy_interface_mode_is_rgmii(state->interface) ||
	    state->interface == PHY_INTERFACE_MODE_RMII ||
	    state->interface == PHY_INTERFACE_MODE_MII)
		return 1;

	return -EINVAL;
}

static int ethss_pre_init(struct phylink_pcs *pcs)
{
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);
	struct ethss *ethss = ethss_port->ethss;
	u32 val, mask;

	/* Start RX clock if required */
	if (pcs->rxc_always_on) {
		/* In MII through mode, the clock signals will be driven by the
		 * external PHY, which might not be initialized yet. Set RMII
		 * as default mode to ensure that a reference clock signal is
		 * generated.
		 */
		ethss_port->interface = PHY_INTERFACE_MODE_RMII;

		val = FIELD_PREP(ETHSS_CONVCTRL_CONV_MODE, CONV_MODE_RMII) |
		      FIELD_PREP(ETHSS_CONVCTRL_CONV_SPEED, CONV_MODE_100MBPS);
		mask = ETHSS_CONVCTRL_CONV_MODE | ETHSS_CONVCTRL_CONV_SPEED;

		ethss_reg_rmw(ethss, ETHSS_CONVCTRL(ethss_port->port), mask, val);

		ethss_converter_enable(ethss, ethss_port->port, 1);
	}

	return 0;
}

static const struct phylink_pcs_ops ethss_phylink_ops = {
	.pcs_validate = ethss_validate,
	.pcs_config = ethss_config,
	.pcs_link_up = ethss_link_up,
	.pcs_pre_init = ethss_pre_init,
};

struct phylink_pcs *ethss_create(struct device *dev, struct device_node *np)
{
	struct platform_device *pdev;
	struct ethss_port *ethss_port;
	struct device_node *pcs_np;
	struct ethss *ethss;
	u32 port;

	if (!of_device_is_available(np))
		return ERR_PTR(-ENODEV);

	if (of_property_read_u32(np, "reg", &port))
		return ERR_PTR(-EINVAL);

	if (port >= ETHSS_MAX_NR_PORTS)
		return ERR_PTR(-EINVAL);

	/* The PCS pdev is attached to the parent node */
	pcs_np = of_get_parent(np);
	if (!pcs_np)
		return ERR_PTR(-ENODEV);

	if (!of_device_is_available(pcs_np)) {
		of_node_put(pcs_np);
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(pcs_np);
	of_node_put(pcs_np);
	if (!pdev || !platform_get_drvdata(pdev)) {
		if (pdev)
			put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	ethss_port = kzalloc(sizeof(*ethss_port), GFP_KERNEL);
	if (!ethss_port) {
		put_device(&pdev->dev);
		return ERR_PTR(-ENOMEM);
	}

	ethss = platform_get_drvdata(pdev);
	device_link_add(dev, ethss->dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	put_device(&pdev->dev);

	ethss_port->ethss = ethss;
	ethss_port->port = port;
	ethss_port->pcs.ops = &ethss_phylink_ops;
	ethss_port->pcs.neg_mode = true;

	return &ethss_port->pcs;
}
EXPORT_SYMBOL(ethss_create);

void ethss_destroy(struct phylink_pcs *pcs)
{
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);

	ethss_converter_enable(ethss_port->ethss, ethss_port->port, 0);
	kfree(ethss_port);
}
EXPORT_SYMBOL(ethss_destroy);

void ethss_switchcore_adjust(struct phylink_pcs *pcs, int duplex, int speed)
{
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);
	struct ethss *ethss = ethss_port->ethss;
	int port = ethss_port->port;
	u32 val;

	/* We only handle speed/duplex changes on the switch ports */
	if (port > 2)
		return;

	/* Speed */
	if (speed == SPEED_1000)
		val = ETHSS_SWCTRL_MPBS_1000(port);
	else if (speed == SPEED_100)
		val = ETHSS_SWCTRL_MPBS_100(port);
	else
		val = ETHSS_SWCTRL_MPBS_10(port);

	ethss_reg_rmw(ethss, ETHSS_SWCTRL, ETHSS_SWCTRL_MPBS_MASK(port), val);

	/* Duplex */
	val = 0;
	if (duplex == DUPLEX_FULL)
		val = ETHSS_SWDUPC_DUPLEX_FULL(port);

	ethss_reg_rmw(ethss, ETHSS_SWDUPC, ETHSS_SWDUPC_DUPLEX_MASK(port), val);
}
EXPORT_SYMBOL(ethss_switchcore_adjust);

int ethss_gmac_ptp_timer(struct ethss *ethss, int gmac, int ethsw_timer)
{
	if (gmac > 2)
		return -EINVAL;

	if (ethsw_timer > 1)
		return -EINVAL;

	if (ethsw_timer)
		ethss_reg_rmw(ethss, ETHSS_PTPMCTRL,
			      ETHSS_PTPMCTRL_GMAC(gmac), ETHSS_PTPMCTRL_GMAC(gmac));

	return 0;
}
EXPORT_SYMBOL(ethss_gmac_ptp_timer);

void ethss_esc_config(struct ethss *ethss, int eeprom_size, int phy_offset,
							int port_delay)
{
	ethss_reg_writel(ethss, ESC_ECATOFFADR, phy_offset);
	ethss_reg_writel(ethss, ESC_ECATOPMOD, eeprom_size);
	ethss_reg_writel(ethss, ESC_ECATDBGC, (port_delay << 4)
				| (port_delay << 2) | (port_delay << 0));
}
EXPORT_SYMBOL(ethss_esc_config);

void ethss_esc_reset_out(struct ethss *ethss, int rst_val)
{
	ethss_reg_rmw(ethss, ESC_ECATRESOUT, ESC_ECATRESOUT_RST_MASK, rst_val);
}
EXPORT_SYMBOL(ethss_esc_reset_out);

static int ethss_init_hw(struct ethss *ethss, u32 cfg_mode)
{
	int port;

	for (port = 0; port < ETHSS_MAX_NR_PORTS; port++) {
		ethss_converter_enable(ethss, port, 0);
		/* Disable speed/duplex control from these registers, datasheet
		 * says switch registers should be used to setup switch port
		 * speed and duplex.
		 */
		ethss_reg_writel(ethss, ETHSS_SWCTRL, 0x0);
		ethss_reg_writel(ethss, ETHSS_SWDUPC, 0x0);
	}

	ethss_reg_writel(ethss, ETHSS_MODCTRL,
			FIELD_PREP(ETHSS_MODCTRL_SW_MODE, cfg_mode));

	return 0;
}

static bool ethss_modctrl_match(s8 table_val[ETHSS_MODCTRL_CONF_CONV_NUM],
			       s8 dt_val[ETHSS_MODCTRL_CONF_CONV_NUM])
{
	int i;

	for (i = 0; i < ETHSS_MODCTRL_CONF_CONV_NUM; i++) {
		if (dt_val[i] == ETHSS_MODCTRL_CONF_NONE)
			continue;

		if (dt_val[i] != table_val[i])
			return false;
	}

	return true;
}

static void ethss_dump_conf(struct device *dev,
			   s8 conf[ETHSS_MODCTRL_CONF_CONV_NUM])
{
	const char *conf_name;
	int i;

	for (i = 0; i < ETHSS_MODCTRL_CONF_CONV_NUM; i++) {
		if (conf[i] != ETHSS_MODCTRL_CONF_NONE)
			conf_name = conf_to_string[conf[i]];
		else
			conf_name = "NONE";

		dev_err(dev, "%s: %s\n", index_to_string[i], conf_name);
	}
}

static int ethss_match_dt_conf(struct device *dev,
			      s8 dt_val[ETHSS_MODCTRL_CONF_CONV_NUM],
			      u32 *mode_cfg)
{
	struct modctrl_match *table_entry;
	int i;

	ethss_dump_conf(dev, dt_val);

	for (i = 0; i < ARRAY_SIZE(modctrl_match_table); i++) {
		table_entry = &modctrl_match_table[i];

		if (ethss_modctrl_match(table_entry->conv, dt_val)) {
			*mode_cfg = table_entry->mode_cfg;
			return 0;
		}
	}

	dev_err(dev, "Failed to apply requested configuration\n");

	return -EINVAL;
}

static int ethss_parse_dt(struct device *dev, u32 *mode_cfg)
{
	s8 dt_val[ETHSS_MODCTRL_CONF_CONV_NUM];
	struct device_node *np = dev->of_node;
	struct device_node *conv;
	u32 conf;
	int port;

	memset(dt_val, ETHSS_MODCTRL_CONF_NONE, sizeof(dt_val));

	if (of_property_read_u32(np, "renesas,ethss-switch-portmanagement", &conf) == 0)
		dt_val[0] = conf;

	for_each_child_of_node(np, conv) {
		if (of_property_read_u32(conv, "reg", &port))
			continue;

		if (!of_device_is_available(conv))
			continue;

		if (of_property_read_u32(conv, "renesas,ethss-port", &conf) == 0)
			dt_val[port + 1] = conf;
	}

	return ethss_match_dt_conf(dev, dt_val, mode_cfg);
}

static void ethss_parse_esclink(struct ethss *ethss, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *conv;
	int val, esclink, port;

	for_each_child_of_node(np, conv) {
		if (of_property_read_u32(conv, "renesas,esc-phylink", &esclink))
			continue;

		if (of_property_read_u32(conv, "reg", &port))
			continue;

		if (!of_device_is_available(conv))
			continue;

		/* ESC_PHYLINK active high */
		val = 0;
		if (esclink == 1)
			val = ETHSS_PHYLNK_ESCLINK_HIGH(port);

		ethss_reg_rmw(ethss, ETHSS_PHYLNK, ETHSS_PHYLNK_ESCLINK_MASK(port), val);
	}
}

static void ethss_parse_phylnk(struct ethss *ethss, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *conv;
	int val, swlink, port;

	for_each_child_of_node(np, conv) {
		if (of_property_read_u32(conv, "renesas,ethsw-phylink", &swlink))
			continue;

		if (of_property_read_u32(conv, "reg", &port))
			continue;

		if (!of_device_is_available(conv))
			continue;

		/* ETHSW_PHYLINK active low */
		val = 0;
		if (swlink == 0)
			val = ETHSS_PHYLNK_SWLINK_LOW(port);

		ethss_reg_rmw(ethss, ETHSS_PHYLNK, ETHSS_PHYLNK_SWLINK_MASK(port), val);
	}
}

static int ethss_parse_pulse_gen(struct ethss *ethss, struct device *dev)
{
	u32 pulse_gen_timer;

	of_property_read_u32(dev->of_node, "ethsw_pulse_gen_timer", &pulse_gen_timer);

	if (pulse_gen_timer > 1)
		return -EINVAL;

	if (pulse_gen_timer)
		ethss_reg_rmw(ethss, ETHSS_PTPMCTRL,
				ETHSS_PTPMCTRL_PULSE_GEN, ETHSS_PTPMCTRL_PULSE_GEN);

	return 0;
}

static int ethss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ethss *ethss;
	u32 mode_cfg;
	int ret;

	ret = ethss_parse_dt(dev, &mode_cfg);
	if (ret < 0)
		return ret;

	ethss = devm_kzalloc(dev, sizeof(*ethss), GFP_KERNEL);
	if (!ethss)
		return -ENOMEM;

	spin_lock_init(&ethss->lock);
	ethss->dev = dev;
	ethss->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ethss->base))
		return PTR_ERR(ethss->base);

	ethss->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ethss->clk)) {
		dev_err(dev, "failed to get device clock\n");
		return PTR_ERR(ethss->clk);
	}
	clk_prepare_enable(ethss->clk);

	ethss->rst_ethss = devm_reset_control_get(dev, "reset_ethss");
	if (IS_ERR(ethss->rst_ethss)) {
		dev_err(dev, "failed to get reset_ethss\n");
		return PTR_ERR(ethss->rst_ethss);
	}
	reset_control_deassert(ethss->rst_ethss);

	ethss->rst_conv = devm_reset_control_get(dev, "reset_conv");
	if (IS_ERR(ethss->rst_conv)) {
		dev_err(dev, "failed to get reset_conv\n");
		return PTR_ERR(ethss->rst_conv);
	}
	reset_control_deassert(ethss->rst_conv);

	ret = devm_pm_runtime_enable(dev);
	if (ret < 0)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = ethss_init_hw(ethss, mode_cfg);
	if (ret)
		goto disable_runtime_pm;

	ethss_parse_phylnk(ethss, dev);

	/* Release Pulse Generator reset state */
	ethss_reg_rmw(ethss, ETHSS_PTPMCTRL,
			ETHSS_PTPMCTRL_PLS_RST, ETHSS_PTPMCTRL_PLS_RST);

	ret = ethss_parse_pulse_gen(ethss, dev);
	if (ret)
		dev_err(dev, "failed to set Pulse Generator\n");

	ethss_parse_esclink(ethss, dev);

	/* ethss_create() relies on that fact that data are attached to the
	 * platform device to determine if the driver is ready so this needs to
	 * be the last thing to be done after everything is initialized
	 * properly.
	 */
	platform_set_drvdata(pdev, ethss);

	dev_info(dev, "ETH Subsystem running in mode 0x%x\n", mode_cfg);
	dev_info(dev, "ETH Subsystem probed OK\n");

	return 0;

disable_runtime_pm:
	pm_runtime_put(dev);

	return ret;
}

static void ethss_remove(struct platform_device *pdev)
{
	struct ethss *ethss = platform_get_drvdata(pdev);

	pm_runtime_put(&pdev->dev);
	reset_control_assert(ethss->rst_ethss);
	reset_control_assert(ethss->rst_conv);
	clk_disable_unprepare(ethss->clk);
}

static const struct of_device_id ethss_of_mtable[] = {
	{ .compatible = "renesas,rzt2h-ethss" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ethss_of_mtable);

static struct platform_driver ethss_driver = {
	.driver = {
		.name	 = "rzt2h_ethss",
		.suppress_bind_attrs = true,
		.of_match_table = ethss_of_mtable,
	},
	.probe = ethss_probe,
	.remove_new = ethss_remove,
};
module_platform_driver(ethss_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas MII converter PCS driver");
MODULE_AUTHOR("Long Luu <long.luu.ur@renesas.com>");

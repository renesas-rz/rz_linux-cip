// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3E LVDS Encoder
 *
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "rzg3e_lvds.h"
#include "rzg3e_lvds_regs.h"

#define MAX_LVDS_CHAN_NUM 2

struct rzg3e_lvds;

enum rzg3e_lvds_mode {
	RZG3E_LVDS_MODE_JEIDA = 0,
	RZG3E_LVDS_MODE_JEIDA_MIRROR = 1,
	RZG3E_LVDS_MODE_MODE2 = 2,
	RZG3E_LVDS_MODE_MODE2_MIRROR = 3,
	RZG3E_LVDS_MODE_VESA = 4,
	RZG3E_LVDS_MODE_VESA_MIRROR = 5,
	RZG3E_LVDS_MODE_MODE6 = 6,
	RZG3E_LVDS_MODE_MODE6_MIRROR = 7,
};

enum rzg3e_lvds_link_type {
	RZG3E_LVDS_SINGLE_LINK = 0,
	RZG3E_LVDS_DUAL_LINK_EVEN_ODD_PIXELS = 1,
	RZG3E_LVDS_DUAL_LINK_ODD_EVEN_PIXELS = 2,
};

struct rzg3e_lvds {
	struct device *dev;

	struct reset_control *rstc;
	struct drm_bridge bridge;

	struct drm_bridge *next_bridge;
	struct drm_panel *panel;

	struct clk *pclk;
	struct clk *phyclk;
	struct clk *dotclk;

	struct regmap *regmap;

	struct drm_bridge *companion;
	enum rzg3e_lvds_link_type link_type;

	const char *in0_name;
	const char *in1_name;

	u32 ch, mode;

	bool out0_enabled;
	bool out1_enabled;
};

#define bridge_to_rzg3e_lvds(b) \
	container_of(b, struct rzg3e_lvds, bridge)

static int rzg3e_lvds_reset_deassert_pclk_enable(struct device *dev)
{
	struct rzg3e_lvds *lvds = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(lvds->rstc);
	if (ret < 0) {
		dev_err(lvds->dev, "deassert error");
		return ret;
	}

	ret = clk_prepare_enable(lvds->pclk);
	if (ret) {
		reset_control_assert(lvds->rstc);
		dev_err(lvds->dev, "cannot enable pclk");
		return ret;
	}

	return ret;
}

static void rzg3e_lvds_reset_assert_pclk_disable(struct device *dev)
{
	struct rzg3e_lvds *lvds = dev_get_drvdata(dev);

	clk_disable_unprepare(lvds->pclk);
	reset_control_assert(lvds->rstc);
}

static void rzg3e_lvds_read(struct rzg3e_lvds *lvds, u32 reg, u32 *data)
{
	regmap_read(lvds->regmap, reg, data);
}

static void rzg3e_lvds_write(struct rzg3e_lvds *lvds, u32 reg, u32 data)
{
	regmap_write(lvds->regmap, reg, data);
}

static void rzg3e_lvds_rmw(struct rzg3e_lvds *lvds, u32 offs, u32 msk, u32 val)
{
	u32 reg_val;

	rzg3e_lvds_read(lvds, offs, &reg_val);

	reg_val &= ~msk;
	reg_val |= (val & msk);

	rzg3e_lvds_write(lvds, offs, reg_val);
}

/* -----------------------------------------------------------------------------
 * Bridge
 */
static void rzg3e_lvds_dbg(struct rzg3e_lvds *lvds)
{
	struct device *dev = lvds->dev;

	dev_dbg(dev, "link type detected: %d\n", lvds->link_type);

	switch (lvds->mode) {
	case LVDS_SINGLE_LINK_CH0:
		dev_dbg(dev, "ch: %d, mode: LVDS_SINGLE_LINK_CH0\n",
			lvds->ch);
		break;
	case LVDS_SINGLE_LINK_CH1:
		dev_dbg(dev, "ch: %d, mode: LVDS_SINGLE_LINK_CH1\n",
			lvds->ch);
		break;
	case LVDS_SINGLE_LINK_DUAL:
		dev_dbg(dev, "ch: %d, mode: LVDS_SINGLE_LINK_DUAL\n",
			lvds->ch);
		break;
	case LVDS_SINGLE_LINK_MULTI:
		dev_dbg(dev, "ch: %d, mode: LVDS_SINGLE_LINK_MULTI\n",
			lvds->ch);
		break;
	case LVDS_DUAL_LINK:
		dev_dbg(dev, "ch: %d, mode: LVDS_DUAL_LINK\n",
			lvds->ch);
		break;
	default:
		dev_dbg(dev, "ch: %d, mode: INVALID\n",
			lvds->ch);
	}
}

static u32 rzg3e_lvds_single_link_mode(struct device *dev, u32 id)
{
	struct rzg3e_lvds *lvds = dev_get_drvdata(dev);
	u32 mode;

	mode = lvds->mode;

	if (lvds->out0_enabled && lvds->out1_enabled) {
		if (!strcmp(lvds->in0_name, lvds->in1_name))
			mode = LVDS_SINGLE_LINK_MULTI;
		else
			mode = LVDS_SINGLE_LINK_DUAL;
	}

	return mode;
}

static void rzg3e_lvds_enable(struct drm_bridge *bridge,
			      struct drm_atomic_state *state,
			      struct drm_crtc *crtc,
			      struct drm_connector *connector,
			      u32 fmt)
{
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);
	u32 reg_msk, reg_val;
	u32 skw_adj = 0;
	int ret;

	/* Enable the companion LVDS encoder in dual-link mode. */
	if (lvds->link_type != RZG3E_LVDS_SINGLE_LINK && lvds->companion)
		rzg3e_lvds_enable(lvds->companion, state, crtc, connector,
				  fmt);

	ret = clk_prepare_enable(lvds->phyclk);
	if (ret < 0) {
		dev_err(lvds->dev, "phyclk error");
		return;
	}

	ret = clk_prepare_enable(lvds->dotclk);
	if (ret < 0) {
		clk_disable_unprepare(lvds->phyclk);
		dev_err(lvds->dev, "dotclk error");
		return;
	}

	reg_msk = LVDS_CTL_FMT_SEL;
	reg_val = LVDS_CTL_SET(FMT_SEL, 0, fmt);

	if (lvds->ch == 0) {
		rzg3e_lvds_rmw(lvds, LVDS_0_PHY_OFFSET,
			       LVDS_PHY_CH_IO_EN, 0x1F);
		rzg3e_lvds_rmw(lvds, LVDS_0_CTL_OFFSET, reg_msk, reg_val);
		reg_msk = LVDS_PHY_CH_SKW_ADJ;
		reg_val = LVDS_PHY_SET(CH_SKW_ADJ, 0, skw_adj);
		rzg3e_lvds_rmw(lvds, LVDS_0_PHY_OFFSET, reg_msk, reg_val);
	}

	if (lvds->ch == 1) {
		rzg3e_lvds_rmw(lvds, LVDS_1_PHY_OFFSET,
			       LVDS_PHY_CH_IO_EN, 0x1F);
		rzg3e_lvds_rmw(lvds, LVDS_1_CTL_OFFSET, reg_msk, reg_val);
		reg_msk = LVDS_PHY_CH_SKW_ADJ;
		reg_val = LVDS_PHY_SET(CH_SKW_ADJ, 0, skw_adj);
		rzg3e_lvds_rmw(lvds, LVDS_1_PHY_OFFSET, reg_msk, reg_val);
	}

	/* Wait 200us (Analog stable period is 100 us. this time count adds margin.) */
	usleep_range(200, 250);
}

static void rzg3e_lvds_atomic_enable(struct drm_bridge *bridge,
				     struct drm_bridge_state *old_bridge_state)
{
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);
	const struct drm_bridge_state *bridge_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	u32 fmt;
	int ret;

	/* Get the LVDS format from the bridge state. */
	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	if (!bridge_state) {
		dev_err(lvds->dev, "failed to get bridge state");
		return;
	}

	switch (bridge_state->output_bus_cfg.format) {
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		fmt = RZG3E_LVDS_MODE_JEIDA;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		fmt = RZG3E_LVDS_MODE_VESA;
		break;
	default:
		fmt = RZG3E_LVDS_MODE_VESA;
		dev_warn(lvds->dev,
			 "Unsupported LVDS bus format 0x%04x, please check output bridge driver. Falling back to vesa-24.\n",
			 bridge_state->output_bus_cfg.format);
		break;
	}

	ret = rzg3e_lvds_reset_deassert_pclk_enable(lvds->dev);
	if (ret < 0) {
		dev_err(lvds->dev, "deassert error");
		return;
	}

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (!connector) {
		dev_err(lvds->dev, "failed to get connector");
		return;
	}

	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	if (!crtc) {
		dev_err(lvds->dev, "failed to get crtc");
		return;
	}

	rzg3e_lvds_write(lvds, LVDS_CMN, LVDS_UNUSED);
	if (lvds->link_type == RZG3E_LVDS_SINGLE_LINK)
		lvds->mode = rzg3e_lvds_single_link_mode(lvds->dev,
							 lvds->ch);

	rzg3e_lvds_dbg(lvds);

	rzg3e_lvds_write(lvds, LVDS_CMN, lvds->mode);
	rzg3e_lvds_enable(bridge, state, crtc, connector, fmt);
	rzg3e_lvds_write(lvds, LVDS_CMN, lvds->mode | LVDS_CMN_PHY_RESET_N);
}

static void rzg3e_lvds_disable(struct drm_bridge *bridge,
			       struct drm_atomic_state *state)
{
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);

	/* Disable the companion LVDS encoder in dual-link mode. */
	if (lvds->link_type != RZG3E_LVDS_SINGLE_LINK && lvds->companion)
		rzg3e_lvds_disable(lvds->companion, state);

	clk_disable_unprepare(lvds->phyclk);
	clk_disable_unprepare(lvds->dotclk);
}

static void
rzg3e_lvds_atomic_disable(struct drm_bridge *bridge,
			  struct drm_bridge_state *old_bridge_state)
{
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);

	rzg3e_lvds_disable(bridge, state);
	rzg3e_lvds_reset_assert_pclk_disable(lvds->dev);
}

static bool rzg3e_lvds_mode_fixup(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	adjusted_mode->clock = clamp(adjusted_mode->clock, 5400, 187500);

	return true;
}

static int rzg3e_lvds_attach(struct drm_bridge *bridge,
			     enum drm_bridge_attach_flags flags)
{
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);

	if (!lvds->next_bridge)
		return 0;

	return drm_bridge_attach(bridge->encoder, lvds->next_bridge, bridge, flags);
}

static const struct drm_bridge_funcs rzg3e_lvds_bridge_ops = {
	.attach = rzg3e_lvds_attach,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_enable = rzg3e_lvds_atomic_enable,
	.atomic_disable = rzg3e_lvds_atomic_disable,
	.mode_fixup = rzg3e_lvds_mode_fixup,
};

static bool rzg3e_lvds_dual_link_check(struct drm_bridge *bridge)
{
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);

	return lvds->link_type != RZG3E_LVDS_SINGLE_LINK;
}

static bool rzg3e_lvds_is_connected_check(struct drm_bridge *bridge)
{
	struct rzg3e_lvds *lvds = bridge_to_rzg3e_lvds(bridge);

	return !!lvds->next_bridge;
}

bool rzg3e_lvds_dual_link(struct drm_bridge *bridge)
{
	return rzg3e_lvds_dual_link_check(bridge);
}
EXPORT_SYMBOL_GPL(rzg3e_lvds_dual_link);

bool rzg3e_lvds_is_connected(struct drm_bridge *bridge)
{
	return rzg3e_lvds_is_connected_check(bridge);
}
EXPORT_SYMBOL_GPL(rzg3e_lvds_is_connected);

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */
static u32 rzg3e_lvds_get_channel_by_node(struct device_node *node)
{
	u32 ch;
	int ret;

	ret = of_property_read_u32(node, "renesas,id", &ch);
	if (ret || ch > MAX_LVDS_CHAN_NUM - 1)
		return -EINVAL;

	return ch;
}

static int rzg3e_lvds_parse_dt_companion(struct rzg3e_lvds *lvds)
{
	const struct of_device_id *match;
	struct device_node *companion;
	struct device_node *port0, *port1;
	struct rzg3e_lvds *companion_lvds;
	struct device *dev = lvds->dev;
	int dual_link;
	int ret = 0;

	/* Locate the companion LVDS encoder for dual-link operation, if any. */
	companion = of_parse_phandle(dev->of_node, "renesas,companion", 0);
	if (!companion)
		return 0;

	/*
	 * Sanity check: the companion encoder must have the same compatible
	 * string.
	 */
	match = of_match_device(dev->driver->of_match_table, dev);
	if (!of_device_is_compatible(companion, match->compatible)) {
		ret = dev_err_probe(dev, -ENXIO, "Companion LVDS encoder is invalid\n");
		goto done;
	}

	/*
	 * We need to work out if the sink is expecting us to function in
	 * dual-link mode. We do this by looking at the DT port nodes we are
	 * connected to, if they are marked as expecting even pixels and
	 * odd pixels than we need to enable vertical stripe output.
	 */
	port0 = of_graph_get_port_by_id(dev->of_node, 1);
	port1 = of_graph_get_port_by_id(companion, 1);
	dual_link = drm_of_lvds_get_dual_link_pixel_order(port0, port1);
	of_node_put(port0);
	of_node_put(port1);

	switch (dual_link) {
	case DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS:
		lvds->link_type = RZG3E_LVDS_DUAL_LINK_ODD_EVEN_PIXELS;
		break;
	case DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS:
		lvds->link_type = RZG3E_LVDS_DUAL_LINK_EVEN_ODD_PIXELS;
		break;
	default:
		/*
		 * Early dual-link bridge specific implementations populate the
		 * timings field of drm_bridge. If the flag is set, we assume
		 * that we are expected to generate even pixels from the primary
		 * encoder, and odd pixels from the companion encoder.
		 */
		if (lvds->next_bridge->timings &&
		    lvds->next_bridge->timings->dual_link)
			lvds->link_type = RZG3E_LVDS_DUAL_LINK_EVEN_ODD_PIXELS;
	}

	lvds->mode = LVDS_DUAL_LINK;
	lvds->companion = of_drm_find_bridge(companion);
	if (!lvds->companion) {
		ret = -EPROBE_DEFER;
		goto done;
	}

	if (lvds->link_type == RZG3E_LVDS_DUAL_LINK_ODD_EVEN_PIXELS)
		dev_dbg(dev, "Data swapping required\n");

	/*
	 * FIXME: We should not be messing with the companion encoder private
	 * data from the primary encoder, we should rather let the companion
	 * encoder work things out on its own. However, the companion encoder
	 * doesn't hold a reference to the primary encoder, and
	 * drm_of_lvds_get_dual_link_pixel_order needs to be given references
	 * to the output ports of both encoders, therefore leave it like this
	 * for the time being.
	 */
	companion_lvds = bridge_to_rzg3e_lvds(lvds->companion);
	companion_lvds->link_type = lvds->link_type;
	companion_lvds->mode = LVDS_DUAL_LINK;

done:
	of_node_put(companion);

	return ret;
}

static void rzg3e_lvds_parse_dt_channel(struct rzg3e_lvds *lvds,
					struct device_node *node,
					unsigned int ch)
{
	struct device_node *ep_in, *ep_out, *remote, *parent_node;
	const char **in_name;
	bool *out_enabled;

	if (ch == 0) {
		in_name = &lvds->in0_name;
		out_enabled = &lvds->out0_enabled;
	} else {
		in_name = &lvds->in1_name;
		out_enabled = &lvds->out1_enabled;
	}

	if (!of_device_is_available(node)) {
		*out_enabled = false;
		return;
	}

	/* Get input endpoint and trace back to DU node */
	ep_in = of_graph_get_endpoint_by_regs(node, 0, -1);
	if (ep_in) {
		remote = of_graph_get_remote_endpoint(ep_in);
		if (remote) {
			parent_node = of_get_parent(remote);
			parent_node = of_get_parent(parent_node);
			parent_node = of_get_parent(parent_node);
			*in_name = of_node_full_name(parent_node);
			of_node_put(parent_node);
			of_node_put(remote);
		}
		of_node_put(ep_in);
	}

	/* Check output endpoint availability */
	ep_out = of_graph_get_endpoint_by_regs(node, 1, -1);
	*out_enabled = of_device_is_available(ep_out);
	of_node_put(ep_out);
}

static int rzg3e_lvds_parse_dt(struct rzg3e_lvds *lvds)
{
	struct device *dev = lvds->dev;
	struct device_node *parent = dev->parent->of_node;
	struct device_node *child;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0,
					  &lvds->panel, &lvds->next_bridge);
	if (ret)
		return -EPROBE_DEFER;

	if (lvds->panel) {
		lvds->next_bridge = devm_drm_panel_bridge_add(dev, lvds->panel);
		if (IS_ERR_OR_NULL(lvds->next_bridge))
			return -EINVAL;
	}

	lvds->ch = rzg3e_lvds_get_channel_by_node(dev->of_node);
	if (lvds->ch < 0)
		return dev_err_probe(dev, -EINVAL,
				     "invalid channel: %u\n", lvds->ch);

	/* Check in/out port connections for all LVDS channels */
	for_each_child_of_node(parent, child) {
		int ch;

		ch = rzg3e_lvds_get_channel_by_node(child);
		if (ch < 0) {
			of_node_put(child);
			return dev_err_probe(dev, -EINVAL,
					     "invalid channel: %d\n", ch);
		}

		if (ch < MAX_LVDS_CHAN_NUM)
			rzg3e_lvds_parse_dt_channel(lvds, child, ch);
	}

	dev_dbg(dev, "ch: %d, out0_enabled: %d, out1_enabled: %d\n",
		lvds->ch, lvds->out0_enabled, lvds->out1_enabled);
	dev_dbg(dev, "in0_node_name: %s\n", lvds->in0_name);
	dev_dbg(dev, "in1_node_name: %s\n", lvds->in1_name);

	if (lvds->out0_enabled)
		lvds->mode = LVDS_SINGLE_LINK_CH0;
	else if (lvds->out1_enabled)
		lvds->mode = LVDS_SINGLE_LINK_CH1;

	return rzg3e_lvds_parse_dt_companion(lvds);
}

static int rzg3e_lvds_get_clocks(struct rzg3e_lvds *lvds)
{
	lvds->pclk = devm_clk_get(lvds->dev, "pclk");
	if (IS_ERR(lvds->pclk))
		return PTR_ERR(lvds->pclk);

	lvds->phyclk = devm_clk_get(lvds->dev, "phyclk");
	if (IS_ERR(lvds->phyclk))
		return PTR_ERR(lvds->phyclk);

	lvds->dotclk = devm_clk_get(lvds->dev, "dotclk");
	if (IS_ERR(lvds->dotclk))
		return PTR_ERR(lvds->dotclk);

	return 0;
}

static int rzg3e_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg3e_lvds *lvds;
	int ret;

	lvds = devm_drm_bridge_alloc(&pdev->dev, struct rzg3e_lvds, bridge,
				     &rzg3e_lvds_bridge_ops);
	if (IS_ERR(lvds))
		return PTR_ERR(lvds);

	lvds->dev = &pdev->dev;
	lvds->bridge.funcs = &rzg3e_lvds_bridge_ops;
	lvds->bridge.of_node = pdev->dev.of_node;

	ret = rzg3e_lvds_get_clocks(lvds);
	if (ret < 0)
		return ret;

	lvds->regmap = syscon_node_to_regmap(dev->of_node->parent);
	if (IS_ERR(lvds->regmap))
		return PTR_ERR(lvds->regmap);

	lvds->rstc = devm_reset_control_get_shared(lvds->dev, NULL);
	if (IS_ERR(lvds->rstc))
		return dev_err_probe(lvds->dev, PTR_ERR(lvds->rstc),
				     "failed to get rst\n");

	ret = rzg3e_lvds_parse_dt(lvds);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, lvds);
	devm_drm_bridge_add(dev, &lvds->bridge);

	return 0;
}

static const struct of_device_id rzg3e_lvds_of_table[] = {
	{ .compatible = "renesas,r9a09g047-lvds" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, rzg3e_lvds_of_table);

static struct platform_driver rzg3e_lvds_platform_driver = {
	.probe		= rzg3e_lvds_probe,
	.driver		= {
		.name	= "rzg3e-lvds",
		.of_match_table = rzg3e_lvds_of_table,
	},
};

module_platform_driver(rzg3e_lvds_platform_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_AUTHOR("Tommaso Merciai <tommaso.merciai.xr@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G3E LVDS Encoder Driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_rgb_dummy.c  --  R-Car RGB Dummy Encoder
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

struct rcar_rgb_dummy {
	struct device *dev;

	struct drm_bridge bridge;

	struct drm_connector connector;
	struct drm_panel *panel;
};

#define bridge_to_rcar_rgb_dummy(b) \
	container_of(b, struct rcar_rgb_dummy, bridge)

#define connector_to_rcar_rgb_dummy(c) \
	container_of(c, struct rcar_rgb_dummy, connector)

/* -----------------------------------------------------------------------------
 * Connector & Panel
 */

static int rcar_rgb_dummy_connector_get_modes(struct drm_connector *connector)
{
	struct rcar_rgb_dummy *rgb = connector_to_rcar_rgb_dummy(connector);

	return drm_panel_get_modes(rgb->panel);
}

static int
rcar_rgb_dummy_connector_atomic_check(struct drm_connector *connector,
				      struct drm_connector_state *state)
{
	struct rcar_rgb_dummy *rgb = connector_to_rcar_rgb_dummy(connector);
	const struct drm_display_mode *panel_mode;
	struct drm_crtc_state *crtc_state;

	if (!state->crtc)
		return 0;

	if (list_empty(&connector->modes)) {
		dev_dbg(rgb->dev, "connector: empty modes list\n");
		return -EINVAL;
	}

	panel_mode = list_first_entry(&connector->modes,
				      struct drm_display_mode, head);

	/* We're not allowed to modify the resolution */
	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if ((crtc_state->mode.hdisplay != panel_mode->hdisplay) ||
	    (crtc_state->mode.vdisplay != panel_mode->vdisplay))
		return -EINVAL;

	/* The flat panel mode is fixed, just copy it to the adjusted mode */
	drm_mode_copy(&crtc_state->adjusted_mode, panel_mode);

	return 0;
}

static const struct
drm_connector_helper_funcs rcar_rgb_dummy_conn_helper_funcs = {
	.get_modes = rcar_rgb_dummy_connector_get_modes,
	.atomic_check = rcar_rgb_dummy_connector_atomic_check,
};

static const struct drm_connector_funcs rcar_rgb_dummy_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int rcar_rgb_dummy_attach(struct drm_bridge *bridge)
{
	struct rcar_rgb_dummy *rgb = bridge_to_rcar_rgb_dummy(bridge);
	struct drm_connector *connector = &rgb->connector;
	struct drm_encoder *encoder = bridge->encoder;
	int ret;

	ret = drm_connector_init(bridge->dev, connector,
				 &rcar_rgb_dummy_conn_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &rcar_rgb_dummy_conn_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	return drm_panel_attach(rgb->panel, connector);
}

static void rcar_rgb_dummy_detach(struct drm_bridge *bridge)
{
	struct rcar_rgb_dummy *rgb = bridge_to_rcar_rgb_dummy(bridge);

	if (rgb->panel)
		drm_panel_detach(rgb->panel);
}

static void rcar_rgb_dummy_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_state *state)
{
	struct rcar_rgb_dummy *rgb = bridge_to_rcar_rgb_dummy(bridge);

	drm_panel_prepare(rgb->panel);
	drm_panel_enable(rgb->panel);
}

static void rcar_rgb_dummy_atomic_disable(struct drm_bridge *bridge,
				     struct drm_atomic_state *state)
{
	struct rcar_rgb_dummy *rgb = bridge_to_rcar_rgb_dummy(bridge);

	drm_panel_disable(rgb->panel);
	drm_panel_unprepare(rgb->panel);
}

static const struct drm_bridge_funcs rcar_rgb_dummy_bridge_ops = {
	.attach = rcar_rgb_dummy_attach,
	.detach = rcar_rgb_dummy_detach,
	.atomic_enable = rcar_rgb_dummy_atomic_enable,
	.atomic_disable = rcar_rgb_dummy_atomic_disable,
};

static int rcar_rgb_dummy_parse_dt(struct rcar_rgb_dummy *rgb)
{
	int ret;
	struct drm_bridge *bridge;

	ret = drm_of_find_panel_or_bridge(rgb->dev->of_node, 1, 0,
					  &rgb->panel, &bridge);
	if (bridge) {
		dev_err(rgb->dev,
		"No need to use RGB Dummy Encoder if we already have a bridge");
		return -EINVAL;
	}

	return ret;
}

static int rcar_rgb_dummy_probe(struct platform_device *pdev)
{
	struct rcar_rgb_dummy *rgb;
	int ret;

	rgb = devm_kzalloc(&pdev->dev, sizeof(*rgb), GFP_KERNEL);
	if (rgb == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, rgb);

	rgb->dev = &pdev->dev;

	ret = rcar_rgb_dummy_parse_dt(rgb);
	if (ret < 0)
		return ret;

	rgb->bridge.driver_private = rgb;
	rgb->bridge.funcs = &rcar_rgb_dummy_bridge_ops;
	rgb->bridge.of_node = pdev->dev.of_node;

	drm_bridge_add(&rgb->bridge);

	dev_info(&pdev->dev, "Registered RGB Dummy Encoder");

	return ret;
}

static int rcar_rgb_dummy_remove(struct platform_device *pdev)
{
	struct rcar_rgb_dummy *rgb = platform_get_drvdata(pdev);

	drm_bridge_remove(&rgb->bridge);

	return 0;
}

static const struct of_device_id rcar_rgb_dummy_of_table[] = {
	{ .compatible = "renesas,rcar-rgb-dummy", },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_rgb_dummy_of_table);

static struct platform_driver rcar_rgb_dummy_platform_driver = {
	.probe	= rcar_rgb_dummy_probe,
	.remove	= rcar_rgb_dummy_remove,
	.driver	= {
		.name   = "rcar-rgb-dummy",
		.of_match_table = rcar_rgb_dummy_of_table,
	},
};

module_platform_driver(rcar_rgb_dummy_platform_driver);

MODULE_AUTHOR("Hien Huynh <hien.huynh.px@renesas.com>");
MODULE_DESCRIPTION("Renesas R-Car RGB Dummy Encoder Driver");
MODULE_LICENSE("GPL");

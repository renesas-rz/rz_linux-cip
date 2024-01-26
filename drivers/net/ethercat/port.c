// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) Renesas Electronics Corp.
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/if_bridge.h>
#include <linux/notifier.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/rtnetlink.h>

#include "esc_device.h"

static struct device_type esc_type = {
	.name   = "esc",
};

static int esc_port_dev_phy_connect(struct net_device *port_dev, int addr)
{
	struct esc_port *ep = esc_port_dev_to_port(port_dev);
	struct esc_device *ed = ep->ed;

	port_dev->phydev = mdiobus_get_phy(ed->port_dev_mii_bus, addr);
	if (!port_dev->phydev) {
		netdev_err(port_dev, "no phy at %d\n", addr);
		return -ENODEV;
	}

	return phylink_connect_phy(ep->pl, port_dev->phydev);
}

int esc_port_phy_setup(struct esc_port *ep)
{
	struct device_node *port_dn = ep->dn;
	struct esc_device *ed = ep->ed;
	phy_interface_t mode;
	u32 phy_flags = 0;
	int ret;

	ret = of_get_phy_mode(port_dn, &mode);
	if (ret)
		mode = PHY_INTERFACE_MODE_NA;

	SET_NETDEV_DEVTYPE(ep->port_dev, &esc_type);
	SET_NETDEV_DEV(ep->port_dev, ep->ed->dev);

	ep->pl_config.dev = &ep->port_dev->dev;
	ep->pl_config.type = PHYLINK_NETDEV;

	ep->pl = phylink_create(&ep->pl_config, of_fwnode_handle(port_dn), mode,
				&esc_port_phylink_mac_ops);
	if (IS_ERR(ep->pl)) {
		netdev_err(ep->port_dev,
			   "error creating PHYLINK: %ld\n", PTR_ERR(ep->pl));
		return PTR_ERR(ep->pl);
	}

	if (ed->ops->get_phy_flags)
		phy_flags = ed->ops->get_phy_flags(ed, ep->index);

	ret = phylink_of_phy_connect(ep->pl, port_dn, phy_flags);
	if (ret == -ENODEV && ed->port_dev_mii_bus) {
		/* We could not connect to a designated PHY or SFP, so try to
		 * use the switch internal MDIO bus instead
		 */
		ret = esc_port_dev_phy_connect(ep->port_dev, ep->index);
	}

	if (ret) {
		netdev_err(ep->port_dev, "failed to connect to PHY: %pe\n",
			   ERR_PTR(ret));
		phylink_destroy(ep->pl);
	}

	return ret;
}

void esc_port_phy_remove(struct esc_port *ep)
{
	rtnl_lock();
	phylink_disconnect_phy(ep->pl);
	rtnl_unlock();
	phylink_destroy(ep->pl);
}

int esc_port_enable_rt(struct esc_port *ep, struct phy_device *phy)
{
	struct esc_device *ed = ep->ed;
	int port = ep->index;
	int err;

	if (ed->ops->port_enable) {
		err = ed->ops->port_enable(ed, port, phy);
		if (err)
			return err;
	}

	if (ep->pl)
		phylink_start(ep->pl);

	return 0;
}

int esc_port_enable(struct esc_port *ep, struct phy_device *phy)
{
	int err;

	rtnl_lock();
	err = esc_port_enable_rt(ep, phy);
	rtnl_unlock();

	return err;
}

void esc_port_disable_rt(struct esc_port *ep)
{
	struct esc_device *ed = ep->ed;
	int port = ep->index;

	if (ep->pl)
		phylink_stop(ep->pl);

	if (ed->ops->port_disable)
		ed->ops->port_disable(ed, port);
}

void esc_port_disable(struct esc_port *ep)
{
	rtnl_lock();
	esc_port_disable_rt(ep);
	rtnl_unlock();
}

static struct phy_device *esc_port_get_phy_device(struct esc_port *ep)
{
	struct device_node *phy_dn;
	struct phy_device *phydev;

	phy_dn = of_parse_phandle(ep->dn, "phy-handle", 0);
	if (!phy_dn)
		return NULL;

	phydev = of_phy_find_device(phy_dn);
	if (!phydev) {
		of_node_put(phy_dn);
		return ERR_PTR(-EPROBE_DEFER);
	}

	of_node_put(phy_dn);
	return phydev;
}

static void esc_port_phylink_validate(struct phylink_config *config,
				      unsigned long *supported,
				      struct phylink_link_state *state)
{
	struct esc_port *ep = container_of(config, struct esc_port, pl_config);
	struct esc_device *ed = ep->ed;

	if (!ed->ops->phylink_validate)
		return;

	ed->ops->phylink_validate(ed, ep->index, supported, state);
}

static void esc_port_phylink_mac_pcs_get_state(struct phylink_config *config,
					       struct phylink_link_state *state)
{
	struct esc_port *ep = container_of(config, struct esc_port, pl_config);
	struct esc_device *ed = ep->ed;
	int err;

	/* Only called for inband modes */
	if (!ed->ops->phylink_mac_link_state) {
		state->link = 0;
		return;
	}

	err = ed->ops->phylink_mac_link_state(ed, ep->index, state);
	if (err < 0) {
		dev_err(ed->dev, "p%d: phylink_mac_link_state() failed: %d\n",
			ep->index, err);
		state->link = 0;
	}
}

static void esc_port_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
	struct esc_port *ep = container_of(config, struct esc_port, pl_config);
	struct esc_device *ed = ep->ed;

	if (!ed->ops->phylink_mac_config)
		return;

	ed->ops->phylink_mac_config(ed, ep->index, mode, state);
}

static void esc_port_phylink_mac_an_restart(struct phylink_config *config)
{
	struct esc_port *ep = container_of(config, struct esc_port, pl_config);
	struct esc_device *ed = ep->ed;

	if (!ed->ops->phylink_mac_an_restart)
		return;

	ed->ops->phylink_mac_an_restart(ed, ep->index);
}

static void esc_port_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
	struct esc_port *ep = container_of(config, struct esc_port, pl_config);
	struct phy_device *phydev = NULL;
	struct esc_device *ed = ep->ed;

	phydev = ep->port_dev->phydev;
	/* Connect phydev to port first, when reigster mdio */

	if (!ed->ops->phylink_mac_link_down) {
		if (ed->ops->adjust_link && phydev)
			ed->ops->adjust_link(ed, ep->index, phydev);
		return;
	}

	ed->ops->phylink_mac_link_down(ed, ep->index, mode, interface);
}

static void esc_port_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
	struct esc_port *ep = container_of(config, struct esc_port, pl_config);
	struct esc_device *ed = ep->ed;

	if (!ed->ops->phylink_mac_link_up) {
		if (ed->ops->adjust_link && phydev)
			ed->ops->adjust_link(ed, ep->index, phydev);
		return;
	}

	ed->ops->phylink_mac_link_up(ed, ep->index, mode, interface, phydev,
				     speed, duplex, tx_pause, rx_pause);
}

const struct phylink_mac_ops esc_port_phylink_mac_ops = {
	.validate = esc_port_phylink_validate,
	.mac_pcs_get_state = esc_port_phylink_mac_pcs_get_state,
	.mac_config = esc_port_phylink_mac_config,
	.mac_an_restart = esc_port_phylink_mac_an_restart,
	.mac_link_down = esc_port_phylink_mac_link_down,
	.mac_link_up = esc_port_phylink_mac_link_up,
};

int esc_port_setup_phy_of(struct esc_port *ep, bool enable)
{
	struct esc_device *ed = ep->ed;
	struct phy_device *phydev;
	int port = ep->index;
	int err = 0;

	phydev = esc_port_get_phy_device(ep);
	if (!phydev)
		return 0;

	if (IS_ERR(phydev))
		return PTR_ERR(phydev);

	if (enable) {
		err = genphy_resume(phydev);
		if (err < 0)
			goto err_put_dev;

		err = genphy_read_status(phydev);
		if (err < 0)
			goto err_put_dev;
	} else {
		err = genphy_suspend(phydev);
		if (err < 0)
			goto err_put_dev;
	}

	if (ed->ops->adjust_link)
		ed->ops->adjust_link(ed, port, phydev);

	dev_dbg(ed->dev, "enabled port's phy: %s", phydev_name(phydev));

err_put_dev:
	put_device(&phydev->mdio.dev);
	return err;
}

int esc_port_get_phy_strings(struct esc_port *ep, uint8_t *data)
{
	struct phy_device *phydev;
	int ret = -EOPNOTSUPP;

	phydev = esc_port_get_phy_device(ep);
	if (IS_ERR_OR_NULL(phydev))
		return ret;

	ret = phy_ethtool_get_strings(phydev, data);
	put_device(&phydev->mdio.dev);

	return ret;
}
EXPORT_SYMBOL_GPL(esc_port_get_phy_strings);

int esc_port_get_ethtool_phy_stats(struct esc_port *ep, uint64_t *data)
{
	struct phy_device *phydev;
	int ret = -EOPNOTSUPP;

	phydev = esc_port_get_phy_device(ep);
	if (IS_ERR_OR_NULL(phydev))
		return ret;

	ret = phy_ethtool_get_stats(phydev, NULL, data);
	put_device(&phydev->mdio.dev);

	return ret;
}
EXPORT_SYMBOL_GPL(esc_port_get_ethtool_phy_stats);

int esc_port_get_phy_sset_count(struct esc_port *ep)
{
	struct phy_device *phydev;
	int ret = -EOPNOTSUPP;

	phydev = esc_port_get_phy_device(ep);
	if (IS_ERR_OR_NULL(phydev))
		return ret;

	ret = phy_ethtool_get_sset_count(phydev);
	put_device(&phydev->mdio.dev);

	return ret;
}
EXPORT_SYMBOL_GPL(esc_port_get_phy_sset_count);

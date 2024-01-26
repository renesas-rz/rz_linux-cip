// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) Renesas Electronics Corp.
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <net/devlink.h>

#include "esc_device.h"

static DEFINE_MUTEX(esc_mutex);

static int esc_port_setup(struct esc_port *ep)
{
	int err = 0;

	ep->mac = of_get_mac_address(ep->dn);

	/*  Create dummy network device for realize port number when probe
	 *  and have instance binding with PHY
	 */
	ep->port_dev = alloc_netdev_mqs(sizeof(struct esc_port_dev_priv),
					ep->name, NET_NAME_UNKNOWN,
					ether_setup, 1, 1);
	if (!ep->port_dev)
		return -ENOMEM;

	err = esc_port_phy_setup(ep);
	if (err) {
		netdev_err(ep->port_dev,
			   "error %d setting up PHY for esc, port %d\n",
			   err, ep->index);
		goto err_port_dev;
	}

	err = esc_port_setup_phy_of(ep, true);
	if (err)
		goto err_phy_remove;

	err = esc_port_enable(ep, NULL);
	if (err)
		goto err_phy_of;

	return 0;

err_phy_of:
	esc_port_setup_phy_of(ep, false);
err_phy_remove:
	esc_port_phy_remove(ep);
err_port_dev:
	ep->port_dev = NULL;

	return err;
}

static void esc_port_teardown(struct esc_port *ep)
{
	esc_port_disable(ep);
	esc_port_setup_phy_of(ep, false);
	esc_port_phy_remove(ep);
	ep->port_dev = NULL;
}

static int esc_setup_ports(struct esc_device *ed)
{
	struct esc_port *ep;
	int err;

	list_for_each_entry(ep, &ed->ports, list) {
		err = esc_port_setup(ep);
		if (err)
			goto teardown;
	}

	return 0;

teardown:
	list_for_each_entry(ep, &ed->ports, list)
		esc_port_teardown(ep);

	return err;
}

static void esc_teardown_ports(struct esc_device *ed)
{
	struct esc_port *ep;

	list_for_each_entry(ep, &ed->ports, list)
		esc_port_teardown(ep);
}

static int esc_setup(struct esc_device *ed)
{
	int err;

	if (ed->ops->setup) {
		err = ed->ops->setup(ed);
		if (err)
			return err;

		pr_debug("ESC: setup esc device done\n");
	}

	err = esc_setup_ports(ed);
	if (err)
		return err;

	pr_debug("ESC: list ports setup\n");

	return 0;
}

static void esc_teardown(struct esc_device *ed)
{
	esc_teardown_ports(ed);

	pr_info("ESC: list port torn down\n");
}

static struct esc_port *esc_port_touch(struct esc_device *ed, int index)
{
	struct esc_port *ep;

	list_for_each_entry(ep, &ed->ports, list)
		if (ep->ed == ed && ep->index == index)
			return ep;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return NULL;

	ep->ed = ed;
	ep->index = index;

	INIT_LIST_HEAD(&ep->list);
	list_add_tail(&ep->list, &ed->ports);

	return ep;
}

static int esc_port_parse_of_name(struct esc_port *ep, struct device_node *dn)
{
	const char *name = of_get_property(dn, "label", NULL);

	ep->dn = dn;

	if (!name)
		name = "esc%d";

	ep->name = name;

	return 0;
}

static int esc_device_parse_ports_of(struct esc_device *ed,
				     struct device_node *dn)
{
	struct device_node *ports, *port;
	struct esc_port *ep;
	int err = 0;
	u32 reg;

	ports = of_get_child_by_name(dn, "ports");
	if (!ports) {
		/* The second possibility is "ethernet-ports" */
		ports = of_get_child_by_name(dn, "ethernet-ports");
		if (!ports) {
			dev_err(ed->dev, "no ports child node found\n");
			return -EINVAL;
		}
	}

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err)
			return -EINVAL;

		if (reg >= ed->max_num_ports)
			return -EINVAL;

		ep = esc_port_touch(ed, reg);
		if (!ep)
			return -ENOMEM;

		err = esc_port_parse_of_name(ep, port);
		if (err)
			return -EINVAL;
	}

	return 0;
}

static int esc_device_parse_of(struct esc_device *ed, struct device_node *dn)
{
	INIT_LIST_HEAD(&ed->ports);

	return esc_device_parse_ports_of(ed, dn);
}

static void esc_device_release_ports(struct esc_device *ed)
{
	struct esc_port *ep, *next;

	list_for_each_entry_safe(ep, next, &ed->ports, list) {
		if (ep->ed != ed)
			continue;
		list_del(&ep->list);
		kfree(ep);
	}
}

static int esc_device_probe(struct esc_device *ed)
{
	struct device_node *np;
	int err;

	if (!ed->dev)
		return -ENODEV;

	np = ed->dev->of_node;

	if (!ed->max_num_ports)
		return -EINVAL;

	if (np) {
		err = esc_device_parse_of(ed, np);
		if (err)
			esc_device_release_ports(ed);
	} else {
		err = -ENODEV;
	}

	if (err)
		return err;

	err = esc_setup(ed);
	if (err) {
		esc_device_release_ports(ed);
		esc_teardown(ed);
	}

	return err;
}

int esc_register(struct esc_device *ed)
{
	int err;

	mutex_lock(&esc_mutex);
	err = esc_device_probe(ed);
	mutex_unlock(&esc_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(esc_register);

static void esc_device_remove(struct esc_device *ed)
{
	esc_teardown(ed);
	esc_device_release_ports(ed);
}

void esc_unregister(struct esc_device *ed)
{
	mutex_lock(&esc_mutex);
	esc_device_remove(ed);
	mutex_unlock(&esc_mutex);
}
EXPORT_SYMBOL_GPL(esc_unregister);

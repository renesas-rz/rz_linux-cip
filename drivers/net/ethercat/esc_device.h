/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) Renesas Electronics Corp.
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/ethtool.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <net/gro_cells.h>

#ifndef _ESC_DEVICE_H_
#define _ESC_DEVICE_H_

struct phy_device;
struct phylink_link_state;
struct esc_device;

struct esc_port {
	struct esc_device	*ed;
	unsigned int		index;
	const char		*name;
	const char		*mac;
	struct device_node	*dn;
	struct net_device	*port_dev;
	struct phylink		*pl;
	struct phylink_config	pl_config;

	struct list_head list;

	void *priv;

	/* Original copy of the esc port netdev ethtool_ops */
	const struct ethtool_ops *orig_ethtool_ops;

	/* Original copy of the esc port netdev net_device_ops */
	const struct esc_netdevice_ops *netdev_ops;
};

struct esc_device {
	struct device *dev;

	/* List of esc ports */
	struct list_head ports;

	void *priv;

	/* The esc operations */
	const struct esc_device_ops	*ops;

	/* Slave mii_bus and devices for the individual ports */
	u32			phys_mii_mask;
	struct mii_bus		*port_dev_mii_bus;

	/* MAC PCS does not provide link state change interrupt, and requires
	 * polling. Flag passed on to PHYLINK.
	 */
	bool			pcs_poll;

	size_t max_num_ports;
};

struct esc_port_dev_priv {
	/* Copy of esc port xmit for faster access in port_dev
	 * transmit hot path
	 */
	struct sk_buff *        (*xmit)(struct sk_buff *skb,
					struct net_device *dev);

	struct pcpu_sw_netstats __percpu *stats64;

	struct gro_cells        gcells;

	/* ESC port data, such as esc, port index, etc. */
	struct esc_port         *ep;

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll          *netpoll;
#endif

	/* TC context */
	struct list_head        mall_tc_list;
};

static inline struct esc_port *esc_to_port(struct esc_device *ed, int p)
{
	struct esc_port *ep;

	list_for_each_entry(ep, &ed->ports, list)
		if (ep->ed == ed && ep->index == p)
			return ep;

	return NULL;
}

struct esc_device_ops {
	int	(*setup)(struct esc_device *ed);
	u32	(*get_phy_flags)(struct esc_device *ed, int port);

	/* Access to the esc's PHY registers */
	int	(*phy_read)(struct esc_device *ed, int port, int regnum);
	int	(*phy_write)(struct esc_device *ed, int port,
			     int regnum, u16 val);

	/* Link state adjustment (called from libphy) */
	void	(*adjust_link)(struct esc_device *ed, int port,
			       struct phy_device *phydev);
	/* PHYLINK integration */
	void	(*phylink_validate)(struct esc_device *ed, int port,
				    unsigned long *supported,
				    struct phylink_link_state *state);
	int	(*phylink_mac_link_state)(struct esc_device *ed, int port,
					  struct phylink_link_state *state);
	void	(*phylink_mac_config)(struct esc_device *ed, int port,
				      unsigned int mode,
				      const struct phylink_link_state *state);
	void	(*phylink_mac_an_restart)(struct esc_device *ed, int port);
	void	(*phylink_mac_link_down)(struct esc_device *ed, int port,
					 unsigned int mode,
					 phy_interface_t interface);
	void	(*phylink_mac_link_up)(struct esc_device *ed, int port,
				       unsigned int mode,
				       phy_interface_t interface,
				       struct phy_device *phydev,
				       int speed, int duplex,
				       bool tx_pause, bool rx_pause);

	/* ethtool hardware statistics  */
	void	(*get_strings)(struct esc_device *ed, int port,
			       u32 stringset, uint8_t *data);
	void	(*get_ethtool_stats)(struct esc_device *ed,
				     int port, uint64_t *data);
	int	(*get_sset_count)(struct esc_device *ed, int port, int sset);
	void	(*get_ethtool_phy_stats)(struct esc_device *ed,
					 int port, uint64_t *data);

	/* Port enable/disable */
	int	(*port_enable)(struct esc_device *ed, int port,
			       struct phy_device *phy);
	void	(*port_disable)(struct esc_device *ed, int port);

	/* Register access */
	int	(*get_regs_len)(struct esc_device *ed, int port);
	void	(*get_regs)(struct esc_device *ed, int port,
			    struct ethtool_regs *regs, void *p);
};

struct esc_skb_cb {
	struct sk_buff *clone;
};

#define ESC_SKB_CB(skb) ((struct esc_skb_cb *)((skb)->cb))

void esc_unregister(struct esc_device *ed);
int esc_register(struct esc_device *ed);
void esc_device_shutdown(struct esc_device *ed);
struct esc_device *esc_device_find(int tree_index, int sw_index);
#ifdef CONFIG_PM_SLEEP
int esc_device_suspend(struct esc_device *ed);
int esc_device_resume(struct esc_device *ed);
#else
static inline int esc_device_suspend(struct esc_device *ed)
{
	return 0;
}

static inline int esc_device_resume(struct esc_device *ed)
{
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

int esc_port_phy_setup(struct esc_port *ep);
void esc_port_phy_remove(struct esc_port *ep);
int esc_port_enable_rt(struct esc_port *ep, struct phy_device *phy);
int esc_port_enable(struct esc_port *ep, struct phy_device *phy);
void esc_port_disable_rt(struct esc_port *ep);
void esc_port_disable(struct esc_port *ep);
int esc_port_setup_phy_of(struct esc_port *ep, bool enable);

int esc_port_get_phy_strings(struct esc_port *ep, uint8_t *data);
int esc_port_get_ethtool_phy_stats(struct esc_port *ep, uint64_t *data);
int esc_port_get_phy_sset_count(struct esc_port *ep);
void esc_port_phylink_mac_change(struct esc_device *ed, int port, bool up);

extern const struct phylink_mac_ops esc_port_phylink_mac_ops;

static inline struct esc_port *esc_port_dev_to_port(const struct net_device *dev)
{
	struct esc_port_dev_priv *p = netdev_priv(dev);

	return p->ep;
}

#endif

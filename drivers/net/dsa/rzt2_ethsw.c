// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Schneider-Electric
 */

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/gpio/consumer.h>
#include <linux/reset.h>
#include <net/dsa.h>
#include <net/tc_act/tc_gate.h>
#include <net/pkt_sched.h>
#include <linux/net/renesas/rzt2-ethss.h>
#include <linux/net/renesas/rzt2_timer_hwtstamp.h>
#include <linux/stmmac_dsa_xdp.h>
#include <net/page_pool/helpers.h>

#include "rzt2_ethsw.h"

extern u32 dsa_user_xdp_run_skb(struct dsa_port *dp, struct sk_buff *skb);

struct ethsw_stats {
	u16 offset;
	const char name[ETH_GSTRING_LEN];
};

#define STAT_DESC(_offset) {	\
	.offset = ETHSW_##_offset,	\
	.name = __stringify(_offset),	\
}

#define ETHSW_A5PSW_PORT_MASK  GENMASK(3, 0)

static const struct ethsw_stats ethsw_stats[] = {
	STAT_DESC(aFramesTransmittedOK),
	STAT_DESC(aFramesReceivedOK),
	STAT_DESC(aFrameCheckSequenceErrors),
	STAT_DESC(aAlignmentErrors),
	STAT_DESC(aOctetsTransmittedOK),
	STAT_DESC(aOctetsReceivedOK),
	STAT_DESC(aTxPAUSEMACCtrlFrames),
	STAT_DESC(aRxPAUSEMACCtrlFrames),
	STAT_DESC(ifInErrors),
	STAT_DESC(ifOutErrors),
	STAT_DESC(ifInUcastPkts),
	STAT_DESC(ifInMulticastPkts),
	STAT_DESC(ifInBroadcastPkts),
	STAT_DESC(ifOutDiscards),
	STAT_DESC(ifOutUcastPkts),
	STAT_DESC(ifOutMulticastPkts),
	STAT_DESC(ifOutBroadcastPkts),
	STAT_DESC(etherStatsDropEvents),
	STAT_DESC(etherStatsOctets),
	STAT_DESC(etherStatsPkts),
	STAT_DESC(etherStatsUndersizePkts),
	STAT_DESC(etherStatsOversizePkts),
	STAT_DESC(etherStatsPkts64Octets),
	STAT_DESC(etherStatsPkts65to127Octets),
	STAT_DESC(etherStatsPkts128to255Octets),
	STAT_DESC(etherStatsPkts256to511Octets),
	STAT_DESC(etherStatsPkts1024to1518Octets),
	STAT_DESC(etherStatsPkts1519toXOctets),
	STAT_DESC(etherStatsJabbers),
	STAT_DESC(etherStatsFragments),
	STAT_DESC(VLANReceived),
	STAT_DESC(VLANTransmitted),
	STAT_DESC(aDeferred),
	STAT_DESC(aMultipleCollisions),
	STAT_DESC(aSingleCollisions),
	STAT_DESC(aLateCollisions),
	STAT_DESC(aExcessiveCollisions),
	STAT_DESC(aCarrierSenseErrors),
};

static void ethsw_reg_writel(struct ethsw *ethsw, int offset, u32 value)
{
	writel(value, ethsw->base + offset);
}

static u32 ethsw_reg_readl(struct ethsw *ethsw, int offset)
{
	return readl(ethsw->base + offset);
}

static void ethsw_reg_rmw(struct ethsw *ethsw, int offset, u32 mask, u32 val)
{
	u32 reg;

	spin_lock(&ethsw->reg_lock);

	reg = ethsw_reg_readl(ethsw, offset);
	reg &= ~mask;
	reg |= val;
	ethsw_reg_writel(ethsw, offset, reg);

	spin_unlock(&ethsw->reg_lock);
}

static void ethss_reg_writel(struct ethsw *ethsw, int offset, u32 value)
{
	writel(value, ethsw->ethss->base + offset);
}

static u32 ethss_reg_readl(struct ethsw *ethsw, int offset)
{
	return readl(ethsw->ethss->base + offset);
}

static enum dsa_tag_protocol ethsw_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_RZT2_ETHSW;
}

static void ethsw_port_pattern_set(struct ethsw *ethsw, int port, int pattern,
				   bool enable)
{
	u32 rx_match = 0;

	if (enable)
		rx_match |= ETHSW_RXMATCH_CONFIG_PATTERN(pattern);

	ethsw_reg_rmw(ethsw, ETHSW_RXMATCH_CONFIG(port),
		      ETHSW_RXMATCH_CONFIG_PATTERN(pattern), rx_match);
}

static void ethsw_port_mgmtfwd_set(struct ethsw *ethsw, int port, bool enable)
{
	/* Enable "management forward" pattern matching, this will forward
	 * packets from this port only towards the management port and thus
	 * isolate the port.
	 */
	ethsw_port_pattern_set(ethsw, port, ETHSW_PATTERN_MGMTFWD, enable);
}

static void ethsw_port_tx_enable(struct ethsw *ethsw, int port, bool enable)
{
	u32 mask = ETHSW_PORT_ENA_TX(port);
	u32 reg = enable ? mask : 0;

	/* Even though the port TX is disabled through TXENA bit in the
	 * PORT_ENA register, it can still send BPDUs. This depends on the tag
	 * configuration added when sending packets from the CPU port to the
	 * switch port. Indeed, when using forced forwarding without filtering,
	 * even disabled ports will be able to send packets that are tagged.
	 * This allows to implement STP support when ports are in a state where
	 * forwarding traffic should be stopped but BPDUs should still be sent.
	 */
	ethsw_reg_rmw(ethsw, ETHSW_PORT_ENA, mask, reg);
}

static void ethsw_port_enable_set(struct ethsw *ethsw, int port, bool enable)
{
	u32 port_ena = 0;

	if (enable)
		port_ena |= ETHSW_PORT_ENA_TX_RX(port);

	ethsw_reg_rmw(ethsw, ETHSW_PORT_ENA, ETHSW_PORT_ENA_TX_RX(port),
		      port_ena);
}

static int ethsw_lk_execute_ctrl(struct ethsw *ethsw, u32 *ctrl)
{
	int ret;

	ethsw_reg_writel(ethsw, ETHSW_LK_ADDR_CTRL, *ctrl);

	ret = readl_poll_timeout(ethsw->base + ETHSW_LK_ADDR_CTRL, *ctrl,
				 !(*ctrl & ETHSW_LK_ADDR_CTRL_BUSY),
				 ETHSW_LK_BUSY_USEC_POLL, ETHSW_CTRL_TIMEOUT);
	if (ret)
		dev_err(ethsw->dev, "LK_CTRL timeout waiting for BUSY bit\n");

	return ret;
}

static void ethsw_port_fdb_flush(struct ethsw *ethsw, int port)
{
	u32 ctrl = ETHSW_LK_ADDR_CTRL_DELETE_PORT | BIT(port);

	mutex_lock(&ethsw->lk_lock);
	ethsw_lk_execute_ctrl(ethsw, &ctrl);
	mutex_unlock(&ethsw->lk_lock);
}

static void ethsw_port_authorize_set(struct ethsw *ethsw, int port,
				     bool authorize)
{
	u32 reg = ethsw_reg_readl(ethsw, ETHSW_AUTH_PORT(port));

	if (authorize)
		reg |= ETHSW_AUTH_PORT_AUTHORIZED;
	else
		reg &= ~ETHSW_AUTH_PORT_AUTHORIZED;

	ethsw_reg_writel(ethsw, ETHSW_AUTH_PORT(port), reg);
}

static void ethsw_port_disable(struct dsa_switch *ds, int port)
{
	struct ethsw *ethsw = ds->priv;

	ethsw_port_authorize_set(ethsw, port, false);
	ethsw_port_enable_set(ethsw, port, false);
}

static int ethsw_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct ethsw *ethsw = ds->priv;

	ethsw_port_authorize_set(ethsw, port, true);
	ethsw_port_enable_set(ethsw, port, true);

	return 0;
}

static int ethsw_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct ethsw *ethsw = ds->priv;

	new_mtu += ETH_HLEN + ETHSW_EXTRA_MTU_LEN + ETH_FCS_LEN;
	ethsw_reg_writel(ethsw, ETHSW_FRM_LENGTH(port), new_mtu);

	return 0;
}

static int ethsw_port_max_mtu(struct dsa_switch *ds, int port)
{
	return ETHSW_MAX_MTU;
}

static void ethsw_phylink_get_caps(struct dsa_switch *ds, int port,
					struct phylink_config *config)
{
	unsigned long *intf = config->supported_interfaces;

	config->mac_capabilities = MAC_1000FD;

	if (dsa_is_cpu_port(ds, port)) {
		/* GMII is used internally and GMAC0 is connected to the switch
		 * using 1000Mbps Full-Duplex mode only (cf ethernet manual)
		 */
		__set_bit(PHY_INTERFACE_MODE_GMII, intf);
	} else {
		config->mac_capabilities |= MAC_100 | MAC_10;
		phy_interface_set_rgmii(intf);
		__set_bit(PHY_INTERFACE_MODE_RMII, intf);
		__set_bit(PHY_INTERFACE_MODE_MII, intf);
	}
}

static struct phylink_pcs *
ethsw_phylink_mac_select_pcs(struct phylink_config *config,
				phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct ethsw *ethsw = dp->ds->priv;

	if (dsa_port_is_cpu(dp))
		return NULL;

	return ethsw->pcs[dp->index];
}

static void ethsw_phylink_mac_config(struct phylink_config *config,
				     unsigned int mode,
				     const struct phylink_link_state *state)
{
}

static void ethsw_phylink_mac_link_down(struct phylink_config *config,
					unsigned int mode,
					phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct ethsw *ethsw = dp->ds->priv;
	int port = dp->index;
	u32 cmd_cfg;

	cmd_cfg = ethsw_reg_readl(ethsw, ETHSW_CMD_CFG(port));
	cmd_cfg &= ~(ETHSW_CMD_CFG_RX_ENA | ETHSW_CMD_CFG_TX_ENA);
	ethsw_reg_writel(ethsw, ETHSW_CMD_CFG(port), cmd_cfg);
}

static void ethsw_phylink_mac_link_up(struct phylink_config *config,
				      struct phy_device *phydev,
				      unsigned int mode,
				      phy_interface_t interface,
				      int speed, int duplex, bool tx_pause,
				      bool rx_pause)
{
	u32 cmd_cfg = ETHSW_CMD_CFG_RX_ENA | ETHSW_CMD_CFG_TX_ENA |
		      ETHSW_CMD_CFG_TX_CRC_APPEND;
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct ethsw *ethsw = dp->ds->priv;
	struct phylink_pcs *pcs = NULL;
	struct ethss_port *ethss_port = NULL;

	if (dp->index < ARRAY_SIZE(ethsw->pcs))
		pcs = ethsw->pcs[dp->index];

	if (pcs)
		ethss_port = phylink_pcs_to_ethss_port(pcs);

	if (ethss_port)
		ethss_port->speed = speed;

	if (pcs && dp->index != ETHSW_CPU_PORT &&
	    mode != MLO_AN_FIXED &&
	    interface != PHY_INTERFACE_MODE_INTERNAL)
		ethss_switchcore_adjust(pcs, duplex, speed);

	if (speed == SPEED_1000)
		cmd_cfg |= ETHSW_CMD_CFG_ETH_SPEED;

	if (duplex == DUPLEX_HALF)
		cmd_cfg |= ETHSW_CMD_CFG_HD_ENA;

	cmd_cfg |= ETHSW_CMD_CFG_CNTL_FRM_ENA;

	if (!rx_pause)
		cmd_cfg &= ~ETHSW_CMD_CFG_PAUSE_IGNORE;

	if (ethsw->ethsw_ptp_timer)
		cmd_cfg |= ETHSW_CMD_CFG_TIMER_SEL;

	ethsw_reg_writel(ethsw, ETHSW_CMD_CFG(dp->index), cmd_cfg);
}

static int ethsw_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct ethsw *ethsw = ds->priv;
	unsigned long rate;
	u64 max, tmp;
	u32 agetime;

	rate = clk_get_rate(ethsw->clk);
	max = div64_ul(((u64)ETHSW_LK_AGETIME_MASK * ETHSW_TABLE_ENTRIES * 1024),
		       rate) * 1000;
	if (msecs > max)
		return -EINVAL;

	tmp = div_u64(rate, MSEC_PER_SEC);
	agetime = div_u64(msecs * tmp, 1024 * ETHSW_TABLE_ENTRIES);

	ethsw_reg_writel(ethsw, ETHSW_LK_AGETIME, agetime);

	return 0;
}

static void ethsw_port_learning_set(struct ethsw *ethsw, int port, bool learn)
{
	u32 mask = ETHSW_INPUT_LEARN_DIS(port);
	u32 reg = !learn ? mask : 0;

	ethsw_reg_rmw(ethsw, ETHSW_INPUT_LEARN, mask, reg);
}

static void ethsw_port_rx_block_set(struct ethsw *ethsw, int port, bool block)
{
	u32 mask = ETHSW_INPUT_LEARN_BLOCK(port);
	u32 reg = block ? mask : 0;

	ethsw_reg_rmw(ethsw, ETHSW_INPUT_LEARN, mask, reg);
}

static void ethsw_flooding_set_resolution(struct ethsw *ethsw, int port,
					  bool set)
{
	u8 offsets[] = {ETHSW_UCAST_DEF_MASK, ETHSW_BCAST_DEF_MASK,
			ETHSW_MCAST_DEF_MASK};
	int i;

	for (i = 0; i < ARRAY_SIZE(offsets); i++)
		ethsw_reg_rmw(ethsw, offsets[i], BIT(port),
				set ? BIT(port) : 0);
}

static void ethsw_port_set_standalone(struct ethsw *ethsw, int port,
				      bool standalone)
{
	ethsw_port_learning_set(ethsw, port, !standalone);
	ethsw_flooding_set_resolution(ethsw, port, !standalone);
	ethsw_port_mgmtfwd_set(ethsw, port, standalone);
}

static int ethsw_port_bridge_join(struct dsa_switch *ds, int port,
				  struct dsa_bridge bridge,
				  bool *tx_fwd_offload,
				  struct netlink_ext_ack *extack)
{
	struct ethsw *ethsw = ds->priv;

	/* We only support 1 bridge device */
	if (ethsw->br_dev && bridge.dev != ethsw->br_dev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Forwarding offload supported for a single bridge");
		return -EOPNOTSUPP;
	}

	ethsw->br_dev = bridge.dev;
	ethsw_port_set_standalone(ethsw, port, false);

	ethsw->bridged_ports |= BIT(port);

	return 0;
}

static void ethsw_port_bridge_leave(struct dsa_switch *ds, int port,
				    struct dsa_bridge bridge)
{
	struct ethsw *ethsw = ds->priv;

	ethsw->bridged_ports &= ~BIT(port);

	ethsw_port_set_standalone(ethsw, port, true);

	/* No more ports bridged */
	if (ethsw->bridged_ports == BIT(ETHSW_CPU_PORT))
		ethsw->br_dev = NULL;
}

static int ethsw_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				       struct switchdev_brport_flags flags,
				       struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
			   BR_BCAST_FLOOD))
		return -EINVAL;

	return 0;
}

static int
ethsw_port_bridge_flags(struct dsa_switch *ds, int port,
			struct switchdev_brport_flags flags,
			struct netlink_ext_ack *extack)
{
	struct ethsw *ethsw = ds->priv;
	u32 val;

	/* If a port is set as standalone, we do not want to be able to
	 * configure flooding nor learning which would result in joining the
	 * unique bridge. This can happen when a port leaves the bridge, in
	 * which case the DSA core will try to "clear" all flags for the
	 * standalone port (ie enable flooding, disable learning). In that case
	 * do not fail but do not apply the flags.
	 */
	if (!(ethsw->bridged_ports & BIT(port)))
		return 0;

	if (flags.mask & BR_LEARNING) {
		val = flags.val & BR_LEARNING ? 0 : ETHSW_INPUT_LEARN_DIS(port);
		ethsw_reg_rmw(ethsw, ETHSW_INPUT_LEARN,
			      ETHSW_INPUT_LEARN_DIS(port), val);
	}

	if (flags.mask & BR_FLOOD) {
		val = flags.val & BR_FLOOD ? BIT(port) : 0;
		ethsw_reg_rmw(ethsw, ETHSW_UCAST_DEF_MASK, BIT(port), val);
	}

	if (flags.mask & BR_MCAST_FLOOD) {
		val = flags.val & BR_MCAST_FLOOD ? BIT(port) : 0;
		ethsw_reg_rmw(ethsw, ETHSW_MCAST_DEF_MASK, BIT(port), val);
	}

	if (flags.mask & BR_BCAST_FLOOD) {
		val = flags.val & BR_BCAST_FLOOD ? BIT(port) : 0;
		ethsw_reg_rmw(ethsw, ETHSW_BCAST_DEF_MASK, BIT(port), val);
	}

	return 0;
}

static void ethsw_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	bool learning_enabled, rx_enabled, tx_enabled;
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct ethsw *ethsw = ds->priv;

	switch (state) {
	case BR_STATE_DISABLED:
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		rx_enabled = false;
		tx_enabled = false;
		learning_enabled = false;
		break;
	case BR_STATE_LEARNING:
		rx_enabled = false;
		tx_enabled = false;
		learning_enabled = dp->learning;
		break;
	case BR_STATE_FORWARDING:
		rx_enabled = true;
		tx_enabled = true;
		learning_enabled = dp->learning;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ethsw_port_learning_set(ethsw, port, learning_enabled);
	ethsw_port_rx_block_set(ethsw, port, !rx_enabled);
	ethsw_port_tx_enable(ethsw, port, tx_enabled);
}

static void ethsw_port_fast_age(struct dsa_switch *ds, int port)
{
	struct ethsw *ethsw = ds->priv;

	ethsw_port_fdb_flush(ethsw, port);
}

static int ethsw_lk_execute_lookup(struct ethsw *ethsw, union lk_data *lk_data,
				   u16 *entry)
{
	u32 ctrl;
	int ret;

	ethsw_reg_writel(ethsw, ETHSW_LK_DATA_LO, lk_data->lo);
	ethsw_reg_writel(ethsw, ETHSW_LK_DATA_HI, lk_data->hi);

	ctrl = ETHSW_LK_ADDR_CTRL_LOOKUP;
	ret = ethsw_lk_execute_ctrl(ethsw, &ctrl);
	if (ret)
		return ret;

	*entry = ctrl & ETHSW_LK_ADDR_CTRL_ADDRESS;

	return 0;
}

static int ethsw_port_fdb_add(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid,
			      struct dsa_db db)
{
	struct ethsw *ethsw = ds->priv;
	union lk_data lk_data = {0};
	bool inc_learncount = false;
	int ret = 0;
	u16 entry;
	u32 reg;

	ether_addr_copy(lk_data.entry.mac, addr);
	lk_data.entry.port_mask = BIT(port);

	mutex_lock(&ethsw->lk_lock);

	/* Set the value to be written in the lookup table */
	ret = ethsw_lk_execute_lookup(ethsw, &lk_data, &entry);
	if (ret)
		goto lk_unlock;

	lk_data.hi = ethsw_reg_readl(ethsw, ETHSW_LK_DATA_HI);
	if (!lk_data.entry.valid) {
		inc_learncount = true;
		/* port_mask set to 0x1f when entry is not valid, clear it */
		lk_data.entry.port_mask = 0;
		lk_data.entry.prio = 0;
	}

	lk_data.entry.port_mask |= BIT(port);
	lk_data.entry.is_static = 1;
	lk_data.entry.valid = 1;

	ethsw_reg_writel(ethsw, ETHSW_LK_DATA_HI, lk_data.hi);

	reg = ETHSW_LK_ADDR_CTRL_WRITE | entry;
	ret = ethsw_lk_execute_ctrl(ethsw, &reg);
	if (ret)
		goto lk_unlock;

	if (inc_learncount) {
		reg = ETHSW_LK_LEARNCOUNT_MODE_INC;
		ethsw_reg_writel(ethsw, ETHSW_LK_LEARNCOUNT, reg);
	}

lk_unlock:
	mutex_unlock(&ethsw->lk_lock);

	return ret;
}

static int ethsw_port_fdb_del(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid,
			      struct dsa_db db)
{
	struct ethsw *ethsw = ds->priv;
	union lk_data lk_data = {0};
	bool clear = false;
	u16 entry;
	u32 reg;
	int ret;

	ether_addr_copy(lk_data.entry.mac, addr);

	mutex_lock(&ethsw->lk_lock);

	ret = ethsw_lk_execute_lookup(ethsw, &lk_data, &entry);
	if (ret)
		goto lk_unlock;

	lk_data.hi = ethsw_reg_readl(ethsw, ETHSW_LK_DATA_HI);

	/* Our hardware does not associate any VID to the FDB entries so this
	 * means that if two entries were added for the same mac but for
	 * different VID, then, on the deletion of the first one, we would also
	 * delete the second one. Since there is unfortunately nothing we can do
	 * about that, do not return an error...
	 */
	if (!lk_data.entry.valid)
		goto lk_unlock;

	lk_data.entry.port_mask &= ~BIT(port);
	/* If there is no more port in the mask, clear the entry */
	if (lk_data.entry.port_mask == 0)
		clear = true;

	ethsw_reg_writel(ethsw, ETHSW_LK_DATA_HI, lk_data.hi);

	reg = entry;
	if (clear)
		reg |= ETHSW_LK_ADDR_CTRL_CLEAR;
	else
		reg |= ETHSW_LK_ADDR_CTRL_WRITE;

	ret = ethsw_lk_execute_ctrl(ethsw, &reg);
	if (ret)
		goto lk_unlock;

	/* Decrement LEARNCOUNT */
	if (clear) {
		reg = ETHSW_LK_LEARNCOUNT_MODE_DEC;
		ethsw_reg_writel(ethsw, ETHSW_LK_LEARNCOUNT, reg);
	}

lk_unlock:
	mutex_unlock(&ethsw->lk_lock);

	return ret;
}

static int ethsw_port_fdb_dump(struct dsa_switch *ds, int port,
			       dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ethsw *ethsw = ds->priv;
	union lk_data lk_data;
	int i = 0, ret = 0;
	u32 reg;

	mutex_lock(&ethsw->lk_lock);

	for (i = 0; i < ETHSW_TABLE_ENTRIES; i++) {
		reg = ETHSW_LK_ADDR_CTRL_READ | ETHSW_LK_ADDR_CTRL_WAIT | i;

		ret = ethsw_lk_execute_ctrl(ethsw, &reg);
		if (ret)
			goto out_unlock;

		lk_data.hi = ethsw_reg_readl(ethsw, ETHSW_LK_DATA_HI);
		/* If entry is not valid or does not contain the port, skip */
		if (!lk_data.entry.valid ||
		    !(lk_data.entry.port_mask & BIT(port)))
			continue;

		lk_data.lo = ethsw_reg_readl(ethsw, ETHSW_LK_DATA_LO);

		ret = cb(lk_data.entry.mac, 0, lk_data.entry.is_static, data);
		if (ret)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(&ethsw->lk_lock);

	return ret;
}

static int ethsw_port_vlan_filtering(struct dsa_switch *ds, int port,
				     bool vlan_filtering,
				     struct netlink_ext_ack *extack)
{
	u32 mask = BIT(port + ETHSW_VLAN_VERI_SHIFT) |
		   BIT(port + ETHSW_VLAN_DISC_SHIFT);
	u32 val = vlan_filtering ? mask : 0;
	struct ethsw *ethsw = ds->priv;

	/* Disable/enable vlan tagging */
	ethsw_reg_rmw(ethsw, ETHSW_VLAN_IN_MODE_ENA, BIT(port),
		      vlan_filtering ? BIT(port) : 0);

	/* Disable/enable vlan input filtering */
	ethsw_reg_rmw(ethsw, ETHSW_VLAN_VERIFY, mask, val);

	return 0;
}

static int ethsw_find_vlan_entry(struct ethsw *ethsw, u16 vid)
{
	u32 vlan_res;
	int i;

	/* Find vlan for this port */
	for (i = 0; i < ETHSW_VLAN_COUNT; i++) {
		vlan_res = ethsw_reg_readl(ethsw, ETHSW_VLAN_RES(i));
		if (FIELD_GET(ETHSW_VLAN_RES_VLANID, vlan_res) == vid)
			return i;
	}

	return -1;
}

static int ethsw_new_vlan_res_entry(struct ethsw *ethsw, u16 newvid)
{
	u32 vlan_res;
	int i;

	/* Find a free VLAN entry */
	for (i = 0; i < ETHSW_VLAN_COUNT; i++) {
		vlan_res = ethsw_reg_readl(ethsw, ETHSW_VLAN_RES(i));
		if (!(FIELD_GET(ETHSW_VLAN_RES_PORTMASK, vlan_res))) {
			vlan_res = FIELD_PREP(ETHSW_VLAN_RES_VLANID, newvid);
			ethsw_reg_writel(ethsw, ETHSW_VLAN_RES(i), vlan_res);
			return i;
		}
	}

	return -1;
}

static void ethsw_port_vlan_tagged_cfg(struct ethsw *ethsw,
				       unsigned int vlan_res_id, int port,
				       bool set)
{
	u32 mask = ETHSW_VLAN_RES_WR_PORTMASK | ETHSW_VLAN_RES_RD_TAGMASK |
		   BIT(port);
	u32 vlan_res_off = ETHSW_VLAN_RES(vlan_res_id);
	u32 val = ETHSW_VLAN_RES_WR_TAGMASK, reg;

	if (set)
		val |= BIT(port);

	/* Toggle tag mask read */
	ethsw_reg_writel(ethsw, vlan_res_off, ETHSW_VLAN_RES_RD_TAGMASK);
	reg = ethsw_reg_readl(ethsw, vlan_res_off);
	ethsw_reg_writel(ethsw, vlan_res_off, ETHSW_VLAN_RES_RD_TAGMASK);

	reg &= ~mask;
	reg |= val;
	ethsw_reg_writel(ethsw, vlan_res_off, reg);
}

static void ethsw_port_vlan_cfg(struct ethsw *ethsw, unsigned int vlan_res_id,
				int port, bool set)
{
	u32 mask = ETHSW_VLAN_RES_WR_TAGMASK | BIT(port);
	u32 reg = ETHSW_VLAN_RES_WR_PORTMASK;

	if (set)
		reg |= BIT(port);

	ethsw_reg_rmw(ethsw, ETHSW_VLAN_RES(vlan_res_id), mask, reg);
}

static int ethsw_port_vlan_add(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_vlan *vlan,
				struct netlink_ext_ack *extack)
{
	bool tagged = !(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct ethsw *ethsw = ds->priv;
	u16 vid = vlan->vid;
	int vlan_res_id;

	vlan_res_id = ethsw_find_vlan_entry(ethsw, vid);
	if (vlan_res_id < 0) {
		vlan_res_id = ethsw_new_vlan_res_entry(ethsw, vid);
		if (vlan_res_id < 0)
			return -ENOSPC;
	}

	ethsw_port_vlan_cfg(ethsw, vlan_res_id, port, true);
	if (tagged)
		ethsw_port_vlan_tagged_cfg(ethsw, vlan_res_id, port, true);

	/* Configure port to tag with corresponding VID, but do not enable it
	 * yet: wait for vlan filtering to be enabled to enable vlan port
	 * tagging
	 */
	if (pvid)
		ethsw_reg_writel(ethsw, ETHSW_SYSTEM_TAGINFO(port), vid);

	return 0;
}

static int ethsw_port_vlan_del(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_vlan *vlan)
{
	struct ethsw *ethsw = ds->priv;
	u16 vid = vlan->vid;
	int vlan_res_id;

	vlan_res_id = ethsw_find_vlan_entry(ethsw, vid);
	if (vlan_res_id < 0)
		return -EINVAL;

	ethsw_port_vlan_cfg(ethsw, vlan_res_id, port, false);
	ethsw_port_vlan_tagged_cfg(ethsw, vlan_res_id, port, false);

	return 0;
}

static u64 ethsw_read_stat(struct ethsw *ethsw, u32 offset, int port)
{
	u32 reg_lo, reg_hi;

	reg_lo = ethsw_reg_readl(ethsw, offset + ETHSW_PORT_OFFSET(port));
	/* ETHSW_STATS_HIWORD is latched on stat read */
	reg_hi = ethsw_reg_readl(ethsw, ETHSW_STATS_HIWORD);

	return ((u64)reg_hi << 32) | reg_lo;
}

static void ethsw_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			      uint8_t *data)
{
	unsigned int u;

	if (stringset != ETH_SS_STATS)
		return;

	for (u = 0; u < ARRAY_SIZE(ethsw_stats); u++) {
		memcpy(data + u * ETH_GSTRING_LEN, ethsw_stats[u].name,
		       ETH_GSTRING_LEN);
	}
}

static void ethsw_get_ethtool_stats(struct dsa_switch *ds, int port,
				    uint64_t *data)
{
	struct ethsw *ethsw = ds->priv;
	unsigned int u;

	for (u = 0; u < ARRAY_SIZE(ethsw_stats); u++)
		data[u] = ethsw_read_stat(ethsw, ethsw_stats[u].offset, port);
}

static int ethsw_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(ethsw_stats);
}

static void ethsw_get_eth_mac_stats(struct dsa_switch *ds, int port,
				    struct ethtool_eth_mac_stats *mac_stats)
{
	struct ethsw *ethsw = ds->priv;

#define RD(name) ethsw_read_stat(ethsw, ETHSW_##name, port)
	mac_stats->FramesTransmittedOK = RD(aFramesTransmittedOK);
	mac_stats->SingleCollisionFrames = RD(aSingleCollisions);
	mac_stats->MultipleCollisionFrames = RD(aMultipleCollisions);
	mac_stats->FramesReceivedOK = RD(aFramesReceivedOK);
	mac_stats->FrameCheckSequenceErrors = RD(aFrameCheckSequenceErrors);
	mac_stats->AlignmentErrors = RD(aAlignmentErrors);
	mac_stats->OctetsTransmittedOK = RD(aOctetsTransmittedOK);
	mac_stats->FramesWithDeferredXmissions = RD(aDeferred);
	mac_stats->LateCollisions = RD(aLateCollisions);
	mac_stats->FramesAbortedDueToXSColls = RD(aExcessiveCollisions);
	mac_stats->FramesLostDueToIntMACXmitError = RD(ifOutErrors);
	mac_stats->CarrierSenseErrors = RD(aCarrierSenseErrors);
	mac_stats->OctetsReceivedOK = RD(aOctetsReceivedOK);
	mac_stats->FramesLostDueToIntMACRcvError = RD(ifInErrors);
	mac_stats->MulticastFramesXmittedOK = RD(ifOutMulticastPkts);
	mac_stats->BroadcastFramesXmittedOK = RD(ifOutBroadcastPkts);
	mac_stats->FramesWithExcessiveDeferral = RD(aDeferred);
	mac_stats->MulticastFramesReceivedOK = RD(ifInMulticastPkts);
	mac_stats->BroadcastFramesReceivedOK = RD(ifInBroadcastPkts);
#undef RD
}

static const struct ethtool_rmon_hist_range ethsw_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 1518 },
	{ 1519, ETHSW_MAX_MTU },
	{}
};

static void ethsw_get_rmon_stats(struct dsa_switch *ds, int port,
				 struct ethtool_rmon_stats *rmon_stats,
				 const struct ethtool_rmon_hist_range **ranges)
{
	struct ethsw *ethsw = ds->priv;

#define RD(name) ethsw_read_stat(ethsw, ETHSW_##name, port)
	rmon_stats->undersize_pkts = RD(etherStatsUndersizePkts);
	rmon_stats->oversize_pkts = RD(etherStatsOversizePkts);
	rmon_stats->fragments = RD(etherStatsFragments);
	rmon_stats->jabbers = RD(etherStatsJabbers);
	rmon_stats->hist[0] = RD(etherStatsPkts64Octets);
	rmon_stats->hist[1] = RD(etherStatsPkts65to127Octets);
	rmon_stats->hist[2] = RD(etherStatsPkts128to255Octets);
	rmon_stats->hist[3] = RD(etherStatsPkts256to511Octets);
	rmon_stats->hist[4] = RD(etherStatsPkts512to1023Octets);
	rmon_stats->hist[5] = RD(etherStatsPkts1024to1518Octets);
	rmon_stats->hist[6] = RD(etherStatsPkts1519toXOctets);
#undef RD

	*ranges = ethsw_rmon_ranges;
}

static void ethsw_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
				     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct ethsw *ethsw = ds->priv;
	u64 stat;

	stat = ethsw_read_stat(ethsw, ETHSW_aTxPAUSEMACCtrlFrames, port);
	ctrl_stats->MACControlFramesTransmitted = stat;
	stat = ethsw_read_stat(ethsw, ETHSW_aRxPAUSEMACCtrlFrames, port);
	ctrl_stats->MACControlFramesReceived = stat;
}

static void ethsw_vlan_setup(struct ethsw *ethsw, int port)
{
	u32 reg;

	/* Enable TAG always mode for the port, this is actually controlled
	 * by VLAN_IN_MODE_ENA field which will be used for PVID insertion
	 */
	reg = ETHSW_VLAN_IN_MODE_TAG_ALWAYS;
	reg <<= ETHSW_VLAN_IN_MODE_PORT_SHIFT(port);
	ethsw_reg_rmw(ethsw, ETHSW_VLAN_IN_MODE, ETHSW_VLAN_IN_MODE_PORT(port),
		      reg);

	/* Set transparent mode for output frame manipulation, this will depend
	 * on the VLAN_RES configuration mode
	 */
	reg = ETHSW_VLAN_OUT_MODE_TRANSPARENT;
	reg <<= ETHSW_VLAN_OUT_MODE_PORT_SHIFT(port);
	ethsw_reg_rmw(ethsw, ETHSW_VLAN_OUT_MODE,
		      ETHSW_VLAN_OUT_MODE_PORT(port), reg);
}

static void ethsw_map_vlan_priotity_to_queue(struct ethsw *ethsw)
{
	int port;

	/* Mapping VLAN priority to each queue.
	 * When VLAN priority is enabled, corresponding traffic will be routed
	 * to corresponding queue
	 */

	for (port = 0; port <= 3; port++) {
		ethsw_reg_writel(ethsw, ETHSW_PRIORITY_VLAN_PRIORITY(port),
				 ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY0(0)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY1(1)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY2(2)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY3(3)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY4(4)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY5(5)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY6(6)
				 | ETHSW_PRIORITY_VLAN_PRIORITY_PRIORITY7(7));
	}
}

static int ethsw_register_xdp_callback(struct ethsw *ethsw);

static void ethsw_unregister_xdp_callback(struct ethsw *ethsw);

static int ethsw_setup(struct dsa_switch *ds)
{
	struct ethsw *ethsw = ds->priv;
	int port, vlan, ret;
	struct dsa_port *dp;
	u32 reg;
	struct timespec64 now;

	/* Validate that there is only 1 CPU port with index ETHSW_CPU_PORT */
	dsa_switch_for_each_cpu_port(dp, ds) {
		if (dp->index != ETHSW_CPU_PORT) {
			dev_err(ethsw->dev, "Invalid CPU port\n");
			return -EINVAL;
		}
	}

	/* Configure management port */
	reg = ETHSW_CPU_PORT | ETHSW_MGMT_CFG_ENABLE;
	ethsw_reg_writel(ethsw, ETHSW_MGMT_CFG, reg);

	/* Set pattern 0 to forward all frame to mgmt port */
	ethsw_reg_writel(ethsw, ETHSW_PATTERN_CTRL(ETHSW_PATTERN_MGMTFWD),
			 ETHSW_PATTERN_CTRL_MGMTFWD);

	/* Enable port tagging */
	reg = FIELD_PREP(ETHSW_MGMT_TAG_CFG_TAGFIELD, ETH_P_DSA_ETHSW);
	reg |= ETHSW_MGMT_TAG_CFG_ENABLE | ETHSW_MGMT_TAG_CFG_ALL_FRAMES;
	ethsw_reg_writel(ethsw, ETHSW_MGMT_TAG_CFG, reg);

	/* Enable normal switch operation */
	reg = ETHSW_LK_ADDR_CTRL_ENABLE | ETHSW_LK_ADDR_CTRL_LEARNING |
	      ETHSW_LK_ADDR_CTRL_AGEING | ETHSW_LK_ADDR_CTRL_ALLOW_MIGR |
	      ETHSW_LK_ADDR_CTRL_CLEAR_TABLE;
	ethsw_reg_writel(ethsw, ETHSW_LK_CTRL, reg);

	ret = readl_poll_timeout(ethsw->base + ETHSW_LK_CTRL, reg,
				 !(reg & ETHSW_LK_ADDR_CTRL_CLEAR_TABLE),
				 ETHSW_LK_BUSY_USEC_POLL, ETHSW_CTRL_TIMEOUT);
	if (ret) {
		dev_err(ethsw->dev, "Failed to clear lookup table\n");
		return ret;
	}

	/* Reset learn count to 0 */
	reg = ETHSW_LK_LEARNCOUNT_MODE_SET;
	ethsw_reg_writel(ethsw, ETHSW_LK_LEARNCOUNT, reg);

	/* Clear VLAN resource table */
	reg = ETHSW_VLAN_RES_WR_PORTMASK | ETHSW_VLAN_RES_WR_TAGMASK;
	for (vlan = 0; vlan < ETHSW_VLAN_COUNT; vlan++)
		ethsw_reg_writel(ethsw, ETHSW_VLAN_RES(vlan), reg);

	/* Reset all ports */
	dsa_switch_for_each_port(dp, ds) {
		port = dp->index;

		/* Reset the port */
		ethsw_reg_writel(ethsw, ETHSW_CMD_CFG(port),
				 ETHSW_CMD_CFG_SW_RESET);

		/* Enable only CPU port */
		ethsw_port_enable_set(ethsw, port, dsa_port_is_cpu(dp));

		if (dsa_port_is_unused(dp))
			continue;

		/* Enable egress flooding and learning for CPU port */
		if (dsa_port_is_cpu(dp)) {
			ethsw_flooding_set_resolution(ethsw, port, true);
			ethsw_port_learning_set(ethsw, port, true);
		}

		/* Enable standalone mode for user ports */
		if (dsa_port_is_user(dp))
			ethsw_port_set_standalone(ethsw, port, true);

		ethsw_vlan_setup(ethsw, port);
	}

	/* Initialize both ethsw timer with current system time */
	ktime_get_real_ts64(&now);
	ethsw_time_init(ethsw->base, 0);
	ethsw_time_init(ethsw->base, 1);

	/* Register XDP callback with stmmac — ignore error if conduit not ready */
	ret = ethsw_register_xdp_callback(ethsw);
	if (ret && ret != -ENODEV)
		dev_warn(ethsw->dev, "XDP callback registration failed: %d\n", ret);

	/* Mapping VLAN priority to queue */
	ethsw_map_vlan_priotity_to_queue(ethsw);

	return 0;
}

static int ethsw_port_xdp_setup(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	int err;

	if (!dp || !dp->user)
		return -ENODEV;

	err = xdp_rxq_info_reg(&dp->xdp_rxq, dp->user, 0, 0);
	if (err)
		return err;

	/* Use PAGE_SHARED initially; upgrade to PAGE_POOL lazily on first attach */
	err = xdp_rxq_info_reg_mem_model(&dp->xdp_rxq,
					 MEM_TYPE_PAGE_SHARED, NULL);
	if (err) {
		xdp_rxq_info_unreg(&dp->xdp_rxq);
		return err;
	}

	RCU_INIT_POINTER(dp->xdp_prog, NULL);
	dp->xdp_prog_attached = false;
	dp->rx_pp = NULL;

	netdev_info(dp->user, "ETHSW XDP: port %d rxq registered OK\n", port);
	return 0;
}

static void ethsw_port_xdp_teardown(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!dp)
		return;

	xdp_rxq_info_unreg(&dp->xdp_rxq);
	dp->rx_pp = NULL;
	dp->xdp_prog_attached = false;
}

static u32 ethsw_port_xdp_run(struct dsa_switch *ds, int port,
			      struct sk_buff *skb)
{
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!dp)
		return XDP_PASS;

	return dsa_user_xdp_run_skb(dp, skb);
}

/* A5PSW tag helpers in ethsw context */
static inline int ethsw_a5psw_get_port(const struct xdp_buff *xdp)
{
	const u8 *frame = xdp->data;
	const u8 *data_end = xdp->data_end;
	__be16 ctrl_data;

	/* [DA:6][SA:6][ctrl_tag:2][ctrl_data:2] = 16 bytes minimum */
	if (unlikely((data_end - frame) < (ETH_ALEN * 2 + 4)))
		return -EINVAL;

	/* Port number is in ctrl_data bits[3:0], at offset ETH_ALEN*2+2 = 14 */
	ctrl_data = get_unaligned_be16(frame + ETH_ALEN * 2 + 2);
	return (int)(ctrl_data & ETHSW_A5PSW_PORT_MASK);
}

static u32 ethsw_dp_dispatch(struct ethsw *ethsw,
			     struct xdp_buff *xdp,
			     struct page *page,
			     struct page_pool *pool)
{
	struct dsa_switch *ds = &ethsw->ds;
	struct bpf_prog *prog;
	struct dsa_port *dp;
	int port_num;
	u32 act;
	int res;

	port_num = ethsw_a5psw_get_port(xdp);
	if (port_num < 0)
		return XDP_PASS;

	dp = dsa_to_port(ds, port_num);
	if (!dp)
		return XDP_PASS;

	/* Hold RCU read lock for the entire dispatch to prevent
	 * dp->xdp_prog from being set to NULL during detach while
	 * we are still using it. Without this, Ctrl+C on xdp-bench
	 * causes a NULL pointer dereference in bpf_prog_run_xdp().
	 */
	rcu_read_lock();

	prog = rcu_dereference(dp->xdp_prog);
	if (!prog || !dp->xdp_prog_attached) {
		rcu_read_unlock();
		return XDP_PASS;
	}

	xdp->rxq = &dp->xdp_rxq;
	act = bpf_prog_run_xdp(prog, xdp);

	switch (act) {
	case XDP_DROP:
		rcu_read_unlock();
		page_pool_recycle_direct(pool, page);
		return XDP_DROP;
	case XDP_PASS:
		xdp->rxq = NULL;
		rcu_read_unlock();
		return XDP_PASS;
	case XDP_TX:
		rcu_read_unlock();
		res = stmmac_xdp_xmit_back_for_dsa(ethsw->stmmac, xdp);
		if (res == STMMAC_XDP_TX)
			return XDP_TX;
		page_pool_recycle_direct(pool, page);
		return XDP_DROP;
	case XDP_REDIRECT:
		res = xdp_do_redirect(dp->user, xdp, prog);
		rcu_read_unlock();
		if (!res)
			return XDP_REDIRECT;
		page_pool_recycle_direct(pool, page);
		return XDP_DROP;
	default:
		bpf_warn_invalid_xdp_action(dp->user, prog, act);
		rcu_read_unlock();
		page_pool_recycle_direct(pool, page);
		return XDP_DROP;
	}
}

static u32 ethsw_xdp_dispatch_cb(void *priv,
				 struct xdp_buff *xdp,
				 struct page *page,
				 struct page_pool *pool)
{
	return ethsw_dp_dispatch((struct ethsw *)priv, xdp, page, pool);
}

static struct stmmac_dsa_xdp_ops ethsw_xdp_ops = {
	.xdp_dispatch = ethsw_xdp_dispatch_cb,
};

static int ethsw_register_xdp_callback(struct ethsw *ethsw)
{
	struct dsa_port *cpu_dp;
	struct net_device *conduit;

	dsa_switch_for_each_cpu_port(cpu_dp, &ethsw->ds)
		break;

	if (!cpu_dp || !cpu_dp->conduit) {
		pr_warn("ETHSW XDP: conduit not ready, skip callback\n");
		return -ENODEV;
	}

	conduit = cpu_dp->conduit;
	ethsw->stmmac = netdev_priv(conduit);
	ethsw_xdp_ops.priv = ethsw;
	return stmmac_register_dsa_xdp_cb(conduit, &ethsw_xdp_ops);
}

static void ethsw_unregister_xdp_callback(struct ethsw *ethsw)
{
	struct dsa_port *cpu_dp;
	struct net_device *conduit;

	dsa_switch_for_each_cpu_port(cpu_dp, &ethsw->ds)
		break;

	if (!cpu_dp || !cpu_dp->conduit)
		return;

	conduit = cpu_dp->conduit;
	stmmac_unregister_dsa_xdp_cb(conduit);
}

static void ethsw_port_xdp_xmit_prepare(struct dsa_switch *ds,
					int port,
					struct sk_buff *skb)
{
	/* Use skb->mark instead of skb->cb since dsa_user_xmit()
	 * calls memset(skb->cb, 0) before ethsw_tag_xmit() is called,
	 * which would clear any flag set in skb->cb.
	 */
	 skb->mark |= ETHSW_SKB_MARK_XDP_REDIRECT;  /* XDP redirect marker — high bit */
}

static void ethsw_tdma_gcl_set(struct ethsw *ethsw, const u32 gcl_ix,
			       struct tc_taprio_sched_entry *entry, int port, u32 time_offset)
{
	u32 tcv_seq_ctrl = 0, tcv_d_ctrl = 0;

	/* sets TCV sequence */
	if (gcl_ix == 0)
		tcv_seq_ctrl |= ETHSW_TCV_SEQ_CTRL_START;

	/* sets CQF TCV sequence */
	if ((entry->gate_mask & BIT(ethsw->cqf_port_config[port].base_queue)) && ethsw->cqf_port_config[port].enable)
		tcv_seq_ctrl |= ETHSW_TCV_SEQ_CTRL_GPIO(0);
	else
		tcv_seq_ctrl &= ~ETHSW_TCV_SEQ_CTRL_GPIO(0);

	tcv_seq_ctrl |= ETHSW_TCV_SEQ_CTRL_D_INDEX(gcl_ix);

	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_ADDR, ETHSW_TCV_SEQ_ADDR_S_ADDR(gcl_ix));
	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_CTRL, tcv_seq_ctrl);

	/* sets TCV data */
	ethsw_reg_writel(ethsw, ETHSW_TCV_D_ADDR, ETHSW_TCV_D_ADDR_ADDR(gcl_ix));
	ethsw_reg_writel(ethsw, ETHSW_TCV_D_OFFSET, time_offset);

	tcv_d_ctrl = ETHSW_TCV_D_CTRL_QGATE(entry->gate_mask)
		   | ETHSW_TCV_D_CTRL_PMASK(BIT(port))
		   | ETHSW_TCV_D_CTRL_GATE_MODE
		   | ETHSW_TCV_D_CTRL_IN_CT_ENA
		   | ETHSW_TCV_D_CTRL_OUT_CT_ENA
		   | ETHSW_TCV_D_CTRL_INC_CTR0;

	ethsw_reg_writel(ethsw, ETHSW_TCV_D_CTRL, tcv_d_ctrl);
}

static void ethsw_tdma_start_time(struct ethsw *ethsw, u32 base_time,
				  u32 *tdma_start, u32 *tdma_ctr)
{
	u64 now, start_time;
	struct timespec64 ts;

	ethsw_time_get(ethsw->base, &now, ethsw->ethsw_ptp_timer);
	ts = ns_to_timespec64(now);
	*tdma_ctr = ts.tv_nsec;

	start_time = now + base_time;
	ts = ns_to_timespec64(start_time);
	*tdma_start = ts.tv_nsec;
}

static int ethsw_tc_taprio_set_schedule(struct ethsw *ethsw, int port,
					struct tc_taprio_qopt_offload *taprio)
{
	int i, time_offset, queue_gate, mmctl_qgate;
	u32 tdma_start, tdma_ctr;

	/* Disable TDMA operation */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TDMA_ENA, 0);

	/* Setting gate control */
	for (i = 0, time_offset = 0; i < taprio->num_entries; i++) {
		ethsw_tdma_gcl_set(ethsw, i, &taprio->entries[i], port, time_offset);
		time_offset += taprio->entries[i].interval;
	}

	ethsw_reg_writel(ethsw, ETHSW_TDMA_TCV_START, 0);
	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_LAST, ETHSW_TCV_SEQ_LAST_LAST(taprio->num_entries - 1));

	/* Set base time, cycle */
	ethsw_tdma_start_time(ethsw, taprio->base_time, &tdma_start, &tdma_ctr);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_START, tdma_start);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_MODULO, 1000*1000*1000);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CYCLE, taprio->cycle_time);

	ethsw_reg_rmw(ethsw, ETHSW_TDMA_ENA_CTRL, BIT(port), BIT(port));

	/* Select timer 0 */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TIMER_SEL, 0);

	/* Set timer 0 */
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CTR0, tdma_ctr);

	/* Enable TDMA */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TDMA_ENA, ETHSW_TDMA_CONFIG_TDMA_ENA);

	/* Close all queue gate to wait for TDMA control */
	queue_gate = 0;
	for (i = 0; i < ETHSW_NUM_TC; i++)
		queue_gate |= (ETHSW_MMCTL_QGATE_CLOSE & 0x3) << (i * 2);

	mmctl_qgate  = BIT(port);
	mmctl_qgate |= ETHSW_MMCTL_QGATE_QUEUE_GATE(queue_gate);

	ethsw_reg_writel(ethsw, ETHSW_MMCTL_QGATE, mmctl_qgate);

	return 0;
}

static int ethsw_tc_taprio_del_schedule(struct ethsw *ethsw, int port,
					struct tc_taprio_qopt_offload *taprio)
{
	int i, queue_gate, mmctl_qgate;

	/* Disable TDMA operation */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TDMA_ENA, 0);

	/* Remove gate control */
	for (i = 0; i < taprio->num_entries; i++)
		ethsw_tdma_gcl_set(ethsw, i, &taprio->entries[i], port, 0);

	ethsw_reg_writel(ethsw, ETHSW_TDMA_TCV_START, 0);
	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_LAST, 0);

	/* Reset base time, cycle */
	ethsw_reg_writel(ethsw, ETHSW_TDMA_START, 0);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_MODULO, 0);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CYCLE, 0);

	ethsw_reg_writel(ethsw, ETHSW_TDMA_ENA_CTRL, 0);

	ethsw_reg_writel(ethsw, ETHSW_TDMA_CTR0, 0);

	/* Disable TDMA */
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CONFIG, 0);

	/* Open all queue gate for normal operation */
	queue_gate = 0;
	for (i = 0; i < ETHSW_NUM_TC; i++)
		queue_gate |= (ETHSW_MMCTL_QGATE_OPEN & 0x3) << (i * 2);

	mmctl_qgate  = BIT(port);
	mmctl_qgate |= ETHSW_MMCTL_QGATE_QUEUE_GATE(queue_gate);

	ethsw_reg_writel(ethsw, ETHSW_MMCTL_QGATE, mmctl_qgate);

	return 0;
}

static int ethsw_setup_tc_taprio(struct ethsw *ethsw, int port,
				 struct tc_taprio_qopt_offload *taprio)
{
	int err = 0;

	switch (taprio->cmd) {
	case TAPRIO_CMD_REPLACE:
		err = ethsw_tc_taprio_set_schedule(ethsw, port, taprio);
		break;
	case TAPRIO_CMD_DESTROY:
		err = ethsw_tc_taprio_del_schedule(ethsw, port, taprio);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static int ethsw_port_setup_tc(struct dsa_switch *ds, int port,
			       enum tc_setup_type type,
			       void *type_data)
{
	struct ethsw *ethsw = ds->priv;

	switch (type) {
	case TC_SETUP_QDISC_TAPRIO:
		return ethsw_setup_tc_taprio(ethsw, port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int ethsw_filter_table_set(struct ethsw *ethsw, int port, int sid,
				  struct ethsw_qci_stream_filter *flt_entry)
{
	if (sid > ETHSW_MAX_SID)
		return -EINVAL;

	/* Disable stream filter table */
	ethsw_reg_rmw(ethsw, ETHSW_QSFTBL(port, sid), ETHSW_QSFTBL_QSTE, 0);

	/* Set MAC address */
	ethsw_reg_rmw(ethsw, ETHSW_QSTMACU(port, sid), BIT(ETHSW_QSTMACU_DASA),
		      flt_entry->qdasa << ETHSW_QSTMACU_DASA);
	ethsw_reg_rmw(ethsw, ETHSW_QSTMACU(port, sid), ETHSW_QSTMACU_MACA_MASK,
		      flt_entry->qmac[0] << 8 | flt_entry->qmac[1]);
	ethsw_reg_writel(ethsw, ETHSW_QSTMACD(port, sid),
			 ((u32)flt_entry->qmac[2] << 24U) |
			 ((u32)flt_entry->qmac[3] << 16U) |
			 ((u32)flt_entry->qmac[4] << 8U) |
			 ((u32)flt_entry->qmac[5]));

	/* Set MAC address mask*/
	ethsw_reg_writel(ethsw, ETHSW_QSTMAMU(port, sid),
			 flt_entry->qmam[0] << 8 | flt_entry->qmam[1]);
	ethsw_reg_writel(ethsw, ETHSW_QSTMAMD(port, sid),
			 ((u32)flt_entry->qmam[2] << 24U) |
			 ((u32)flt_entry->qmam[3] << 16U) |
			 ((u32)flt_entry->qmam[4] << 8U) |
			 ((u32)flt_entry->qmam[5]));

	/* Set VLAN */
	ethsw_reg_writel(ethsw, ETHSW_QSFTVL(port, sid),
			 ETHSW_QSFTVL_TAGMD(flt_entry->tagmd) |
			 ETHSW_QSFTVL_PCP(flt_entry->pcp) |
			 ETHSW_QSFTVL_DEI(flt_entry->dei) |
			 ETHSW_QSFTVL_VLANID(flt_entry->vlanid));

	/* Set VLAN Mask*/
	ethsw_reg_writel(ethsw, ETHSW_QSFTVLM(port, sid),
			 ETHSW_QSFTVLM_PCPM(flt_entry->pcpm) |
			 ETHSW_QSFTVLM_DEIM(flt_entry->deim) |
			 ETHSW_QSFTVLM_VLANIDM(flt_entry->vlanidm));

	return 0;
}

static void ethsw_filter_table_enable(struct ethsw *ethsw, int port, int sid, bool enable)
{
	if (enable)
		ethsw_reg_rmw(ethsw, ETHSW_QSFTBL(port, sid), ETHSW_QSFTBL_QSTE, ETHSW_QSFTBL_QSTE);
	else
		ethsw_reg_writel(ethsw, ETHSW_QSFTBL(port, sid), 0);
}

static int ethsw_flow_metering_set(struct ethsw *ethsw, int port,
				   struct netlink_ext_ack *extack,
				   int sid,
				   struct ethsw_flow_meter *p_meter)
{
	if (sid > ETHSW_MAX_SID) {
		NL_SET_ERR_MSG_MOD(extack, "Can only metering 7 stream");
		return -EINVAL;
	}

	if (p_meter->meid > ETHSW_MAX_MEID) {
		NL_SET_ERR_MSG_MOD(extack, "Can only metering 7 stream");
		return -EINVAL;
	}

	/* Set meid */
	ethsw_reg_rmw(ethsw, ETHSW_QSFTBL(port, sid), ETHSW_QSFTBL_MEID_MASK,
		      p_meter->meid << ETHSW_QSFTBL_MEID_POS);
	/* Enable Meter check */
	ethsw_reg_rmw(ethsw, ETHSW_QSFTBL(port, sid),
		      BIT(ETHSW_QSFTBL_MEIDV_POS), BIT(ETHSW_QSFTBL_MEIDV_POS));

	/* Red frame drop */
	ethsw_reg_rmw(ethsw, ETHSW_QMDESC(port, p_meter->meid), ETHSW_QMDESC_RFD, ETHSW_QMDESC_RFD);

	/* Set CBS, CIR */
	ethsw_reg_writel(ethsw, ETHSW_QMCBSC(port, p_meter->meid), p_meter->cbs);
	ethsw_reg_writel(ethsw, ETHSW_QMCIRC(port, p_meter->meid), p_meter->cir);

	/* Enable meter */
	ethsw_reg_rmw(ethsw, ETHSW_QMEC(port), BIT(p_meter->meid), BIT(p_meter->meid));

	return 0;
}

static int ethsw_flower_parse_filter(struct netlink_ext_ack *extack,
				     struct flow_cls_offload *cls,
				     struct ethsw_qci_stream_filter *filter)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	int i;

	if (dissector->used_keys &
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS))) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_has_control_flags(rule, extack))
		return -EOPNOTSUPP;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (match.key->n_proto) {
			NL_SET_ERR_MSG_MOD(extack,
					"Matching on protocol not supported");
			return -EOPNOTSUPP;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);

		if (!is_zero_ether_addr(match.mask->dst) &&
		    !is_zero_ether_addr(match.mask->src)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot match on both source and destination MAC");
			return -EINVAL;
		}

		if (!is_zero_ether_addr(match.mask->dst)) {
			ether_addr_copy(filter->qmac, match.key->dst);
			ether_addr_copy(filter->qmam, match.mask->dst);
			for (i = 0; i < 6; i++)
				filter->qmam[i] = ~filter->qmam[i];
			filter->qdasa = 1;
		}

		if (!is_zero_ether_addr(match.mask->src)) {
			ether_addr_copy(filter->qmac, match.key->src);
			ether_addr_copy(filter->qmam, match.mask->src);
			for (i = 0; i < 6; i++)
				filter->qmam[i] = ~filter->qmam[i];
			filter->qdasa = 0;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		filter->tagmd   = ETHSW_VLAN_TAG_MODE_C_TAGGED;
		filter->vlanid  = match.key->vlan_id;
		filter->dei     = match.key->vlan_dei;
		filter->pcp     = match.key->vlan_priority;
	}

	return 0;
}

static int ethsw_policer_validate(const struct flow_action *action,
				  const struct flow_action_entry *act,
				  struct netlink_ext_ack *extack)
{
	if (act->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id == FLOW_ACTION_ACCEPT &&
	    !flow_action_is_last_entry(action, act)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but action is not last");
		return -EOPNOTSUPP;
	}

	if (act->police.peakrate_bytes_ps ||
	    act->police.avrate || act->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	if (act->police.rate_pkt_ps) {
		NL_SET_ERR_MSG_MOD(extack,
				   "QoS offload not support packets per second");
		return -EOPNOTSUPP;
	}

	return 0;
}

static void ethsw_efp_table_enable(struct ethsw *ethsw, int port, bool enable)
{
	u8 addr;

	ethsw_reg_writel(ethsw, ETHSW_ASI_MEM_WDATA(0), 0);
	ethsw_reg_writel(ethsw, ETHSW_ASI_MEM_WDATA(1), 0);
	ethsw_reg_writel(ethsw, ETHSW_ASI_MEM_WDATA(2), 0);
	ethsw_reg_writel(ethsw, ETHSW_ASI_MEM_WDATA(3), 0);

	if (enable) {
		for (addr = 0; addr < ETHSW_EFP_ASI_ADDR_NUM; addr++)
			ethsw_reg_writel(ethsw, ETHSW_ASI_MEM_ADDR, (BIT(port) << 8) | ETHSW_MEM_WEN_ENABLE | addr);
		/* Enable EFP port */
		ethsw_reg_rmw(ethsw, ETHSW_CMD_CFG(port), ETHSW_CMD_CFG_EFPI_SELECT, ETHSW_CMD_CFG_EFPI_SELECT);
	} else {
		for (addr = 0; addr < ETHSW_EFP_ASI_ADDR_NUM; addr++)
			ethsw_reg_writel(ethsw, ETHSW_ASI_MEM_ADDR, (BIT(port) << 8) | addr);
		/* Disable EFP port */
		ethsw_reg_rmw(ethsw, ETHSW_CMD_CFG(port), ETHSW_CMD_CFG_EFPI_SELECT, 0);
	}
}

static void ethsw_efp_channel_enable(struct ethsw *ethsw, int port, bool enable)
{
	if (enable) {
		ethsw_reg_rmw(ethsw, ETHSW_CHANNEL_ENABLE, BIT(port), BIT(port));
		while (!((ethsw_reg_readl(ethsw, ETHSW_CHANNEL_STATE) >> port) & 0x1U))
			;
	} else {
		ethsw_reg_rmw(ethsw, ETHSW_CHANNEL_DISABLE, BIT(port), BIT(port));
		while ((ethsw_reg_readl(ethsw, ETHSW_CHANNEL_STATE) >> port) & 0x1U)
			;
	}
}

static int ethsw_flower_parse_meter(struct ethsw *ethsw, int port,
				    struct netlink_ext_ack *extack, u8 meid,
				    u64 rate_bytes_per_sec,
				    u32 burst,
				    struct ethsw_flow_meter *p_meter)
{
	struct phylink_pcs *pcs = ethsw->pcs[port];
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);
	u64 rate_bytes_per_sec_max, cir;

	if (burst < MAX_ETH_FRAME) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Burst must greater than Maximum frame size of an Ethernet Frame 2000");
		return -EINVAL;
	}

	p_meter->cbs = burst;
	p_meter->meid = meid;

	/* Calculate byte per second */
	rate_bytes_per_sec_max = (ethss_port->speed * 1000 * 1000) / 8;
	if (rate_bytes_per_sec > rate_bytes_per_sec_max) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Police rate must smaller than port speed");
		return -EINVAL;
	}

	/* Calculate fractional part, replenisher at 200MHz */
	cir = (rate_bytes_per_sec * U16_MAX) / 200000000;
	p_meter->cir = cir;

	return 0;
}

static void ethsw_tdma_gpio_gate_set(struct ethsw *ethsw, const u32 gcl_ix,
				     struct action_gate_entry *entry,
				     int port, u32 time_offset)
{
	u32 tcv_seq_ctrl = 0, tcv_d_ctrl = 0;

	/* sets TCV sequence */
	if (gcl_ix == 0)
		tcv_seq_ctrl |= ETHSW_TCV_SEQ_CTRL_START;

	tcv_seq_ctrl |= ETHSW_TCV_SEQ_CTRL_D_INDEX(gcl_ix);

	/* Use tdma_gpio0 for flow gate control */
	if (entry->gate_state)
		tcv_seq_ctrl |= ETHSW_TCV_SEQ_CTRL_GPIO(0);

	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_ADDR, ETHSW_TCV_SEQ_ADDR_S_ADDR(gcl_ix));
	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_CTRL, tcv_seq_ctrl);

	/* sets TCV data */
	ethsw_reg_writel(ethsw, ETHSW_TCV_D_ADDR, ETHSW_TCV_D_ADDR_ADDR(gcl_ix));
	ethsw_reg_writel(ethsw, ETHSW_TCV_D_OFFSET, time_offset);

	/* Use timer 0 for flow gate control */
	tcv_d_ctrl = ETHSW_TCV_D_CTRL_INC_CTR0;

	ethsw_reg_writel(ethsw, ETHSW_TCV_D_CTRL, tcv_d_ctrl);
}

static int ethsw_flow_gate_schedule(struct ethsw *ethsw, int port, int sid,
				    const struct flow_action_entry *act)
{
	int i, time_offset;
	u32 tdma_start, tdma_ctr;

	/* Disable TDMA operation */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TDMA_ENA, 0);

	/* Setting flow gate control */
	for (i = 0, time_offset = 0; i < act->gate.num_entries; i++) {
		ethsw_tdma_gpio_gate_set(ethsw, i, &act->gate.entries[i], port, time_offset);
		time_offset += act->gate.entries[i].interval;
	}

	ethsw_reg_writel(ethsw, ETHSW_TDMA_TCV_START, 0);
	ethsw_reg_writel(ethsw, ETHSW_TCV_SEQ_LAST, ETHSW_TCV_SEQ_LAST_LAST(act->gate.num_entries - 1));

	/* Set base time, cycle */
	ethsw_tdma_start_time(ethsw, act->gate.basetime, &tdma_start, &tdma_ctr);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_START, tdma_start);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_MODULO, 1000*1000*1000);
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CYCLE, act->gate.cycletime);

	ethsw_reg_rmw(ethsw, ETHSW_TDMA_ENA_CTRL, BIT(port), BIT(port));

	/* Select timer 0 */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TIMER_SEL, 0);

	/* Set timer 0 */
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CTR0, tdma_ctr);

	/* Enable TDMA */
	ethsw_reg_rmw(ethsw, ETHSW_TDMA_CONFIG, ETHSW_TDMA_CONFIG_TDMA_ENA, ETHSW_TDMA_CONFIG_TDMA_ENA);

	/* Set Gating Check */
	ethsw_reg_rmw(ethsw, ETHSW_QSFTBL(port, sid), ETHSW_QSFTBL_GAIDV, ETHSW_QSFTBL_GAIDV);
	/* Use tdma_gpio0 for Gating Check */
	ethsw_reg_rmw(ethsw, ETHSW_QSFTBL(port, sid), ETHSW_QSFTBL_GAID_MASK, 0);

	return 0;
}

static int ethsw_cls_flower_add(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct ethsw *ethsw = ds->priv;
	const struct flow_action_entry *act;
	struct ethsw_qci_stream_filter filter;
	int ret, i;

	ethsw_efp_table_enable(ethsw, port, 1);
	ethsw_efp_channel_enable(ethsw, port, 1);

	ret = ethsw_flower_parse_filter(extack, cls, &filter);
	if (ret)
		return ret;

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_POLICE:
			struct ethsw_flow_meter p_meter;

			ret = ethsw_policer_validate(&rule->action, act, extack);
			if (ret)
				break;

			ret = ethsw_filter_table_set(ethsw, port, i, &filter);
			if (ret)
				break;

			/* Mapping meid and sid */
			ret = ethsw_flower_parse_meter(ethsw, port, extack, i,
						       act->police.rate_bytes_ps,
						       act->police.burst, &p_meter);
			if (ret)
				break;

			ret = ethsw_flow_metering_set(ethsw, port, extack, i,
						      &p_meter);
			if (ret)
				break;

			ethsw_filter_table_enable(ethsw, port, i, 1);

			break;
		case FLOW_ACTION_GATE:
			ret = ethsw_filter_table_set(ethsw, port, i, &filter);
			if (ret)
				break;

			ret = ethsw_flow_gate_schedule(ethsw, port, i, act);
			if (ret)
				break;

			ethsw_filter_table_enable(ethsw, port, i, 1);

			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Action not supported");
			ret = -EOPNOTSUPP;
		}
	}

	return ret;
}

static int ethsw_cls_flower_del(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress)
{
	struct ethsw *ethsw = ds->priv;
	int i;

	ethsw_efp_table_enable(ethsw, port, 0);
	ethsw_efp_channel_enable(ethsw, port, 0);

	for (i = 0; i <= ETHSW_MAX_SID; i++)
		ethsw_filter_table_enable(ethsw, port, i, 0);

	/* Disable TDMA */
	ethsw_reg_writel(ethsw, ETHSW_TDMA_CONFIG, 0);

	return 0;
}

static const struct phylink_mac_ops ethsw_phylink_mac_ops = {
	.mac_select_pcs = ethsw_phylink_mac_select_pcs,
	.mac_config = ethsw_phylink_mac_config,
	.mac_link_down = ethsw_phylink_mac_link_down,
	.mac_link_up = ethsw_phylink_mac_link_up,
};

static const struct dsa_switch_ops ethsw_switch_ops = {
	.get_tag_protocol = ethsw_get_tag_protocol,
	.setup = ethsw_setup,
	.port_disable = ethsw_port_disable,
	.port_enable = ethsw_port_enable,
	.phylink_get_caps = ethsw_phylink_get_caps,
	.port_change_mtu = ethsw_port_change_mtu,
	.port_max_mtu = ethsw_port_max_mtu,
	.get_sset_count = ethsw_get_sset_count,
	.get_strings = ethsw_get_strings,
	.get_ethtool_stats = ethsw_get_ethtool_stats,
	.get_eth_mac_stats = ethsw_get_eth_mac_stats,
	.get_eth_ctrl_stats = ethsw_get_eth_ctrl_stats,
	.get_rmon_stats = ethsw_get_rmon_stats,
	.set_ageing_time = ethsw_set_ageing_time,
	.port_bridge_join = ethsw_port_bridge_join,
	.port_bridge_leave = ethsw_port_bridge_leave,
	.port_pre_bridge_flags = ethsw_port_pre_bridge_flags,
	.port_bridge_flags = ethsw_port_bridge_flags,
	.port_stp_state_set = ethsw_port_stp_state_set,
	.port_fast_age = ethsw_port_fast_age,
	.port_vlan_filtering = ethsw_port_vlan_filtering,
	.port_vlan_add = ethsw_port_vlan_add,
	.port_vlan_del = ethsw_port_vlan_del,
	.port_fdb_add = ethsw_port_fdb_add,
	.port_fdb_del = ethsw_port_fdb_del,
	.port_fdb_dump = ethsw_port_fdb_dump,
	.get_ts_info = ethsw_get_ts_info,
	.port_hwtstamp_set = ethsw_port_hwtstamp_set,
	.port_hwtstamp_get = ethsw_port_hwtstamp_get,
	.port_rxtstamp = ethsw_port_rxtstamp,
	.port_txtstamp = ethsw_port_txtstamp,
	.port_setup_tc = ethsw_port_setup_tc,
	.cls_flower_add	= ethsw_cls_flower_add,
	.cls_flower_del	= ethsw_cls_flower_del,
	.port_xdp_setup    = ethsw_port_xdp_setup,
	.port_xdp_teardown = ethsw_port_xdp_teardown,
	.port_xdp_run      = ethsw_port_xdp_run,
	.port_xdp_xmit_prepare = ethsw_port_xdp_xmit_prepare,
};

static int ethsw_mdio_wait_busy(struct ethsw *ethsw)
{
	u32 status;
	int err;

	err = readl_poll_timeout(ethsw->base + ETHSW_MDIO_CFG_STATUS, status,
				 !(status & ETHSW_MDIO_CFG_STATUS_BUSY), 10,
				 1000 * USEC_PER_MSEC);
	if (err)
		dev_err(ethsw->dev, "MDIO command timeout\n");

	return err;
}

static int ethsw_mdio_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	struct ethsw *ethsw = bus->priv;
	u32 cmd, status;
	int ret;

	cmd = ETHSW_MDIO_COMMAND_READ;
	cmd |= FIELD_PREP(ETHSW_MDIO_COMMAND_REG_ADDR, phy_reg);
	cmd |= FIELD_PREP(ETHSW_MDIO_COMMAND_PHY_ADDR, phy_id);

	ethsw_reg_writel(ethsw, ETHSW_MDIO_COMMAND, cmd);

	ret = ethsw_mdio_wait_busy(ethsw);
	if (ret)
		return ret;

	ret = ethsw_reg_readl(ethsw, ETHSW_MDIO_DATA) & ETHSW_MDIO_DATA_MASK;

	status = ethsw_reg_readl(ethsw, ETHSW_MDIO_CFG_STATUS);
	if (status & ETHSW_MDIO_CFG_STATUS_READERR)
		return -EIO;

	return ret;
}

static int ethsw_mdio_write(struct mii_bus *bus, int phy_id, int phy_reg,
			    u16 phy_data)
{
	struct ethsw *ethsw = bus->priv;
	u32 cmd;

	cmd = FIELD_PREP(ETHSW_MDIO_COMMAND_REG_ADDR, phy_reg);
	cmd |= FIELD_PREP(ETHSW_MDIO_COMMAND_PHY_ADDR, phy_id);

	ethsw_reg_writel(ethsw, ETHSW_MDIO_COMMAND, cmd);
	ethsw_reg_writel(ethsw, ETHSW_MDIO_DATA, phy_data);

	return ethsw_mdio_wait_busy(ethsw);
}

static int ethsw_mdio_config(struct ethsw *ethsw, u32 mdio_freq)
{
	unsigned long rate;
	unsigned long div;
	u32 cfgstatus;
	u32 hold;

	rate = clk_get_rate(ethsw->clk);
	div = ((rate / mdio_freq) / 2);
	if (div > FIELD_MAX(ETHSW_MDIO_CFG_STATUS_CLKDIV) ||
	    div < ETHSW_MDIO_CLK_DIV_MIN) {
		dev_err(ethsw->dev, "MDIO clock div %ld out of range\n", div);
		return -ERANGE;
	}

	/* MDIO Hold Time Setting */
	hold = ETHSW_MDIO_CFG_STATUS_HOLD; /* 3 PCLKM clock cycles */

	cfgstatus = FIELD_PREP(ETHSW_MDIO_CFG_STATUS_CLKDIV, div);
	cfgstatus |= hold;

	ethsw_reg_writel(ethsw, ETHSW_MDIO_CFG_STATUS, cfgstatus);

	return 0;
}

static int ethsw_probe_mdio(struct ethsw *ethsw, struct device_node *node)
{
	struct device *dev = ethsw->dev;
	struct mii_bus *bus;
	u32 mdio_freq;
	int ret;

	if (of_property_read_u32(node, "clock-frequency", &mdio_freq))
		mdio_freq = ETHSW_MDIO_DEF_FREQ;

	ret = ethsw_mdio_config(ethsw, mdio_freq);
	if (ret)
		return ret;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "ethsw_mdio";
	bus->read = ethsw_mdio_read;
	bus->write = ethsw_mdio_write;
	bus->priv = ethsw;
	bus->parent = dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	ethsw->mii_bus = bus;

	return devm_of_mdiobus_register(dev, bus, node);
}

static void ethsw_pcs_free(struct ethsw *ethsw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ethsw->pcs); i++) {
		if (ethsw->pcs[i])
			ethss_destroy(ethsw->pcs[i]);
	}
}

static int ethsw_pcs_get(struct ethsw *ethsw)
{
	struct device_node *ports, *port, *pcs_node;
	struct phylink_pcs *pcs;
	struct ethss_port *ethss_port;
	int ret;
	u32 reg;

	ports = of_get_child_by_name(ethsw->dev->of_node, "ethernet-ports");
	if (!ports)
		return -EINVAL;

	for_each_available_child_of_node(ports, port) {
		pcs_node = of_parse_phandle(port, "pcs-handle", 0);
		if (!pcs_node)
			continue;

		if (of_property_read_u32(port, "reg", &reg)) {
			ret = -EINVAL;
			goto free_pcs;
		}

		if (reg >= ARRAY_SIZE(ethsw->pcs)) {
			ret = -ENODEV;
			goto free_pcs;
		}

		pcs = ethss_create(ethsw->dev, pcs_node);
		if (IS_ERR(pcs)) {
			dev_err(ethsw->dev, "Failed to create PCS for port %d\n",
				reg);
			ret = PTR_ERR(pcs);
			goto free_pcs;
		}

		ethsw->pcs[reg] = pcs;
		ethss_port = phylink_pcs_to_ethss_port(pcs);
		ethsw->ethss = ethss_port->ethss;
		of_node_put(pcs_node);

	}
	of_node_put(ports);

	return 0;

free_pcs:
	of_node_put(pcs_node);
	of_node_put(port);
	of_node_put(ports);
	ethsw_pcs_free(ethsw);

	return ret;
}

static int detach_sec_ns(const char *s, u32 *sec, u32 *ns)
{
	char *strsec, *strns;
	char pad[10] = "0000000000";
	int ret, len;

	strsec = strsep((char **)&s, ".");
	strns = (char *)s;

	len = (int)strlen(strns);
	if (len < 10) {
		strns[len - 1] = 0;
		strncat(strns, pad, 10 - len);
	}

	ret = kstrtouint(strsec, 10, sec);
	if (ret)
		return ret;

	ret = kstrtouint(strns, 10, ns);
	if (ret)
		return ret;

	return 0;
}

static ssize_t PTPOUT0_enable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(0), 1);
	else
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(0), 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT0_enable_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMEN(0));

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t PTPOUT0_start_time_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMSTSEC(0), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMSTNS(0), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT0_start_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMSTSEC(0));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMSTNS(0));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT0_period_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMPSEC(0), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMPNS(0), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT0_period_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMPSEC(0));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMPNS(0));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT0_width_ns_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, width, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val >= 524280) {
		dev_err(ethsw->dev, "Pulse width must smaller than 524280 ns\n");
		return -EINVAL;
	}

	width = val / 8;

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMWTH(0), width);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT0_width_ns_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMWTH(0));

	ret *= 8;

	return sprintf(buf, "%u ns\r\n", ret);
}

static ssize_t PTPOUT1_enable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(1), 1);
	else
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(1), 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT1_enable_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMEN(1));

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t PTPOUT1_start_time_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMSTSEC(1), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMSTNS(1), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT1_start_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMSTSEC(1));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMSTNS(1));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT1_period_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMPSEC(1), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMPNS(1), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT1_period_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMPSEC(1));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMPNS(1));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT1_width_ns_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, width, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val >= 524280) {
		dev_err(ethsw->dev, "Pulse width must smaller than 524280 ns\n");
		return -EINVAL;
	}

	width = val / 8;

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMWTH(1), width);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT1_width_ns_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMWTH(1));

	ret *= 8;

	return sprintf(buf, "%u ns\r\n", ret);
}

static ssize_t PTPOUT2_enable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(2), 1);
	else
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(2), 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT2_enable_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMEN(2));

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t PTPOUT2_start_time_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMSTSEC(2), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMSTNS(2), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT2_start_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMSTSEC(2));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMSTNS(2));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT2_period_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMPSEC(2), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMPNS(2), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT2_period_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMPSEC(2));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMPNS(2));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT2_width_ns_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, width, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val >= 524280) {
		dev_err(ethsw->dev, "Pulse width must smaller than 524280 ns\n");
		return -EINVAL;
	}

	width = val / 8;

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMWTH(2), width);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT2_width_ns_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMWTH(2));

	ret *= 8;

	return sprintf(buf, "%u ns\r\n", ret);
}

static ssize_t PTPOUT3_enable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(3), 1);
	else
		ethss_reg_writel(ethsw, ETHSW_SWTMEN(3), 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT3_enable_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMEN(3));

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t PTPOUT3_start_time_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMSTSEC(3), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMSTNS(3), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT3_start_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMSTSEC(3));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMSTNS(3));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT3_period_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns, ret;

	ret = detach_sec_ns(buf, &sec, &ns);
	if (ret) {
		dev_err(ethsw->dev, "Invalid value\n");
		return ret;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMPSEC(3), sec);
	ethss_reg_writel(ethsw, ETHSW_SWTMPNS(3), ns);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT3_period_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 sec, ns;

	sec = ethss_reg_readl(ethsw, ETHSW_SWTMPSEC(3));
	ns = ethss_reg_readl(ethsw, ETHSW_SWTMPNS(3));

	return sprintf(buf, "%u sec %u ns\r\n", sec, ns);
}

static ssize_t PTPOUT3_width_ns_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, width, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val >= 524280) {
		dev_err(ethsw->dev, "Pulse width must smaller than 524280 ns\n");
		return -EINVAL;
	}

	width = val / 8;

	mutex_lock(&ethsw->sysfs_lock);

	ethss_reg_writel(ethsw, ETHSW_SWTMWTH(3), width);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t PTPOUT3_width_ns_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethss_reg_readl(ethsw, ETHSW_SWTMWTH(3));

	ret *= 8;

	return sprintf(buf, "%u ns\r\n", ret);
}

static ssize_t VLAN_priority_port0_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(0), ETHSW_PRIORITY_CFG_VLANEN, ETHSW_PRIORITY_CFG_VLANEN);
	else
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(0), ETHSW_PRIORITY_CFG_VLANEN, 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t VLAN_priority_port0_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw_reg_readl(ethsw, ETHSW_PRIORITY_CFG(0)) & ETHSW_PRIORITY_CFG_VLANEN;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t VLAN_priority_port1_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(1), ETHSW_PRIORITY_CFG_VLANEN, ETHSW_PRIORITY_CFG_VLANEN);
	else
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(1), ETHSW_PRIORITY_CFG_VLANEN, 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t VLAN_priority_port1_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw_reg_readl(ethsw, ETHSW_PRIORITY_CFG(1)) & ETHSW_PRIORITY_CFG_VLANEN;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t VLAN_priority_port2_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(2), ETHSW_PRIORITY_CFG_VLANEN, ETHSW_PRIORITY_CFG_VLANEN);
	else
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(2), ETHSW_PRIORITY_CFG_VLANEN, 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t VLAN_priority_port2_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw_reg_readl(ethsw, ETHSW_PRIORITY_CFG(2)) & ETHSW_PRIORITY_CFG_VLANEN;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t VLAN_priority_port3_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(3), ETHSW_PRIORITY_CFG_VLANEN, ETHSW_PRIORITY_CFG_VLANEN);
	else
		ethsw_reg_rmw(ethsw, ETHSW_PRIORITY_CFG(3), ETHSW_PRIORITY_CFG_VLANEN, 0);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t VLAN_priority_port3_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw_reg_readl(ethsw, ETHSW_PRIORITY_CFG(3)) & ETHSW_PRIORITY_CFG_VLANEN;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_port0_enable_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw->cqf_port_config[0].enable = 1;
	else
		ethsw->cqf_port_config[0].enable = 0;

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_port0_enable_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret =  ethsw->cqf_port_config[0].enable;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_priority_port0_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) priorities\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[0].priority = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(0), ETHSW_MMCTL_CQF_PRIO_MASK, BIT(val));

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_priority_port0_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[0].priority;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_base_queue_port0_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) queues\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[0].base_queue = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(0), ETHSW_MMCTL_CQF_QUEUE_MASK, val << 8);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_base_queue_port0_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[0].base_queue;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_port1_enable_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw->cqf_port_config[1].enable = 1;
	else
		ethsw->cqf_port_config[1].enable = 0;

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_port1_enable_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret =  ethsw->cqf_port_config[1].enable;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_priority_port1_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) priorities\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[1].priority = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(1), ETHSW_MMCTL_CQF_PRIO_MASK, BIT(val));

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_priority_port1_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[1].priority;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_base_queue_port1_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) queues\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[1].base_queue = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(1), ETHSW_MMCTL_CQF_QUEUE_MASK, val << 8);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_base_queue_port1_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[1].base_queue;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_port2_enable_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw->cqf_port_config[2].enable = 1;
	else
		ethsw->cqf_port_config[2].enable = 0;

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_port2_enable_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret =  ethsw->cqf_port_config[2].enable;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_priority_port2_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) priorities\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[2].priority = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(2), ETHSW_MMCTL_CQF_PRIO_MASK, BIT(val));

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_priority_port2_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[2].priority;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_base_queue_port2_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) queues\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[2].base_queue = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(2), ETHSW_MMCTL_CQF_QUEUE_MASK, val << 8);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_base_queue_port2_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[2].base_queue;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_port3_enable_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_err(ethsw->dev, "Only 0 or 1 is valid value\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	if (val)
		ethsw->cqf_port_config[3].enable = 1;
	else
		ethsw->cqf_port_config[3].enable = 0;

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_port3_enable_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret =  ethsw->cqf_port_config[3].enable;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_priority_port3_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) priorities\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[3].priority = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(3), ETHSW_MMCTL_CQF_PRIO_MASK, BIT(val));

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_priority_port3_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[3].priority;

	return sprintf(buf, "%u\r\n", ret);
}

static ssize_t CQF_base_queue_port3_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 7) {
		dev_err(ethsw->dev, "ETHSW only have 8 (0-7) queues\n");
		return -EINVAL;
	}

	mutex_lock(&ethsw->sysfs_lock);

	ethsw->cqf_port_config[3].base_queue = val;
	ethsw_reg_rmw(ethsw, ETHSW_MMCTL_CQF_CTRL(3), ETHSW_MMCTL_CQF_QUEUE_MASK, val << 8);

	mutex_unlock(&ethsw->sysfs_lock);

	return ret ? : count;
}

static ssize_t CQF_base_queue_port3_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ethsw *ethsw = platform_get_drvdata(pdev);
	u32 ret;

	ret = ethsw->cqf_port_config[3].base_queue;

	return sprintf(buf, "%u\r\n", ret);
}

static DEVICE_ATTR_RW(PTPOUT0_enable);
static DEVICE_ATTR_RW(PTPOUT0_start_time);
static DEVICE_ATTR_RW(PTPOUT0_period);
static DEVICE_ATTR_RW(PTPOUT0_width_ns);
static DEVICE_ATTR_RW(PTPOUT1_enable);
static DEVICE_ATTR_RW(PTPOUT1_start_time);
static DEVICE_ATTR_RW(PTPOUT1_period);
static DEVICE_ATTR_RW(PTPOUT1_width_ns);
static DEVICE_ATTR_RW(PTPOUT2_enable);
static DEVICE_ATTR_RW(PTPOUT2_start_time);
static DEVICE_ATTR_RW(PTPOUT2_period);
static DEVICE_ATTR_RW(PTPOUT2_width_ns);
static DEVICE_ATTR_RW(PTPOUT3_enable);
static DEVICE_ATTR_RW(PTPOUT3_start_time);
static DEVICE_ATTR_RW(PTPOUT3_period);
static DEVICE_ATTR_RW(PTPOUT3_width_ns);
static DEVICE_ATTR_RW(VLAN_priority_port0);
static DEVICE_ATTR_RW(VLAN_priority_port1);
static DEVICE_ATTR_RW(VLAN_priority_port2);
static DEVICE_ATTR_RW(VLAN_priority_port3);
static DEVICE_ATTR_RW(CQF_port0_enable);
static DEVICE_ATTR_RW(CQF_priority_port0);
static DEVICE_ATTR_RW(CQF_base_queue_port0);
static DEVICE_ATTR_RW(CQF_port1_enable);
static DEVICE_ATTR_RW(CQF_priority_port1);
static DEVICE_ATTR_RW(CQF_base_queue_port1);
static DEVICE_ATTR_RW(CQF_port2_enable);
static DEVICE_ATTR_RW(CQF_priority_port2);
static DEVICE_ATTR_RW(CQF_base_queue_port2);
static DEVICE_ATTR_RW(CQF_port3_enable);
static DEVICE_ATTR_RW(CQF_priority_port3);
static DEVICE_ATTR_RW(CQF_base_queue_port3);

static struct attribute *attrs[] = {
	&dev_attr_PTPOUT0_enable.attr,
	&dev_attr_PTPOUT0_start_time.attr,
	&dev_attr_PTPOUT0_period.attr,
	&dev_attr_PTPOUT0_width_ns.attr,
	&dev_attr_PTPOUT1_enable.attr,
	&dev_attr_PTPOUT1_start_time.attr,
	&dev_attr_PTPOUT1_period.attr,
	&dev_attr_PTPOUT1_width_ns.attr,
	&dev_attr_PTPOUT2_enable.attr,
	&dev_attr_PTPOUT2_start_time.attr,
	&dev_attr_PTPOUT2_period.attr,
	&dev_attr_PTPOUT2_width_ns.attr,
	&dev_attr_PTPOUT3_enable.attr,
	&dev_attr_PTPOUT3_start_time.attr,
	&dev_attr_PTPOUT3_period.attr,
	&dev_attr_PTPOUT3_width_ns.attr,
	&dev_attr_VLAN_priority_port0.attr,
	&dev_attr_VLAN_priority_port1.attr,
	&dev_attr_VLAN_priority_port2.attr,
	&dev_attr_VLAN_priority_port3.attr,
	&dev_attr_CQF_port0_enable.attr,
	&dev_attr_CQF_priority_port0.attr,
	&dev_attr_CQF_base_queue_port0.attr,
	&dev_attr_CQF_port1_enable.attr,
	&dev_attr_CQF_priority_port1.attr,
	&dev_attr_CQF_base_queue_port1.attr,
	&dev_attr_CQF_port2_enable.attr,
	&dev_attr_CQF_priority_port2.attr,
	&dev_attr_CQF_base_queue_port2.attr,
	&dev_attr_CQF_port3_enable.attr,
	&dev_attr_CQF_priority_port3.attr,
	&dev_attr_CQF_base_queue_port3.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attrs,
};

static irqreturn_t ethsw_intr_irq_handler(int irq, void *data)
{
	struct ethsw *ethsw = data;
	u32 stat_ack;

	stat_ack = ethsw_reg_readl(ethsw, ETHSW_INT_STAT_ACK);

	/* Always claim the interrupt even when stat_ack is zero.
	 * At high XDP redirect rates (~110k pps), TSM fires per-packet.
	 * Returning IRQ_NONE would accumulate unhandled count rapidly,
	 * triggering kernel "nobody cared" and disabling IRQ #85.
	 * IRQ_HANDLED prevents this without affecting PTP/gPTP since
	 * ethsw_isr_tsm() is only called when TSM_INT bit is set.
	 */

	if (!stat_ack)
		return IRQ_HANDLED;

	/* Clear interrupt status */
	ethsw_reg_writel(ethsw, ETHSW_INT_STAT_ACK, stat_ack);

	/* TSM Interrupt */
	if (stat_ack & ETHSW_INT_STAT_ACK_TSM_INT)
		return ethsw_isr_tsm(ethsw);

	return IRQ_HANDLED;
}

static irqreturn_t ethsw_intr_irq_handler_thread(int irq, void *data)
{
	struct ethsw *ethsw = data;

	ethsw_isr_tsm_thread(ethsw);

	return IRQ_HANDLED;
}

static int ethsw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *mdio;
	struct dsa_switch *ds;
	struct ethsw *ethsw;
	int ret;

	ethsw = devm_kzalloc(dev, sizeof(*ethsw), GFP_KERNEL);
	if (!ethsw)
		return -ENOMEM;

	ethsw->dev = dev;
	mutex_init(&ethsw->vlan_lock);
	mutex_init(&ethsw->lk_lock);
	mutex_init(&ethsw->sysfs_lock);
	spin_lock_init(&ethsw->reg_lock);
	ethsw->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ethsw->base))
		return PTR_ERR(ethsw->base);

	ethsw->bridged_ports = BIT(ETHSW_CPU_PORT);

	ret = ethsw_pcs_get(ethsw);
	if (ret)
		return ret;

	ethsw->ethss->ethsw_base = ethsw->base;

	ethsw->reset = devm_gpiod_get(&pdev->dev, "phy-reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ethsw->reset)) {
		ret = PTR_ERR(ethsw->reset);
		goto free_pcs;
	}
	gpiod_set_value(ethsw->reset, 1);

	ethsw->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ethsw->clk)) {
		dev_err(dev, "failed get clk switch clock\n");
		ret = PTR_ERR(ethsw->clk);
		goto reset_gpio;
	}

	ret = clk_prepare_enable(ethsw->clk);
	if (ret)
		goto reset_gpio;

	ethsw->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(ethsw->rst)) {
		ret = PTR_ERR(ethsw->rst);
		goto clk_disable;
	}

	ret = reset_control_deassert(ethsw->rst);
	if (ret < 0)
		goto clk_disable;

	dev_dbg(&pdev->dev, "Deassert reset control OK\n");

	mdio = of_get_child_by_name(dev->of_node, "mdio");
	if (of_device_is_available(mdio)) {
		ret = ethsw_probe_mdio(ethsw, mdio);
		if (ret) {
			of_node_put(mdio);
			dev_err(dev, "Failed to register MDIO: %d\n", ret);
			goto reset;
		}
	}

	of_node_put(mdio);

	platform_set_drvdata(pdev, ethsw);

	ret = sysfs_create_group(&dev->kobj, &attr_group);
	if (ret < 0) {
		dev_err(dev, "failed to create sysfs: %d\n", ret);
		goto reset;
	}

	ds = &ethsw->ds;
	ds->dev = dev;
	ds->num_ports = ETHSW_PORTS_NUM;
	ds->num_tx_queues = ETHSW_NUM_TC;
	ds->ops = &ethsw_switch_ops;
	ds->phylink_mac_ops = &ethsw_phylink_mac_ops;
	ds->priv = ethsw;

	ret = dsa_register_switch(ds);
	if (ret) {
		dev_err(dev, "Failed to register DSA switch: %d\n", ret);
		goto remove_sysfs;
	}

	ret = of_property_read_u32(dev->of_node, "ethsw_ptp_timer", &ethsw->ethsw_ptp_timer);
	if (ret) {
		dev_err(dev, "Failed to get ETHSW PTP timer\n");
	} else {
		ethsw->clk_ptp_rate = 125000000;
		dev_info(dev, "ETHSW use timer %d for PTP\n", ethsw->ethsw_ptp_timer);
	}

	ethsw->intr_irq = platform_get_irq_byname(pdev, "ethsw_intr");
	if (ethsw->intr_irq < 0) {
		dev_err(dev, "Failed to obtain IRQ\n");
		ret = ethsw->intr_irq;
		goto unregister_dsa;
	}

	ret = devm_request_threaded_irq(dev, ethsw->intr_irq,
					ethsw_intr_irq_handler,
					ethsw_intr_irq_handler_thread, 0,
					dev_name(ethsw->dev), ethsw);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		goto unregister_dsa;
	}

	ret = ethsw_ptp_register(ethsw);
	if (ret) {
		dev_err(dev, "Failed to setup PTP!\n");
		goto free_irq;
	}

	ret = ethsw_hwtstamp_setup(ethsw);
	if (ret) {
		dev_err(dev, "Failed to setup hardware timestamping!\n");
		goto unregister_ptp;
	}

	dev_info(dev, "ETHSW Switch probed OK\n");

	return 0;

unregister_ptp:
	ethsw_ptp_unregister(ethsw);
free_irq:
	devm_free_irq(dev, ethsw->intr_irq, ethsw);
unregister_dsa:
	dsa_unregister_switch(ds);
remove_sysfs:
	sysfs_remove_group(&dev->kobj, &attr_group);
reset:
	reset_control_assert(ethsw->rst);
clk_disable:
	clk_disable_unprepare(ethsw->clk);
reset_gpio:
	gpiod_set_value(ethsw->reset, 0);
free_pcs:
	ethsw_pcs_free(ethsw);

	return ret;
}

static void ethsw_remove(struct platform_device *pdev)
{
	struct ethsw *ethsw = platform_get_drvdata(pdev);

	if (!ethsw)
		return;

	ethsw_hwtstamp_free(ethsw);
	ethsw_ptp_unregister(ethsw);
	ethsw_unregister_xdp_callback(ethsw);
	dsa_unregister_switch(&ethsw->ds);
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);
	ethsw_pcs_free(ethsw);
	gpiod_set_value(ethsw->reset, 0);
	clk_disable_unprepare(ethsw->clk);
	reset_control_assert(ethsw->rst);
}

static void ethsw_shutdown(struct platform_device *pdev)
{
	struct ethsw *ethsw = platform_get_drvdata(pdev);

	if (!ethsw)
		return;

	dsa_switch_shutdown(&ethsw->ds);

	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id ethsw_of_mtable[] = {
	{ .compatible = "renesas,rzt2-ethsw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ethsw_of_mtable);

static struct platform_driver ethsw_driver = {
	.driver = {
		.name	 = "rzt2_ethsw",
		.of_match_table = ethsw_of_mtable,
	},
	.probe = ethsw_probe,
	.remove_new = ethsw_remove,
	.shutdown = ethsw_shutdown,
};
module_platform_driver(ethsw_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas RZ/T2N Ethernet Switch driver");
MODULE_AUTHOR("LongLuu <long.luu.ur@renesas.com>");

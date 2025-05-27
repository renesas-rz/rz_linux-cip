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
#include <linux/net/renesas/rzt2h-ethss.h>

#include "rzt2h_ethsw.h"

struct ethsw_stats {
	u16 offset;
	const char name[ETH_GSTRING_LEN];
};

#define STAT_DESC(_offset) {	\
	.offset = ETHSW_##_offset,	\
	.name = __stringify(_offset),	\
}

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

static enum dsa_tag_protocol ethsw_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_RZT2H_ETHSW;
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

	if (dsa_is_cpu_port(ds, port))
		new_mtu += ETHSW_EXTRA_FRM_LENGTH;

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

	if (dp->index != ETHSW_CPU_PORT)
		ethss_switchcore_adjust(ethsw->pcs[dp->index], duplex, speed);

	if (speed == SPEED_1000)
		cmd_cfg |= ETHSW_CMD_CFG_ETH_SPEED;

	if (duplex == DUPLEX_HALF)
		cmd_cfg |= ETHSW_CMD_CFG_HD_ENA;

	cmd_cfg |= ETHSW_CMD_CFG_CNTL_FRM_ENA;

	if (!rx_pause)
		cmd_cfg &= ~ETHSW_CMD_CFG_PAUSE_IGNORE;

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

static int ethsw_setup(struct dsa_switch *ds)
{
	struct ethsw *ethsw = ds->priv;
	int port, vlan, ret;
	struct dsa_port *dp;
	u32 reg;

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

	rate = clk_get_rate(ethsw->clk);
	div = ((rate / mdio_freq) / 2);
	if (div > FIELD_MAX(ETHSW_MDIO_CFG_STATUS_CLKDIV) ||
	    div < ETHSW_MDIO_CLK_DIV_MIN) {
		dev_err(ethsw->dev, "MDIO clock div %ld out of range\n", div);
		return -ERANGE;
	}

	cfgstatus = FIELD_PREP(ETHSW_MDIO_CFG_STATUS_CLKDIV, div);

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

	ds = &ethsw->ds;
	ds->dev = dev;
	ds->num_ports = ETHSW_PORTS_NUM;
	ds->ops = &ethsw_switch_ops;
	ds->phylink_mac_ops = &ethsw_phylink_mac_ops;
	ds->priv = ethsw;

	ret = dsa_register_switch(ds);
	if (ret) {
		dev_err(dev, "Failed to register DSA switch: %d\n", ret);
		goto reset;
	}

	dev_info(dev, "ETHSW Switch probed OK\n");

	return 0;

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

	dsa_unregister_switch(&ethsw->ds);
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
	{ .compatible = "renesas,rzt2h-ethsw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ethsw_of_mtable);

static struct platform_driver ethsw_driver = {
	.driver = {
		.name	 = "rzt2h_ethsw",
		.of_match_table = ethsw_of_mtable,
	},
	.probe = ethsw_probe,
	.remove_new = ethsw_remove,
	.shutdown = ethsw_shutdown,
};
module_platform_driver(ethsw_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas RZ/T2H Ethernet Switch driver");
MODULE_AUTHOR("LongLuu <long.luu.ur@renesas.com>");

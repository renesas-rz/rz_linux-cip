// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Schneider Electric
 */

#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <net/dsa.h>

#include "tag.h"

/* To define the outgoing port and to discover the incoming port a TAG is
 * inserted after Src MAC :
 *
 *       Dest MAC       Src MAC           TAG         Type
 * ...| 1 2 3 4 5 6 | 1 2 3 4 5 6 | 1 2 3 4 5 6 7 8 | 1 2 |...
 *                                |<--------------->|
 *
 * See struct ethsw_tag for layout
 */

#define ETHSW_NAME			"ethsw"

#define ETH_P_DSA_ETHSW			0xE001
#define ETHSW_TAG_LEN			8
#define ETHSW_CTRL_DATA_FORCE_FORWARD	BIT(0)
/* This is both used for xmit tag and rcv tagging */
#define ETHSW_CTRL_DATA_PORT		GENMASK(3, 0)

struct ethsw_tag {
	__be16 ctrl_tag;
	__be16 ctrl_data;
	__be16 ctrl_data2_hi;
	__be16 ctrl_data2_lo;
};

static struct sk_buff *ethsw_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	struct ethsw_tag *ptag;
	u32 data2_val;

	BUILD_BUG_ON(sizeof(*ptag) != ETHSW_TAG_LEN);

	/* The Ethernet switch we are interfaced with needs packets to be at
	 * least 60 bytes otherwise they will be discarded when they enter the
	 * switch port logic.
	 */
	if (__skb_put_padto(skb, ETH_ZLEN, false))
		return NULL;

	/* provide 'ETHSW_TAG_LEN' bytes additional space */
	skb_push(skb, ETHSW_TAG_LEN);

	/* make room between MACs and Ether-Type to insert tag */
	dsa_alloc_etype_header(skb, ETHSW_TAG_LEN);

	ptag = dsa_etype_header_pos_tx(skb);

	data2_val = FIELD_PREP(ETHSW_CTRL_DATA_PORT, BIT(dp->index));
	ptag->ctrl_tag = htons(ETH_P_DSA_ETHSW);
	ptag->ctrl_data = htons(ETHSW_CTRL_DATA_FORCE_FORWARD);
	ptag->ctrl_data2_lo = htons(data2_val);
	ptag->ctrl_data2_hi = 0;

	return skb;
}

static struct sk_buff *ethsw_tag_rcv(struct sk_buff *skb,
					struct net_device *dev)
{
	struct ethsw_tag *tag;
	int port;

	if (unlikely(!pskb_may_pull(skb, ETHSW_TAG_LEN))) {
		dev_warn_ratelimited(&dev->dev,
				     "Dropping packet, cannot pull\n");
		return NULL;
	}

	tag = dsa_etype_header_pos_rx(skb);

	if (tag->ctrl_tag != htons(ETH_P_DSA_ETHSW)) {
		dev_warn_ratelimited(&dev->dev, "Dropping packet due to invalid TAG marker\n");
		return NULL;
	}

	port = FIELD_GET(ETHSW_CTRL_DATA_PORT, ntohs(tag->ctrl_data));

	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev)
		return NULL;

	skb_pull_rcsum(skb, ETHSW_TAG_LEN);
	dsa_strip_etype_header(skb, ETHSW_TAG_LEN);

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static const struct dsa_device_ops ethsw_netdev_ops = {
	.name	= ETHSW_NAME,
	.proto	= DSA_TAG_PROTO_RZT2H_ETHSW,
	.xmit	= ethsw_tag_xmit,
	.rcv	= ethsw_tag_rcv,
	.needed_headroom = ETHSW_TAG_LEN,
};

MODULE_DESCRIPTION("DSA tag driver for Renesas RZ/T2H ETHSW");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_ETHSW, ETHSW_NAME);
module_dsa_tag_driver(ethsw_netdev_ops);

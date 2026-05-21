// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026, Renesas Corporation.
 *
 * XDP native mode for the Renesas RZ/T2N Ethernet Switch Module (ESWM).
 */

#ifndef _ESWM_XDP_H
#define _ESWM_XDP_H

#define ESWM_XDP_HEADROOM	XDP_PACKET_HEADROOM	/* 256 bytes */
#define ESWM_XDP_MAX_MTU	(PAGE_SIZE - ESWM_XDP_HEADROOM - \
				SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

struct eswm_xdp_stats {
	u64 xdp_pass;
	u64 xdp_drop;
	u64 xdp_tx;
	u64 xdp_tx_err;
	u64 xdp_redirect;
};

void eswm_get_strings(struct net_device *ndev, u32 stringset, u8 *data);
int eswm_get_sset_count(struct net_device *ndev, int sset);
void eswm_get_ethtool_stats(struct net_device *ndev,
				struct ethtool_stats *stats, u64 *data);
bool eswm_rx_xdp(struct net_device *ndev, int *quota);
int eswm_xdp_xmit(struct net_device *ndev, int num_frame,
				struct xdp_frame **frames, u32 flags);
int eswm_bpf(struct net_device *ndev, struct netdev_bpf *bpf);
int eswm_pp_create(struct net_device *ndev, struct eswm_gwca_queue *gq);
void eswm_pp_destroy(struct eswm_gwca_queue *gq);
#endif /* _ESWM_XDP_H_ */

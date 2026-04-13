/* SPDX-License-Identifier: GPL-2.0 */
/*
 * stmmac DSA XDP integration API
 *
 * Provides hooks for DSA switch drivers to inject XDP processing
 * into the stmmac NAPI receive loop (True Native XDP for DSA ports).
 */
#ifndef __LINUX_STMMAC_DSA_XDP_H
#define __LINUX_STMMAC_DSA_XDP_H

/* XDP return codes for stmmac internal use */
#define STMMAC_XDP_CONSUMED  BIT(0)
#define STMMAC_XDP_TX        BIT(1)
#define STMMAC_XDP_REDIRECT  BIT(2)

/**
 * struct stmmac_dsa_xdp_ops - DSA XDP callback ops registered by switch driver
 * @xdp_dispatch: called per-packet in stmmac NAPI loop, pre-skb allocation
 * @priv:         opaque pointer passed back to xdp_dispatch
 * @stmmac:       filled by stmmac_register_dsa_xdp_cb(), do not set manually
 * @xdp_status:   internal, unused by callers
 */
struct stmmac_dsa_xdp_ops {
	u32 (*xdp_dispatch)(void *priv,
			    struct xdp_buff *xdp,
			    struct page *page,
			    struct page_pool *pool);
	void *priv;
	struct stmmac_priv *stmmac;
	int  *xdp_status;
};

struct page_pool *stmmac_get_rx_page_pool(struct net_device *ndev, int q);
int  stmmac_register_dsa_xdp_cb(struct net_device *ndev,
				 struct stmmac_dsa_xdp_ops *ops);
void stmmac_unregister_dsa_xdp_cb(struct net_device *ndev);
int  stmmac_xdp_xmit_back_for_dsa(struct stmmac_priv *priv,
				   struct xdp_buff *xdp);

#endif /* __LINUX_STMMAC_DSA_XDP_H */

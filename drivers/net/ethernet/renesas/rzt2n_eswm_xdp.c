// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026, Renesas Corporation.
 *
 * XDP native mode for the Renesas RZ/T2N Ethernet Switch Module (ESWM).
 */

#include "rzt2n_eswm.h"
#include "rzt2n_eswm_xdp.h"

enum eswm_xdp_stat_id {
	ESWM_XDP_PASS,
	ESWM_XDP_DROP,
	ESWM_XDP_TX,
	ESWM_XDP_TX_ERR,
	ESWM_XDP_REDIRECT,
};

struct eswm_xdp_stat_entry {
	const char *name;
	u32 id;
};

static const struct eswm_xdp_stat_entry eswm_xdp_stat_strings[] = {
	{ "xdp_pass",     ESWM_XDP_PASS     },
	{ "xdp_drop",     ESWM_XDP_DROP     },
	{ "xdp_tx",       ESWM_XDP_TX       },
	{ "xdp_tx_err",   ESWM_XDP_TX_ERR   },
	{ "xdp_redirect", ESWM_XDP_REDIRECT },
};

static u64 eswm_read_xdp_stat(struct eswm_device *rdev, enum eswm_xdp_stat_id id)
{
	u64 val = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		const struct eswm_xdp_stats *xs = per_cpu_ptr(rdev->xdp_stats, cpu);

		switch (id) {
		case ESWM_XDP_PASS:
			val += xs->xdp_pass;
			break;
		case ESWM_XDP_DROP:
			val += xs->xdp_drop;
			break;
		case ESWM_XDP_TX:
			val += xs->xdp_tx;
			break;
		case ESWM_XDP_TX_ERR:
			val += xs->xdp_tx_err;
			break;
		case ESWM_XDP_REDIRECT:
			val += xs->xdp_redirect;
			break;
		default:
			break;
		}
	}

	return val;
}

void eswm_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	unsigned int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(eswm_xdp_stat_strings); i++)
		ethtool_puts(&data, eswm_xdp_stat_strings[i].name);
}

int eswm_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(eswm_xdp_stat_strings);
}

void eswm_get_ethtool_stats(struct net_device *ndev,
				struct ethtool_stats *stats, u64 *data)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(eswm_xdp_stat_strings); i++)
		data[i] = eswm_read_xdp_stat(rdev, eswm_xdp_stat_strings[i].id);
}

int eswm_pp_create(struct net_device *ndev, struct eswm_gwca_queue *gq)
{
	struct page_pool_params pp = {
		.order     = 0,
		.flags     = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.pool_size = gq->ring_size,
		.dev       = ndev->dev.parent,
		.dma_dir   = DMA_BIDIRECTIONAL,
		.offset    = ESWM_XDP_HEADROOM,
		.max_len   = ESWM_DESC_BUF_SIZE,
	};

	gq->pp = page_pool_create(&pp);
	if (IS_ERR(gq->pp)) {
		netdev_err(ndev, "Failed to create page pool\n");
		return PTR_ERR(gq->pp);
	}

	return 0;
}

void eswm_pp_destroy(struct eswm_gwca_queue *gq)
{
	if (!gq->pp)
		return;

	page_pool_destroy(gq->pp);
	gq->pp = NULL;
}

static int eswm_pp_alloc_rx_bufs(struct eswm_gwca_queue *gq,
					unsigned int start_index,
					unsigned int num)
{
	unsigned int i, index;
	struct page *page;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;

		if (gq->rx_bufs[index])
			continue;

		page = page_pool_dev_alloc_pages(gq->pp);
		if (!page)
			return -ENOMEM;

		gq->rx_bufs[index] = page_address(page) + ESWM_XDP_HEADROOM;
	}

	return 0;
}

static void eswm_xdp_put_buff(struct eswm_gwca_queue *gq,
					struct xdp_buff *xdp)
{
	struct skb_shared_info *sinfo;
	int i;

	if (likely(!xdp_buff_has_frags(xdp)))
		goto out;

	sinfo = xdp_get_shared_info_from_buff(xdp);
	for (i = 0; i < sinfo->nr_frags; i++)
		page_pool_put_full_page(gq->pp, skb_frag_page(&sinfo->frags[i]), true);

out:
	page_pool_put_full_page(gq->pp, virt_to_head_page(xdp->data), true);
}

static inline unsigned int eswm_xdp_num_tx_descs(struct xdp_frame *xdpf)
{
	if (!xdp_frame_has_frags(xdpf))
		return 1;

	return 1 + xdp_get_shared_info_from_frame(xdpf)->nr_frags;
}

static int eswm_xdp_submit_frame(struct eswm_device *rdev,
					struct xdp_frame *xdpf,
					bool is_ndo_xmit)
{
	struct eswm_gwca_queue *gq = rdev->tx_queues[0];
	struct skb_shared_info *sinfo = NULL;
	unsigned int nr_descs, i, cur;
	dma_addr_t dma_addr;
	u8 die_dt;

	nr_descs = eswm_xdp_num_tx_descs(xdpf);

	if (eswm_get_num_cur_queues(gq) >= gq->ring_size - nr_descs)
		return -ENOSPC;

	if (xdp_frame_has_frags(xdpf))
		sinfo = xdp_get_shared_info_from_frame(xdpf);

	for (i = 0; i < nr_descs; i++) {
		struct eswm_ext_desc *desc;
		u16 len;

		cur = eswm_next_queue_index(gq, true, i);
		desc = &gq->tx_ring[cur];

		if (i == 0) {
			len = xdpf->len;
			if (is_ndo_xmit) {
				dma_addr = dma_map_single(rdev->ndev->dev.parent,
							xdpf->data, len, DMA_TO_DEVICE);

				if (dma_mapping_error(rdev->ndev->dev.parent, dma_addr))
					goto err_unmap;

				gq->unmap_addrs[cur] = dma_addr;
			} else {
				struct page *page = virt_to_head_page(xdpf->data);

				dma_addr = page_pool_get_dma_addr(page) +
						(xdpf->data - (page_address(page) + ESWM_XDP_HEADROOM));

				dma_sync_single_for_device(rdev->ndev->dev.parent,
								dma_addr, len, DMA_BIDIRECTIONAL);

				gq->unmap_addrs[cur] = 0;
			}
		} else {
			skb_frag_t *frag = &sinfo->frags[i - 1];
			len = skb_frag_size(frag);

			if (is_ndo_xmit) {
				dma_addr = dma_map_page(rdev->ndev->dev.parent, skb_frag_page(frag),
								skb_frag_off(frag), len, DMA_TO_DEVICE);

				if (dma_mapping_error(rdev->ndev->dev.parent, dma_addr))
					goto err_unmap;

				gq->unmap_addrs[cur] = dma_addr;
			} else {
				struct page *page = skb_frag_page(frag);

				dma_addr = page_pool_get_dma_addr(page) + skb_frag_off(frag);
				dma_sync_single_for_device(rdev->ndev->dev.parent, dma_addr, len,
								DMA_BIDIRECTIONAL);

				gq->unmap_addrs[cur] = dma_addr;
			}
		}

		eswm_desc_set_dptr(&desc->desc, dma_addr);
		desc->desc.info_ds = cpu_to_le16(len);
		desc->info1 = cpu_to_le64(INFO1_DV(BIT(rdev->etha->index)) |
						INFO1_IPV(GWCA_IPV_NUM) | INFO1_FMT);

		gq->skbs[cur] = NULL;
		gq->xdpf[cur] = (i == 0) ? xdpf : NULL;
		gq->is_xdp_tx[cur] = true;
		gq->xdp_from_ndo[cur] = is_ndo_xmit;
	}

	/* DT_FSTART should be set at last. So, this is reverse order. */
	for (i = nr_descs; i-- > 0; ) {
		struct eswm_ext_desc *desc;

		cur  = eswm_next_queue_index(gq, true, i);
		desc = &gq->tx_ring[cur];

		if (nr_descs == 1)
			die_dt = DT_FSINGLE | DIE;
		else if (i == 0)
			die_dt = DT_FSTART;
		else if (i == nr_descs - 1)
			die_dt = DT_FEND | DIE;
		else
			die_dt = DT_FMID;

		dma_wmb();
		desc->desc.die_dt = die_dt;
	}

	wmb(); /* gq->cur must be incremented after die_dt was set */

	gq->cur = eswm_next_queue_index(gq, true, nr_descs);
	eswm_modify(rdev->addr, GWTRC(gq->index), 0, BIT(gq->index % 32));

	return 0;

err_unmap:
	for (; i-- > 1;) {
		unsigned int slot = eswm_next_queue_index(gq, true, i);

		if (gq->unmap_addrs[slot]) {
			skb_frag_t *frag = &sinfo->frags[i - 1];
			dma_unmap_page(rdev->ndev->dev.parent, gq->unmap_addrs[slot],
						skb_frag_size(frag), DMA_TO_DEVICE);
			gq->unmap_addrs[slot] = 0;
		}
	}

	cur = eswm_next_queue_index(gq, true, 0);

	if (is_ndo_xmit && gq->unmap_addrs[cur]) {
		dma_unmap_single(rdev->ndev->dev.parent, gq->unmap_addrs[cur],
						xdpf->len, DMA_TO_DEVICE);
		gq->unmap_addrs[cur] = 0;
	}

	return -EIO;
}

static int eswm_xdp_run_verdict(struct net_device *ndev,
					struct eswm_gwca_queue *gq,
					struct bpf_prog *prog,
					struct xdp_buff *xdp,
					struct eswm_xdp_stats *xs,
					u64 hwtstamp_ns, bool has_ts,
					bool *xdp_redirect)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	u32 act;
	int ret;

	act = bpf_prog_run_xdp(prog, xdp);

	switch (act) {
	case XDP_PASS: {
		unsigned int metasize = xdp->data - xdp->data_meta;
		struct sk_buff *skb;

		skb = napi_build_skb(xdp->data_hard_start, xdp->frame_sz);
		if (unlikely(!skb)) {
			eswm_xdp_put_buff(gq, xdp);
			xs->xdp_drop++;
			return -ENOMEM;
		}

		skb_reserve(skb, xdp->data - xdp->data_hard_start);
		__skb_put(skb, xdp->data_end - xdp->data);
		skb_mark_for_recycle(skb);

		if (metasize)
			skb_metadata_set(skb, metasize);

		if (xdp_buff_has_frags(xdp)) {
			struct skb_shared_info *sinfo = xdp_get_shared_info_from_buff(xdp);

			xdp_update_skb_shared_info(skb, sinfo->nr_frags, sinfo->xdp_frags_size,
							PAGE_SIZE * sinfo->nr_frags, false);
		}

		if (has_ts) {
			struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);

			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			shhwtstamps->hwtstamp = ns_to_ktime(hwtstamp_ns);
		}

		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&rdev->napi, skb);

		xs->xdp_pass++;
		ndev->stats.rx_bytes += xdp->data_end - xdp->data;
		break;
	}

	case XDP_TX: {
		struct xdp_frame *xdpf = xdp_convert_buff_to_frame(xdp);

		if (unlikely(!xdpf)) {
			eswm_xdp_put_buff(gq, xdp);
			xs->xdp_tx_err++;
			return -ENOBUFS;
		}

		ret = eswm_xdp_submit_frame(rdev, xdpf, false);
		if (unlikely(ret)) {
			xdp_return_frame_rx_napi(xdpf);
			xs->xdp_tx_err++;
			return ret;
		}

		xs->xdp_tx++;
		break;
	}

	case XDP_REDIRECT:
		ret = xdp_do_redirect(ndev, xdp, prog);
		if (unlikely(ret)) {
			eswm_xdp_put_buff(gq, xdp);
			xs->xdp_drop++;
			return ret;
		}

		xs->xdp_redirect++;
		*xdp_redirect = true;
		break;

	default:
		bpf_warn_invalid_xdp_action(ndev, prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(ndev, prog, act);
		fallthrough;
	case XDP_DROP:
		eswm_xdp_put_buff(gq, xdp);
		xs->xdp_drop++;
		break;
	}

	return 0;
}

bool eswm_rx_xdp(struct net_device *ndev, int *quota)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	struct eswm_gwca_queue *gq = rdev->rx_queue;
	struct bpf_prog	*prog = READ_ONCE(rdev->xdp_prog);
	struct eswm_xdp_stats *xs = this_cpu_ptr(rdev->xdp_stats);
	struct eswm_ext_ts_desc	*desc;

	struct xdp_buff  xdp_mb; /* Multi buffer */
	struct xdp_buff *xdp_head = NULL;

	bool xdp_redirect = false;
	int boguscnt, limit, ret;
	unsigned int num;

	if (*quota <= 0)
		return true;

	boguscnt = min_t(int, gq->ring_size, *quota);
	limit = boguscnt;
	desc = &gq->rx_ring[gq->cur];

	while ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY) {
		void *buf_addr;
		struct page *page;
		dma_addr_t dma_addr;
		u8 die_dt;
		u16 pkt_len;
		u32 get_ts;
		u64 hwtstamp_ns = 0;
		bool has_ts = false;

		dma_rmb();
		die_dt = desc->desc.die_dt & DT_MASK;
		pkt_len = le16_to_cpu(desc->desc.info_ds) & RX_DS;

		buf_addr = gq->rx_bufs[gq->cur];
		if (unlikely(!buf_addr))
			break;

		gq->rx_bufs[gq->cur] = NULL;

		page = virt_to_head_page(buf_addr - ESWM_XDP_HEADROOM);

		dma_addr = page_pool_get_dma_addr(page) + ESWM_XDP_HEADROOM;
		dma_sync_single_for_cpu(rdev->ndev->dev.parent, dma_addr, pkt_len, DMA_BIDIRECTIONAL);

		if (die_dt == DT_FSINGLE || die_dt == DT_FSTART) {
			bool has_frags = (die_dt == DT_FSTART);

			xdp_init_buff(&xdp_mb, PAGE_SIZE, &gq->xdp_rxq);
			xdp_buff_clear_frags_flag(&xdp_mb);
			xdp_prepare_buff(&xdp_mb, buf_addr - ESWM_XDP_HEADROOM,
						ESWM_XDP_HEADROOM, pkt_len, false);

			if (has_frags) {
				xdp_head = &xdp_mb;
				goto next_desc;
			}

		} else {
			struct skb_shared_info *sinfo;

			if (unlikely(!xdp_head)) {
				page_pool_put_full_page(gq->pp, page, false);
				xs->xdp_drop++;
				goto next_desc;
			}

			sinfo = xdp_get_shared_info_from_buff(xdp_head);

			if (unlikely(sinfo->nr_frags >= MAX_SKB_FRAGS)) {
				page_pool_put_full_page(gq->pp, page, false);
				eswm_xdp_put_buff(gq, xdp_head);
				xdp_head = NULL;
				xs->xdp_drop++;
				goto next_desc;
			}

			if (!xdp_buff_has_frags(xdp_head))
				sinfo->nr_frags = 0;

			skb_frag_fill_page_desc(&sinfo->frags[sinfo->nr_frags],
						page, ESWM_XDP_HEADROOM, pkt_len);
			sinfo->nr_frags++;

			if (!xdp_buff_has_frags(xdp_head)) {
				sinfo->xdp_frags_size += pkt_len;
				xdp_buff_set_frags_flag(xdp_head);
			} else {
				sinfo->xdp_frags_size += pkt_len;
			}

			if (page_is_pfmemalloc(page))
				xdp_buff_set_frag_pfmemalloc(xdp_head);

			if (die_dt == DT_FMID)
				goto next_desc;

			xdp_head = NULL;
		}

		get_ts = rdev->priv->ptp_priv->tstamp_rx_ctrl & ESWM_RXTSTAMP_TYPE_V2_L2_EVENT;
		if (get_ts) {
			struct timespec64 ts = {
				.tv_sec  = __le32_to_cpu(desc->ts_sec),
				.tv_nsec = __le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3fffffff)),
			};

			hwtstamp_ns = timespec64_to_ns(&ts);
			has_ts = true;
		}

		ret = eswm_xdp_run_verdict(ndev, gq, prog, &xdp_mb, xs, hwtstamp_ns,
							has_ts, &xdp_redirect);
		if (ret)
			goto err_halt;

next_desc:
		gq->cur = eswm_next_queue_index(gq, true, 1);
		desc = &gq->rx_ring[gq->cur];

		if (--boguscnt <= 0)
			break;
	}

	if (xdp_redirect)
		xdp_do_flush();

	num = eswm_get_num_cur_queues(gq);
	ret = eswm_pp_alloc_rx_bufs(gq, gq->dirty, num);
	if (ret < 0)
		goto err_halt;

	ret = eswm_gwca_queue_ext_ts_fill(ndev, gq, gq->dirty, num);
	if (ret < 0)
		goto err_halt;

	gq->dirty = eswm_next_queue_index(gq, false, num);
	*quota   -= limit - boguscnt;
	return boguscnt <= 0;

err_halt:
	eswm_gwca_halt(rdev->priv);
	return false;
}

static void eswm_xdp_clean_tx_queue(struct net_device *ndev)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	struct eswm_gwca_queue *txq;
	struct eswm_ext_desc *desc;
	unsigned int j;

	txq = rdev->tx_queues[0];
	if (!txq)
		return;

	for (j = 0; j < txq->ring_size; j++) {
		struct xdp_frame *xdpf = txq->xdpf[j];

		if (xdpf) {
			if (txq->unmap_addrs[j]) {
				dma_unmap_single(ndev->dev.parent,
							txq->unmap_addrs[j],
							xdpf->len, DMA_TO_DEVICE);
				txq->unmap_addrs[j] = 0;
			}

			xdp_return_frame(xdpf);
			txq->xdpf[j] = NULL;
		} else if (txq->unmap_addrs[j]) {
			desc = &txq->tx_ring[j];
			dma_unmap_page(ndev->dev.parent, txq->unmap_addrs[j],
						le16_to_cpu(desc->desc.info_ds),
						DMA_TO_DEVICE);
			txq->unmap_addrs[j] = 0;
		}

		txq->is_xdp_tx[j] = false;
		txq->xdp_from_ndo[j] = false;
	}

	txq->cur = 0;
	txq->dirty = 0;
}

static void eswm_xdp_clean_rx_queue(struct net_device *ndev)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	struct eswm_gwca_queue *gq = rdev->rx_queue;
	unsigned int i;

	if (!gq || !gq->pp)
		return;

	for (i = 0; i < gq->ring_size; i++) {
		if (!gq->rx_bufs[i])
			continue;

		page_pool_put_full_page(gq->pp, virt_to_head_page(gq->rx_bufs[i] - ESWM_XDP_HEADROOM), false);
		gq->rx_bufs[i] = NULL;
	}

	gq->cur = 0;
	gq->dirty = 0;
}

static void eswm_skb_clean_rx_queue(struct net_device *ndev,
					struct eswm_gwca_queue *gq)
{
	unsigned int i;

	for (i = 0; i < gq->ring_size; i++) {
		struct eswm_ext_ts_desc *desc = &gq->rx_ring[i];
		dma_addr_t dma_addr;

		if (!gq->rx_bufs[i])
			continue;

		dma_addr = eswm_desc_get_dptr(&desc->desc);
		dma_unmap_single(ndev->dev.parent, dma_addr,
					ESWM_MAP_BUF_SIZE, DMA_FROM_DEVICE);

		skb_free_frag(gq->rx_bufs[i]);
		gq->rx_bufs[i] = NULL;
	}

	gq->cur = 0;
	gq->dirty = 0;
}

static int eswm_reset_tx_queue(struct net_device *ndev,
					struct eswm_private *priv,
					struct eswm_gwca_queue *txq)
{
	int err;

	err = eswm_gwca_queue_format(ndev, priv, txq);
	if (err) {
		netdev_err(ndev, "Failed format tx_queue\n");
		return err;
	}

	txq->cur = 0;
	txq->dirty = 0;

	return 0;
}

static int eswm_xdp_set_prog(struct net_device *ndev, struct bpf_prog *prog,
					struct netlink_ext_ack *extack)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	struct eswm_private *priv = rdev->priv;
	struct eswm_gwca_queue *gq = rdev->rx_queue;
	struct bpf_prog *old_prog;
	bool if_running, need_update;
	unsigned int i;
	unsigned long flags;
	int err = 0;

	if_running  = netif_running(ndev);
	need_update = !!rdev->xdp_prog != !!prog;

	if (prog && ndev->mtu > ESWM_XDP_MAX_MTU && !prog->aux->xdp_has_frags) {
		NL_SET_ERR_MSG_MOD(extack, "MTU too large for XDP without frags support");
		return -EOPNOTSUPP;
	}

	if (if_running && need_update) {
		napi_disable(&rdev->napi);

		spin_lock_irqsave(&priv->lock, flags);
		eswm_enadis_data_irq(priv, rdev->tx_queues[0]->index, false);
		eswm_enadis_data_irq(priv, rdev->rx_queue->index, false);
		spin_unlock_irqrestore(&priv->lock, flags);

		synchronize_rcu();
	}

	if (need_update) {
		if (rdev->xdp_prog) {
			eswm_xdp_clean_tx_queue(ndev);
			eswm_xdp_clean_rx_queue(ndev);
			synchronize_net();
		} else {
			eswm_skb_clean_rx_queue(ndev, gq);
		}

		err = eswm_reset_tx_queue(ndev, priv, rdev->tx_queues[0]);
		if (err)
			goto out;
	}

	if (prog) {
		err = xdp_rxq_info_reg(&gq->xdp_rxq, ndev, 0, rdev->napi.napi_id);
		if (err < 0) {
			netdev_err(ndev, "Failed to register XDP rxq info: %d\n", err);
			goto out;
		} else {
			netdev_info(ndev, "Register successfully rxq info");
		}

		err = xdp_rxq_info_reg_mem_model(&gq->xdp_rxq, MEM_TYPE_PAGE_POOL, gq->pp);
		if (err < 0) {
			netdev_err(ndev, "Failed to register XDP memory model: %d\n", err);
			goto err_unreg_rxq;
		} else {
			netdev_info(ndev, "XDP running with MEM_TYPE_PAGE_POOL on RxQ-0\n");
		}

		err = eswm_pp_alloc_rx_bufs(gq, 0, gq->ring_size);
		if (err)
			goto err_unreg_rxq;

		gq->rx_use_page_pool = true;

		err = eswm_gwca_queue_ext_ts_format(ndev, priv, gq);
		if (err)
			goto err_drain_pp;
	} else {
		gq->rx_use_page_pool = false;

		if (xdp_rxq_info_is_reg(&gq->xdp_rxq))
			xdp_rxq_info_unreg(&gq->xdp_rxq);

		err = eswm_gwca_queue_alloc_rx_buf(gq, 0, gq->ring_size);
		if (err)
			goto out;

		err = eswm_gwca_queue_ext_ts_format(ndev, priv, gq);
		if (err)
			goto out;
	}

	old_prog = xchg(&rdev->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

out:
	if (if_running && need_update) {
		spin_lock_irqsave(&priv->lock, flags);
		eswm_enadis_data_irq(priv, rdev->tx_queues[0]->index, true);
		eswm_enadis_data_irq(priv, rdev->rx_queue->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);

		napi_enable(&rdev->napi);
	}

	return err;

err_drain_pp:
	gq->rx_use_page_pool = false;
	for (i = 0; i < gq->ring_size; i++) {
		if (!gq->rx_bufs[i])
			continue;
		page_pool_put_full_page(gq->pp, virt_to_head_page(gq->rx_bufs[i] - ESWM_XDP_HEADROOM), false);
		gq->rx_bufs[i] = NULL;
	}

err_unreg_rxq:
	xdp_rxq_info_unreg(&gq->xdp_rxq);
	goto out;
}

int eswm_xdp_xmit(struct net_device *ndev, int num_frame,
		   struct xdp_frame **frames, u32 flags)
{
	struct eswm_device *rdev = netdev_priv(ndev);
	int i, sent = 0, err = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	spin_lock_bh(&rdev->xdp_tx_lock);
	for (i = 0; i < num_frame; i++) {
		err = eswm_xdp_submit_frame(rdev, frames[i], true);
		if (err)
			break;
		sent++;
	}
	spin_unlock_bh(&rdev->xdp_tx_lock);

	if (err == -ENOSPC)
		napi_schedule(&rdev->napi);

	return sent;
}

int eswm_bpf(struct net_device *ndev, struct netdev_bpf *bpf)
{
	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return eswm_xdp_set_prog(ndev, bpf->prog, bpf->extack);
	default:
		return -EOPNOTSUPP;
	}
}

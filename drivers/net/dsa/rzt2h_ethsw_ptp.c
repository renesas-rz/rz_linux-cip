// SPDX-License-Identifier: GPL-2.0-only
/*
 * This implements the API for managing DSA RZT2H PTP.
 *
 * Author: Long Luu <long.luu.ur@renesas.com>
 *
 */

#include <linux/ptp_classify.h>
#include <linux/net/renesas/rzt2h-ethss.h>
#include <linux/net/renesas/rzt2h_timer_hwtstamp.h>
#include "rzt2h_ethsw.h"

#define ptp_to_ethsw(ptp)					\
	container_of(ptp, struct ethsw, ptp_clock_info)

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

int ethsw_get_ts_info(struct dsa_switch *ds, int port,
		      struct kernel_ethtool_ts_info *info)
{
	struct ethsw *ethsw = ds->priv;

	info->phc_index = ethsw->ptp_clock ?
		ptp_clock_index(ethsw->ptp_clock) : -1;
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	/* enabled tx timestamping */
	info->tx_types = BIT(HWTSTAMP_TX_ON);

	/* L2 & L4 PTPv2 event rx messages are timestamped */
	info->rx_filters = BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

/* Enabling/disabling TX and RX HW timestamping for different PTP messages is
 * not available in the switch. Thus, this function only serves as a check if
 * the user requested what is actually available or not
 */
static int ethsw_set_hwtstamp_config(struct ethsw *ethsw, int port,
				     struct hwtstamp_config *config)
{
	struct ethsw_port_hwtstamp *ps = &ethsw->port_hwtstamp[port];
	bool tx_tstamp_enable = false;
	bool rx_tstamp_enable = false;

	/* Interaction with the timestamp hardware is prevented here.  It is
	 * enabled when this config function ends successfully
	 */
	clear_bit_unlock(ETHSW_HWTSTAMP_ENABLED, &ps->state);

	switch (config->tx_type) {
	case HWTSTAMP_TX_ON:
		tx_tstamp_enable = true;
		break;

	/* TX HW timestamping can't be disabled on the switch */
	case HWTSTAMP_TX_OFF:
		config->tx_type = HWTSTAMP_TX_ON;
		break;

	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	/* RX HW timestamping can't be disabled on the switch */
	case HWTSTAMP_FILTER_NONE:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;

	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		rx_tstamp_enable = true;
		break;

	/* RX HW timestamping can't be enabled for all messages on the switch */
	case HWTSTAMP_FILTER_ALL:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;

	default:
		return -ERANGE;
	}

	if (!tx_tstamp_enable)
		return -ERANGE;

	if (!rx_tstamp_enable)
		return -ERANGE;

	/* If this point is reached, then the requested hwtstamp config is
	 * compatible with the hwtstamp offered by the switch.  Therefore,
	 * enable the interaction with the HW timestamping
	 */
	set_bit(ETHSW_HWTSTAMP_ENABLED, &ps->state);

	return 0;
}

int ethsw_port_hwtstamp_set(struct dsa_switch *ds, int port,
			    struct ifreq *ifr)
{
	struct ethsw *ethsw = ds->priv;
	struct ethsw_port_hwtstamp *ps;
	struct hwtstamp_config config;
	int err;

	ps = &ethsw->port_hwtstamp[port];

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = ethsw_set_hwtstamp_config(ethsw, port, &config);
	if (err)
		return err;

	/* Save the chosen configuration to be returned later */
	memcpy(&ps->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

int ethsw_port_hwtstamp_get(struct dsa_switch *ds, int port,
			    struct ifreq *ifr)
{
	struct ethsw *ethsw = ds->priv;
	struct ethsw_port_hwtstamp *ps;
	struct hwtstamp_config *config;

	ps = &ethsw->port_hwtstamp[port];
	config = &ps->tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/* Returns a pointer to the PTP header if the caller should time stamp, or NULL
 * if the caller should not.
 */
static struct ptp_header *ethsw_should_tstamp(struct ethsw *ethsw,
					      int port, struct sk_buff *skb,
					      unsigned int type)
{
	struct ethsw_port_hwtstamp *ps = &ethsw->port_hwtstamp[port];
	struct ptp_header *hdr;

	hdr = ptp_parse_header(skb, type);
	if (!hdr)
		return NULL;

	if (!test_bit(ETHSW_HWTSTAMP_ENABLED, &ps->state))
		return NULL;

	return hdr;
}

static void ethsw_get_rxts(struct ethsw *ethsw,
			   struct ethsw_port_hwtstamp *ps,
			   struct sk_buff *skb, struct sk_buff_head *rxq,
			   int port)
{
	struct skb_shared_hwtstamps *shwt;
	struct sk_buff_head received;
	unsigned long flags;
	struct phylink_pcs *pcs = ethsw->pcs[port];
	struct ethss_port *ethss_port = phylink_pcs_to_ethss_port(pcs);

	/* Construct Rx timestamps for all received PTP packets. */
	__skb_queue_head_init(&received);
	spin_lock_irqsave(&rxq->lock, flags);
	skb_queue_splice_tail_init(rxq, &received);
	spin_unlock_irqrestore(&rxq->lock, flags);

	for (; skb; skb = __skb_dequeue(&received)) {
		u64 sec_read, ns_read, ns;

		/* Get nanoseconds from ptp packet */
		ns =  ETHSW_SKB_CB(skb)->rxtstamp;

		/* Get seconds part from reading ETHSW timer directly */
		mutex_lock(&ethsw->ptp_lock);
		ethsw_time_get(ethsw->base, &ns_read, ethsw->ethsw_ptp_timer);
		mutex_unlock(&ethsw->ptp_lock);

		sec_read = ns_read / 1000000000ULL;
		ns_read = ns_read % 1000000000ULL;
		if (ns_read < ns)
			sec_read -= 1;

		/* Calculate the rxtstamp of ptp packet */
		ns += sec_read * 1000000000ULL;

		if (ethss_port->speed == SPEED_10)
			ns -= INGRESS_DELAY_10M;
		else if (ethss_port->speed == SPEED_100)
			ns -= INGRESS_DELAY_100M;
		else if (ethss_port->speed == SPEED_1000)
			ns -= INGRESS_DELAY_1G;

		/* Save time stamp */
		shwt = skb_hwtstamps(skb);
		memset(shwt, 0, sizeof(*shwt));
		shwt->hwtstamp = ns_to_ktime(ns);
		netif_rx(skb);
	}
}

static void ethsw_rxtstamp_work(struct ethsw *ethsw,
				struct ethsw_port_hwtstamp *ps,
				int port)
{
	struct sk_buff *skb;

	skb = skb_dequeue(&ps->rx_queue);
	if (skb)
		ethsw_get_rxts(ethsw, ps, skb, &ps->rx_queue, port);
}

static long ethsw_hwtstamp_work(struct ptp_clock_info *ptp)
{
	struct ethsw *ethsw = ptp_to_ethsw(ptp);
	struct dsa_switch *ds = &ethsw->ds;
	int i;

	for (i = 0; i < ds->num_ports; i++) {
		struct ethsw_port_hwtstamp *ps;

		if (!dsa_is_user_port(ds, i))
			continue;

		ps = &ethsw->port_hwtstamp[i];

		ethsw_rxtstamp_work(ethsw, ps, i);
	}

	return -1;
}

void ethsw_port_txtstamp(struct dsa_switch *ds, int port,
			 struct sk_buff *skb)
{
	struct ethsw *ethsw = ds->priv;
	struct ethsw_port_hwtstamp *ps;
	struct ptp_header *hdr;
	struct sk_buff *clone;
	unsigned int type;

	ps = &ethsw->port_hwtstamp[port];

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return;

	/* Make sure the message is a PTP message that needs to be timestamped
	 * and the interaction with the HW timestamping is enabled. If not, stop
	 * here
	 */
	hdr = ethsw_should_tstamp(ethsw, port, skb, type);
	if (!hdr)
		return;

	clone = skb_clone_sk(skb);
	if (!clone)
		return;

	if (test_and_set_bit_lock(ETHSW_HWTSTAMP_TX_IN_PROGRESS,
				  &ps->state)) {
		kfree_skb(clone);
		return;
	}

	ps->tx_skb = clone;
	ps->ts_status = 0;
	ps->txtstamp = 0;

	/* store the number of ticks occurred since system start-up till this
	 * moment
	 */
	ps->tx_tstamp_start = jiffies;
}

bool ethsw_port_rxtstamp(struct dsa_switch *ds, int port,
			 struct sk_buff *skb, unsigned int type)
{
	struct ethsw *ethsw = ds->priv;
	struct ethsw_port_hwtstamp *ps;
	struct ptp_header *hdr;

	ps = &ethsw->port_hwtstamp[port];

	/* This check only fails if the user did not initialize hardware
	 * timestamping beforehand.
	 */
	if (ps->tstamp_config.rx_filter != HWTSTAMP_FILTER_PTP_V2_EVENT)
		return false;

	/* Make sure the message is a PTP message that needs to be timestamped
	 * and the interaction with the HW timestamping is enabled. If not, stop
	 * here
	 */
	hdr = ethsw_should_tstamp(ethsw, port, skb, type);
	if (!hdr)
		return false;

	skb_queue_tail(&ps->rx_queue, skb);

	ptp_schedule_worker(ethsw->ptp_clock, 0);

	return true;
}

static void ethsw_hwtstamp_port_setup(struct ethsw *ethsw, int port)
{
	struct ethsw_port_hwtstamp *ps = &ethsw->port_hwtstamp[port];

	skb_queue_head_init(&ps->rx_queue);
}

int ethsw_hwtstamp_setup(struct ethsw *ethsw)
{
	struct dsa_switch *ds = &ethsw->ds;
	int i;

	/* Initialize timestamping ports. */
	for (i = 0; i < ds->num_ports; ++i) {
		if (!dsa_is_user_port(ds, i))
			continue;

		ethsw_hwtstamp_port_setup(ethsw, i);
	}

	/* Initialize TSM hardware */
	ethsw_reg_rmw(ethsw, ETHSW_TSM_CONFIG,
		      ETHSW_TSM_CONFIG_IRQ_TX_MASK,
		      0x7 << ETHSW_TSM_CONFIG_IRQ_TX_POS);

	ethsw_reg_rmw(ethsw, ETHSW_TSM_CONFIG,
		      ETHSW_TSM_CONFIG_IRQ_EN,
		      ETHSW_TSM_CONFIG_IRQ_EN);

	ethsw_reg_rmw(ethsw, ETHSW_INT_CONFIG,
		      ETHSW_INT_CONFIG_IRQ_EN | ETHSW_INT_CONFIG_TSM_INT,
		      ETHSW_INT_CONFIG_IRQ_EN | ETHSW_INT_CONFIG_TSM_INT);

	return 0;
}

void ethsw_hwtstamp_free(struct ethsw *ethsw)
{
	/* Nothing todo */
}

int ethsw_isr_tsm(struct ethsw *ethsw)
{
	struct dsa_switch *ds = &ethsw->ds;
	struct ethsw_port_hwtstamp *ps;
	u32 irq_stat_ack, port;
	int ts_status;
	int ret = IRQ_HANDLED;

	irq_stat_ack = ethsw_reg_readl(ethsw, ETHSW_TSM_IRQ_STAT_ACK);
	/* Clear Interrupt */
	ethsw_reg_writel(ethsw, ETHSW_TSM_IRQ_STAT_ACK, irq_stat_ack);

	for (port = 0; port < ds->num_ports; port++) {
		if (irq_stat_ack & (BIT(port) << ETHSW_TSM_IRQ_STAT_ACK_TX_POS)) {
			ps = &ethsw->port_hwtstamp[port];

			ethsw_reg_writel(ethsw, ETHSW_TS_FIFO_READ_CTRL,
					 port & ETHSW_TS_FIFO_READ_PORTMASK);
			ts_status = ethsw_reg_readl(ethsw, ETHSW_TS_FIFO_READ_CTRL)
				    & ETHSW_TS_FIFO_READ_TS_VALID;

			if (!ts_status)
				continue;

			ret = IRQ_WAKE_THREAD;

			ps->ts_status = 1;
			ps->txtstamp = ethsw_reg_readl(ethsw, ETHSW_TS_FIFO_READ_TIMESTAMP);
		}
	}

	return ret;
}

void ethsw_isr_tsm_thread(struct ethsw *ethsw)
{
	struct dsa_switch *ds = &ethsw->ds;
	struct ethsw_port_hwtstamp *ps;
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *tmp_skb;
	u64 ns = 0, sec_read, ns_read;
	int port;
	struct phylink_pcs *pcs;
	struct ethss_port *ethss_port;

	for (port = 0; port < ds->num_ports; port++) {
		ps = &ethsw->port_hwtstamp[port];
		if (!ps->ts_status)
			continue;

		if (!test_bit(ETHSW_HWTSTAMP_TX_IN_PROGRESS, &ps->state))
			continue;

		if (!ps->tx_skb)
			continue;

		/* Check whether the operation of reading the tx timestamp has
		 * exceeded its allowed period
		 */
		if (time_is_before_jiffies(ps->tx_tstamp_start +
					   TX_TSTAMP_TIMEOUT)) {
			dev_err(ethsw->dev, "Timeout while waiting for Tx timestamp!\n");

			dev_kfree_skb_any(ps->tx_skb);
			ps->tx_skb = NULL;
			ps->ts_status = 0;
			ps->txtstamp = 0;
			clear_bit_unlock(ETHSW_HWTSTAMP_TX_IN_PROGRESS, &ps->state);
		}

		/* Available ts */
		/* Get nanoseconds from ts fifo */
		ns = ps->txtstamp;

		/* Get seconds part from reading ETHSW timer directly */
		mutex_lock(&ethsw->ptp_lock);
		ethsw_time_get(ethsw->base, &ns_read, ethsw->ethsw_ptp_timer);
		mutex_unlock(&ethsw->ptp_lock);

		sec_read = ns_read / 1000000000ULL;
		ns_read = ns_read % 1000000000ULL;
		if (ns_read < ns)
			sec_read -= 1;

		/* Calculate the txtstamp of ptp packet */
		ns += sec_read * 1000000000ULL;

		/* Adding PHY delay */
		pcs = ethsw->pcs[port];
		ethss_port = phylink_pcs_to_ethss_port(pcs);

		if (ethss_port->speed == SPEED_10)
			ns += EGRESS_DELAY_10M;
		else if (ethss_port->speed == SPEED_100)
			ns += EGRESS_DELAY_100M;
		else if (ethss_port->speed == SPEED_1000)
			ns += EGRESS_DELAY_1G;

		/* Now we have the timestamp in nanoseconds, store it in the correct
		 * structure in order to send it to the user
		 */
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ns_to_ktime(ns);

		tmp_skb = ps->tx_skb;
		ps->tx_skb = NULL;
		ps->ts_status = 0;
		ps->txtstamp = 0;

		/* skb_complete_tx_timestamp() frees up the client to make another
		 * timestampable transmit.  We have to be ready for it by clearing the
		 * ps->tx_skb "flag" beforehand
		 */
		clear_bit_unlock(ETHSW_HWTSTAMP_TX_IN_PROGRESS, &ps->state);

		/* Deliver a clone of the original outgoing tx_skb with tx hwtstamp */
		skb_complete_tx_timestamp(tmp_skb, &shhwtstamps);
	}
}

static int ethsw_timer_adjust_freq(struct ptp_clock_info *ptp, long scaled_ppm)
{
	s32 ppb = scaled_ppm_to_ppb(scaled_ppm);
	struct ethsw *ethsw = ptp_to_ethsw(ptp);

	mutex_lock(&ethsw->ptp_lock);
	ethsw_time_adjust_frequency(ethsw->base, ethsw->ethsw_ptp_timer,
				    ppb, ethsw->clk_ptp_rate);
	mutex_unlock(&ethsw->ptp_lock);

	return 0;
}

static int ethsw_timer_adjust_phase(struct ptp_clock_info *ptp, s32 phase)
{
	struct ethsw *ethsw = ptp_to_ethsw(ptp);

	mutex_lock(&ethsw->ptp_lock);
	ethsw_time_adjust_offset(ethsw->base, ethsw->ethsw_ptp_timer, phase);
	mutex_unlock(&ethsw->ptp_lock);

	return 0;
}

static int ethsw_timer_adjust_time(struct ptp_clock_info *ptp, s64 delta)
{
	struct ethsw *ethsw = ptp_to_ethsw(ptp);
	u64 now;

	mutex_lock(&ethsw->ptp_lock);
	ethsw_time_get(ethsw->base, &now, ethsw->ethsw_ptp_timer);
	ethsw_time_set(ethsw->base, now + delta, ethsw->ethsw_ptp_timer);
	mutex_unlock(&ethsw->ptp_lock);

	return 0;
}

static int ethsw_timer_get_time(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ethsw *ethsw = ptp_to_ethsw(ptp);
	u64 ns = 0;

	mutex_lock(&ethsw->ptp_lock);
	ethsw_time_get(ethsw->base, &ns, ethsw->ethsw_ptp_timer);
	mutex_unlock(&ethsw->ptp_lock);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int ethsw_timer_set_time(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct ethsw *ethsw = ptp_to_ethsw(ptp);
	u64 ns = 0;

	ns = timespec64_to_ns(ts);

	mutex_lock(&ethsw->ptp_lock);
	ethsw_time_set(ethsw->base, ns, ethsw->ethsw_ptp_timer);
	mutex_unlock(&ethsw->ptp_lock);

	return 0;
}

/* structure describing a PTP hardware clock */
static struct ptp_clock_info ethsw_ptp_clock_ops = {
	.owner = THIS_MODULE,
	.name = "ethsw ptp",
	.max_adj = 100000000,
	.n_alarm = 0,
	.n_ext_ts = 0,
	.n_per_out = 0, /* will be overwritten in ethsw_ptp_register */
	.n_pins = 0,
	.pps = 0,
	.adjfine = ethsw_timer_adjust_freq,
	.adjphase = ethsw_timer_adjust_phase,
	.adjtime = ethsw_timer_adjust_time,
	.gettime64 = ethsw_timer_get_time,
	.settime64 = ethsw_timer_set_time,
	.do_aux_work = ethsw_hwtstamp_work,
};

/**
 * ethsw_ptp_register
 * @priv: driver private structure
 * Description: this function will register the ptp clock driver
 * to kernel. It also does some house keeping work.
 */
int ethsw_ptp_register(struct ethsw *ethsw)
{
	int ret = 0;

	mutex_init(&ethsw->ptp_lock);

	ethsw->ptp_clock_info = ethsw_ptp_clock_ops;
	ethsw->ptp_clock = ptp_clock_register(&ethsw->ptp_clock_info,
					      ethsw->dev);
	if (IS_ERR(ethsw->ptp_clock)) {
		dev_err(ethsw->dev, "ptp_clock_register failed\n");
		ethsw->ptp_clock = NULL;
		ret = PTR_ERR(ethsw->ptp_clock);
	} else if (ethsw->ptp_clock) {
		dev_err(ethsw->dev, "registered PTP clock\n");
		ret = 0;
	}

	return ret;
}

/**
 * ethsw_ptp_unregister
 * @priv: driver private structure
 * Description: this function will remove/unregister the ptp clock driver
 * from the kernel.
 */
void ethsw_ptp_unregister(struct ethsw *ethsw)
{
	if (ethsw->ptp_clock) {
		ptp_clock_unregister(ethsw->ptp_clock);
		ethsw->ptp_clock = NULL;
		dev_dbg(ethsw->dev, "Removed PTP HW clock successfully\n");
	}

	mutex_destroy(&ethsw->ptp_lock);
}

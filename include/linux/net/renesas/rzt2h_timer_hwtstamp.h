/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __RZT2H_TIMER_HWTSTAMP_H__
#define __RZT2H_TIMER_HWTSTAMP_H__

#define ETHSW_ATIME_CLK_RATE			125000000
#define ETHSW_ATIME_NUM(time)			(0x20 * (time))
#define ETHSW_ATIME_CTRL(time)			(0x680 + ETHSW_ATIME_NUM(time))
#define ETHSW_ATIME_CTRL_ENABLE			BIT(0)
#define ETHSW_ATIME_CTRL_CAPTURE		BIT(11)

#define ETHSW_ATIME(time)			(0x684 + ETHSW_ATIME_NUM(time))
#define ETHSW_ATIME_OFFSET(time)		(0x688 + ETHSW_ATIME(time))
#define ETHSW_ATIME_CORR(time)			(0x690 + ETHSW_ATIME_NUM(time))

#define ETHSW_ATIME_INC(time)			(0x694 + ETHSW_ATIME_NUM(time))
#define ETHSW_ATIME_INC_PERIOD_MASK		GENMASK(6, 0)
#define ETHSW_ATIME_INC_PERIOD_NORMAL		0x8
#define ETHSW_ATIME_INC_CORR_INC_MASK		GENMASK(14, 8)
#define ETHSW_ATIME_INC_CORR_INC_POS		8
#define ETHSW_ATIME_INC_OFFS_CORR_INC_MASK	GENMASK(22, 16)
#define ETHSW_ATIME_INC_OFFS_CORR_INC_POS	16
#define ETHSW_ATIME_CLOCK_OFFS_CORR		4

#define ETHSW_ATIME_SEC(time)			(0x698 + ETHSW_ATIME_NUM(time))
#define ETHSW_ATIME_OFFS_CORR(time)		(0x69C + ETHSW_ATIME(time))

struct ethsw_skb_cb {
	u32 rxtstamp;
};

#define ETHSW_SKB_CB(skb) \
	((struct ethsw_skb_cb *)((skb)->cb))

void ethsw_time_init(void __iomem *ioaddr, u32 timer);
void ethsw_time_set(void __iomem *ioaddr, u64 systime, u32 timer);
void ethsw_time_get(void __iomem *ioaddr, u64 *systime, u32 timer);
void ethsw_time_adjust_frequency(void __iomem *ioaddr, u32 timer,
				s64 i_ppb, u32 clk_ptp_rate);
void ethsw_time_adjust_offset(void __iomem *ioaddr, u32 timer, s64 ofs);
#endif

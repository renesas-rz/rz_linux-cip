// SPDX-License-Identifier: GPL-2.0-only
/*
 * This implements the API for managing HW timestamp of RZT2H PTP Timer.
 *
 * Author: Long Luu <long.luu.ur@renesas.com>
 *
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/net/renesas/rzt2h_timer_hwtstamp.h>

static void ethsw_timer_reg_rmw(void __iomem *ioaddr, int offset, u32 mask, u32 val)
{
	u32 reg;

	reg = readl(ioaddr + offset);
	reg &= ~mask;
	reg |= val;
	writel(reg, ioaddr + offset);
}

void ethsw_time_init(void __iomem *ioaddr, u32 timer)
{
	/* Initialize ETHSW Timer increments with every clock cycle */
	ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_INC(timer),
			    ETHSW_ATIME_INC_PERIOD_MASK, ETHSW_ATIME_INC_PERIOD_NORMAL);
	/* Start timer */
	ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_CTRL(timer), ETHSW_ATIME_CTRL_ENABLE, 1);
}
EXPORT_SYMBOL(ethsw_time_init);

void ethsw_time_set(void __iomem *ioaddr, u32 sec, u32 nsec, u32 timer)
{
	writel(sec, ioaddr + ETHSW_ATIME_SEC(timer));
	writel(nsec, ioaddr + ETHSW_ATIME(timer));
}
EXPORT_SYMBOL(ethsw_time_set);

void ethsw_time_get(void __iomem *ioaddr, u64 *systime, u32 timer)
{
	u64 nsec, sec;
	u32 status;
	int err;

	/* issue read command */
	ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_CTRL(timer),
			    ETHSW_ATIME_CTRL_CAPTURE, ETHSW_ATIME_CTRL_CAPTURE);

	err = readl_poll_timeout(ioaddr + ETHSW_ATIME_CTRL(timer), status,
				 !(status & ETHSW_ATIME_CTRL_CAPTURE), 10,
				 10000);

	if (err)
		pr_info("Read command timeout\n");

	/* Get the nano second value */
	nsec = readl(ioaddr + ETHSW_ATIME(timer));
	/* Get the second value */
	sec = readl(ioaddr + ETHSW_ATIME_SEC(timer));

	if (systime)
		*systime = nsec + (sec * 1000000000ULL);
}
EXPORT_SYMBOL(ethsw_time_get);

void ethsw_time_adjust_inc(void __iomem *ioaddr, u32 tick_diff,
			   u32 neg_adj, u32 clk_ptp_rate, u32 timer)
{
	u32 corr;

	/* Stop timer */
	ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_CTRL(timer), ETHSW_ATIME_CTRL_ENABLE, 0);

	/* Calculate timer increment:
	 * Formula is:
	 * ATIME_CORR = (delta_inc * clk_ptp_rate / delta_tick) - 1;
	 * where delta_inc = CORR_INC - INC_PERIOD_NORMAL
	 * We choose delta_inc = 7, INC_PERIOD_NORMAL always = 8
	 * => To speed up timer : CORR_INC = 15
	 *    To slow dowm timer: CORR_INC = 1
	 */

	if (neg_adj)
		ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_INC(timer),
				    ETHSW_ATIME_CORR_INC_MASK, ETHSW_ATIME_CORR_INC_SLOW_DOWN);
	else
		ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_INC(timer),
				    ETHSW_ATIME_CORR_INC_MASK, ETHSW_ATIME_CORR_INC_SPEED_UP);

	corr = (7 * clk_ptp_rate) / tick_diff - 1;
	writel(corr, ioaddr + ETHSW_ATIME_CORR(timer));

	/* Start timer */
	ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_CTRL(timer), ETHSW_ATIME_CTRL_ENABLE, 1);
}
EXPORT_SYMBOL(ethsw_time_adjust_inc);

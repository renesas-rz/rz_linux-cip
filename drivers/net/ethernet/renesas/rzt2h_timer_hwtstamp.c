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

void ethsw_time_set(void __iomem *ioaddr, u64 systime, u32 timer)
{
	struct timespec64 ts;

	/* Add Timer load delay */
	systime = systime + 94;
	ts = ns_to_timespec64(systime);
	writel(ts.tv_sec, ioaddr + ETHSW_ATIME_SEC(timer));
	writel(ts.tv_nsec, ioaddr + ETHSW_ATIME(timer));
}
EXPORT_SYMBOL(ethsw_time_set);

void ethsw_time_get(void __iomem *ioaddr, u64 *systime, u32 timer)
{
	u64 nsec, sec;
	s64 sign_nsec;
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

	sign_nsec = (s64)nsec;
	/* Subtract Timer capture delay */
	sign_nsec = sign_nsec - 75;

	if (sign_nsec < 0) {
		sec -= 1;
		sign_nsec += 1000000000;
	}

	nsec = (u64)sign_nsec;

	if (systime)
		*systime = nsec + (sec * 1000000000ULL);
}
EXPORT_SYMBOL(ethsw_time_get);

void ethsw_time_adjust_frequency(void __iomem *ioaddr, u32 timer,
				s64 i_ppb, u32 clk_ptp_rate)
{
	u8 corr_inc;
	u32 corr;

	if (i_ppb > 0) {
		corr_inc = 9;
		corr = (u32)(clk_ptp_rate / i_ppb);
	} else {
		corr_inc = 7;
		corr = (u32)(clk_ptp_rate / i_ppb * -1);
	}

	ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_INC(timer),
			    ETHSW_ATIME_INC_CORR_INC_MASK,
			    corr_inc << ETHSW_ATIME_INC_CORR_INC_POS);
	writel(corr, ioaddr + ETHSW_ATIME_CORR(timer));
}
EXPORT_SYMBOL(ethsw_time_adjust_frequency);

void ethsw_time_adjust_offset(void __iomem *ioaddr, u32 timer, s64 ofs)
{
	u64 ofs_abs, current_time;
	u32 offs_corr, offset, atime_inc_offs, clock_correction;
	u8 offs_inc;

	if (0 > ofs)
		ofs_abs = -ofs;
	else
		ofs_abs = ofs;

	if (ofs_abs > 10000000) {
		/* offset is more than 10ms */
		ethsw_time_get(ioaddr, &current_time, timer);

		if(0 > ofs)
			current_time -= ofs_abs;
		else
			current_time += ofs_abs;

		ethsw_time_set(ioaddr, current_time, timer);
	} else {
		if (ofs_abs >= 1000000)
			/* offset >= 1ms  */
			clock_correction = 5;
		else
			/* offset < 1ms  */
			clock_correction = 1;

		if(0 > ofs)
			/* Slow down */
			atime_inc_offs = ETHSW_ATIME_INC_PERIOD_NORMAL - clock_correction;
		else
			/* Speed up */
			atime_inc_offs = ETHSW_ATIME_INC_PERIOD_NORMAL + clock_correction;
		offs_inc = atime_inc_offs & 0x7F;
		offs_corr = ETHSW_ATIME_CLOCK_OFFS_CORR;
		offset = (u32)(ofs_abs / clock_correction);

		ethsw_timer_reg_rmw(ioaddr, ETHSW_ATIME_INC(timer),
				    ETHSW_ATIME_INC_OFFS_CORR_INC_MASK,
				    offs_inc << ETHSW_ATIME_INC_OFFS_CORR_INC_POS);
		writel(offs_corr, ioaddr + ETHSW_ATIME_OFFS_CORR(timer));
		writel(offset, ioaddr + ETHSW_ATIME_OFFSET(timer));
	}
}
EXPORT_SYMBOL(ethsw_time_adjust_offset);

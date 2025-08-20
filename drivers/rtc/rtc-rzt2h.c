// SPDX-License-Identifier: GPL-2.0
/*
 * On-Chip RTC Support available on RZ/T2H SoC
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/bcd.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/rtc.h>

/* Control registers. */
#define RTCA0CTL0	0x00
#define RTCA0SLSB	BIT(4)
#define RTCA0AMPM	BIT(5)
#define RTCA0CEST	BIT(6)
#define RTCA0CE		BIT(7)
#define RTCA0CTL1	0x04
#define RTCA0CT		GENMASK(2, 0)
#define DAY_INT		0x06
#define RTCA01SE	BIT(3)
#define RTCA0ALME	BIT(4)
#define RTCA01HZE	BIT(5)
#define RTCA0CTL2	0x08
#define RTCA0WAIT	BIT(0)
#define RTCA0WST	BIT(1)
#define RTCA0RSUB	BIT(2)
#define RTCA0RSST	BIT(3)
#define RTCA0WSST	BIT(4)

/* Count Buffer Register */
#define RTCA0SEC	0x14
#define RTCA0MIN	0x18
#define RTCA0HOUR	0x1c
#define RTCA0WEEK	0x20
#define RTCA0DAY	0x24
#define RTCA0MONTH	0x28
#define RTCA0YEAR	0x2c

/* Count Compare Register */
#define RTCA0SCMP	0x3c
#define MAIN_CLK	25000000
#define RTC_CLK		(MAIN_CLK / 128)

/* Count Register */
#define RTCA0SECC	0x4c
#define RTCA0MINC	0x50
#define RTCA0HOURC	0x54
#define RTCA0WEEKC	0x58
#define RTCA0DAYC	0x5c
#define RTCA0MONC	0x60
#define RTCA0YEARC	0x64

#define HOUR_MASK	GENMASK(4, 0)
#define PM_FLAG		BIT(5)

/* Alarm registers. */
#define RTCA0ALM	0x40
#define RTCA0ALH	0x44
#define RTCA0ALW	0x48

struct rzt2h_rtc {
	void __iomem		*rbase;
	struct rtc_time		alarm_time;
	u32			nday_int_left;
	unsigned long		regsize;
	struct resource		*res;
	int			alarm_irq;
	int			periodic_irq;
	int			carry_irq;
	struct clk		*clk;
	struct rtc_device	*rtc_dev;
	spinlock_t		lock;
	unsigned short		periodic_freq;
};

static void rzt2h_rtc_update_bits(struct rzt2h_rtc *rtc, u32 offset,  u32 mask, u32 val)
{
	writel((readl(rtc->rbase + offset) & ~mask) | val, rtc->rbase + offset);
}

static void rzt2h_rtc_set_bit(struct rzt2h_rtc *rtc, u32 offset,  u32 mask)
{
	writel(readl(rtc->rbase + offset) | mask, rtc->rbase + offset);
}

static void rzt2h_rtc_clear_bit(struct rzt2h_rtc *rtc, u32 offset,  u32 mask)
{
	writel(readl(rtc->rbase + offset) & ~mask, rtc->rbase + offset);
}

static u32 rzt2h_rtc_get_bit(struct rzt2h_rtc *rtc, u32 offset,  u32 mask)
{
	return readl(rtc->rbase + offset) & mask;
}

static irqreturn_t rzt2h_rtc_carry_isr(int irq, void *data)
{
	struct rzt2h_rtc *rtc = data;

	spin_lock(&rtc->lock);

	;

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t rzt2h_rtc_alarm_isr(int irq, void *data)
{
	struct rzt2h_rtc *rtc = data;

	spin_lock(&rtc->lock);

	dev_info(&rtc->rtc_dev->dev, "Alarm occurred!\n");
	/* Turn off repeating weekly alarms */
	rzt2h_rtc_clear_bit(rtc, RTCA0CTL1, RTCA0ALME);
	/* Don't update RTC_AF and RTC_IRQF avoid calling set_alarm again. */

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t rzt2h_rtc_periodic_isr(int irq, void *data)
{
	struct rzt2h_rtc *rtc = data;

	spin_lock(&rtc->lock);

	rtc->nday_int_left--;
	/* If there's less than 1 week left, alarm will be enabled,
	 * also, periodic interrupt will be not needed anymore.
	 */
	if ((rtc->nday_int_left / 7) == 0) {
		rzt2h_rtc_set_bit(rtc, RTCA0CTL1, RTCA0ALME);
		rtc->nday_int_left = 0;
		rzt2h_rtc_clear_bit(rtc, RTCA0CTL1, RTCA0CT);
	}

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static inline void rzt2h_rtc_setaie(struct device *dev, unsigned int enable)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);

	spin_lock_irq(&rtc->lock);

	if (enable)
		rzt2h_rtc_set_bit(rtc, RTCA0CTL1, RTCA0ALME);
	else
		rzt2h_rtc_clear_bit(rtc, RTCA0CTL1, RTCA0ALME);

	spin_unlock_irq(&rtc->lock);
}

static int rzt2h_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	tmp = readb(rtc->rbase + RTCA0CTL1);
	seq_printf(seq, "carry_IRQ\t: %s\n", (tmp & RTCA01SE) ? "yes" : "no");

	tmp = readb(rtc->rbase + RTCA0CTL1);
	seq_printf(seq, "periodic_IRQ\t: %s\n", (tmp & RTCA0CT) ? "yes" : "no");

	return 0;
}

static inline void rzt2h_rtc_setcie(struct device *dev, unsigned int enable)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);

	spin_lock_irq(&rtc->lock);

	if (enable)
		rzt2h_rtc_set_bit(rtc, RTCA0CTL1, RTCA01SE);
	else
		rzt2h_rtc_clear_bit(rtc, RTCA0CTL1, RTCA01SE);

	spin_unlock_irq(&rtc->lock);
}

static int rzt2h_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	rzt2h_rtc_setaie(dev, enabled);

	return 0;
}

static int rzt2h_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);
	int hr, pm_flag;

	do {
		spin_lock_irq(&rtc->lock);

		/* Confirm that writing counter buffer RTCA0SEC to RTCA0HOUR
		 * (a.k.a set_time rtc operation) has ended. This confirmation
		 * is only necessary when reading the time immediately after
		 * setting. Normally, just get the time, this action is wasted.
		 */
		while (rzt2h_rtc_get_bit(rtc, RTCA0CTL2, RTCA0WST))
			;

		tm->tm_sec = bcd2bin(readl(rtc->rbase + RTCA0SECC));
		tm->tm_min = bcd2bin(readl(rtc->rbase + RTCA0MINC));
		if (rzt2h_rtc_get_bit(rtc, RTCA0CTL0, RTCA0AMPM))
			/* This is 24h format */
			tm->tm_hour = bcd2bin(readl(rtc->rbase + RTCA0HOURC));
		else {
			/* This is 12h format */
			hr = readl(rtc->rbase + RTCA0HOURC);
			pm_flag = !!(hr & PM_FLAG);
			hr = bcd2bin(hr & HOUR_MASK);
			tm->tm_hour = (hr - (hr / 12) * 12) + pm_flag * 12;
		}

		tm->tm_wday = bcd2bin(readl(rtc->rbase + RTCA0WEEKC));
		tm->tm_mday = bcd2bin(readl(rtc->rbase + RTCA0DAYC));
		tm->tm_mon  = bcd2bin(readl(rtc->rbase + RTCA0MONC)) - 1;
		/* tm-year offset from 1900, in this century, start from 2000.
		 * While device only spend 2 digits for year.
		 * So must offset 100 again.
		 */
		tm->tm_year = bcd2bin(readl(rtc->rbase + RTCA0YEARC)) + 100;

		spin_unlock_irq(&rtc->lock);
	} while (bcd2bin(readl(rtc->rbase + RTCA0SECC)) != tm->tm_sec);

	return 0;
}

static int rzt2h_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);
	int hr;

	spin_lock_irq(&rtc->lock);

	while (rzt2h_rtc_get_bit(rtc, RTCA0CTL2, RTCA0WST))
		;

	rzt2h_rtc_set_bit(rtc, RTCA0CTL2, RTCA0WAIT);
	while (!rzt2h_rtc_get_bit(rtc, RTCA0CTL2, RTCA0WST))
		;

	writel(bin2bcd(tm->tm_sec),  rtc->rbase + RTCA0SEC);
	writel(bin2bcd(tm->tm_min),  rtc->rbase + RTCA0MIN);
	if (rzt2h_rtc_get_bit(rtc, RTCA0CTL0, RTCA0AMPM))
		/* This is 24h format */
		writel(bin2bcd(tm->tm_hour), rtc->rbase + RTCA0HOUR);
	else {
		/* This is 12h format */
		hr = tm->tm_hour;
		hr = bin2bcd(hr % 12 + 12 * !(hr % 12)) | (PM_FLAG * (hr / 12));
		writel(hr, rtc->rbase + RTCA0HOUR);
	}

	writel(bin2bcd(tm->tm_wday), rtc->rbase + RTCA0WEEK);
	writel(bin2bcd(tm->tm_mday), rtc->rbase + RTCA0DAY);
	writel(bin2bcd(tm->tm_mon + 1), rtc->rbase + RTCA0MONTH);
	writel(bin2bcd(tm->tm_year - 100), rtc->rbase + RTCA0YEAR);

	rzt2h_rtc_clear_bit(rtc, RTCA0CTL2, RTCA0WAIT);

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int rzt2h_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);

	spin_lock_irq(&rtc->lock);

	wkalrm->time = rtc->alarm_time;
	wkalrm->enabled = !!rzt2h_rtc_get_bit(rtc, RTCA0CTL1, RTCA0ALME);

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int rzt2h_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &wkalrm->time;
	struct rtc_time current_tm;
	time64_t tm_diff;
	int hr;

	if (tm->tm_sec != 0) {
		dev_info(dev, "Alarm: Value of second will be set to 0\n");
		tm->tm_sec = 0;
	}

	if (tm->tm_min < 0 || tm->tm_min > 59) {
		dev_err(dev, "Alarm: Value of minute invalid\n");
		return -EINVAL;
	}

	if (tm->tm_hour < 0 || tm->tm_hour > 23) {
		dev_err(dev, "Alarm: Value of hour invalid\n");
		return -EINVAL;
	}

	rzt2h_rtc_read_time(dev, &current_tm);
	tm_diff = rtc_tm_sub(tm, &current_tm);
	/* Make sure the alarm time is valid */
	if (tm_diff <= 0) {
		dev_err(dev, "Alarm time must be set after current time!");
		return -EINVAL;
	}

	if (tm_diff > 604800)
		dev_warn(dev, "Recommended: Should set alarm for a week!\n");

	rtc->alarm_time = *tm;

	spin_lock_irq(&rtc->lock);

	/* disable alarm interrupt even if there is no */
	rzt2h_rtc_clear_bit(rtc, RTCA0CTL1, RTCA0ALME);

	/* This board can only set min, hour and date alarm which compatible with
	 * API of Kernel.
	 */
	writel(bin2bcd(tm->tm_min), rtc->rbase + RTCA0ALM);
	if (readl(rtc->rbase + RTCA0CTL0) & RTCA0AMPM)
		writel(bin2bcd(tm->tm_hour), rtc->rbase + RTCA0ALH);
	else {
		hr = tm->tm_hour;
		hr = bin2bcd(hr % 12 + 12 * !(hr % 12)) | (PM_FLAG * (hr / 12));
		writel(hr, rtc->rbase + RTCA0ALH);
	}

	writel(1 << tm->tm_wday, rtc->rbase + RTCA0ALW);

	/* Enable alarm interrupt if any */
	if (wkalrm->enabled) {
		if (tm_diff < 604800) {
			rzt2h_rtc_clear_bit(rtc, RTCA0CTL1, RTCA0CT);
			rzt2h_rtc_set_bit(rtc, RTCA0CTL1, RTCA0ALME);
		} else {
			rtc->nday_int_left = tm_diff / 86400;
			rzt2h_rtc_update_bits(rtc, RTCA0CTL1, RTCA0CT, DAY_INT);
		}
	}

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static const struct rtc_class_ops rzt2h_rtc_ops = {
	.read_time		= rzt2h_rtc_read_time,
	.set_time		= rzt2h_rtc_set_time,
	.read_alarm		= rzt2h_rtc_read_alarm,
	.set_alarm		= rzt2h_rtc_set_alarm,
	.proc			= rzt2h_rtc_proc,
	.alarm_irq_enable	= rzt2h_rtc_alarm_irq_enable,
};

static int rzt2h_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzt2h_rtc *rtc;
	struct resource *res;
	int ret;

	rtc = devm_kzalloc(dev, sizeof(*rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;

	spin_lock_init(&rtc->lock);

	/* get periodic/carry/alarm irqs */
	rtc->alarm_irq = platform_get_irq(pdev, 0);
	if (unlikely(rtc->alarm_irq <= 0)) {
		dev_err(dev, "Get alarm interrupt failed.\n");
		return -ENOENT;
	}

	rtc->carry_irq = platform_get_irq(pdev, 1);
	if (unlikely(rtc->carry_irq <= 0)) {
		dev_err(dev, "Get carry interrupt failed.\n");
		return -ENOENT;
	}
	rtc->periodic_irq = platform_get_irq(pdev, 2);
	if (unlikely(rtc->periodic_irq <= 0)) {
		dev_err(dev, "Get periodic interrupt failed.\n");
		return -ENOENT;
	}

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(res == NULL)) {
		dev_err(&pdev->dev, "No IO resource\n");
		return -ENOENT;
	}

	rtc->regsize = resource_size(res);

	rtc->res = devm_request_mem_region(dev, res->start,
						rtc->regsize, pdev->name);
	if (unlikely(!rtc->res))
		return -EBUSY;

	rtc->rbase = devm_ioremap(dev, rtc->res->start, rtc->regsize);
	if (unlikely(!rtc->rbase))
		return -EINVAL;

	rtc->clk = devm_clk_get_optional(dev, "rtc0");
	if (IS_ERR(rtc->clk)) {
		ret = PTR_ERR(rtc->clk);
		dev_err(dev, "Failed to get RTC clk : %i\n", ret);
		return ret;
	}

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	ret = clk_prepare_enable(rtc->clk);
	if (ret) {
		dev_err(dev, "Failed to enable RTC clk: %i\n", ret);
		return ret;
	}

	/* Get, register periodic/carry/alarm irqs */
	ret = devm_request_irq(&pdev->dev, rtc->periodic_irq,
			rzt2h_rtc_periodic_isr, 0, "rzt2h-rtc periodic", rtc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"request period IRQ failed with %d, IRQ %d\n",
			ret, rtc->periodic_irq);
		clk_disable_unprepare(rtc->clk);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, rtc->carry_irq,
			rzt2h_rtc_carry_isr, 0, "rzt2h-rtc carry", rtc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"request carry IRQ failed with %d, IRQ %d\n",
			ret, rtc->carry_irq);
		clk_disable_unprepare(rtc->clk);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, rtc->alarm_irq,
			rzt2h_rtc_alarm_isr, 0, "rzt2h-rtc alarm", rtc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"request alarm IRQ failed with %d, IRQ %d\n",
			ret, rtc->alarm_irq);
		clk_disable_unprepare(rtc->clk);
		return ret;
	}

	platform_set_drvdata(pdev, rtc);

	/* everything disabled by default */
	rzt2h_rtc_setaie(&pdev->dev, 1);
	rzt2h_rtc_setcie(&pdev->dev, 0);

	rtc->rtc_dev->ops = &rzt2h_rtc_ops;
	rtc->rtc_dev->max_user_freq = 256;
	rtc->rtc_dev->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->rtc_dev->range_max = RTC_TIMESTAMP_END_2099;

	device_init_wakeup(&pdev->dev, 1);

	/* Initial setting of RTC */
	rzt2h_rtc_set_bit(rtc, RTCA0CTL0, RTCA0AMPM);
	rzt2h_rtc_set_bit(rtc, RTCA0CTL0, RTCA0SLSB);
	rzt2h_rtc_clear_bit(rtc, RTCA0CTL0, RTCA0CE);
	while (rzt2h_rtc_get_bit(rtc, RTCA0CTL0, RTCA0CEST))
		;

	writel(RTC_CLK - 1, rtc->rbase + RTCA0SCMP);
	rzt2h_rtc_set_bit(rtc, RTCA0CTL0, RTCA0CE);

	ret = devm_rtc_register_device(rtc->rtc_dev);
	if (ret)
		clk_disable_unprepare(rtc->clk);

	return ret;
}

static void rzt2h_rtc_remove(struct platform_device *pdev)
{
	struct rzt2h_rtc *rtc = platform_get_drvdata(pdev);

	rzt2h_rtc_setaie(&pdev->dev, 0);
	rzt2h_rtc_setcie(&pdev->dev, 0);

	clk_disable_unprepare(rtc->clk);
}

static void rzt2h_rtc_set_irq_wake(struct device *dev, int enabled)
{
	struct rzt2h_rtc *rtc = dev_get_drvdata(dev);

	irq_set_irq_wake(rtc->periodic_irq, enabled);

	if (rtc->carry_irq > 0) {
		irq_set_irq_wake(rtc->carry_irq, enabled);
		irq_set_irq_wake(rtc->alarm_irq, enabled);
	}
}

static int rzt2h_rtc_suspend(struct device *dev)
{
	if (device_may_wakeup(dev))
		rzt2h_rtc_set_irq_wake(dev, 1);

	return 0;
}

static int rzt2h_rtc_resume(struct device *dev)
{
	if (device_may_wakeup(dev))
		rzt2h_rtc_set_irq_wake(dev, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rzt2h_rtc_pm_ops, rzt2h_rtc_suspend, rzt2h_rtc_resume);

static const struct of_device_id rzt2h_rtc_of_match[] = {
	{ .compatible = "renesas,rzt2h-rtc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzt2h_rtc_of_match);

static struct platform_driver rzt2h_rtc_platform_driver = {
	.driver		= {
		.name	= "rzt2h-rtc",
		.pm	= pm_ptr(&rzt2h_rtc_pm_ops),
		.of_match_table = rzt2h_rtc_of_match,
	},
	.probe		= rzt2h_rtc_probe,
	.remove		= rzt2h_rtc_remove,
};

module_platform_driver(rzt2h_rtc_platform_driver);

MODULE_DESCRIPTION("RZT2H on-chip RTC driver");
MODULE_AUTHOR("Anh Hoang <anh.hoang.yk@renesas.com>");
MODULE_LICENSE("GPL v2");

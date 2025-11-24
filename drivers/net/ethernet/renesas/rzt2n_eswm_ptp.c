// SPDX-License-Identifier: GPL-2.0
/* Renesas gPTP device driver using for ESWM
 * Based on gPTP driver for Ethernet Switch2
 * support for Rcar Gen4 from Linux kernel v6.11-rc2
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "rzt2n_eswm_ptp.h"
#define ptp_to_priv(ptp)	container_of(ptp, struct eswm_ptp_private, info)

static const struct eswm_ptp_reg_offset rzt2n_offs = {
	.enable = PTPTMEC,
	.disable = PTPTMDC,
	.increment = PTPTIVC0,
	.config_t0 = PTPTOVC00,
	.config_t1 = PTPTOVC10,
	.config_t2 = PTPTOVC20,
	.monitor_t0 = PTPGPTPTM00,
	.monitor_t1 = PTPGPTPTM10,
	.monitor_t2 = PTPGPTPTM20,
};

static int eswm_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct eswm_ptp_private *ptp_priv = ptp_to_priv(ptp);
	bool neg_adj = scaled_ppm < 0 ? true : false;
	s64 addend = ptp_priv->default_addend;
	s64 diff;

	if (neg_adj)
		scaled_ppm = -scaled_ppm;
	diff = div_s64(addend * scaled_ppm_to_ppb(scaled_ppm), NSEC_PER_SEC);
	addend = neg_adj ? addend - diff : addend + diff;

	iowrite32(addend, ptp_priv->addr + ptp_priv->offs->increment);

	return 0;
}

/* Caller must hold the lock */
static void _eswm_ptp_gettime(struct ptp_clock_info *ptp,
				   struct timespec64 *ts)
{
	struct eswm_ptp_private *ptp_priv = ptp_to_priv(ptp);

	ts->tv_nsec = ioread32(ptp_priv->addr + ptp_priv->offs->monitor_t0);
	ts->tv_sec = ioread32(ptp_priv->addr + ptp_priv->offs->monitor_t1) |
		     ((s64)ioread32(ptp_priv->addr + ptp_priv->offs->monitor_t2) << 32);
}

int eswm_ptp_gettime(struct ptp_clock_info *ptp,
		     struct timespec64 *ts)
{
	struct eswm_ptp_private *ptp_priv = ptp_to_priv(ptp);
	unsigned long flags;

	spin_lock_irqsave(&ptp_priv->lock, flags);
	_eswm_ptp_gettime(ptp, ts);
	spin_unlock_irqrestore(&ptp_priv->lock, flags);

	return 0;
}

/* Caller must hold the lock */
static void _eswm_ptp_settime(struct ptp_clock_info *ptp,
				   const struct timespec64 *ts)
{
	struct eswm_ptp_private *ptp_priv = ptp_to_priv(ptp);

	iowrite32(1, ptp_priv->addr + ptp_priv->offs->disable);
	iowrite32(0, ptp_priv->addr + ptp_priv->offs->config_t2);
	iowrite32(0, ptp_priv->addr + ptp_priv->offs->config_t1);
	iowrite32(0, ptp_priv->addr + ptp_priv->offs->config_t0);
	iowrite32(1, ptp_priv->addr + ptp_priv->offs->enable);
	iowrite32(ts->tv_sec >> 32, ptp_priv->addr + ptp_priv->offs->config_t2);
	iowrite32(ts->tv_sec, ptp_priv->addr + ptp_priv->offs->config_t1);
	iowrite32(ts->tv_nsec, ptp_priv->addr + ptp_priv->offs->config_t0);
}

static int eswm_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct eswm_ptp_private *ptp_priv = ptp_to_priv(ptp);
	unsigned long flags;

	spin_lock_irqsave(&ptp_priv->lock, flags);
	_eswm_ptp_settime(ptp, ts);
	spin_unlock_irqrestore(&ptp_priv->lock, flags);

	return 0;
}

static int eswm_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct eswm_ptp_private *ptp_priv = ptp_to_priv(ptp);
	struct timespec64 ts;
	unsigned long flags;
	s64 now;

	spin_lock_irqsave(&ptp_priv->lock, flags);
	_eswm_ptp_gettime(ptp, &ts);
	now = ktime_to_ns(timespec64_to_ktime(ts));
	ts = ns_to_timespec64(now + delta);
	_eswm_ptp_settime(ptp, &ts);
	spin_unlock_irqrestore(&ptp_priv->lock, flags);

	return 0;
}

static int eswm_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info eswm_ptp_info = {
	.owner = THIS_MODULE,
	.name = "eswm_ptp",
	.max_adj = 50000000,
	.adjfine = eswm_ptp_adjfine,
	.adjtime = eswm_ptp_adjtime,
	.gettime64 = eswm_ptp_gettime,
	.settime64 = eswm_ptp_settime,
	.enable = eswm_ptp_enable,
};

static int eswm_ptp_set_offs(struct eswm_ptp_private *ptp_priv,
				   enum eswm_ptp_reg_layout layout)
{
	if (layout != ESWM_PTP_REG_LAYOUT)
		return -EINVAL;

	ptp_priv->offs = &rzt2n_offs;

	return 0;
}

int eswm_ptp_register(struct eswm_ptp_private *ptp_priv,
			   enum eswm_ptp_reg_layout layout, u32 clock)
{
	if (ptp_priv->initialized)
		return 0;

	spin_lock_init(&ptp_priv->lock);

	eswm_ptp_set_offs(ptp_priv, layout);

	ptp_priv->default_addend = clock;
	iowrite32(ptp_priv->default_addend, ptp_priv->addr + ptp_priv->offs->increment);
	ptp_priv->clock = ptp_clock_register(&ptp_priv->info, NULL);
	if (IS_ERR(ptp_priv->clock))
		return PTR_ERR(ptp_priv->clock);

	iowrite32(0x01, ptp_priv->addr + ptp_priv->offs->enable);
	ptp_priv->initialized = true;

	return 0;
}
EXPORT_SYMBOL_GPL(eswm_ptp_register);

int eswm_ptp_unregister(struct eswm_ptp_private *ptp_priv)
{
	iowrite32(1, ptp_priv->addr + ptp_priv->offs->disable);

	return ptp_clock_unregister(ptp_priv->clock);
}
EXPORT_SYMBOL_GPL(eswm_ptp_unregister);

struct eswm_ptp_private *eswm_ptp_alloc(struct platform_device *pdev)
{
	struct eswm_ptp_private *ptp;

	ptp = devm_kzalloc(&pdev->dev, sizeof(*ptp), GFP_KERNEL);
	if (!ptp)
		return NULL;

	ptp->info = eswm_ptp_info;

	return ptp;
}
EXPORT_SYMBOL_GPL(eswm_ptp_alloc);

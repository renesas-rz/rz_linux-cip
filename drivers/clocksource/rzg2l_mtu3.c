// SPDX-License-Identifier: GPL-2.0
/*
 * RZG Timer Support - MTU3
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * Based on sh_mtu2.c
 *
 * Copyright (C) 2009 Magnus Damm
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sh_timer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/clocksource.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/pwm.h>

struct rzg2l_mtu3_device;

enum mtu3_functions {
	MTU3_CLOCKSOURCE,	/* Assign clocksource function */
	MTU3_CLOCKEVENT,	/* Assign clockevent function */
	MTU3_PWM_MODE_1,	/* Assign pwm mode 1 function */
};

enum mtu3_pins {
	MTIOCA, MTIOCB, MTIOCC, MTIOCD,
};

struct mtu3_pwm_device {
	u8 ch;
	u8 output;
};

static const struct pwm_ops mtu3_pwm_ops;

struct rzg2l_mtu3_channel {
	struct rzg2l_mtu3_device *mtu;
	unsigned int index;
	void __iomem *base;
	void __iomem *iostart;
	void __iomem *ioctrl;
	struct clock_event_device ced;
	struct clocksource cs;
	u64 total_cycles;
	raw_spinlock_t lock;
	unsigned long flags;
	bool cs_enabled;
	enum mtu3_functions function;
};

struct rzg2l_mtu3_device {
	struct platform_device *pdev;
	void __iomem *mapbase;
	struct clk *clk;
	struct reset_control *rstc;
	unsigned long rate;
	raw_spinlock_t lock; /* Protect the shared registers */
	struct rzg2l_mtu3_channel *channels;
	unsigned int num_channels;
	bool has_clockevent;
	bool has_clocksource;
	struct pwm_chip pwm_chip;
	struct mtu3_pwm_device *pwms;

};

#define TSTR -1 /* shared register */
#define TSTRA -1 /* shared register */
#define TSTRB -2 /* shared register */
#define TOERA -3 /* shared register */
#define TOERB -4 /* shared register */

/* 8 bit channel registers */
#define TCR		0 /* Timer control register */
#define TMDR1		1 /* Timer mode register 1 */
#define TIORH		2 /* Timer I/O control register H */
#define TIOR		2 /* Timer I/O control register */
#define TIORL		3 /* Timer I/O control register L */
#define TIER		4 /* channel register */
#define TSR		5 /* channel register */
#define TCR2		6 /* channel register */

/* 16 bit channel registers */
#define TCNT		7 /* Timer counter */
#define TGRA		8 /* Timer general register A */
#define TGRB		9 /* Timer general register B */
#define TGRC		10 /* Timer general register C */
#define TGRD		11 /* Timer general register D */
/* 32 bit channel registers */


#define TCR_CCLR_NONE		(0 << 5)
#define TCR_CCLR_TGRA		(1 << 5)
#define TCR_CCLR_TGRB		(2 << 5)
#define TCR_CCLR_SYNC		(3 << 5)
#define TCR_CCLR_TGRC		(5 << 5)
#define TCR_CCLR_TGRD		(6 << 5)
#define TCR_CCLR_MASK		(7 << 5)
#define TCR_CKEG_RISING		(0 << 3)
#define TCR_CKEG_FALLING	(1 << 3)
#define TCR_CKEG_BOTH		(2 << 3)
#define TCR_CKEG_MASK		(3 << 3)
/* Values 4 to 7 are channel-dependent */
#define TCR_TPSC_P1		(0 << 0)
#define TCR_TPSC_P4		(1 << 0)
#define TCR_TPSC_P16		(2 << 0)
#define TCR_TPSC_P64		(3 << 0)
#define TCR_TPSC_CH0_TCLKA	(4 << 0)
#define TCR_TPSC_CH0_TCLKB	(5 << 0)
#define TCR_TPSC_CH0_TCLKC	(6 << 0)
#define TCR_TPSC_CH0_TCLKD	(7 << 0)
#define TCR_TPSC_CH1_TCLKA	(4 << 0)
#define TCR_TPSC_CH1_TCLKB	(5 << 0)
#define TCR_TPSC_CH1_P256	(6 << 0)
#define TCR_TPSC_CH1_TCNT2	(7 << 0)
#define TCR_TPSC_CH2_TCLKA	(4 << 0)
#define TCR_TPSC_CH2_TCLKB	(5 << 0)
#define TCR_TPSC_CH2_TCLKC	(6 << 0)
#define TCR_TPSC_CH2_P1024	(7 << 0)
#define TCR_TPSC_CH34_P256	(4 << 0)
#define TCR_TPSC_CH34_P1024	(5 << 0)
#define TCR_TPSC_CH34_TCLKA	(6 << 0)
#define TCR_TPSC_CH34_TCLKB	(7 << 0)
#define TCR_TPSC_MASK		(7 << 0)

#define TMDR_BFE		(1 << 6)
#define TMDR_BFB		(1 << 5)
#define TMDR_BFA		(1 << 4)
#define TMDR_MD_NORMAL		(0 << 0)
#define TMDR_MD_PWM_1		(2 << 0)
#define TMDR_MD_PWM_2		(3 << 0)
#define TMDR_MD_PHASE_1		(4 << 0)
#define TMDR_MD_PHASE_2		(5 << 0)
#define TMDR_MD_PHASE_3		(6 << 0)
#define TMDR_MD_PHASE_4		(7 << 0)
#define TMDR_MD_PWM_SYNC	(8 << 0)
#define TMDR_MD_PWM_COMP_CREST	(13 << 0)
#define TMDR_MD_PWM_COMP_TROUGH	(14 << 0)
#define TMDR_MD_PWM_COMP_BOTH	(15 << 0)
#define TMDR_MD_MASK		(15 << 0)

#define TIOC_IOCH(n)		((n) << 4)
#define TIOC_IOCL(n)		((n) << 0)
#define TIOR_OC_RETAIN		(0 << 0)
#define TIOR_OC_0_L_COMP_MATCH		(1 << 0)
#define TIOR_OC_0_H_COMP_MATCH		(2 << 0)
#define TIOR_OC_0_TOGGLE	(3 << 0)

#define TIOR_OC_1_L_COMP_MATCH		(5 << 0)
#define TIOR_OC_1_H_COMP_MATCH		(6 << 0)
#define TIOR_OC_1_TOGGLE	(7 << 0)
#define TIOR_IC_RISING		(8 << 0)
#define TIOR_IC_FALLING		(9 << 0)
#define TIOR_IC_BOTH		(10 << 0)
#define TIOR_IC_TCNT		(12 << 0)
#define TIOR_MASK		(15 << 0)
#define TIER_TTGE		(1 << 7)
#define TIER_TTGE2		(1 << 6)
#define TIER_TCIEU		(1 << 5)
#define TIER_TCIEV		(1 << 4)
#define TIER_TGIED		(1 << 3)
#define TIER_TGIEC		(1 << 2)
#define TIER_TGIEB		(1 << 1)
#define TIER_TGIEA		(1 << 0)

#define TSR_TCFD		(1 << 7)
#define TSR_TCFU		(1 << 5)
#define TSR_TCFV		(1 << 4)
#define TSR_TGFD		(1 << 3)
#define TSR_TGFC		(1 << 2)
#define TSR_TGFB		(1 << 1)
#define TSR_TGFA		(1 << 0)
#define TSR_TGIA		(1 << 0)

/* private flags */
#define FLAG_CLOCKEVENT (1 << 0)
#define FLAG_CLOCKSOURCE (1 << 1)
#define FLAG_REPROGRAM (1 << 2)
#define FLAG_SKIPEVENT (1 << 3)
#define FLAG_IRQCONTEXT (1 << 4)

static unsigned long rzg2l_mtu_reg_offs[][12] = {
	{[TCR] = 0x0, [TMDR1] = 0x1, [TIORH] = 0x2, [TIORL] = 0x3, [TIER] = 0x4,
	 [TCNT] = 0x6, [TGRA] = 0x8, [TGRB] = 0xA, [TGRC] = 0xC, [TGRD] = 0xE,
	 [TCR2] = 0x28 },
	{[TCR] = 0x0, [TMDR1] = 0x1, [TIOR] = 0x2, [TIER] = 0x4, [TSR] = 0x5,
	 [TCNT] = 0x6, [TGRA] = 0x8, [TGRB] = 0xA, [TCR2] = 0x14 },
	{[TCR] = 0x0, [TMDR1] = 0x1, [TIOR] = 0x2, [TIER] = 0x4, [TSR] = 0x5,
	 [TCNT] = 0x6, [TGRA] = 0x8, [TGRB] = 0xA, [TCR2] = 0xC },
	{[TCR] = 0x0, [TMDR1] = 0x2, [TIORH] = 0x4, [TIORL] = 0x5, [TIER] = 0x8,
	 [TCNT] = 0x10, [TGRA] = 0x18, [TGRB] = 0x1A, [TGRC] = 0x24,
	 [TGRD] = 0x26, [TCR2] = 0x4C, [TSR] = 0x2C },
	{[TCR] = 0x0, [TMDR1] = 0x2, [TIORH] = 0x5, [TIORL] = 0x6, [TIER] = 0x8,
	 [TCNT] = 0x11, [TGRA] = 0x1B, [TGRB] = 0x1D, [TGRC] = 0x27,
	 [TGRD] = 0x29, [TSR] = 0x2C, [TCR2] = 0x4C },
	{},
	{[TCR] = 0x0, [TMDR1] = 0x2, [TIORH] = 0x4, [TIORL] = 0x5, [TIER] = 0x8,
	 [TCNT] = 0x10, [TGRA] = 0x18, [TGRB] = 0x1A, [TGRC] = 0x24,
	 [TGRD] = 0x26, [TSR] = 0x2C, [TCR2] = 0x4C },
	{[TCR] = 0x0, [TMDR1] = 0x2, [TIORH] = 0x5, [TIORL] = 0x6, [TIER] = 0x8,
	 [TCNT] = 0x11, [TGRA] = 0x1B, [TGRB] = 0x1D, [TGRC] = 0x27,
	 [TGRD] = 0x29, [TSR] = 0x2C, [TCR2] = 0x4C },
	{[TCR] = 0x0, [TMDR1] = 0x1, [TIORH] = 0x2, [TIORL] = 0x3, [TIER] = 0x4,
	 [TCNT] = 0x8, [TGRA] = 0xC, [TGRB] = 0x10, [TGRC] = 0x14,
	 [TGRD] = 0x18, [TCR2] = 0x6 }
};

static inline unsigned long rzg2l_mtu3_read(struct rzg2l_mtu3_channel *ch,
					    int reg_nr)
{
	unsigned long offs;

	if (reg_nr == TSTRA)
		return ioread8(ch->mtu->mapbase + 0x80);
	else if (reg_nr == TSTRB)
		return ioread8(ch->mtu->mapbase + 0x880);
	else if (reg_nr == TOERA)
		return ioread8(ch->mtu->mapbase + 0xA);
	else if (reg_nr == TOERB)
		return ioread8(ch->mtu->mapbase + 0x80A);

	offs = rzg2l_mtu_reg_offs[ch->index][reg_nr];

	if ((reg_nr <= TCR2) && (reg_nr >= TCR))
		return ioread8(ch->base + offs);
	else if ((reg_nr >= TCNT) && (reg_nr <= TGRD))
		return ioread16(ch->base + offs);
	return 0;
}

static inline void rzg2l_mtu3_write(struct rzg2l_mtu3_channel *ch, int reg_nr,
			unsigned long value)
{
	unsigned long offs;

	if (reg_nr == TSTRA)
		iowrite8((u8)value, ch->mtu->mapbase + 0x80);
	else if (reg_nr == TSTRB)
		iowrite8((u8)value, ch->mtu->mapbase + 0x880);
	else if (reg_nr == TOERA)
		iowrite8((u8)value, ch->mtu->mapbase + 0xA);
	else if (reg_nr == TOERB)
		iowrite8((u8)value, ch->mtu->mapbase + 0x80A);

	offs = rzg2l_mtu_reg_offs[ch->index][reg_nr];
	if ((reg_nr <= TCR2) && (reg_nr >= TCR))
		iowrite8((u8)value, ch->base + offs);
	else if ((reg_nr >= TCNT) && (reg_nr <= TGRD))
		iowrite16((u16)value, ch->base + offs);
}

static void rzg2l_mtu3_start_stop_ch(struct rzg2l_mtu3_channel *ch, bool start)
{
	unsigned long flags, value;
	u8 offs;

	/* start stop register shared by multiple timer channels */
	raw_spin_lock_irqsave(&ch->mtu->lock, flags);

	if ((ch->index == 6) || (ch->index == 7)) {
		value = rzg2l_mtu3_read(ch, TSTRB);
		if (start)
			value |= 1 << ch->index;
		else
			value &= ~(1 << ch->index);
		rzg2l_mtu3_write(ch, TSTRB, value);
	} else if (ch->index != 5) {
		value = rzg2l_mtu3_read(ch, TSTRA);
		if (ch->index == 8)
			offs = 0x08;
		else if (ch->index < 3)
			offs = 1 << ch->index;
		else
			offs = 1 << (ch->index + 3);
		if (start)
			value |= offs;
		else
			value &= ~offs;
		rzg2l_mtu3_write(ch, TSTRA, value);
	}

	raw_spin_unlock_irqrestore(&ch->mtu->lock, flags);
}

static int rzg2l_mtu3_enable(struct rzg2l_mtu3_channel *ch)
{
	unsigned long periodic;
	unsigned long rate;
	int ret;

	pm_runtime_get_sync(&ch->mtu->pdev->dev);
	dev_pm_syscore_device(&ch->mtu->pdev->dev, true);

	/* enable clock */
	ret = clk_enable(ch->mtu->clk);
	if (ret) {
		dev_err(&ch->mtu->pdev->dev, "ch%u: cannot enable clock\n",
			ch->index);
		return ret;
	}

	/* make sure channel is disabled */
	rzg2l_mtu3_start_stop_ch(ch, false);

	rate = clk_get_rate(ch->mtu->clk) / 64;
	periodic = (rate + HZ/2) / HZ;

	/*
	 * "Periodic Counter Operation"
	 * Clear on TGRA compare match, divide clock by 64.
	 */
	if (ch->function == MTU3_CLOCKSOURCE) {
		rzg2l_mtu3_write(ch, TCR, TCR_TPSC_P64);
		rzg2l_mtu3_write(ch, TIER, 0);
	} else if (ch->function == MTU3_CLOCKEVENT) {
		rzg2l_mtu3_write(ch, TCR, TCR_CCLR_TGRA | TCR_TPSC_P64);
		rzg2l_mtu3_write(ch, TIOR, TIOC_IOCH(TIOR_OC_1_L_COMP_MATCH) |
			TIOC_IOCL(TIOR_OC_1_L_COMP_MATCH));
		rzg2l_mtu3_write(ch, TGRA, periodic);
		rzg2l_mtu3_write(ch, TMDR1, TMDR_MD_NORMAL);
		rzg2l_mtu3_write(ch, TIER, TIER_TGIEA);
	}

	/* enable channel */
	rzg2l_mtu3_start_stop_ch(ch, true);
	return 0;
}

static void rzg2l_mtu3_disable(struct rzg2l_mtu3_channel *ch)
{
	/* disable channel */
	rzg2l_mtu3_start_stop_ch(ch, false);
	/* stop clock */
	clk_disable(ch->mtu->clk);
	dev_pm_syscore_device(&ch->mtu->pdev->dev, false);
	pm_runtime_put(&ch->mtu->pdev->dev);
}

static int rzg2l_mtu3_start(struct rzg2l_mtu3_channel *ch, unsigned long flag)
{
	int ret = 0;
	unsigned long flags;

	raw_spin_lock_irqsave(&ch->lock, flags);

	if (!(ch->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE)))
		ret = rzg2l_mtu3_enable(ch);

	if (ret)
		goto out;
	ch->flags |= flag;

	/* setup timeout if no clockevent */
out:
	raw_spin_unlock_irqrestore(&ch->lock, flags);
	return ret;
}

static irqreturn_t rzg2l_mtu3_interrupt(int irq, void *dev_id)
{
	struct rzg2l_mtu3_channel *ch = dev_id;

	/* acknowledge interrupt */
	/* notify clockevent layer */
	if (ch->flags & FLAG_CLOCKEVENT)
		ch->ced.event_handler(&ch->ced);
	return IRQ_HANDLED;
}

static void rzg2l_mtu3_stop(struct rzg2l_mtu3_channel *ch, unsigned long flag)
{
	unsigned long flags;
	unsigned long f;

	raw_spin_lock_irqsave(&ch->lock, flags);
	f = ch->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE);
	ch->flags &= ~flag;

	if (f && !(ch->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE)))
		rzg2l_mtu3_disable(ch);

	/* adjust the timeout to maximum if only clocksource left */

	raw_spin_unlock_irqrestore(&ch->lock, flags);
}

static struct rzg2l_mtu3_channel *ced_to_rzg2l_mtu3(
				struct clock_event_device *ced)
{
	return container_of(ced, struct rzg2l_mtu3_channel, ced);
}

static int rzg2l_mtu3_clock_event_shutdown(struct clock_event_device *ced)
{
	struct rzg2l_mtu3_channel *ch = ced_to_rzg2l_mtu3(ced);

	if (clockevent_state_periodic(ced))
		rzg2l_mtu3_disable(ch);

	return 0;
}

static int rzg2l_mtu3_clock_event_set_periodic(struct clock_event_device *ced)
{
	struct rzg2l_mtu3_channel *ch = ced_to_rzg2l_mtu3(ced);

	if (clockevent_state_periodic(ced))
		rzg2l_mtu3_disable(ch);

	dev_info(&ch->mtu->pdev->dev, "ch%u: used for periodic clock events\n",
		ch->index);
	rzg2l_mtu3_enable(ch);

	return 0;
}

static void rzg2l_mtu3_clock_event_suspend(struct clock_event_device *ced)
{
	pm_genpd_syscore_poweroff(&ced_to_rzg2l_mtu3(ced)->mtu->pdev->dev);
}

static void rzg2l_mtu3_clock_event_resume(struct clock_event_device *ced)
{
	pm_genpd_syscore_poweron(&ced_to_rzg2l_mtu3(ced)->mtu->pdev->dev);
}

static struct rzg2l_mtu3_channel *cs_to_sh_mtu(struct clocksource *cs)
{
	return container_of(cs, struct rzg2l_mtu3_channel, cs);
}

static u32 rzg2l_mtu3_get_counter(struct rzg2l_mtu3_channel *ch)
{
	u32 v2;

	v2 = rzg2l_mtu3_read(ch, TCNT);

	return v2;
}

static u64 rzg2l_mtu3_clocksource_read(struct clocksource *cs)
{
	struct rzg2l_mtu3_channel *ch = cs_to_sh_mtu(cs);
	unsigned long flags;
	u64 value;
	u32 raw;

	raw_spin_lock_irqsave(&ch->lock, flags);
	value = ch->total_cycles;
	raw = rzg2l_mtu3_get_counter(ch);
	raw_spin_unlock_irqrestore(&ch->lock, flags);

	return value + raw;
}

static int rzg2l_mtu3_clocksource_enable(struct clocksource *cs)
{
	int ret;
	struct rzg2l_mtu3_channel *ch = cs_to_sh_mtu(cs);

	WARN_ON(ch->cs_enabled);
	ch->total_cycles = 0;
	ret = rzg2l_mtu3_start(ch, FLAG_CLOCKSOURCE);
	if (!ret)
		ch->cs_enabled = true;

	return ret;
}

static void rzg2l_mtu3_clocksource_disable(struct clocksource *cs)
{
	struct rzg2l_mtu3_channel *ch = cs_to_sh_mtu(cs);

	WARN_ON(!ch->cs_enabled);
	rzg2l_mtu3_stop(ch, FLAG_CLOCKSOURCE);
	ch->cs_enabled = false;
}

static void rzg2l_mtu3_clocksource_suspend(struct clocksource *cs)
{
	struct rzg2l_mtu3_channel *ch = cs_to_sh_mtu(cs);

	if (!ch->cs_enabled)
		return;
	rzg2l_mtu3_stop(ch, FLAG_CLOCKSOURCE);
	pm_genpd_syscore_poweroff(&ch->mtu->pdev->dev);
}

static void rzg2l_mtu3_clocksource_resume(struct clocksource *cs)
{
	struct rzg2l_mtu3_channel *ch = cs_to_sh_mtu(cs);

	if (!ch->cs_enabled)
		return;
	pm_genpd_syscore_poweron(&ch->mtu->pdev->dev);
	rzg2l_mtu3_start(ch, FLAG_CLOCKSOURCE);
}

static void rzg2l_mtu3_register_clockevent(struct rzg2l_mtu3_channel *ch,
			const char *name)
{
	struct clock_event_device *ced = &ch->ced;

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->rating = 200;
	ced->cpumask = cpu_possible_mask;
	ced->set_state_shutdown = rzg2l_mtu3_clock_event_shutdown;
	ced->set_state_periodic = rzg2l_mtu3_clock_event_set_periodic;
	ced->suspend = rzg2l_mtu3_clock_event_suspend;
	ced->resume = rzg2l_mtu3_clock_event_resume;
	dev_info(&ch->mtu->pdev->dev, "ch%u: used for clock events\n",
		ch->index);
	clockevents_register_device(ced);
}

static int rzg2l_mtu3_register_clocksource(struct rzg2l_mtu3_channel *ch,
			const char *name)
{
	struct clocksource *cs = &ch->cs;

	cs->name = name;
	cs->rating = 126;
	cs->read = rzg2l_mtu3_clocksource_read;
	cs->enable = rzg2l_mtu3_clocksource_enable;
	cs->disable = rzg2l_mtu3_clocksource_disable;
	cs->suspend = rzg2l_mtu3_clocksource_suspend;
	cs->resume = rzg2l_mtu3_clocksource_resume;
	cs->mask = 0xffff;
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;
	dev_info(&ch->mtu->pdev->dev, "ch%u: used as clock source\n",
		ch->index);
	clocksource_register_hz(cs, ch->mtu->rate);
	return 0;
}

static int rzg2l_mtu3_register(struct rzg2l_mtu3_channel *ch,
				const char *name)
{
	if (ch->function == MTU3_CLOCKEVENT)
		rzg2l_mtu3_register_clockevent(ch, name);
	else if (ch->function == MTU3_CLOCKSOURCE)
		rzg2l_mtu3_register_clocksource(ch, name);
	return 0;
}

static int rzg2l_mtu3_setup_channel(struct rzg2l_mtu3_channel *ch,
				    unsigned int index,
				    struct rzg2l_mtu3_device *mtu)
{
	static const unsigned int channel_offsets[] = {
		0x100, 0x180, 0x200, 0x000, 0x001, 0xA80, 0x800, 0x801, 0x400
	};
	char name[6];
	int irq;
	int ret;

	ch->mtu = mtu;

	sprintf(name, "tgi%ua", index);
	irq = platform_get_irq_byname(mtu->pdev, name);
	if (irq < 0) {
		/* Skip channels with no declared interrupt. */
		return 0;
	}

	ret = request_irq(irq, rzg2l_mtu3_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL | IRQF_NOBALANCING,
			  dev_name(&ch->mtu->pdev->dev), ch);
	if (ret) {
		dev_err(&ch->mtu->pdev->dev, "ch%u: failed to request irq %d\n",
			index, irq);
		return ret;
	}

	ch->base = mtu->mapbase + channel_offsets[index];
	ch->index = index;

	return rzg2l_mtu3_register(ch, dev_name(&mtu->pdev->dev));
}

static int rzg2l_mtu3_map_memory(struct rzg2l_mtu3_device *mtu)
{
	struct resource *res;

	res = platform_get_resource(mtu->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&mtu->pdev->dev, "failed to get I/O memory\n");
		return -ENXIO;
	}

	mtu->mapbase = ioremap_nocache(res->start, resource_size(res));
	if (mtu->mapbase == NULL)
		return -ENXIO;

	return 0;
}

static void rzg2l_mtu3_pin_setup(struct rzg2l_mtu3_channel *ch,
				 enum mtu3_pins pin,
				 u8 state)
{
	u8 val;

	switch (pin) {
	case MTIOCA:
		val = rzg2l_mtu3_read(ch, TIORH);
		val &= 0xF0;
		val |= state;
		rzg2l_mtu3_write(ch, TIORH, val);
		break;
	case MTIOCB:
		val = rzg2l_mtu3_read(ch, TIORH);
		val &= 0x0F;
		val |= (state << 4);
		rzg2l_mtu3_write(ch, TIORH, val);
		break;
	case MTIOCC:
		val = rzg2l_mtu3_read(ch, TIORL);
		val &= 0xF0;
		val |= state;
		rzg2l_mtu3_write(ch, TIORL, val);
		break;
	case MTIOCD:
		val = rzg2l_mtu3_read(ch, TIORL);
		val &= 0x0F;
		val |= (state << 4);
		rzg2l_mtu3_write(ch, TIORL, val);
		break;
	}
}

/* MTU3 PWM APIs */
static inline struct rzg2l_mtu3_device
			*pwm_chip_to_mtu3_device(struct pwm_chip *chip)
{
	return container_of(chip, struct rzg2l_mtu3_device, pwm_chip);
}

static int rzg2l_mtu3_pwm_request(struct pwm_chip *chip,
				  struct pwm_device *pwm)
{
	return pm_runtime_get_sync(chip->dev);
}

static void rzg2l_mtu3_pwm_free(struct pwm_chip *chip,
				struct pwm_device *pwm)
{
	pm_runtime_put_sync(chip->dev);
}

static int rzg2l_mtu3_pwm_enable(struct pwm_chip *chip,
				 struct pwm_device *pwm)
{
	struct rzg2l_mtu3_device *mtu3 = pwm_chip_to_mtu3_device(chip);
	struct rzg2l_mtu3_channel *ch;
	u8 output_pin, count;

	ch = &mtu3->channels[mtu3->pwms[pwm->hwpwm].ch];
	if ((ch->function == MTU3_PWM_MODE_1) &&
	   (mtu3->pwms[pwm->hwpwm].output == 0)) {
		output_pin = MTIOCA;
		count = MTIOCB;
	} else if ((ch->function == MTU3_PWM_MODE_1) &&
		  (mtu3->pwms[pwm->hwpwm].output == 1)) {
		output_pin = MTIOCC;
		count = MTIOCD;
	}
	rzg2l_mtu3_write(ch, TMDR1, TMDR_MD_PWM_1);

	/* Setting output waveform modes for MTU3 pins */
	rzg2l_mtu3_pin_setup(ch, output_pin, TIOR_OC_1_TOGGLE);
	if (pwm->state.polarity == PWM_POLARITY_INVERSED)
		rzg2l_mtu3_pin_setup(ch, count, TIOR_OC_1_H_COMP_MATCH);
	else
		rzg2l_mtu3_pin_setup(ch, count, TIOR_OC_0_L_COMP_MATCH);

	rzg2l_mtu3_start_stop_ch(ch, true);
	return 0;
}

static void rzg2l_mtu3_pwm_disable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct rzg2l_mtu3_device *mtu3 = pwm_chip_to_mtu3_device(chip);
	struct rzg2l_mtu3_channel *ch;

	ch = &mtu3->channels[mtu3->pwms[pwm->hwpwm].ch];
	/* Return to normal mode and disable output pins of MTU3 channel */
	rzg2l_mtu3_write(ch, TMDR1, TMDR_MD_NORMAL);

	/* Disable output waveform of MTU3 pins */
	if ((ch->function == MTU3_PWM_MODE_1) &&
	   (mtu3->pwms[pwm->hwpwm].output == 0))
		rzg2l_mtu3_pin_setup(ch, MTIOCA, TIOR_OC_RETAIN);
	else if ((ch->function == MTU3_PWM_MODE_1) &&
		(mtu3->pwms[pwm->hwpwm].output == 1))
		rzg2l_mtu3_pin_setup(ch, MTIOCC, TIOR_OC_RETAIN);

	rzg2l_mtu3_start_stop_ch(ch, false);
}


static int rzg2l_mtu3_pwm_config(struct pwm_chip *chip,
				 struct pwm_device *pwm,
				 int duty_ns, int period_ns)
{
	struct rzg2l_mtu3_device *mtu3 = pwm_chip_to_mtu3_device(chip);
	struct rzg2l_mtu3_channel *ch;
	static const unsigned int prescalers[] = { 1, 4, 16, 64 };
	unsigned int prescaler;
	u32 clk_rate;
	u32 period;
	u32 duty;

	ch = &mtu3->channels[mtu3->pwms[pwm->hwpwm].ch];
	clk_rate = clk_get_rate(mtu3->clk);

	for (prescaler = 0; prescaler < ARRAY_SIZE(prescalers); ++prescaler) {
		period = clk_rate / prescalers[prescaler]
			/ (NSEC_PER_SEC / period_ns);
		if (period <= 0xffff)
			break;
	}

	if (prescaler == ARRAY_SIZE(prescalers) || period == 0) {
		dev_err(&ch->mtu->pdev->dev, "clock rate mismatch\n");
		return -ENOTSUPP;
	}

	if (duty_ns) {
		duty = clk_rate / prescalers[prescaler]
			/ (NSEC_PER_SEC / duty_ns);
		if (duty > period)
			return -EINVAL;
	} else {
		duty = 0;
	}

	dev_dbg(&mtu3->pdev->dev,
		"rate %u, prescaler %u, period %u, duty %u\n",
		clk_rate, prescalers[prescaler], period, duty);

	if ((ch->function == MTU3_PWM_MODE_1) &&
		(mtu3->pwms[pwm->hwpwm].output == 0)) {
		rzg2l_mtu3_write(ch, TCR, TCR_CCLR_TGRA |
				TCR_CKEG_RISING | prescaler);
		rzg2l_mtu3_write(ch, TGRB, duty);
		rzg2l_mtu3_write(ch, TGRA, period);
	} else if ((ch->function == MTU3_PWM_MODE_1) &&
			(mtu3->pwms[pwm->hwpwm].output == 1)) {
		rzg2l_mtu3_write(ch, TCR, TCR_CCLR_TGRC |
				TCR_CKEG_RISING | prescaler);
		rzg2l_mtu3_write(ch, TGRD, duty);
		rzg2l_mtu3_write(ch, TGRC, period);
	}

	return 0;
}

static int rzg2l_mtu3_register_pwm(struct rzg2l_mtu3_device *mtu,
				   struct device_node *np)
{
	u32 tmp, ch_num, num_pwm, num_args;
	int offset, ret, i, j;

	if (!of_get_property(np, "pwm_mode1", &tmp))
		return 1;

	num_args = 2;
	num_pwm = tmp/(sizeof(u32)*num_args);
	mtu->pwms = kzalloc(sizeof(mtu->pwms), GFP_KERNEL);
	mtu->pwm_chip.npwm = 0;

	for (i = 0, j = 0; i < num_pwm; i++) {
		offset =  i*num_args;
		of_property_read_u32_index(np, "pwm_mode1", offset, &ch_num);

		/*
		 * pwm mode 1 supports for MTu3 channels[0:7].
		 * Only Setting for channel which has not assigned
		 * for pwm mode 1 function.
		 */
		if ((ch_num < 8) && (ch_num != 5) &&
		(mtu->channels[ch_num].function != MTU3_PWM_MODE_1)) {
			mtu->channels[ch_num].index = ch_num;
		/*
		 * In PWM MODE1, channel 1/2 of MTU3 can only output
		 * 1 pwm signal at their MTIOCA pins
		 * while others can use MTIOCC pins more.
		 */
			of_property_read_u32_index(np, "pwm_mode1",
						offset + 1, &tmp);
			if (tmp == 0 || (tmp == 1 &&
			   !(mtu->channels[ch_num].index == 1
			   || mtu->channels[ch_num].index == 2))) {
				mtu->pwms[j].output = tmp;
				mtu->channels[ch_num].function =
					MTU3_PWM_MODE_1;
				mtu->pwms[j].ch = ch_num;
				rzg2l_mtu3_setup_channel(&mtu->channels[ch_num],
							ch_num, mtu);
				mtu->pwm_chip.npwm += 1;
				dev_info(&mtu->pdev->dev,
					"ch%u: used as pwm mode 1 with pin %s\n",
					mtu->channels[ch_num].index,
					mtu->pwms[j].output ?
					"MTIOCC" : "MTIOCA");
				j++;
			}
		}
	}

	/* No pwm device set */
	if (j == 0)
		return -EINVAL;

	mtu->pwm_chip.dev = &mtu->pdev->dev;
	mtu->pwm_chip.ops = &mtu3_pwm_ops;
	mtu->pwm_chip.base = -1;
	ret = pwmchip_add(&mtu->pwm_chip);

	return ret;
}

static int rzg2l_mtu3_pwm_set_polarity(struct pwm_chip *chip,
					struct pwm_device *pwm,
					enum pwm_polarity polarity)
{
	pwm->state.polarity = polarity;
	return 0;
}

static const struct pwm_ops mtu3_pwm_ops = {
	.request        = rzg2l_mtu3_pwm_request,
	.free		= rzg2l_mtu3_pwm_free,
	.enable         = rzg2l_mtu3_pwm_enable,
	.disable        = rzg2l_mtu3_pwm_disable,
	.config         = rzg2l_mtu3_pwm_config,
	.set_polarity	= rzg2l_mtu3_pwm_set_polarity,
};

/* Setting mtu3 */
static int rzg2l_mtu3_setup(struct rzg2l_mtu3_device *mtu,
			struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret, i;

	mtu->pdev = pdev;
	raw_spin_lock_init(&mtu->lock);

	mtu->rstc = devm_reset_control_get(&pdev->dev, NULL);

	if (IS_ERR(mtu->rstc))
		dev_warn(&pdev->dev, "failed to get cpg reset\n");
	else
		reset_control_deassert(mtu->rstc);

	/* Get hold of clock. */
	mtu->clk = clk_get(&mtu->pdev->dev, "fck");
	if (IS_ERR(mtu->clk)) {
		dev_err(&mtu->pdev->dev, "cannot get clock\n");
		return PTR_ERR(mtu->clk);
	}

	ret = clk_prepare(mtu->clk);
	if (ret < 0)
		goto err_clk_put;

	ret = clk_enable(mtu->clk);
	if (ret < 0)
		goto err_clk_unprepare;
	mtu->rate = clk_get_rate(mtu->clk)/64;

	/* Map the memory resource. */
	ret = rzg2l_mtu3_map_memory(mtu);
	if (ret < 0) {
		dev_err(&mtu->pdev->dev, "failed to remap I/O memory\n");
		goto err_clk_unprepare;
	}

	/* Allocate and setup the channels. */
	mtu->has_clockevent = false;
	mtu->has_clocksource = false;
	mtu->num_channels = 9;

	mtu->channels = kcalloc(mtu->num_channels, sizeof(*mtu->channels),
		GFP_KERNEL);
	if (mtu->channels == NULL) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	ret = rzg2l_mtu3_register_pwm(mtu, np);
	if (ret < 0)
		dev_err(&mtu->pdev->dev,
			"failed to register PWM chip: %d\n", ret);
	else if (!ret)
		dev_info(&mtu->pdev->dev,
			" used for pwm controller of mtu3\n");

	for (i = 0; i < mtu->num_channels; i++) {
		if (mtu->channels[i].function != MTU3_PWM_MODE_1) {
			mtu->channels[i].index = i;
			if (!(mtu->has_clocksource)) {
				mtu->channels[i].function = MTU3_CLOCKSOURCE;
				ret = rzg2l_mtu3_setup_channel(
					&mtu->channels[i], i, mtu);
				if (ret < 0)
					goto err_unmap;
				mtu->has_clocksource = true;
			} else if (!(mtu->has_clockevent)) {
				mtu->channels[i].function = MTU3_CLOCKEVENT;
				ret = rzg2l_mtu3_setup_channel(
					&mtu->channels[i], i, mtu);
				if (ret < 0)
					goto err_unmap;
				mtu->has_clockevent = true;
			}
		}
	}

	clk_disable(mtu->clk);

	platform_set_drvdata(pdev, mtu);

	return 0;

err_unmap:
	kfree(mtu->channels);
	iounmap(mtu->mapbase);
err_clk_unprepare:
	clk_unprepare(mtu->clk);
err_clk_put:
	clk_put(mtu->clk);
	return ret;
}

static int rzg2l_mtu3_probe(struct platform_device *pdev)
{
	struct rzg2l_mtu3_device *mtu = platform_get_drvdata(pdev);
	int ret;

	if (!is_early_platform_device(pdev)) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	}

	if (mtu) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		goto out;
	}

	mtu = kzalloc(sizeof(*mtu), GFP_KERNEL);
	if (mtu == NULL)
		return -ENOMEM;

	ret = rzg2l_mtu3_setup(mtu, pdev);
	if (ret) {
		kfree(mtu);
		pm_runtime_idle(&pdev->dev);
		return ret;
	}

	if (is_early_platform_device(pdev))
		return 0;

out:
	if (mtu->has_clockevent)
		pm_runtime_irq_safe(&pdev->dev);
	else
		pm_runtime_idle(&pdev->dev);

	return 0;
}

static int rzg2l_mtu3_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent */
}

static const struct platform_device_id rzg2l_mtu3_id_table[] = {

	{ "sh-mtu3", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, rzg2l_mtu3_id_table);

static const struct of_device_id rzg2l_mtu3_of_table[] __maybe_unused = {
	{ .compatible = "renesas,rzg2l-mtu3" },
	{ }
};
MODULE_DEVICE_TABLE(of, rzg2l_mtu3_of_table);

static struct platform_driver rzg2l_mtu3_device_driver = {
	.probe		= rzg2l_mtu3_probe,
	.remove		= rzg2l_mtu3_remove,
	.driver		= {
		.name	= "rzg2l_mtu3",
		.of_match_table = of_match_ptr(rzg2l_mtu3_of_table),
	},
	.id_table	= rzg2l_mtu3_id_table,
};

static int __init rzg2l_mtu3_init(void)
{
	return platform_driver_register(&rzg2l_mtu3_device_driver);

}

static void __exit rzg2l_mtu3_exit(void)
{
	platform_driver_unregister(&rzg2l_mtu3_device_driver);
}

early_platform_init("earlytimer", &rzg2l_mtu3_device_driver);
subsys_initcall(rzg2l_mtu3_init);
module_exit(rzg2l_mtu3_exit);

MODULE_DESCRIPTION("RZ/G2L MTU3 Timer Driver");
MODULE_LICENSE("GPL v2");


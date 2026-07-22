// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L General PWM Timer (GPT) driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 *
 * Hardware manual for this IP can be found here
 * https://www.renesas.com/eu/en/document/mah/rzg2l-group-rzg2lc-group-users-manual-hardware-0?language=en
 * https://www.renesas.com/en/document/mah/rzg3e-group-users-manual-hardware
 *
 * Limitations:
 * - Counter must be stopped before modifying Mode and Prescaler.
 * - When PWM is disabled, the output is driven to inactive.
 * - For RZ/G2L, the General PWM Timer (GPT) has 8 HW channels for PWM
 *   operations and each HW channel have 2 IOs (GTIOCn{A, B}).
 * - Each IO is modelled as an independent PWM channel.
 * - For RZ/G3E, the General PWM Timer (GPT) has 16 HW channels for PWM
 *   operations (GPT0: 8 channels, GPT1: 8 Channels) and each HW channel
 *   have 4 IOs (GTIOCn{A,AN,B,BN}). The 2 extra IOs GTIOCnAN and GTIOCnBN
 *   in RZ/G3E are anti-phase signals of GTIOCnA and GTIOCnB. The
 *   anti-phase signals of RZ/G3E are not modelled as PWM channel.
 * - When both channels are used, disabling the channel on one stops the
 *   other.
 * - When both channels are used, the period of both IOs in the HW channel
 *   must be same (for now).
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/reset.h>
#include <linux/time.h>
#include <linux/units.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/irqchip/irq-renesas-rzv2h.h>

#define RZG2L_GET_CH(hwpwm)	((hwpwm) / 2)
#define RZG2L_GET_HWPWM(ch, sub_ch) ((ch) * 2 + (sub_ch))
#define RZG2L_GET_CH_OFFS(ch)	(0x100 * (ch))

#define RZG2L_GTICxSR(ch, sub_ch)	(0x24 + RZG2L_GET_CH_OFFS(ch) + 4 * (sub_ch))
#define RZG2L_GTCR(ch)		(0x2c + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTUDDTYC(ch)	(0x30 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTIOR(ch)		(0x34 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTINTAD(ch)	(0x38 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTST(ch)		(0x3c + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTBER(ch)		(0x40 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTCNT(ch)		(0x48 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTCCRAB(ch, sub_ch)	(0x4c + RZG2L_GET_CH_OFFS(ch) + 4 * (sub_ch))
#define RZG2L_GTCCRCE(ch, sub_ch)	(0x54 + RZG2L_GET_CH_OFFS(ch) + 4 * (sub_ch))
#define RZG2L_GTCCRDF(ch, sub_ch)	(0x5c + RZG2L_GET_CH_OFFS(ch) + 4 * (sub_ch))
#define RZG2L_GTPR(ch)		(0x64 + RZG2L_GET_CH_OFFS(ch))

#define RZG2L_GTCCRx_BUFFER_MASK(sub_ch)	((sub_ch) ? GENMASK(19, 18) : GENMASK(17, 16))
#define RZG2L_GTCCRx_SINGLE_BUFFER(sub_ch)	((sub_ch) ? BIT(18) : BIT(16))
#define RZG2L_GTCCRx_DOUBLE_BUFFER(sub_ch)	((sub_ch) ? BIT(19) : BIT(17))

#define RZG2L_GTST_TCFA		BIT(0)
#define RZG2L_GTST_TCFB		BIT(1)
#define RZG2L_GTST_TCFPO	BIT(6)

#define RZG2L_GTINTAD_GTINTPR_MASK	GENMASK(7, 6)
#define RZG2L_GTINTAD_GTINTPR_OVF	BIT(6)
#define RZG2L_GTINTAD_GTINTPR_UDF	BIT(7)
#define RZG2L_GTINTAD_GTINTPR_OVF_UDF	RZG2L_GTINTAD_GTINTPR_MASK

#define RZG2L_GTINTAD_GTINTA	BIT(0)
#define RZG2L_GTINTAD_GTINTB	BIT(1)
#define RZG2L_GTINTAD_GTINTx(sub_ch)	((sub_ch) ? RZG2L_GTINTAD_GTINTB : RZG2L_GTINTAD_GTINTA)
#define RZG2L_GTCR_CST		BIT(0)
#define RZG2L_GTCR_MD		GENMASK(18, 16)
#define RZG2L_GTCR_TPCS		GENMASK(26, 24)
#define RZG3E_GTCR_TPCS		GENMASK(26, 23)

#define RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE	FIELD_PREP(RZG2L_GTCR_MD, 0)
#define RZG2L_GTCR_TPCS_P0_1024		0x05
#define RZG3E_GTCR_TPCS_P0_1024		0x0A

#define RZG2L_GTUDDTYC_UP	BIT(0)
#define RZG2L_GTUDDTYC_UDF	BIT(1)
#define RZG2L_GTUDDTYC_UP_COUNTING	(RZG2L_GTUDDTYC_UP | RZG2L_GTUDDTYC_UDF)

#define RZG2L_GTIOR_GTIOA	GENMASK(4, 0)
#define RZG2L_GTIOR_GTIOB	GENMASK(20, 16)
#define RZG2L_GTIOR_GTIOx(sub_ch)	((sub_ch) ? RZG2L_GTIOR_GTIOB : RZG2L_GTIOR_GTIOA)
#define RZG2L_GTIOR_GTIOxINIT_OUT(sub_ch)	((sub_ch) ? BIT(20) : BIT(4))
#define RZG2L_GTIOR_OAE		BIT(8)
#define RZG2L_GTIOR_OBE		BIT(24)
#define RZG2L_GTIOR_OxE(sub_ch)		((sub_ch) ? RZG2L_GTIOR_OBE : RZG2L_GTIOR_OAE)

#define RZG2L_GTIOR_NFAEN	BIT(13)
#define RZG2L_GTIOR_NFBEN	BIT(29)
#define RZG2L_GTIOR_NFCSA	GENMASK(15, 14)
#define RZG2L_GTIOR_NFCSB	GENMASK(31, 30)
#define RZG2L_GTIOR_NFCSx(sub_ch)	((sub_ch) ? RZG2L_GTIOR_NFCSB : RZG2L_GTIOR_NFCSA)
#define RZG2L_GTIOR_NFCSx_P0_64		0x3

#define RZG2L_INIT_OUT_HI_OUT_HI_END_TOGGLE	0x1b
#define RZG2L_INIT_OUT_LOW_OUT_LOW_END_TOGGLE	0x07

#define RZG2L_GTIOR_GTIOA_OUT_HI_END_TOGGLE_CMP_MATCH \
	(RZG2L_INIT_OUT_HI_OUT_HI_END_TOGGLE | RZG2L_GTIOR_OAE)
#define RZG2L_GTIOR_GTIOB_OUT_HI_END_TOGGLE_CMP_MATCH \
	(FIELD_PREP(RZG2L_GTIOR_GTIOB, RZG2L_INIT_OUT_HI_OUT_HI_END_TOGGLE) | RZG2L_GTIOR_OBE)

#define RZG2L_GTIOR_GTIOA_OUT_LOW_END_TOGGLE_CMP_MATCH \
	(RZG2L_INIT_OUT_LOW_OUT_LOW_END_TOGGLE | RZG2L_GTIOR_OAE)
#define RZG2L_GTIOR_GTIOB_OUT_LOW_END_TOGGLE_CMP_MATCH \
	(FIELD_PREP(RZG2L_GTIOR_GTIOB, RZG2L_INIT_OUT_LOW_OUT_LOW_END_TOGGLE) | RZG2L_GTIOR_OBE)

#define RZG2L_GTIOR_GTIOx_OUT_HI_END_TOGGLE_CMP_MATCH(sub_ch) \
	((sub_ch) ? RZG2L_GTIOR_GTIOB_OUT_HI_END_TOGGLE_CMP_MATCH : \
	 RZG2L_GTIOR_GTIOA_OUT_HI_END_TOGGLE_CMP_MATCH)

#define RZG2L_GTIOR_GTIOx_OUT_LOW_END_TOGGLE_CMP_MATCH(sub_ch) \
	((sub_ch) ? RZG2L_GTIOR_GTIOB_OUT_LOW_END_TOGGLE_CMP_MATCH : \
	 RZG2L_GTIOR_GTIOA_OUT_LOW_END_TOGGLE_CMP_MATCH)

#define RZG2L_MAX_HW_CHANNELS	8
#define RZG2L_CHANNELS_PER_IO	2
#define RZG2L_MAX_PWM_CHANNELS	(RZG2L_MAX_HW_CHANNELS * RZG2L_CHANNELS_PER_IO)
#define RZG2L_MAX_SCALE_FACTOR	1024
#define RZG2L_MAX_TICKS		((u64)U32_MAX * RZG2L_MAX_SCALE_FACTOR)
#define RZG2L_GPT_HAS_OVF_IRQ	BIT(0)

#define RZG2L_GTPR_MAX_VALUE	0xFFFFFFFF
#define RZG2L_INPUT_CAP_GTIOB_BOTH_EDGE		0x0000F000
#define RZG2L_INPUT_CAP_GTIOB_RISING_EDGE	0x00003000
#define RZG2L_INPUT_CAP_GTIOB_FALLING_EDGE	0x0000C000
#define RZG2L_INPUT_CAP_GTIOA_BOTH_EDGE		0x00000F00
#define RZG2L_INPUT_CAP_GTIOA_RISING_EDGE	0x00000300
#define RZG2L_tmp				0x0100
#define RZG2L_INPUT_CAP_GTIOA_FALLING_EDGE	0x00000C00
#define RZG2L_INPUT_CAP_GTIOx_BOTH_EDGE(sub_ch) \
	((sub_ch) ? RZG2L_INPUT_CAP_GTIOB_BOTH_EDGE : RZG2L_INPUT_CAP_GTIOA_BOTH_EDGE)
#define RZG2L_INPUT_CAP_GTIOx_RISING_EDGE(sub_ch) \
	((sub_ch) ? RZG2L_INPUT_CAP_GTIOB_RISING_EDGE : RZG2L_INPUT_CAP_GTIOA_RISING_EDGE)
#define RZG2L_INPUT_CAP_GTIOx_FALLING_EDGE(sub_ch) \
	((sub_ch) ? RZG2L_INPUT_CAP_GTIOB_FALLING_EDGE : RZG2L_INPUT_CAP_GTIOA_FALLING_EDGE)

enum {
	GTCIV,
	GTCIA,
	GTCIB,
	NR_IRQ_TYPE,
};

struct gpt_irq_desc {
	char *name;
	irq_handler_t isr;
	int res_num;
};

struct rz_gpt_cpt_data {
	u64 snapshot[3];
	unsigned int index;
	unsigned int overflow_count;
	wait_queue_head_t wait;
};

struct rzg2l_gpt_info {
	u32 flags;
	u8 (*calculate_prescale)(u64 period);
	u32 gtcr_tpcs;
	u32 gtcr_tpcs_value;
	u8 prescale_mult;
};

struct rzg2l_gpt_chip {
	struct pwm_chip *chip;
	unsigned long clk;
	void __iomem *mmio;
	struct mutex mutex; /* lock to protect shared channel resources */
	struct rz_gpt_cpt_data *cpt_data;
	const struct rzg2l_gpt_info *info;
	unsigned long rate_khz;
	u32 period_ticks[RZG2L_MAX_HW_CHANNELS];
	u32 channel_request_count[RZG2L_MAX_HW_CHANNELS];
	u32 channel_enable_count[RZG2L_MAX_HW_CHANNELS];
	unsigned int irq_map[RZG2L_MAX_HW_CHANNELS][NR_IRQ_TYPE];
	spinlock_t lock;
};

static inline struct rzg2l_gpt_chip *to_rzg2l_gpt_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static inline unsigned int rzg2l_gpt_subchannel(unsigned int hwpwm)
{
	return hwpwm & 0x1;
}

static inline unsigned int rzg2l_gpt_sibling(unsigned int hwpwm)
{
	return hwpwm ^ 0x1;
}

static void rzg2l_gpt_write(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg, u32 data)
{
	writel(data, rzg2l_gpt->mmio + reg);
}

static u32 rzg2l_gpt_read(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg)
{
	return readl(rzg2l_gpt->mmio + reg);
}

static void rzg2l_gpt_modify(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg, u32 clr,
			     u32 set)
{
	rzg2l_gpt_write(rzg2l_gpt, reg,
			(rzg2l_gpt_read(rzg2l_gpt, reg) & ~clr) | set);
}

static u8 rzg2l_gpt_calculate_prescale(u64 period_ticks)
{
	u32 prescaled_period_ticks;
	u8 prescale;

	prescaled_period_ticks = period_ticks >> 32;
	if (prescaled_period_ticks >= 256)
		prescale = 5;
	else
		prescale = (fls(prescaled_period_ticks) + 1) / 2;

	return prescale;
}

static u8 rzg3e_gpt_calculate_prescale(u64 period_ticks)
{
	u32 prescaled_period_ticks;
	u8 prescale;

	prescaled_period_ticks = period_ticks >> 32;
	if (prescaled_period_ticks >= 64 && prescaled_period_ticks < 256) {
		prescale = 6;
	} else if (prescaled_period_ticks >= 256 && prescaled_period_ticks < 1024) {
		prescale = 8;
	} else if (prescaled_period_ticks >= 1024) {
		prescale = 10;
	} else {
		prescale = fls(prescaled_period_ticks);
		if (prescale > 1)
			prescale -= 1;
	}

	return prescale;
}

static int rzg2l_gpt_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);

	guard(mutex)(&rzg2l_gpt->mutex);
	rzg2l_gpt->channel_request_count[ch]++;

	return 0;
}

static void rzg2l_gpt_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);

	guard(mutex)(&rzg2l_gpt->mutex);
	rzg2l_gpt->channel_request_count[ch]--;
}

static bool rzg2l_gpt_is_ch_enabled(struct rzg2l_gpt_chip *rzg2l_gpt, u8 hwpwm)
{
	u8 ch = RZG2L_GET_CH(hwpwm);
	u32 val;

	val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	if (!(val & RZG2L_GTCR_CST))
		return false;

	val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTIOR(ch));

	return val & RZG2L_GTIOR_OxE(rzg2l_gpt_subchannel(hwpwm));
}

static void rzg2l_gpt_set_polarity(struct rzg2l_gpt_chip *rzg2l_gpt,
				   struct pwm_device *pwm,
				   enum pwm_polarity polarity)
{
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u32 val = RZG2L_GTIOR_GTIOx(sub_ch) | RZG2L_GTIOR_OxE(sub_ch);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	if (polarity == PWM_POLARITY_INVERSED)
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), val,
			RZG2L_GTIOR_GTIOx_OUT_LOW_END_TOGGLE_CMP_MATCH(sub_ch));
	else
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), val,
			RZG2L_GTIOR_GTIOx_OUT_HI_END_TOGGLE_CMP_MATCH(sub_ch));
}

/* Caller holds the lock while calling rzg2l_gpt_enable() */
static void rzg2l_gpt_enable(struct rzg2l_gpt_chip *rzg2l_gpt,
			     struct pwm_device *pwm)
{
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	if (!rzg2l_gpt->channel_enable_count[ch])
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), 0, RZG2L_GTCR_CST);

	rzg2l_gpt->channel_enable_count[ch]++;
}

/* Caller holds the lock while calling rzg2l_gpt_disable() */
static void rzg2l_gpt_disable(struct rzg2l_gpt_chip *rzg2l_gpt,
			      struct pwm_device *pwm)
{
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	/* Stop count, Output low on GTIOCx pin when counting stops */
	rzg2l_gpt->channel_enable_count[ch]--;

	if (!rzg2l_gpt->channel_enable_count[ch])
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

	/* Disable pin output */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), RZG2L_GTIOR_OxE(sub_ch), 0);
}

static u64 rzg2l_gpt_calculate_period_or_duty(struct rzg2l_gpt_chip *rzg2l_gpt,
					      u32 val, u8 prescale)
{
	const struct rzg2l_gpt_info *info = rzg2l_gpt->info;
	u64 tmp;

	/*
	 * The calculation doesn't overflow an u64 because prescale ≤ 5 and so
	 * tmp = val << (2 * prescale) * USEC_PER_SEC
	 *     < 2^32 * 2^10 * 10^6
	 *     < 2^32 * 2^10 * 2^20
	 *     = 2^62
	 */
	tmp = (u64)val << (info->prescale_mult * prescale);
	tmp *= USEC_PER_SEC;

	return DIV64_U64_ROUND_UP(tmp, rzg2l_gpt->rate_khz);
}

static int rzg2l_gpt_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);
	u32 val;

	state->enabled = rzg2l_gpt_is_ch_enabled(rzg2l_gpt, pwm->hwpwm);
	if (state->enabled) {
		u8 prescale;

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
		prescale = field_get(rzg2l_gpt->info->gtcr_tpcs, val);

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTPR(ch));
		state->period = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt, val, prescale);

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCCRAB(ch, sub_ch));
		state->duty_cycle = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt, val, prescale);
		if (state->duty_cycle > state->period)
			state->duty_cycle = state->period;
	}

	/* Set to High Output state for initial value */
	if (!pwm->state.enabled) {
		state->polarity = PWM_POLARITY_NORMAL;
		return 0;
	}

	val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTIOR(ch));
	if (field_get(RZG2L_GTIOR_GTIOxINIT_OUT(sub_ch), val))
		state->polarity = PWM_POLARITY_NORMAL;
	else
		state->polarity = PWM_POLARITY_INVERSED;
	return 0;
}

static u32 rzg2l_gpt_calculate_pv_or_dc(const struct rzg2l_gpt_info *info,
					u64 period_or_duty_cycle, u8 prescale)
{
	return min_t(u64,
		     DIV_ROUND_DOWN_ULL(period_or_duty_cycle,
					1 << (info->prescale_mult * prescale)),
		     U32_MAX);
}

/* Caller holds the lock while calling rzg2l_gpt_config() */
static int rzg2l_gpt_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	const struct rzg2l_gpt_info *info = rzg2l_gpt->info;
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);
	u64 period_ticks, duty_ticks;
	unsigned long pv, dc;
	u8 prescale;

	/* Limit period/duty cycle to max value supported by the HW */
	period_ticks = mul_u64_u64_div_u64(state->period, rzg2l_gpt->rate_khz, USEC_PER_SEC);
	if (period_ticks > RZG2L_MAX_TICKS)
		period_ticks = RZG2L_MAX_TICKS;
	/*
	 * GPT counter is shared by the two IOs of a single channel, so
	 * prescale and period can NOT be modified when there are multiple IOs
	 * in use with different settings.
	 */
	if (rzg2l_gpt->channel_request_count[ch] > 1) {
		u8 sibling_ch = rzg2l_gpt_sibling(pwm->hwpwm);

		if (rzg2l_gpt_is_ch_enabled(rzg2l_gpt, sibling_ch)) {
			if (period_ticks < rzg2l_gpt->period_ticks[ch])
				return -EBUSY;

			period_ticks = rzg2l_gpt->period_ticks[ch];
		}
	}

	prescale = info->calculate_prescale(period_ticks);
	pv = rzg2l_gpt_calculate_pv_or_dc(info, period_ticks, prescale);

	duty_ticks = mul_u64_u64_div_u64(state->duty_cycle, rzg2l_gpt->rate_khz, USEC_PER_SEC);
	if (duty_ticks > period_ticks)
		duty_ticks = period_ticks;
	dc = rzg2l_gpt_calculate_pv_or_dc(info, duty_ticks, prescale);

	/*
	 * GPT counter is shared by multiple channels, we cache the period ticks
	 * from the first enabled channel and use the same value for both
	 * channels.
	 */
	rzg2l_gpt->period_ticks[ch] = period_ticks;

	/*
	 * Use single-buffer mode for runtime duty updates. Write the new duty cycle to
	 * GTCCRCE (buffer register) so the value is latched safely and does not cause
	 * output glitches.
	 */
	if (pwm->state.enabled && rzg2l_gpt_is_ch_enabled(rzg2l_gpt, pwm->hwpwm)) {
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRCE(ch, sub_ch), dc);

		return 0;
	}

	/*
	 * Counter must be stopped before modifying mode, prescaler, timer
	 * counter and buffer enable registers. These registers are shared
	 * between both channels. So allow updating these registers only for the
	 * first enabled channel.
	 */
	if (rzg2l_gpt->channel_enable_count[ch] <= 1) {
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

		/* GPT set operating mode (saw-wave up-counting) */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_MD,
				 RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE);

		/* Set count direction */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUDDTYC(ch), RZG2L_GTUDDTYC_UP_COUNTING);

		/* Select count clock */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), rzg2l_gpt->info->gtcr_tpcs,
				 field_prep(rzg2l_gpt->info->gtcr_tpcs, prescale));

		/* Set period */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), pv);
	}

	/* Set duty cycle */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRCE(ch, sub_ch), dc);

	if (rzg2l_gpt->channel_enable_count[ch] <= 1) {
		/* Set initial value for counter */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);

		/* Set single buffer operation */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTBER(ch),
				RZG2L_GTCCRx_BUFFER_MASK(sub_ch),
				RZG2L_GTCCRx_SINGLE_BUFFER(sub_ch));

		/* Restart the counter after updating the registers */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch),
				 RZG2L_GTCR_CST, RZG2L_GTCR_CST);
	}

	return 0;
}

static int rzg2l_gpt_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	bool enabled = pwm->state.enabled;
	int ret;

	guard(mutex)(&rzg2l_gpt->mutex);
	if (!state->enabled) {
		if (enabled)
			rzg2l_gpt_disable(rzg2l_gpt, pwm);

		return 0;
	}

	ret = rzg2l_gpt_config(chip, pwm, state);
	if (!ret && !enabled)
		rzg2l_gpt_enable(rzg2l_gpt, pwm);

	/* Set polarity output */
	rzg2l_gpt_set_polarity(rzg2l_gpt, pwm, state->polarity);

	return ret;
}

static int rzg2l_gpt_get_ch_from_irq(struct rzg2l_gpt_chip *rzg2l_gpt,
				     int irq, unsigned int irq_type)
{
	int ch;

	for (ch = 0; ch < RZG2L_MAX_HW_CHANNELS; ch++) {
		if (rzg2l_gpt->irq_map[ch][irq_type] == irq)
			return ch;
	}
	return -EINVAL;
}

static int rzg2l_gpt_capture(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_capture *result, unsigned long timeout)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	struct rz_gpt_cpt_data *cpt_data = &rzg2l_gpt->cpt_data[pwm->hwpwm];
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u64 duty, period;
	unsigned int effective_ticks;
	int ret;

	if (rzg2l_gpt->channel_enable_count[ch]) {
		dev_err(rzg2l_gpt->chip->dev,
			"Please keep pwm%d and pwm%d are not enabled to use input capture\n",
			pwm->hwpwm, rzg2l_gpt_sibling(pwm->hwpwm));
		return -EBUSY;
	}

	guard(mutex)(&rzg2l_gpt->mutex);

	result->period = result->duty_cycle = 0;
	cpt_data->index = 0;
	cpt_data->snapshot[0] = 0;
	cpt_data->snapshot[1] = 0;
	cpt_data->snapshot[2] = 0;
	cpt_data->overflow_count = 0;

	/* Set operating mode GTCR.MD[2:0] */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_MD,
			RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE);

	/* Set count direction with GTUDDTYC[1:0]*/
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUDDTYC(ch), RZG2L_GTUDDTYC_UP);

	/* Set count clock GTCR.TPCS[2:0]
	 * Using lowest frequency P0/1024 to avoid overflow
	 */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), rzg2l_gpt->info->gtcr_tpcs,
			field_prep(rzg2l_gpt->info->gtcr_tpcs, rzg2l_gpt->info->gtcr_tpcs_value));

	/* Set cycle in GTPR */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), RZG2L_GTPR_MAX_VALUE);

	/* Set initial value in GTCNT */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);

	/*
	 * Set input pin as capture mode:
	 * - Using noise filter with P0/64 clock
	 */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), RZG2L_GTIOR_NFCSx(sub_ch),
			RZG2L_GTIOR_NFCSx_P0_64);

	/* Select input capture source in GTICASR and GTICBSR */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, sub_ch),
			RZG2L_INPUT_CAP_GTIOx_RISING_EDGE(sub_ch));

	/* Enable input capture and overflow interrupt*/
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTINTAD(ch),
			RZG2L_GTINTAD_GTINTx(sub_ch) | RZG2L_GTINTAD_GTINTPR_OVF);

	/* Start count operation set GTCR.CST to 1 to start count operation*/
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 1);

	ret = wait_event_interruptible_timeout(cpt_data->wait, cpt_data->index > 1,
		msecs_to_jiffies(timeout));

	if (ret == -ERESTARTSYS)
		goto out;

	switch (cpt_data->index) {
	case 0:
	case 1:
		result->period = 0;
		result->duty_cycle = 0;
		break;
	case 2:
		duty = cpt_data->snapshot[1] - cpt_data->snapshot[0];
		period = cpt_data->snapshot[2] - cpt_data->snapshot[0];

		effective_ticks = rzg2l_gpt->rate_khz/1024;
		result->period = mul_u64_u64_div_u64(period, USEC_PER_SEC, effective_ticks);
		result->duty_cycle = mul_u64_u64_div_u64(duty, USEC_PER_SEC, effective_ticks);

		break;
	default:
		dev_err(chip->dev, "Internal error\n");
		break;
	}
out:
	/* Disable capture operation */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, sub_ch), 0);

	/* Disable interrupt */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTINTAD(ch), 0);

	/* Stop count */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), rzg2l_gpt->info->gtcr_tpcs, 0);

	return 0;
}

static irqreturn_t gpt_gtciv_interrupt(int irq, void *data)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = data;
	int ch;

	ch = rzg2l_gpt_get_ch_from_irq(rzg2l_gpt, irq, GTCIV);
	if (ch < 0)
		return IRQ_NONE;

	guard(spinlock_irqsave)(&rzg2l_gpt->lock);

	/* Counting overflow triggered to support input capture mode */
	if (rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTICxSR(ch, 0)))
		rzg2l_gpt->cpt_data[RZG2L_GET_HWPWM(ch, 0)].overflow_count++;
	else if (rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTICxSR(ch, 1)))
		rzg2l_gpt->cpt_data[RZG2L_GET_HWPWM(ch, 1)].overflow_count++;

	/* Disable overflow interrupt flags */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTST(ch), RZG2L_GTST_TCFPO, 0);

	return IRQ_HANDLED;
}

static irqreturn_t gpt_gtcia_interrupt(int irq, void *data)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = data;
	struct rz_gpt_cpt_data *cpt_data;
	unsigned int pwm_id;
	uint32_t tmp = 0;
	int ch;

	ch = rzg2l_gpt_get_ch_from_irq(rzg2l_gpt, irq, GTCIA);
	if (ch < 0)
		return IRQ_NONE;

	guard(spinlock_irqsave)(&rzg2l_gpt->lock);

	pwm_id = RZG2L_GET_HWPWM(ch, 0);

	cpt_data = &rzg2l_gpt->cpt_data[pwm_id];

	cpt_data->snapshot[cpt_data->index] =
		rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCCRAB(ch, 0)) +
		(cpt_data->overflow_count) * RZG2L_GTPR_MAX_VALUE;

	switch (cpt_data->index) {
	case 0:
	case 1:
		tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTICxSR(ch, 0));
		if (tmp & RZG2L_INPUT_CAP_GTIOA_RISING_EDGE)
			rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, 0),
					RZG2L_INPUT_CAP_GTIOA_FALLING_EDGE);
		if (tmp & RZG2L_INPUT_CAP_GTIOA_FALLING_EDGE)
			rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, 0),
					RZG2L_INPUT_CAP_GTIOA_RISING_EDGE);
		cpt_data->index++;
		break;
	case 2:
		/* Disable capture operation */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, 0), 0);
		wake_up(&cpt_data->wait);
		break;
	default:
		return IRQ_NONE;
	}

	/* Disable interrupt flags */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTST(ch), RZG2L_GTST_TCFA, 0);

	return IRQ_HANDLED;
}

static irqreturn_t gpt_gtcib_interrupt(int irq, void *data)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = data;
	struct rz_gpt_cpt_data *cpt_data;
	unsigned int pwm_id;
	uint32_t tmp = 0;
	int ch;

	ch = rzg2l_gpt_get_ch_from_irq(rzg2l_gpt, irq, GTCIB);
	if (ch < 0)
		return IRQ_NONE;

	guard(spinlock_irqsave)(&rzg2l_gpt->lock);

	pwm_id = RZG2L_GET_HWPWM(ch, 1);

	cpt_data = &rzg2l_gpt->cpt_data[pwm_id];

	cpt_data->snapshot[cpt_data->index] =
		rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCCRAB(ch, 1)) +
		(cpt_data->overflow_count) * RZG2L_GTPR_MAX_VALUE;

	switch (cpt_data->index) {
	case 0:
	case 1:
		tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTICxSR(ch, 1));
		if (tmp & RZG2L_INPUT_CAP_GTIOB_RISING_EDGE)
			rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, 1),
					RZG2L_INPUT_CAP_GTIOB_FALLING_EDGE);
		if (tmp & RZG2L_INPUT_CAP_GTIOB_FALLING_EDGE)
			rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, 1),
					RZG2L_INPUT_CAP_GTIOB_RISING_EDGE);
		cpt_data->index++;
		break;
	case 2:
		/* Disable capture operation */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, 1), 0);
		wake_up(&cpt_data->wait);
		break;
	default:
		return IRQ_NONE;
	}

	/* Disable interrupt flags */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTST(ch), RZG2L_GTST_TCFB, 0);

	return IRQ_HANDLED;
}

static const struct pwm_ops rzg2l_gpt_ops = {
	.request = rzg2l_gpt_request,
	.free = rzg2l_gpt_free,
	.get_state = rzg2l_gpt_get_state,
	.apply = rzg2l_gpt_apply,
	.capture = rzg2l_gpt_capture,
};

static const struct gpt_irq_desc gpt_irqs[] = {
	{ .name = "gtcia_n", .isr = gpt_gtcia_interrupt, .res_num = GTCIA },
	{ .name = "gtcib_n", .isr = gpt_gtcib_interrupt, .res_num = GTCIB },
	{ .name = "gtciv_n", .isr = gpt_gtciv_interrupt, .res_num = GTCIV },
};

static int rzg2l_gpt_probe(struct platform_device *pdev)
{
	struct rzg2l_gpt_chip *rzg2l_gpt;
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	struct pwm_chip *chip;
	struct device_node *icu_np __free(device_node);
	unsigned long rate;
	struct clk *clk;
	unsigned int i, j;
	int ret, irq;
	char *irq_name, *req_name;
	u32 channel_id;
	bool use_icu_ovf;

	chip = devm_pwmchip_alloc(dev, RZG2L_MAX_PWM_CHANNELS, sizeof(*rzg2l_gpt));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	rzg2l_gpt = to_rzg2l_gpt_chip(chip);

	rzg2l_gpt->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rzg2l_gpt->mmio))
		return PTR_ERR(rzg2l_gpt->mmio);

	rzg2l_gpt->info = of_device_get_match_data(dev);

	rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc), "Cannot get reset control\n");
	reset_control_deassert(rstc);

	rstc = devm_reset_control_get_optional_exclusive(dev, "rst_s");
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc),
				"Cannot get rst_s reset\n");
	if (rstc) {
		reset_control_deassert(rstc);
		if (IS_ERR(rstc))
			return dev_err_probe(dev, PTR_ERR(rstc),
					"Cannot deassert rst_s reset\n");
	}

	use_icu_ovf = !(rzg2l_gpt->info->flags & RZG2L_GPT_HAS_OVF_IRQ);
	if (use_icu_ovf) {
		ret = of_property_read_u32(dev->of_node, "gpt-channel-id", &channel_id);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to read gpt-channel-id\n");

		icu_np = of_parse_phandle(dev->of_node, "renesas,icu", 0);
		if (!icu_np)
			return dev_err_probe(dev, -ENODEV, "Failed to parse ICU node\n");
	}

	for (i = 0; i < RZG2L_MAX_HW_CHANNELS; i++) {
		for (j = 0; j < ARRAY_SIZE(gpt_irqs); j++) {
			irq_name = devm_kasprintf(dev, GFP_KERNEL, "%s_%u",
						  gpt_irqs[j].name, i);
			if (!irq_name) {
				ret = -ENOMEM;
				return ret;
			}
			if (use_icu_ovf && gpt_irqs[j].res_num == GTCIV) {
				irq = rzv2h_icu_gpt_irq_mapping(icu_np, i, channel_id);
				if (irq == -ENODEV)
					irq = -EPROBE_DEFER;
			} else
				irq = platform_get_irq_byname(pdev, irq_name);

			/* irq_create_fwspec_mapping() returns 0 on failure */
			if (irq <= 0) {
				ret = dev_err_probe(dev, (irq < 0) ? irq : -ENXIO,
						"Failed to obtain IRQ (%s)\n", irq_name);
				return ret;
			}

			req_name = devm_kasprintf(dev, GFP_KERNEL, "%s:%s",
						  dev_name(dev), irq_name);
			if (!req_name) {
				ret = -ENOMEM;
				return ret;
			}

			ret = devm_request_irq(dev, irq, gpt_irqs[j].isr, 0,
					       req_name, rzg2l_gpt);
			if (ret) {
				ret = dev_err_probe(dev, ret,
						"Failed to request IRQ (%s)\n", req_name);
				return ret;
			}
			rzg2l_gpt->irq_map[i][gpt_irqs[j].res_num] = irq;
		}
	}

	clk = devm_clk_get_optional_enabled(dev, "bus");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Cannot get bus clock\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Cannot get clock\n");

	ret = devm_clk_rate_exclusive_get(dev, clk);
	if (ret)
		return ret;

	rate = clk_get_rate(clk);
	if (!rate)
		return dev_err_probe(dev, -EINVAL, "The gpt clk rate is 0");

	/*
	 * Refuse clk rates > 1 GHz to prevent overflow later for computing
	 * period and duty cycle.
	 */
	if (rate > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL, "The gpt clk rate is > 1GHz");

	/*
	 * Rate is in MHz and is always integer for peripheral clk
	 * 2^32 * 2^10 (prescalar) * 10^6 (rate_khz) < 2^64
	 * So make sure rate is multiple of 1000.
	 */
	rzg2l_gpt->rate_khz = rate / KILO;
	if (rzg2l_gpt->rate_khz * KILO != rate)
		return dev_err_probe(dev, -EINVAL, "Rate is not multiple of 1000");

	mutex_init(&rzg2l_gpt->mutex);

	rzg2l_gpt->cpt_data = devm_kcalloc(dev, RZG2L_MAX_PWM_CHANNELS,
					   sizeof(*rzg2l_gpt->cpt_data),
					   GFP_KERNEL);

	if (!rzg2l_gpt->cpt_data)
		return -ENOMEM;

	for (i = 0; i < RZG2L_MAX_PWM_CHANNELS; i++) {
		struct rz_gpt_cpt_data *cpt_data = &rzg2l_gpt->cpt_data[i];

		init_waitqueue_head(&cpt_data->wait);
	}

	chip->ops = &rzg2l_gpt_ops;
	rzg2l_gpt->chip = chip;
	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add PWM chip\n");

	return 0;
}

static const struct rzg2l_gpt_info rzg3e_data = {
	.calculate_prescale = rzg3e_gpt_calculate_prescale,
	.gtcr_tpcs = RZG3E_GTCR_TPCS,
	.gtcr_tpcs_value = RZG3E_GTCR_TPCS_P0_1024,
	.prescale_mult = 1,
};

static const struct rzg2l_gpt_info rzg2l_data = {
	.flags = RZG2L_GPT_HAS_OVF_IRQ,
	.calculate_prescale = rzg2l_gpt_calculate_prescale,
	.gtcr_tpcs = RZG2L_GTCR_TPCS,
	.gtcr_tpcs_value = RZG2L_GTCR_TPCS_P0_1024,
	.prescale_mult = 2,
};

static const struct of_device_id rzg2l_gpt_of_table[] = {
	{ .compatible = "renesas,r9a09g047-gpt", .data = &rzg3e_data },
	{ .compatible = "renesas,rzg2l-gpt", .data = &rzg2l_data },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_gpt_of_table);

static struct platform_driver rzg2l_gpt_driver = {
	.driver = {
		.name = "pwm-rzg2l-gpt",
		.of_match_table = rzg2l_gpt_of_table,
	},
	.probe = rzg2l_gpt_probe,
};
module_platform_driver(rzg2l_gpt_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L General PWM Timer (GPT) Driver");
MODULE_LICENSE("GPL");

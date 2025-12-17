// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L General PWM Timer (GPT) driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 *
 * Hardware manual for this IP can be found here
 * https://www.renesas.com/eu/en/document/mah/rzg2l-group-rzg2lc-group-users-manual-hardware-0?language=en
 *
 * Limitations:
 * - Counter must be stopped before modifying Mode and Prescaler.
 * - When PWM is disabled, the output is driven to inactive.
 * - General PWM Timer (GPT) has 8 HW channels for PWM operations and
 *   each HW channel have 2 IOs.
 * - Each IO is modelled as an independent PWM channel.
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
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/wait.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/poeg-rzg2l.h>

#define RZG2L_GET_CH(hwpwm)	((hwpwm) / 2)
#define RZG2L_GET_HWPWM(ch, sub_ch) ((ch) * 2 + (sub_ch))
#define RZG2L_GET_CH_OFFS(ch)	(0x100 * (ch))

#define RZG2L_GTSSR(ch)		(0x10 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTPSR(ch)		(0x14 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTCSR(ch)		(0x18 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTUPSR(ch)	(0x1C + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTDNSR(ch)	(0x20 + RZG2L_GET_CH_OFFS(ch))
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
#define RZG2L_GTDTCR(ch)	(0x88 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTDVU(ch)		(0x8C + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTDVD(ch)		(0x90 + RZG2L_GET_CH_OFFS(ch))

#define RZG2L_GTCCRA_BUFFER_MASK	GENMASK(17, 16)
#define RZG2L_GTCCRB_BUFFER_MASK	GENMASK(19, 18)
#define RZG2L_GTCCRx_BUFFER_MASK(sub_ch) \
	((sub_ch) ? RZG2L_GTCCRB_BUFFER_MASK : RZG2L_GTCCRA_BUFFER_MASK)

#define RZG2L_GTCCRA_SINGLE_BUFFER	FIELD_PREP(RZG2L_GTCCRA_BUFFER_MASK, 0x1)
#define RZG2L_GTCCRB_SINGLE_BUFFER	FIELD_PREP(RZG2L_GTCCRB_BUFFER_MASK, 0x1)
#define RZG2L_GTCCRx_SINGLE_BUFFER(sub_ch) \
	((sub_ch) ? RZG2L_GTCCRB_SINGLE_BUFFER : RZG2L_GTCCRA_SINGLE_BUFFER)

#define RZG2L_GTCCRA_DOUBLE_BUFFER	FIELD_PREP(RZG2L_GTCCRA_BUFFER_MASK, 0x2)
#define RZG2L_GTCCRB_DOUBLE_BUFFER	FIELD_PREP(RZG2L_GTCCRB_BUFFER_MASK, 0x2)
#define RZG2L_GTCCRx_DOUBLE_BUFFER(sub_ch) \
	((sub_ch) ? RZG2L_GTCCRB_DOUBLE_BUFFER : RZG2L_GTCCRA_DOUBLE_BUFFER)

#define RZG2L_GTST_TCFA		BIT(0)
#define RZG2L_GTST_TCFB		BIT(1)
#define RZG2L_GTST_TCFPO	BIT(6)

#define RZG2L_GTINTAD_GTINTPR_MASK	GENMASK(7, 6)
#define RZG2L_GTINTPROV		BIT(6)

#define RZG2L_GTDTCR_DEADTIME_MODE	BIT(0)
#define RZG2L_GTINTAD_GTINTA	BIT(0)
#define RZG2L_GTINTAD_GTINTB	BIT(1)
#define RZG2L_GTINTAD_GTINTx(sub_ch)	((sub_ch) ? RZG2L_GTINTAD_GTINTB : RZG2L_GTINTAD_GTINTA)
#define RZG2L_GTINTAD_OUTPUT_DISABLE_GRP_MASK	GENMASK(25, 24)
#define RZG2L_GTINTAD_GRPA	(0U << 24)
#define RZG2L_GTINTAD_GRPB	(1U << 24)
#define RZG2L_GTINTAD_GRPC	(2U << 24)
#define RZG2L_GTINTAD_GRPD	(3U << 24)

#define RZG2L_GTINTAD_OUTPUT_DISABLE_POEG_MASK		GENMASK(30, 28)
#define RZG2L_GTINTAD_OUTPUT_DISABLE_DEADTIME_ERROR	BIT(28)
#define RZG2L_GTINTAD_OUTPUT_DISABLE_SAME_LEVEL_HIGH	BIT(29)
#define RZG2L_GTINTAD_OUTPUT_DISABLE_SAME_LEVEL_LOW	BIT(30)

#define RZG2L_GTBER_BUFFER_DEADTIME	BIT(22)

#define RZG2L_GTCR_CST		BIT(0)
#define RZG2L_GTCR_MD		GENMASK(18, 16)
#define RZG2L_GTCR_TPCS		GENMASK(26, 24)

#define RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE	FIELD_PREP(RZG2L_GTCR_MD, 0)
#define RZG2L_GTCR_MD_SAW_WAVE_ONE_SHOT	FIELD_PREP(RZG2L_GTCR_MD, 1)
#define RZG2L_GTCR_TPCS_P0_1024		0x05

#define RZG2L_GTUDDTYC_UP	BIT(0)
#define RZG2L_GTUDDTYC_UDF	BIT(1)
#define RZG2L_GTUDDTYC_UP_COUNTING	(RZG2L_GTUDDTYC_UP | RZG2L_GTUDDTYC_UDF)

#define RZG2L_GTIOR_GTIOA	GENMASK(4, 0)
#define RZG2L_GTIOR_GTIOB	GENMASK(20, 16)
#define RZG2L_GTIOR_GTIOx(sub_ch)	((sub_ch) ? RZG2L_GTIOR_GTIOB : RZG2L_GTIOR_GTIOA)
#define RZG2L_GTIOR_OAE		BIT(8)
#define RZG2L_GTIOR_OBE		BIT(24)
#define RZG2L_GTIOR_OxE(sub_ch)		((sub_ch) ? RZG2L_GTIOR_OBE : RZG2L_GTIOR_OAE)

#define RZG2L_GTIOR_GTIOR_CHANNEL_A_OUTPUT_DISABLE_MASK	GENMASK(10, 9)
#define RZG2L_GTIOR_GTIOR_CHANNEL_B_OUTPUT_DISABLE_MASK GENMASK(26, 25)
#define RZG2L_GTIOR_GTIOR_CHANNEL_x_OUTPUT_DISABLE_MASK(sub_ch)	\
	((sub_ch) ? RZG2L_GTIOR_GTIOR_CHANNEL_B_OUTPUT_DISABLE_MASK : \
		    RZG2L_GTIOR_GTIOR_CHANNEL_A_OUTPUT_DISABLE_MASK)
#define RZG2L_GTIOR_GTIOA_OUTPUT_DISABLE BIT(9)
#define RZG2L_GTIOR_GTIOB_OUTPUT_DISABLE BIT(25)
#define RZG2L_GTIOR_GTIOx_OUTPUT_DISABLE(sub_ch) \
	((sub_ch) ? RZG2L_GTIOR_GTIOB_OUTPUT_DISABLE : RZG2L_GTIOR_GTIOA_OUTPUT_DISABLE)

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

#define RZG2L_GTPR_MAX_VALUE			0xFFFFFFFF
#define RZG2L_INPUT_CAP_GTIOB_BOTH_EDGE		0x0000F000
#define RZG2L_INPUT_CAP_GTIOB_RISING_EDGE	0x00003000
#define RZG2L_INPUT_CAP_GTIOB_FALLING_EDGE	0x0000C000
#define RZG2L_INPUT_CAP_GTIOA_BOTH_EDGE		0x00000F00
#define RZG2L_INPUT_CAP_GTIOA_RISING_EDGE	0x00000300
#define RZG2L_INPUT_CAP_GTIOA_FALLING_EDGE	0x00000C00
#define RZG2L_INPUT_CAP_GTIOx_BOTH_EDGE(sub_ch) \
	((sub_ch) ? RZG2L_INPUT_CAP_GTIOB_BOTH_EDGE : RZG2L_INPUT_CAP_GTIOA_BOTH_EDGE)
#define RZG2L_INPUT_CAP_GTIOx_RISING_EDGE(sub_ch) \
	((sub_ch) ? RZG2L_INPUT_CAP_GTIOB_RISING_EDGE : RZG2L_INPUT_CAP_GTIOA_RISING_EDGE)
#define RZG2L_INPUT_CAP_GTIOx_FALLING_EDGE(sub_ch) \
	((sub_ch) ? RZG2L_INPUT_CAP_GTIOB_FALLING_EDGE : RZG2L_INPUT_CAP_GTIOA_FALLING_EDGE)

#define PHASE_COUNTING_PARAM(_up, _down) \
	{ .gtupsr = (_up), .gtdnsr = (_down), }

#define RESET_COUNTER_PARAM(_gtssr, _gtpsr, _gtcsr) \
	{ .gtssr = (_gtssr), .gtpsr = (_gtpsr), .gtcsr = (_gtcsr), }

/* Support GPT Error Interrupt Status Control for RZ/G3L only */
#define RZG3L_PEISR_OFFSET		0x0088
#define RZG3L_PEVSTATn_BIT(ch)		BIT(ch)

struct rz_gpt_data_cfg {
	bool has_icu_errint_status;
};

static const struct rz_gpt_data_cfg rzg2l_cfg = {
	.has_icu_errint_status = false,
};

static const struct rz_gpt_data_cfg rzg3l_cfg = {
	.has_icu_errint_status = true,
};

enum {
	NOT_USE,
	RZG2L_CHANNEL_A,
	RZG2L_CHANNEL_B,
	RZG2L_BOTH_AB,
};

enum {
	BUFF_0,
	BUFF_1,
	BUFF_2,
	NR_BUFFER,
};

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

static const char *rzg2l_gpt_POEGs[5] = {
	"NOT_USE",
};

struct POEG_params {
	u32 poeg;
	struct platform_device *poeg_dev;
};

static struct POEG_params POEG_mode_set[5] = {
	[NOT_USE] = {
		.poeg = 0,
		.poeg_dev = NULL,
	},
};

struct rz_gpt_cpt_data {
	u64 snapshot[3];
	unsigned int index;
	unsigned int overflow_count;
	wait_queue_head_t wait;
};

struct rz_gpt_pwm_channel {
	unsigned int buffer_mode_count;
	unsigned long buffer[NR_BUFFER];
	unsigned int operation;
	unsigned long deadtime_first, deadtime_second;
};

struct rzg2l_gpt_chip {
	struct pwm_chip *chip;
	void __iomem *mmio;
	struct regmap *icu_regmap;
	struct mutex mutex; /* lock to protect shared channel resources */
	const struct rz_gpt_data_cfg *cfg;
	struct rz_gpt_cpt_data *cpt_data;
	unsigned long rate_khz;
	u32 period_ticks[RZG2L_MAX_HW_CHANNELS];
	u32 counter_mode[RZG2L_MAX_HW_CHANNELS];
	u32 reset_counter[RZG2L_MAX_HW_CHANNELS];
	u8 channel_request[RZG2L_MAX_HW_CHANNELS];
	u8 channel_enable[RZG2L_MAX_HW_CHANNELS];
	unsigned int irq_map[RZG2L_MAX_HW_CHANNELS][NR_IRQ_TYPE];
	struct rz_gpt_pwm_channel channel_data[RZG2L_MAX_PWM_CHANNELS];
	u8 poeg;
	spinlock_t lock;
};

static const char *const gpt_operation_enum[] = {
	"normal_output",
	"single_buffer_output",
	"double_buffer_output",
	"deadtime_output",
	"counting_input",
};

enum {
	NORMAL_OUTPUT,
	SINGLE_BUFFER_OUTPUT,
	DOUBLE_BUFFER_OUTPUT,
	DEADTIME_OUTPUT,
	COUNTING_INPUT,
	NR_GPT_OPERATION,
};

static const char *const rzg2l_gpt_counter_modes[] = {
	"mode-1",
	"mode-2A",
	"mode-2B",
	"mode-2C",
	"mode-3A",
	"mode-3B",
	"mode-3C",
	"mode-4",
	"mode-5A",
	"mode-5B",
};

enum {
	MODE_1,
	MODE_2A,
	MODE_2B,
	MODE_2C,
	MODE_3A,
	MODE_3B,
	MODE_3C,
	MODE_4,
	MODE_5A,
	MODE_5B,
	NR_MODE,
};

struct counter_mode_params {
	u32 gtupsr;
	u32 gtdnsr;
};

static const char *rzg2l_gpt_reset_counters[5] = {
	"NOT_USE",
};

struct reset_counter_params {
	u32 gtssr;
	u32 gtpsr;
	u32 gtcsr;
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

static u32 rzg2l_gpt_read_mask(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg, u32 clr)
{
	return rzg2l_gpt_read(rzg2l_gpt, reg) & clr;
}

static inline int hwpwm_from_pwmdev(struct device *dev)
{
	unsigned int pwm_id;
	int ret;

	ret = sscanf(dev_name(dev), "pwm%d", &pwm_id);
	if (ret != 1) {
		dev_err(dev, "Failed to parse PWM ID from device name\n");
		return -EINVAL;
	}
	return pwm_id;
}

static u8 rzg2l_gpt_calculate_prescale(struct rzg2l_gpt_chip *rzg2l_gpt,
				       u64 period_ticks)
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

static int rzg2l_gpt_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);

	guard(mutex)(&rzg2l_gpt->mutex);
	rzg2l_gpt->channel_request[ch] |= BIT(sub_ch);

	return 0;
}

static void rzg2l_gpt_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);

	guard(mutex)(&rzg2l_gpt->mutex);
	rzg2l_gpt->channel_request[ch] &= ~BIT(sub_ch);
}

static bool rzg2l_gpt_is_deadtime_mode(struct rzg2l_gpt_chip *rzg2l_gpt, u8 hwpwm)
{
	u8 ch = RZG2L_GET_CH(hwpwm);
	u32 val;

	val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTDTCR(ch));

	return val & RZG2L_GTDTCR_DEADTIME_MODE;
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

	if (polarity == PWM_POLARITY_INVERSED) {
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), val,
			RZG2L_GTIOR_GTIOx_OUT_LOW_END_TOGGLE_CMP_MATCH(sub_ch));
	} else if (polarity == PWM_POLARITY_NORMAL) {
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), val,
			RZG2L_GTIOR_GTIOx_OUT_HI_END_TOGGLE_CMP_MATCH(sub_ch));
	}
}

/* Caller holds the lock while calling rzg2l_gpt_enable() */
static void rzg2l_gpt_enable(struct rzg2l_gpt_chip *rzg2l_gpt,
			     struct pwm_device *pwm)
{
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	/* Enable pin output */
	rzg2l_gpt_set_polarity(rzg2l_gpt, pwm, pwm->state.polarity);

	if (!rzg2l_gpt_is_ch_enabled(rzg2l_gpt, pwm->hwpwm))
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), 0, RZG2L_GTCR_CST);

	rzg2l_gpt->channel_enable[ch] |= BIT(sub_ch);
}

/* Caller holds the lock while calling rzg2l_gpt_disable() */
static void rzg2l_gpt_disable(struct rzg2l_gpt_chip *rzg2l_gpt,
			      struct pwm_device *pwm)
{
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	/* Stop count, Output low on GTIOCx pin when counting stops */
	rzg2l_gpt->channel_enable[ch] &= ~BIT(sub_ch);

	if (!rzg2l_gpt->channel_enable[ch])
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

	/* Disable pin output */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), RZG2L_GTIOR_OxE(sub_ch), 0);

	/* Set Negative-Phase Waveform by default */
	if (rzg2l_gpt_is_deadtime_mode(rzg2l_gpt, pwm->hwpwm))
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDTCR(ch), 0);
}

static void rzg2l_reset_period_and_duty(struct rzg2l_gpt_chip *rzg2l_gpt, unsigned int pwm_id)
{
	struct pwm_device *pwm = &rzg2l_gpt->chip->pwms[pwm_id];
	u8 ch = RZG2L_GET_CH(pwm_id);

	rzg2l_gpt_disable(rzg2l_gpt, pwm);

	if (rzg2l_gpt_is_deadtime_mode(rzg2l_gpt, pwm_id)) {
		u8 sibling_ch = rzg2l_gpt_sibling(pwm_id);
		struct pwm_device *sibling_pwm = &rzg2l_gpt->chip->pwms[sibling_ch];

		rzg2l_gpt->channel_data[sibling_ch].operation = DEADTIME_OUTPUT;
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDVU(ch), 0);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDVD(ch), 0);
		rzg2l_gpt->channel_data[pwm_id].deadtime_first = 0;
		rzg2l_gpt->channel_data[pwm_id].deadtime_second = 0;

		/* Reset the channel B states */
		sibling_pwm->state = (struct pwm_state){ 0 };
	}

	if (rzg2l_gpt->channel_data[pwm_id].operation == COUNTING_INPUT) {
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTSSR(ch), 0);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPSR(ch), 0);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCSR(ch), 0);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUPSR(ch), 0);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDNSR(ch), 0);
		rzg2l_gpt->counter_mode[ch] = 0;
		rzg2l_gpt->reset_counter[ch] = 0;
	}

	rzg2l_gpt->channel_data[pwm_id].buffer_mode_count = 0;
	for (unsigned int i = 0; i < NR_BUFFER; i++)
		rzg2l_gpt->channel_data[pwm_id].buffer[i] = 0;

	/* Reset the channel (A or B) states */
	pwm->state = (struct pwm_state){ 0 };
}

static u64 rzg2l_gpt_calculate_period_or_duty(struct rzg2l_gpt_chip *rzg2l_gpt,
					      u32 val, u8 prescale)
{
	u64 tmp;

	/*
	 * The calculation doesn't overflow an u64 because prescale ≤ 5 and so
	 * tmp = val << (2 * prescale) * USEC_PER_SEC
	 *     < 2^32 * 2^10 * 10^6
	 *     < 2^32 * 2^10 * 2^20
	 *     = 2^62
	 */
	tmp = (u64)val << (2 * prescale);
	tmp *= USEC_PER_SEC;

	return DIV64_U64_ROUND_UP(tmp, rzg2l_gpt->rate_khz);
}

static int rzg2l_gpt_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);

	state->enabled = rzg2l_gpt_is_ch_enabled(rzg2l_gpt, pwm->hwpwm);
	if (state->enabled) {
		u32 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
		u32 ch = RZG2L_GET_CH(pwm->hwpwm);
		u8 prescale;
		u32 val;

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
		prescale = FIELD_GET(RZG2L_GTCR_TPCS, val);

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTPR(ch));
		state->period = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt, val, prescale);

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCCRAB(ch, sub_ch));

		state->duty_cycle = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt, val, prescale);
		if (state->duty_cycle > state->period)
			state->duty_cycle = state->period;
	}

	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static u32 rzg2l_gpt_calculate_pv_or_dc(u64 period_or_duty_cycle, u8 prescale)
{
	return min_t(u64, DIV_ROUND_DOWN_ULL(period_or_duty_cycle, 1 << (2 * prescale)),
		     U32_MAX);
}

static void rzg2l_gpt_buffer_setting(struct rzg2l_gpt_chip *rzg2l_gpt, u8 hwpwm)
{
	u8 ch = RZG2L_GET_CH(hwpwm);
	u8 sub_ch = rzg2l_gpt_subchannel(hwpwm);

	switch (rzg2l_gpt->channel_data[hwpwm].operation) {
	case NORMAL_OUTPUT:
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRAB(ch, sub_ch),
				rzg2l_gpt->channel_data[hwpwm].buffer[BUFF_0]);
		break;
	case SINGLE_BUFFER_OUTPUT:
		if (rzg2l_gpt->channel_data[hwpwm].buffer_mode_count == 0)
			rzg2l_gpt->channel_data[hwpwm].buffer_mode_count = 2;

		rzg2l_gpt->channel_data[hwpwm].buffer_mode_count--;

		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRCE(ch, sub_ch),
				rzg2l_gpt->channel_data[hwpwm].buffer[
				rzg2l_gpt->channel_data[hwpwm].buffer_mode_count]);
		break;
	case DOUBLE_BUFFER_OUTPUT:
		if (rzg2l_gpt->channel_data[hwpwm].buffer_mode_count == 0)
			rzg2l_gpt->channel_data[hwpwm].buffer_mode_count = 3;

		rzg2l_gpt->channel_data[hwpwm].buffer_mode_count--;

		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRDF(ch, sub_ch),
				rzg2l_gpt->channel_data[hwpwm].buffer[
				rzg2l_gpt->channel_data[hwpwm].buffer_mode_count]);
		break;
	case DEADTIME_OUTPUT:
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRCE(ch, sub_ch),
				rzg2l_gpt->channel_data[hwpwm].buffer[BUFF_1]);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCRDF(ch, sub_ch),
				rzg2l_gpt->channel_data[hwpwm].buffer[BUFF_2]);
		break;
	}
}

/* Caller holds the lock while calling rzg2l_gpt_config() */
static int rzg2l_gpt_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
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
	if (rzg2l_gpt->channel_request[ch] == RZG2L_BOTH_AB) {
		u8 sibling_ch = rzg2l_gpt_sibling(pwm->hwpwm);

		if (rzg2l_gpt_is_ch_enabled(rzg2l_gpt, sibling_ch)) {
			if (period_ticks < rzg2l_gpt->period_ticks[ch])
				return -EBUSY;

			period_ticks = rzg2l_gpt->period_ticks[ch];
		}
	}

	prescale = rzg2l_gpt_calculate_prescale(rzg2l_gpt, period_ticks);
	pv = rzg2l_gpt_calculate_pv_or_dc(period_ticks, prescale);

	duty_ticks = mul_u64_u64_div_u64(state->duty_cycle, rzg2l_gpt->rate_khz, USEC_PER_SEC);
	if (duty_ticks > period_ticks)
		duty_ticks = period_ticks;
	dc = rzg2l_gpt_calculate_pv_or_dc(duty_ticks, prescale);

	/*
	 * GPT counter is shared by multiple channels, we cache the period ticks
	 * from the first enabled channel and use the same value for both
	 * channels.
	 */
	rzg2l_gpt->period_ticks[ch] = period_ticks;

	/*
	 * Counter must be stopped before modifying mode, prescaler, timer
	 * counter and buffer enable registers. These registers are shared
	 * between both channels. So allow updating these registers only for the
	 * first enabled channel.
	 */
	if (!rzg2l_gpt->channel_enable[ch]) {
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

		/*
		 * GPT set operating mode (saw-wave up-counting) or
		 * saw-wave one shot up-counting for only deadtime mode.
		 */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_MD,
				 rzg2l_gpt_is_deadtime_mode(rzg2l_gpt, pwm->hwpwm) ?
				 RZG2L_GTCR_MD_SAW_WAVE_ONE_SHOT :
				 RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE);

		/* Set count direction */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUDDTYC(ch), RZG2L_GTUDDTYC_UP_COUNTING);

		/* Select count clock */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_TPCS,
				 FIELD_PREP(RZG2L_GTCR_TPCS, prescale));

		/* Set period */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), pv);
	}

	/* Store duty cycle in buffer 0 to support buffer mode */
	rzg2l_gpt->channel_data[pwm->hwpwm].buffer[BUFF_0] = dc;

	if (!rzg2l_gpt_is_ch_enabled(rzg2l_gpt, pwm->hwpwm))
		rzg2l_gpt_buffer_setting(rzg2l_gpt, pwm->hwpwm);

	if (!rzg2l_gpt->channel_enable[ch]) {
		/* Set initial value for counter */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);

		/* Enable overflow interrupt */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTINTAD(ch),
				 RZG2L_GTINTAD_GTINTPR_MASK, RZG2L_GTINTPROV);
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

	if (state->polarity != pwm->state.polarity) {
		rzg2l_gpt_set_polarity(rzg2l_gpt, pwm, state->polarity);
		return 0;
	}

	ret = rzg2l_gpt_config(chip, pwm, state);
	if (!ret && !enabled)
		rzg2l_gpt_enable(rzg2l_gpt, pwm);

	return ret;
}

static int rzg2l_gpt_capture(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_capture *result, unsigned long timeout)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	struct rz_gpt_cpt_data *cpt_data = &rzg2l_gpt->cpt_data[pwm->hwpwm];
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u64 high, low;
	int ret;

	if (rzg2l_gpt->channel_enable[ch]) {
		dev_err(&rzg2l_gpt->chip->dev,
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

	/*
	 * Prepare capture measurement:
	 * - Set operating mode GTCR.MD[2:0] and count clock GTCR.TPCS[2:0]
	 * - Using lowest frequency P0/1024 to avoid overflow
	 */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_MD,
			RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE);
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_TPCS,
			FIELD_PREP(RZG2L_GTCR_TPCS, RZG2L_GTCR_TPCS_P0_1024));

	/* Set count direction with GTUDDTYC[1:0]*/
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUDDTYC(ch), RZG2L_GTUDDTYC_UP);

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
			RZG2L_GTINTAD_GTINTx(sub_ch) | RZG2L_GTINTPROV);

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
		high = cpt_data->snapshot[1] - cpt_data->snapshot[0];
		low = cpt_data->snapshot[2] - cpt_data->snapshot[1];
		result->period = mul_u64_u64_div_u64(high + low, rzg2l_gpt->rate_khz, USEC_PER_SEC);
		result->duty_cycle = mul_u64_u64_div_u64(high, rzg2l_gpt->rate_khz, USEC_PER_SEC);
		break;
	default:
		dev_err(&chip->dev, "Internal error\n");
		break;
	}
out:
	/* Disable capture operation */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTICxSR(ch, sub_ch), 0);

	/* Disable interrupt */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTINTAD(ch), 0);

	/* Stop count */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

	return 0;
}

static int rzg2l_gpt_cnt_write_raw(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   int val, int val2, long mask)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE:
		if (val < 0 || val > 1)
			return -EINVAL;

		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, val);

		return 0;
	case IIO_CHAN_INFO_RAW:
		if (val < 0)
			return -EINVAL;

		if (rzg2l_gpt_read_mask(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST))
			return -EBUSY;

		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), val);

		return 0;
	default:
		return -EINVAL;
	}
}

static int rzg2l_gpt_cnt_read_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int *val, int *val2, long mask)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);
	u32 dat;

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		dat = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCNT(ch));
		*val = dat;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_ENABLE:
		dat = rzg2l_gpt_read_mask(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST);
		*val = dat;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info rzg2l_gpt_cnt_iio_info = {
	.read_raw = rzg2l_gpt_cnt_read_raw,
	.write_raw = rzg2l_gpt_cnt_write_raw,
};

static const struct counter_mode_params mode_set[NR_MODE] = {
	[MODE_1] = PHASE_COUNTING_PARAM(0x00006900, 0x00009600),
	[MODE_2A] = PHASE_COUNTING_PARAM(0x00000800, 0x00000400),
	[MODE_2B] = PHASE_COUNTING_PARAM(0x00000200, 0x00000100),
	[MODE_2C] = PHASE_COUNTING_PARAM(0x00000A00, 0x00000500),
	[MODE_3A] = PHASE_COUNTING_PARAM(0x00000800, 0x00008000),
	[MODE_3B] = PHASE_COUNTING_PARAM(0x00000200, 0x00002000),
	[MODE_3C] = PHASE_COUNTING_PARAM(0x00000A00, 0x0000A000),
	[MODE_4] = PHASE_COUNTING_PARAM(0x00006000, 0x00009000),
	[MODE_5A] = PHASE_COUNTING_PARAM(0x00000C00, 0x00000000),
	[MODE_5B] = PHASE_COUNTING_PARAM(0x0000C000, 0x00000000),
};

static int rzg2l_gpt_get_counter_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	return rzg2l_gpt->counter_mode[ch];
}

static int rzg2l_gpt_set_counter_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      unsigned int type)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	if (rzg2l_gpt_read_mask(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST))
		return -EBUSY;

	rzg2l_gpt->counter_mode[ch] = type;

	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUPSR(ch), mode_set[type].gtupsr);
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDNSR(ch), mode_set[type].gtdnsr);
	/* Reset counter when set mode */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);

	return 0;
}

static const struct iio_enum rzg2l_gpt_counter_mode_en = {
	.items = rzg2l_gpt_counter_modes,
	.num_items = ARRAY_SIZE(rzg2l_gpt_counter_modes),
	.get = rzg2l_gpt_get_counter_mode,
	.set = rzg2l_gpt_set_counter_mode,
};

static struct reset_counter_params reset_counter_mode_set[5] = {
	[NOT_USE] = RESET_COUNTER_PARAM(0x00000000, 0x00000000, 0x00000000),
};

static struct reset_counter_params reset_counter_mode_set_A =
	RESET_COUNTER_PARAM(0x00000002, 0x00000001, 0x00000001);

static struct reset_counter_params reset_counter_mode_set_B =
	RESET_COUNTER_PARAM(0x00000008, 0x00000004, 0x00000004);

static struct reset_counter_params reset_counter_mode_set_C =
	RESET_COUNTER_PARAM(0x00000020, 0x00000010, 0x00000010);

static struct reset_counter_params reset_counter_mode_set_D =
	RESET_COUNTER_PARAM(0x00000080, 0x00000040, 0x00000040);

static int rzg2l_gpt_get_reset_counter(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	return rzg2l_gpt->reset_counter[ch];
}

static int rzg2l_gpt_set_reset_counter(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 unsigned int type)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	rzg2l_gpt->reset_counter[ch] = type;

	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTSSR(ch), reset_counter_mode_set[type].gtssr);
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPSR(ch), reset_counter_mode_set[type].gtpsr);
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCSR(ch), reset_counter_mode_set[type].gtcsr);

	return 0;
}

static const struct iio_enum rzg2l_gpt_reset_counter_en = {
	.items = rzg2l_gpt_reset_counters,
	.num_items = ARRAY_SIZE(rzg2l_gpt_reset_counters),
	.get = rzg2l_gpt_get_reset_counter,
	.set = rzg2l_gpt_set_reset_counter,
};

static ssize_t rzg2l_gpt_cnt_get_counter_preset(struct iio_dev *indio_dev,
						uintptr_t private,
						const struct iio_chan_spec *chan,
						char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);
	u32 tmp = 0;

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTPR(ch));

	return snprintf(buf, PAGE_SIZE, "%u\n", tmp);
}

static ssize_t rzg2l_gpt_cnt_set_counter_preset(struct iio_dev *indio_dev,
						uintptr_t private,
						const struct iio_chan_spec *chan,
						const char *buf, size_t len)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = iio_device_get_drvdata(indio_dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(indio_dev->dev.parent);
	u8 ch = RZG2L_GET_CH(pwm_id);
	int ret, tmp = 0;

	if (rzg2l_gpt->channel_data[pwm_id].operation != COUNTING_INPUT) {
		dev_err(&rzg2l_gpt->chip->dev,
			"Must in counting input operation to use this config\n");
		return -EINVAL;
	}

	if (rzg2l_gpt_read_mask(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST))
		return -EBUSY;

	ret = kstrtoint(buf, 0, &tmp);
	if (ret)
		return ret;

	if ((tmp > (BIT(31)-1)) || (tmp < 0))
		return -EINVAL;

	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), tmp);
	/* Reset counter when set preset */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);

	return len;
}

static const struct iio_chan_spec_ext_info rzg2l_gpt_cnt_ext_info[] = {
	{
		.name = "counter_preset",
		.shared = IIO_SEPARATE,
		.read = rzg2l_gpt_cnt_get_counter_preset,
		.write = rzg2l_gpt_cnt_set_counter_preset,
	},
	IIO_ENUM("counter_mode", IIO_SEPARATE,
		 &rzg2l_gpt_counter_mode_en),
	IIO_ENUM_AVAILABLE("counter_mode", IIO_SEPARATE, &rzg2l_gpt_counter_mode_en),
	IIO_ENUM("reset_counter", IIO_SEPARATE,
		&rzg2l_gpt_reset_counter_en),
	IIO_ENUM_AVAILABLE("reset_counter", IIO_SEPARATE, &rzg2l_gpt_reset_counter_en),
	{}
};

static const struct iio_chan_spec rzg2l_gpt_cnt_channels = {
	.type = IIO_COUNT,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_ENABLE),
	.ext_info = rzg2l_gpt_cnt_ext_info,
	.indexed = 1,
};

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

static irqreturn_t gpt_gtciv_interrupt(int irq, void *data)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = data;
	unsigned int pwm_id;
	int ch;
	u8 sub_ch;
	u32 tmp;

	ch = rzg2l_gpt_get_ch_from_irq(rzg2l_gpt, irq, GTCIV);
	if (ch < 0)
		return IRQ_NONE;

	guard(spinlock_irqsave)(&rzg2l_gpt->lock);

	if (rzg2l_gpt->cfg->has_icu_errint_status) {
		regmap_read(rzg2l_gpt->icu_regmap, RZG3L_PEISR_OFFSET, &tmp);
		if (!(tmp & RZG3L_PEVSTATn_BIT(ch)))
			return IRQ_NONE;

		/* Clear error interrupt */
		regmap_update_bits(rzg2l_gpt->icu_regmap, RZG3L_PEISR_OFFSET,
			RZG3L_PEVSTATn_BIT(ch), ~RZG3L_PEVSTATn_BIT(ch));
	}

	/* Counting overflow triggered to support input capture mode */
	if (rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTICxSR(ch, 0)))
		rzg2l_gpt->cpt_data[RZG2L_GET_HWPWM(ch, 0)].overflow_count++;
	else if (rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTICxSR(ch, 1)))
		rzg2l_gpt->cpt_data[RZG2L_GET_HWPWM(ch, 1)].overflow_count++;

	for_each_set_bit(sub_ch,
			(unsigned long *)&rzg2l_gpt->channel_enable[ch],
			RZG2L_CHANNELS_PER_IO) {
		pwm_id = RZG2L_GET_HWPWM(ch, sub_ch);

		rzg2l_gpt_buffer_setting(rzg2l_gpt, pwm_id);
	}

#if IS_BUILTIN(CONFIG_POEG_RZG2L)
	if (rzg2l_gpt->poeg) {
		/*Clear input edge flag*/
		rzg2l_poeg_clear_bit_export(
			POEG_mode_set[rzg2l_gpt->poeg].poeg_dev,
			RZG2L_POEGG_PIDF, RZG2L_POEGG);
		/*Clear GPT disable flag*/
		rzg2l_poeg_clear_bit_export(
			POEG_mode_set[rzg2l_gpt->poeg].poeg_dev,
			RZG2L_POEGG_IOCF, RZG2L_POEGG);
	}
#endif

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

static ssize_t buff0_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	struct pwm_device *pwm = &rzg2l_gpt->chip->pwms[pwm_id];
	u8 ch = RZG2L_GET_CH(pwm_id);
	unsigned int val;
	int ret;
	u8 prescale;
	u64 val_ticks;
	u32 tmp;

	if ((rzg2l_gpt->channel_data[pwm_id].operation != DEADTIME_OUTPUT) &&
	    (rzg2l_gpt->channel_data[pwm_id].operation != SINGLE_BUFFER_OUTPUT) &&
	    (rzg2l_gpt->channel_data[pwm_id].operation != DOUBLE_BUFFER_OUTPUT)) {
		dev_err(dev, "This operation not use this config\n");
		return -EINVAL;
	}

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	val_ticks = mul_u64_u64_div_u64(val, rzg2l_gpt->rate_khz, USEC_PER_SEC);

	if (pwm->state.period < val_ticks)
		return -EINVAL;

	guard(mutex)(&rzg2l_gpt->mutex);

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);
	rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_1] =
			rzg2l_gpt_calculate_pv_or_dc(val_ticks, prescale);
	if (rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_1] == 0)
		return -EINVAL;

	return ret ? : count;
}

static ssize_t buff0_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	u8 ch = RZG2L_GET_CH(pwm_id);
	u8 prescale;
	u32 tmp;
	unsigned long long time_ns;

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);
	time_ns = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt,
		  rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_1],
		  prescale);

	return sprintf(buf, "%llu\n", time_ns);
}

static ssize_t buff1_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	struct pwm_device *pwm = &rzg2l_gpt->chip->pwms[pwm_id];
	u8 ch = RZG2L_GET_CH(pwm_id);
	unsigned int val, time_0;
	int ret;
	u8 prescale;
	u64 val_ticks;
	u32 tmp;

	if ((rzg2l_gpt->channel_data[pwm_id].operation != DEADTIME_OUTPUT) &&
	    (rzg2l_gpt->channel_data[pwm_id].operation != DOUBLE_BUFFER_OUTPUT)) {
		dev_err(dev, "This operation not use this config\n");
		return -EINVAL;
	}

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	val_ticks = mul_u64_u64_div_u64(val, rzg2l_gpt->rate_khz, USEC_PER_SEC);

	if (pwm->state.period < val_ticks)
		return -EINVAL;

	guard(mutex)(&rzg2l_gpt->mutex);

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);

	if (rzg2l_gpt_is_deadtime_mode(rzg2l_gpt, pwm_id)) {
		time_0 = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt,
			 rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_1], prescale);
		if (time_0 > val) {
			dev_err(dev, "In deadtime, A1 must greater than A0\n");
			return -EINVAL;
		}
	}

	rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_2] =
			rzg2l_gpt_calculate_pv_or_dc(val_ticks, prescale);
	if (rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_2] == 0)
		return -EINVAL;

	return ret ? : count;
}

static ssize_t buff1_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	u8 ch = RZG2L_GET_CH(pwm_id);
	u8 prescale;
	u32 tmp;
	unsigned long long time_ns;

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);
	time_ns = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt,
		  rzg2l_gpt->channel_data[pwm_id].buffer[BUFF_2],
		  prescale);

	return sprintf(buf, "%llu\n", time_ns);
}

static ssize_t gpt_operation_available_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int i;
	size_t len = 0;

	for (i = 0; i < NR_GPT_OPERATION; ++i)
		len += sysfs_emit_at(buf, len, "%s  ", gpt_operation_enum[i]);

	/* replace last space with a newline */
	buf[len - 1] = '\n';

	return len;
}

static ssize_t gpt_operation_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	u8 ch = RZG2L_GET_CH(pwm_id);
	u8 sub_ch = rzg2l_gpt_subchannel(pwm_id);
	int ret;

	ret = sysfs_match_string(gpt_operation_enum, buf);
	if (ret < 0)
		return ret;

	if (rzg2l_gpt->channel_request[ch] != RZG2L_BOTH_AB ||
	    rzg2l_gpt->channel_enable[ch]) {
		if (ret == DEADTIME_OUTPUT || ret == COUNTING_INPUT) {
			dev_err(&rzg2l_gpt->chip->dev,
				"Please keep pwm%d and pwm%d are requested and not enabled to use deadtime output.\n",
				pwm_id, rzg2l_gpt_sibling(pwm_id));
			return -EINVAL;
		}
	}

	guard(mutex)(&rzg2l_gpt->mutex);

	rzg2l_gpt->channel_data[pwm_id].operation = ret;

	/* Reset registers to prevent conflict setting between modes */
	rzg2l_reset_period_and_duty(rzg2l_gpt, pwm_id);

	switch (rzg2l_gpt->channel_data[pwm_id].operation) {
	case NORMAL_OUTPUT:
		/* Set no buffer operation */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTBER(ch),
				RZG2L_GTCCRx_BUFFER_MASK(sub_ch), 0);
		break;
	case SINGLE_BUFFER_OUTPUT:
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTBER(ch),
				RZG2L_GTCCRx_BUFFER_MASK(sub_ch),
				RZG2L_GTCCRx_SINGLE_BUFFER(sub_ch));
		break;
	case DOUBLE_BUFFER_OUTPUT:
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTBER(ch),
				RZG2L_GTCCRx_BUFFER_MASK(sub_ch),
				RZG2L_GTCCRx_DOUBLE_BUFFER(sub_ch));
		break;
	case DEADTIME_OUTPUT:
		/* Set buffer deadtime */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTBER(ch), RZG2L_GTBER_BUFFER_DEADTIME);
		/* Enable deadtime mode */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDTCR(ch), RZG2L_GTDTCR_DEADTIME_MODE);
		break;
	case COUNTING_INPUT:
		/* Maximum frequency*/
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_TPCS, 0);
		/* Default period */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), RZG2L_GTPR_MAX_VALUE);
		/* Set initial value for counter */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);
		/* Using noise filter with P0/64 clock */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), RZG2L_GTIOR_NFCSB,
				FIELD_PREP(RZG2L_GTIOR_NFCSB, RZG2L_GTIOR_NFCSx_P0_64));
		/* Default counting mode */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUPSR(ch), mode_set[MODE_1].gtupsr);
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDNSR(ch), mode_set[MODE_1].gtdnsr);
		/* Default preset */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), BIT(31)-1);
		break;
	}

	return count;
}

static ssize_t gpt_operation_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);

	return sysfs_emit(buf, "%s\n",
		gpt_operation_enum[rzg2l_gpt->channel_data[pwm_id].operation]);
}

static ssize_t deadtime_first_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	struct pwm_device *pwm = &rzg2l_gpt->chip->pwms[pwm_id];
	u8 ch = RZG2L_GET_CH(pwm_id);
	int val, ret;
	unsigned long prescale;
	u32 tmp;
	u64 val_ticks;

	if (rzg2l_gpt->channel_data[pwm_id].operation != DEADTIME_OUTPUT) {
		dev_err(dev, "Must in deadtime mode to set\n");
		return -EINVAL;
	}

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	val_ticks = mul_u64_u64_div_u64(val, rzg2l_gpt->rate_khz, USEC_PER_SEC);

	if (pwm->state.period < val_ticks)
		return -EINVAL;

	guard(mutex)(&rzg2l_gpt->mutex);

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);

	if (!val)
		rzg2l_gpt->channel_data[pwm_id].deadtime_first = 0;
	else
		rzg2l_gpt->channel_data[pwm_id].deadtime_first =
			rzg2l_gpt_calculate_pv_or_dc(val_ticks, prescale);

	/* Set buffer value for deadtime first half in GTDVU */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDVU(ch),
			rzg2l_gpt->channel_data[pwm_id].deadtime_first);

	return ret ? : count;
}

static ssize_t deadtime_first_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	u8 ch = RZG2L_GET_CH(pwm_id);
	unsigned long prescale;
	unsigned long long time_ns;
	u32 tmp;

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);
	time_ns = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt,
		  rzg2l_gpt->channel_data[pwm_id].deadtime_first, prescale);

	return sprintf(buf, "%llu\n", time_ns);
}

static ssize_t deadtime_second_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	struct pwm_device *pwm = &rzg2l_gpt->chip->pwms[pwm_id];
	u8 ch = RZG2L_GET_CH(pwm_id);
	int val, ret;
	unsigned long prescale;
	u32 tmp;
	u64 val_ticks;

	if (rzg2l_gpt->channel_data[pwm_id].operation != DEADTIME_OUTPUT) {
		dev_err(dev, "Must in deadtime mode to set\n");
		return -EINVAL;
	}

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	val_ticks = mul_u64_u64_div_u64(val, rzg2l_gpt->rate_khz, USEC_PER_SEC);

	if (pwm->state.period < val_ticks)
		return -EINVAL;

	guard(mutex)(&rzg2l_gpt->mutex);

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);

	if (!val)
		rzg2l_gpt->channel_data[pwm_id].deadtime_second = 0;
	else
		rzg2l_gpt->channel_data[pwm_id].deadtime_second =
			rzg2l_gpt_calculate_pv_or_dc(val_ticks, prescale);

	/* Set buffer value for deadtime second half in GTDVD */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTDVD(ch),
			rzg2l_gpt->channel_data[pwm_id].deadtime_second);

	return ret ? : count;
}

static ssize_t deadtime_second_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	unsigned int pwm_id = hwpwm_from_pwmdev(dev);
	u8 ch = RZG2L_GET_CH(pwm_id);
	unsigned long prescale;
	unsigned long long time_ns;
	u32 tmp;

	tmp = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	prescale = FIELD_GET(RZG2L_GTCR_TPCS, tmp);
	time_ns = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt,
		  rzg2l_gpt->channel_data[pwm_id].deadtime_second, prescale);

	return sprintf(buf, "%llu\n", time_ns);
}

static ssize_t POEG_available_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned int i;
	size_t len = 0;

	for (i = 0; i < 5; ++i) {
		if (rzg2l_gpt_POEGs[i])
			len += sysfs_emit_at(buf, len, "%s ", rzg2l_gpt_POEGs[i]);
	}

	/* replace last space with a newline */
	buf[len - 1] = '\n';

	return len;
}

static ssize_t POEG_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);
	struct rz_gpt_pwm_channel *channel_data;
	u8 ch, sub_ch;
	u32 val;
	int ret;

	ret = sysfs_match_string(rzg2l_gpt_POEGs, buf);
	if (ret < 0)
		return ret;

	guard(mutex)(&rzg2l_gpt->mutex);

	rzg2l_gpt->poeg = ret;

	for (ch = 0; ch < RZG2L_MAX_HW_CHANNELS; ch++) {
		if (rzg2l_gpt->channel_enable[ch]) {
			channel_data = &rzg2l_gpt->channel_data[RZG2L_GET_HWPWM(ch, 0)];
			/* Set output disable group */
			rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTINTAD(ch),
					RZG2L_GTINTAD_OUTPUT_DISABLE_GRP_MASK,
					POEG_mode_set[rzg2l_gpt->poeg].poeg);

			/* Set output disable source */
			if (channel_data->operation == DEADTIME_OUTPUT)
				val = RZG2L_GTINTAD_OUTPUT_DISABLE_DEADTIME_ERROR;
			else
				val = RZG2L_GTINTAD_OUTPUT_DISABLE_SAME_LEVEL_HIGH |
				      RZG2L_GTINTAD_OUTPUT_DISABLE_SAME_LEVEL_LOW;

			/* Set output disable source */
			rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTINTAD(ch),
					RZG2L_GTINTAD_OUTPUT_DISABLE_POEG_MASK,
					rzg2l_gpt->poeg ? val : 0);

			/* Enable/Disable pin output disable */
			for_each_set_bit(sub_ch,
					(unsigned long *)&rzg2l_gpt->channel_enable[ch],
					RZG2L_CHANNELS_PER_IO) {
				rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch),
						RZG2L_GTIOR_GTIOR_CHANNEL_x_OUTPUT_DISABLE_MASK(sub_ch),
						rzg2l_gpt->poeg ?
						RZG2L_GTIOR_GTIOx_OUTPUT_DISABLE(sub_ch) : 0);
			}

			if (rzg2l_gpt->poeg)
				/* Enable overflow interrupt*/
				rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTINTAD(ch),
						RZG2L_GTINTAD_GTINTPR_MASK, RZG2L_GTINTPROV);
		}
	}

	return count;
}

static ssize_t POEG_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", rzg2l_gpt_POEGs[rzg2l_gpt->poeg]);
}

static DEVICE_ATTR_RW(buff0);
static DEVICE_ATTR_RW(buff1);
static DEVICE_ATTR_RO(gpt_operation_available);
static DEVICE_ATTR_RW(gpt_operation);
static DEVICE_ATTR_RW(deadtime_first);
static DEVICE_ATTR_RW(deadtime_second);
static DEVICE_ATTR_RO(POEG_available);
static DEVICE_ATTR_RW(POEG);

static struct attribute *enhanced_feature_attrs_A[] = {
	&dev_attr_deadtime_first.attr,
	&dev_attr_deadtime_second.attr,
	NULL,
};

static const struct attribute_group enhanced_feature_group_A = {
	.attrs = enhanced_feature_attrs_A,
};

static struct attribute *common_attrs[] = {
	&dev_attr_gpt_operation_available.attr,
	&dev_attr_gpt_operation.attr,
	&dev_attr_buff0.attr,
	&dev_attr_buff1.attr,
	NULL,
};

static const struct attribute_group common_group = {
	.attrs = common_attrs,
};

static struct attribute *poeg_attrs[] = {
	&dev_attr_POEG_available.attr,
	&dev_attr_POEG.attr,
	NULL,
};

static const struct attribute_group poeg_attr_group = {
	.attrs = poeg_attrs,
};

static ssize_t enhanced_function_channels_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rzg2l_gpt_chip *rzg2l_gpt = platform_get_drvdata(pdev);
	struct device *pwm_dev;
	struct iio_dev *indio_dev;
	char name[6];
	unsigned int pwm_id;
	int ret;

	if (kstrtouint(buf, 0, &pwm_id))
		return -EINVAL;

	snprintf(name, sizeof(name), "pwm%u", pwm_id);

	pwm_dev = device_find_child_by_name(&rzg2l_gpt->chip->dev, name);
	if (!pwm_dev)
		return -ENODEV;

	dev_set_drvdata(pwm_dev, rzg2l_gpt);

	if (!(rzg2l_gpt_subchannel(pwm_id))) {
		ret = device_add_group(pwm_dev, &enhanced_feature_group_A);
		if (ret)
			return ret;
	}

	ret = device_add_group(pwm_dev, &common_group);
	if (ret)
		return ret;

	indio_dev = devm_iio_device_alloc(pwm_dev, sizeof(struct rzg2l_gpt_chip *));
	if (!indio_dev)
		return -ENOMEM;

	iio_device_set_drvdata(indio_dev, rzg2l_gpt);

	indio_dev->name = dev_name(pwm_dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &rzg2l_gpt_cnt_iio_info;
	indio_dev->channels = &rzg2l_gpt_cnt_channels;
	indio_dev->num_channels = 1;

	ret = devm_iio_device_register(pwm_dev, indio_dev);
	if (ret < 0) {
		dev_err(pwm_dev, "failed to create counter device: %d\n", ret);
		return ret;
	}
	rzg2l_gpt->channel_data[pwm_id].operation = NORMAL_OUTPUT;

	return count;
}
static DEVICE_ATTR_WO(enhanced_function_channels);

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

static void rzg2l_gpt_poeg_init(struct device *dev)
{
	struct device_node *poeg_np;
	struct platform_device *poeg_dev_np;
	unsigned int i;
	int cells;
	int ret;

	cells = of_property_count_u32_elems(dev->of_node, "renesas,poegs");
	if (cells == -EINVAL)
		return;

	if (cells > 0) {
		ret = sysfs_create_group(&dev->kobj, &poeg_attr_group);
		if (ret < 0) {
			dev_err(dev, "Failed to create sysfs: %d\n", ret);
			return;
		}
	}

	for (i = 0; i < cells; i++) {
		poeg_np = of_parse_phandle(dev->of_node, "renesas,poegs", i);
		if (poeg_np != NULL) {
			poeg_dev_np = of_find_device_by_node(poeg_np);
			if (poeg_dev_np) {
				if (!strncasecmp(poeg_np->name, "poega", 5)) {
					rzg2l_gpt_POEGs[i + 1] = "POEGA";
					POEG_mode_set[i + 1].poeg_dev = poeg_dev_np;
					POEG_mode_set[i + 1].poeg = RZG2L_GTINTAD_GRPA;
					rzg2l_gpt_reset_counters[i + 1] = "GTETRGA";
					reset_counter_mode_set[i + 1] = reset_counter_mode_set_A;
					dev_info(dev, "Can use GTETRGA as POEG, reset_counter\n");
				} else if (!strncasecmp(poeg_np->name, "poegb", 5)) {
					rzg2l_gpt_POEGs[i + 1] = "POEGB";
					POEG_mode_set[i + 1].poeg_dev = poeg_dev_np;
					POEG_mode_set[i + 1].poeg = RZG2L_GTINTAD_GRPB;
					rzg2l_gpt_reset_counters[i + 1] = "GTETRGB";
					reset_counter_mode_set[i + 1] = reset_counter_mode_set_B;
					dev_info(dev, "Can use GTETRGB as POEG, reset_counter\n");
				}  else if (!strncasecmp(poeg_np->name, "poegc", 5)) {
					rzg2l_gpt_POEGs[i + 1] = "POEGC";
					POEG_mode_set[i + 1].poeg_dev = poeg_dev_np;
					POEG_mode_set[i + 1].poeg = RZG2L_GTINTAD_GRPC;
					rzg2l_gpt_reset_counters[i + 1] = "GTETRGC";
					reset_counter_mode_set[i + 1] = reset_counter_mode_set_C;
					dev_info(dev, "Can use GTETRGC as POEG, reset_counter\n");
				} else if (!strncasecmp(poeg_np->name, "poegd", 5)) {
					rzg2l_gpt_POEGs[i + 1] = "POEGD";
					POEG_mode_set[i + 1].poeg_dev = poeg_dev_np;
					POEG_mode_set[i + 1].poeg = RZG2L_GTINTAD_GRPD;
					rzg2l_gpt_reset_counters[i + 1] = "GTETRGD";
					reset_counter_mode_set[i + 1] = reset_counter_mode_set_D;
					dev_info(dev, "Can use GTETRGD as POEG, reset_counter\n");
				}
			}
		}
	}
}

static int rzg2l_gpt_probe(struct platform_device *pdev)
{
	struct rzg2l_gpt_chip *rzg2l_gpt;
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	struct pwm_chip *chip;
	unsigned long rate;
	struct clk *clk;
	unsigned int i;
	int ret, irq;
	char irq_name[10];

	chip = devm_pwmchip_alloc(dev, RZG2L_MAX_PWM_CHANNELS, sizeof(*rzg2l_gpt));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	rzg2l_gpt = to_rzg2l_gpt_chip(chip);

	rzg2l_gpt->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rzg2l_gpt->mmio))
		return PTR_ERR(rzg2l_gpt->mmio);

	rzg2l_gpt_poeg_init(dev);

	rzg2l_gpt->cfg = device_get_match_data(dev);
	if (rzg2l_gpt->cfg->has_icu_errint_status) {
		struct device_node *icu_np __free(device_node) =
			of_parse_phandle(dev->of_node, "renesas,icu", 0);
		if (icu_np != NULL) {
			rzg2l_gpt->icu_regmap = device_node_to_regmap(icu_np);

			if (IS_ERR(rzg2l_gpt->icu_regmap))
				return dev_err_probe(dev, PTR_ERR(rzg2l_gpt->icu_regmap),
						    "Failed to get regmap from IRQC\n");
		}
	}

	rstc = devm_reset_control_get_exclusive(dev, NULL);
	reset_control_deassert(rstc);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc), "Cannot deassert reset control\n");

	for (i = 0; i < RZG2L_MAX_HW_CHANNELS; i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(gpt_irqs); j++) {
			snprintf(irq_name, sizeof(irq_name), "%s_%d", gpt_irqs[j].name, i);

			irq = platform_get_irq_byname(pdev, irq_name);
			if (irq < 0)
				return dev_err_probe(dev, irq, "Failed to obtain IRQ\n");

			ret = devm_request_irq(dev, irq, gpt_irqs[j].isr,
					       0, dev_name(dev), rzg2l_gpt);
			if (ret < 0)
				return dev_err_probe(dev, ret, "Failed to request IRQ\n");

			rzg2l_gpt->irq_map[i][gpt_irqs[j].res_num] = irq;
		}
	}

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

	spin_lock_init(&rzg2l_gpt->lock);
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

	ret = device_create_file(dev, &dev_attr_enhanced_function_channels);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to create sysfs\n");

	dev_info(dev, "RZ/G2L GPT Driver probed\n");
	platform_set_drvdata(pdev, rzg2l_gpt);

	return 0;
}

static const struct of_device_id rzg2l_gpt_of_table[] = {
	{ .compatible = "renesas,rzg2l-gpt", .data = &rzg2l_cfg,},
	{ .compatible = "renesas,rzg3l-gpt", .data = &rzg3l_cfg,},
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

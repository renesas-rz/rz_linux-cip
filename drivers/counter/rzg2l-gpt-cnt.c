// SPDX-License-Identifier: GPL-2.0
/* Renesas RZ/G2L General PWM Timer (GPT) Counter driver */

#include <linux/bitfield.h>
#include <linux/counter.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/rzg2l-gpt.h>

static const char * const rzg2l_gpt_counter_modes[] = {
	"mode-1", "mode-2A", "mode-2B", "mode-2C", "mode-3A",
	"mode-3B", "mode-3C", "mode-4", "mode-5A", "mode-5B",
};

enum rzg2l_gpt_counter_mode {
	MODE_1, MODE_2A, MODE_2B, MODE_2C, MODE_3A,
	MODE_3B, MODE_3C, MODE_4, MODE_5A, MODE_5B, NR_MODE,
};

static const char * const rzg2l_gpt_reset_counters[] = {
	"not_use", "gtioa", "gtiob", "gtioc", "gtiod",
};

enum rzg2l_gpt_reset_counter {
	RESET_NOT_USE, RESET_GTIOA, RESET_GTIOB, RESET_GTIOC,
	RESET_GTIOD, NR_RESET_COUNTER,
};

struct counter_mode_params { u32 gtupsr; u32 gtdnsr; };
struct reset_counter_params { u32 gtssr; u32 gtpsr; u32 gtcsr; };

static const struct counter_mode_params mode_set[NR_MODE] = {
	[MODE_1]  = PHASE_COUNTING_PARAM(0x00006900, 0x00009600),
	[MODE_2A] = PHASE_COUNTING_PARAM(0x00000800, 0x00000400),
	[MODE_2B] = PHASE_COUNTING_PARAM(0x00000200, 0x00000100),
	[MODE_2C] = PHASE_COUNTING_PARAM(0x00000A00, 0x00000500),
	[MODE_3A] = PHASE_COUNTING_PARAM(0x00000800, 0x00008000),
	[MODE_3B] = PHASE_COUNTING_PARAM(0x00000200, 0x00002000),
	[MODE_3C] = PHASE_COUNTING_PARAM(0x00000A00, 0x0000A000),
	[MODE_4]  = PHASE_COUNTING_PARAM(0x00006000, 0x00009000),
	[MODE_5A] = PHASE_COUNTING_PARAM(0x00000C00, 0x00000000),
	[MODE_5B] = PHASE_COUNTING_PARAM(0x0000C000, 0x00000000),
};

static const struct reset_counter_params reset_counter_mode_set[NR_RESET_COUNTER] = {
	[RESET_NOT_USE]	= RESET_COUNTER_PARAM(0x00000000, 0x00000000, 0x00000000),
	[RESET_GTIOA]	= RESET_COUNTER_PARAM(0x00000002, 0x00000001, 0x00000001),
	[RESET_GTIOB]	= RESET_COUNTER_PARAM(0x00000008, 0x00000004, 0x00000004),
	[RESET_GTIOC]	= RESET_COUNTER_PARAM(0x00000020, 0x00000010, 0x00000010),
	[RESET_GTIOD]	= RESET_COUNTER_PARAM(0x00000080, 0x00000040, 0x00000040),
};

struct rzg2l_gpt_counter {
	struct rzg2l_gpt *gpt;
	struct device *dev;
	u32 counter_mode[RZG2L_MAX_HW_CHANNELS];
	u32 reset_counter[RZG2L_MAX_HW_CHANNELS];
};

static int rzg2l_gpt_counter_check_enabled(struct rzg2l_gpt_counter *priv,
					unsigned int ch)
{
	return priv->gpt->counter_enabled[ch] ? 0 : -EACCES;
}

static void rzg2l_gpt_counter_hw_init(struct rzg2l_gpt_counter *priv,
					unsigned int ch)
{
	rzg2l_gpt_modify(priv->gpt, RZG2L_GTCR(ch), RZG2L_GTCR_TPCS, 0);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTPR(ch), RZG2L_GTPR_MAX_VALUE);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTCNT(ch), 0);
	rzg2l_gpt_modify(priv->gpt, RZG2L_GTIOR(ch), RZG2L_GTIOR_NFCSB,
			 FIELD_PREP(RZG2L_GTIOR_NFCSB, RZG2L_GTIOR_NFCSx_P0_64));
	rzg2l_gpt_write(priv->gpt, RZG2L_GTUPSR(ch), mode_set[priv->counter_mode[ch]].gtupsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTDNSR(ch), mode_set[priv->counter_mode[ch]].gtdnsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTSSR(ch),
			reset_counter_mode_set[priv->reset_counter[ch]].gtssr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTPSR(ch),
			reset_counter_mode_set[priv->reset_counter[ch]].gtpsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTCSR(ch),
			reset_counter_mode_set[priv->reset_counter[ch]].gtcsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTPR(ch), BIT(31) - 1);
}

static void rzg2l_gpt_counter_disable(struct rzg2l_gpt_counter *priv,
					unsigned int ch)
{
	if (!priv->gpt->counter_enabled[ch])
		return;

	rzg2l_gpt_modify(priv->gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTSSR(ch), 0);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTPSR(ch), 0);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTCSR(ch), 0);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTUPSR(ch), 0);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTDNSR(ch), 0);

	priv->counter_mode[ch] = MODE_1;
	priv->reset_counter[ch] = RESET_NOT_USE;
	priv->gpt->counter_enabled[ch] = false;

	dev_info(priv->dev, "Phase Counting disabled: ch%u\n", ch);
}

static int rzg2l_gpt_count_read(struct counter_device *counter,
				struct counter_count *count, u64 *val)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;

	*val = rzg2l_gpt_read(priv->gpt, RZG2L_GTCNT(ch));
	return 0;
}

static int rzg2l_gpt_count_write(struct counter_device *counter,
				 struct counter_count *count, u64 val)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;

	if (val > U32_MAX)
		return -EINVAL;

	rzg2l_gpt_write(priv->gpt, RZG2L_GTCNT(ch), (u32)val);
	return 0;
}

static int rzg2l_gpt_enable_read(struct counter_device *counter,
				 struct counter_count *count, u8 *enable)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);

	*enable = priv->gpt->counter_enabled[count->id];
	return 0;
}

static int rzg2l_gpt_enable_write(struct counter_device *counter,
				  struct counter_count *count, u8 enable)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;

	if (priv->gpt->channel_request_count[ch]) {
		dev_err(priv->dev,
				"Please not export pwm%u and pwm%u before enable counter%u\n",
				RZG2L_GET_HWPWM(ch, 0), RZG2L_GET_HWPWM(ch, 1), ch);
		return -EBUSY;
	}

	if (enable > 1)
		return -EINVAL;

	if (!enable) {
		rzg2l_gpt_counter_disable(priv, ch);
		return 0;
	}

	if (priv->gpt->counter_enabled[ch])
		return 0;

	priv->gpt->counter_enabled[ch] = true;
	rzg2l_gpt_counter_hw_init(priv, ch);
	rzg2l_gpt_modify(priv->gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, RZG2L_GTCR_CST);
	dev_info(priv->dev, "Phase Counting enabled: ch%u\n", ch);

	return 0;
}

static int rzg2l_gpt_counter_preset_read(struct counter_device *counter,
					 struct counter_count *count, u64 *val)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;

	*val = rzg2l_gpt_read(priv->gpt, RZG2L_GTPR(ch));
	return 0;
}

static int rzg2l_gpt_counter_preset_write(struct counter_device *counter,
					  struct counter_count *count, u64 val)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;
	if (val > U32_MAX)
		return -EINVAL;

	rzg2l_gpt_write(priv->gpt, RZG2L_GTPR(ch), (u32)val);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTCNT(ch), 0);
	return 0;
}

static int rzg2l_gpt_counter_mode_read(struct counter_device *counter,
				       struct counter_count *count, u32 *mode)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;

	*mode = priv->counter_mode[ch];
	return 0;
}

static int rzg2l_gpt_counter_mode_write(struct counter_device *counter,
					struct counter_count *count, u32 mode)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;
	if (mode >= NR_MODE)
		return -EINVAL;

	priv->counter_mode[ch] = mode;
	rzg2l_gpt_write(priv->gpt, RZG2L_GTUPSR(ch), mode_set[mode].gtupsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTDNSR(ch), mode_set[mode].gtdnsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTCNT(ch), 0);
	return 0;
}

static int rzg2l_gpt_reset_counter_read(struct counter_device *counter,
					struct counter_count *count, u32 *type)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;

	*type = priv->reset_counter[ch];
	return 0;
}

static int rzg2l_gpt_reset_counter_write(struct counter_device *counter,
					 struct counter_count *count, u32 type)
{
	struct rzg2l_gpt_counter *priv = counter_priv(counter);
	unsigned int ch = count->id;
	int ret = rzg2l_gpt_counter_check_enabled(priv, ch);

	if (ret)
		return ret;
	if (type >= NR_RESET_COUNTER)
		return -EINVAL;

	priv->reset_counter[ch] = type;
	rzg2l_gpt_write(priv->gpt, RZG2L_GTSSR(ch), reset_counter_mode_set[type].gtssr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTPSR(ch), reset_counter_mode_set[type].gtpsr);
	rzg2l_gpt_write(priv->gpt, RZG2L_GTCSR(ch), reset_counter_mode_set[type].gtcsr);
	return 0;
}

static DEFINE_COUNTER_ENUM(rzg2l_counter_mode_enum, rzg2l_gpt_counter_modes);
static DEFINE_COUNTER_ENUM(rzg2l_reset_counter_enum, rzg2l_gpt_reset_counters);

static struct counter_comp rzg2l_gpt_count_ext[] = {
	COUNTER_COMP_ENABLE(rzg2l_gpt_enable_read, rzg2l_gpt_enable_write),
	COUNTER_COMP_COUNT_U64("counter_preset",
			       rzg2l_gpt_counter_preset_read,
			       rzg2l_gpt_counter_preset_write),
	COUNTER_COMP_COUNT_ENUM("counter_mode",
				rzg2l_gpt_counter_mode_read,
				rzg2l_gpt_counter_mode_write,
				rzg2l_counter_mode_enum),
	COUNTER_COMP_COUNT_ENUM("reset_counter",
				rzg2l_gpt_reset_counter_read,
				rzg2l_gpt_reset_counter_write,
				rzg2l_reset_counter_enum),
};

#define RZG2L_GPT_COUNT(_id) {		\
	.id = (_id),			\
	.name = "count" #_id,		\
	.ext = rzg2l_gpt_count_ext,	\
	.num_ext = ARRAY_SIZE(rzg2l_gpt_count_ext),	\
}

static struct counter_count rzg2l_gpt_counts[RZG2L_MAX_HW_CHANNELS] = {
	RZG2L_GPT_COUNT(0), RZG2L_GPT_COUNT(1), RZG2L_GPT_COUNT(2), RZG2L_GPT_COUNT(3),
	RZG2L_GPT_COUNT(4), RZG2L_GPT_COUNT(5), RZG2L_GPT_COUNT(6), RZG2L_GPT_COUNT(7),
};

static const struct counter_ops rzg2l_gpt_counter_ops = {
	.count_read = rzg2l_gpt_count_read,
	.count_write = rzg2l_gpt_count_write,
};

static int rzg2l_gpt_counter_probe(struct platform_device *pdev)
{
	struct rzg2l_gpt *gpt = dev_get_drvdata(pdev->dev.parent);
	struct counter_device *counter;
	struct rzg2l_gpt_counter *priv;
	unsigned int i;

	if (!gpt)
		return -ENODEV;

	counter = devm_counter_alloc(&pdev->dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;

	priv = counter_priv(counter);
	priv->gpt = gpt;
	priv->dev = &pdev->dev;

	for (i = 0; i < gpt->max_hw_channels; i++) {
		priv->counter_mode[i] = MODE_1;
		priv->reset_counter[i] = RESET_NOT_USE;
		priv->gpt->counter_enabled[i] = false;
	}

	platform_set_drvdata(pdev, priv);

	counter->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "gpt%u-counter", gpt->id);
	if (!counter->name)
		return -ENOMEM;

	counter->parent = &pdev->dev;
	counter->ops = &rzg2l_gpt_counter_ops;
	counter->counts = rzg2l_gpt_counts;
	counter->num_counts = gpt->max_hw_channels;

	return devm_counter_add(&pdev->dev, counter);
}

static void rzg2l_gpt_counter_remove(struct platform_device *pdev)
{
	struct rzg2l_gpt_counter *priv = platform_get_drvdata(pdev);
	unsigned int ch;

	if (!priv)
		return;

	for (ch = 0; ch < priv->gpt->max_hw_channels; ch++)
		rzg2l_gpt_counter_disable(priv, ch);
}

static struct platform_driver rzg2l_gpt_counter_driver = {
	.probe = rzg2l_gpt_counter_probe,
	.remove_new = rzg2l_gpt_counter_remove,
	.driver = {
		.name = "rzg2l-gpt-counter",
	},
};
module_platform_driver(rzg2l_gpt_counter_driver);

MODULE_ALIAS("platform:rzg2l-gpt-counter");
MODULE_AUTHOR("NghiaVo <nghia.vo.zn@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L GPT Counter driver");
MODULE_LICENSE("GPL");

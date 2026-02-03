// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

#define RZT2_ADCSR_REG			0x00
#define RZT2_ADCSR_ADIE_MASK		BIT(12)
#define RZT2_ADCSR_ADCS_MASK		GENMASK(14, 13)
#define RZT2_ADCSR_ADCS_SINGLE		0b00
#define RZT2_ADCSR_ADCS_CONTINUOUS	0b10
#define RZT2_ADCSR_ADST_MASK		BIT(15)

#define RZT2_ADANSA0_REG		0x04
#define RZT2_ADANSA0_CH_MASK(x)	BIT(x)

#define RZT2_ADDR_REG(x)		(0x20 + 0x2 * (x))

#define RZT2_ADCALCTL_REG		0x1f0
#define RZT2_ADCALCTL_CAL_MASK		BIT(0)
#define RZT2_ADCALCTL_CAL_RDY_MASK	BIT(1)
#define RZT2_ADCALCTL_CAL_ERR_MASK	BIT(2)

#define RZT2_ADC_MAX_CHANNELS		16

#define SINGLE_SCAN_REG			(0x0 << 13)
#define GROUP_SCAN_REG			(0x1 << 13)
#define CONTINUOUS_SCAN_REG		(0x2 << 13)
#define STOP_SCAN_REG			(0x2 << 13)

#define FIELD_MODIFY(_mask, _reg_p, _val)						\
	({										\
		typecheck_pointer(_reg_p);						\
		__BF_FIELD_CHECK(_mask, *(_reg_p), _val, "FIELD_MODIFY: ");		\
		*(_reg_p) &= ~(_mask);							\
		*(_reg_p) |= (((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask));	\
	})

struct rzt2_adc {
	void __iomem *base;
	struct device *dev;

	struct completion completion;
	/* lock to protect against multiple access to the device */
	struct mutex lock;

	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int max_channels;
	bool has_adcalctl;
	int scan_mode;
};

static const char *const rzt2_adc_scan_mode[] = {
	"single",
	"group",
	"continuous",
	"stop",
};

int scan_mode_reg[] = {
	SINGLE_SCAN_REG,
	GROUP_SCAN_REG,
	CONTINUOUS_SCAN_REG,
	STOP_SCAN_REG,
};

enum scan_mode {
	SINGLE_SCAN,
	GROUP_SCAN,
	CONTINUOUS_SCAN,
	STOP_SCAN,
};

static void rzt2_adc_set_bit(struct rzt2_adc *adc, u32 offset, u16 mask)
{
	writew(readw(adc->base + offset) | mask, adc->base + offset);
}

static void rzt2_adc_clear_bit(struct rzt2_adc *adc, u32 offset, u16 mask)
{
	writew(readw(adc->base + offset) & ~mask, adc->base + offset);
}

static u16 rzt2_adc_get_bit(struct rzt2_adc *adc, u32 offset, u16 mask)
{
	return readw(adc->base + offset) & mask;
}

static int rzt2_adc_get_scan_mode(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct rzt2_adc *adc = iio_priv(indio_dev);

	return adc->scan_mode;
}

static int rzt2_adc_set_scan_mode(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					unsigned int type)
{
	struct rzt2_adc *adc = iio_priv(indio_dev);

	adc->scan_mode = type;
	writew(0x00, adc->base + RZT2_ADANSA0_REG);
	return 0;
}

static const struct iio_enum rzt2_adc_scan_mode_en = {
	.items = rzt2_adc_scan_mode,
	.num_items = ARRAY_SIZE(rzt2_adc_scan_mode),
	.get = rzt2_adc_get_scan_mode,
	.set = rzt2_adc_set_scan_mode,
};

static const struct iio_chan_spec_ext_info rzt2_adc_cnt_ext_info[] = {
	IIO_ENUM("scan_mode", IIO_SEPARATE, &rzt2_adc_scan_mode_en),
	IIO_ENUM_AVAILABLE("scan_mode", IIO_SHARED_BY_TYPE, &rzt2_adc_scan_mode_en),
	{}
};

static const struct iio_chan_spec rzt2_adc_cnt_channels = {
	.type = IIO_COUNT,
	.channel = 0,
	.ext_info = rzt2_adc_cnt_ext_info,
	.indexed = 1,
};

static void rzt2_adc_start(struct rzt2_adc *adc, unsigned int conversion_type)
{
	u16 reg;

	reg = readw(adc->base + RZT2_ADCSR_REG);

	/* Set conversion type */
	FIELD_MODIFY(RZT2_ADCSR_ADCS_MASK, &reg, conversion_type);

	/* Set end of conversion interrupt and start bit. */
	reg |= RZT2_ADCSR_ADIE_MASK | RZT2_ADCSR_ADST_MASK;

	writew(reg, adc->base + RZT2_ADCSR_REG);
}

static void rzt2_adc_stop(struct rzt2_adc *adc)
{
	u16 reg;

	reg = readw(adc->base + RZT2_ADCSR_REG);

	/* Clear end of conversion interrupt and start bit. */
	reg &= ~(RZT2_ADCSR_ADIE_MASK | RZT2_ADCSR_ADST_MASK);

	writew(reg, adc->base + RZT2_ADCSR_REG);
}

static int rzt2_adc_read_single(struct rzt2_adc *adc, unsigned int ch, int *val)
{
	int ret;

	ret = pm_runtime_resume_and_get(adc->dev);
	if (ret)
		return ret;

	mutex_lock(&adc->lock);

	reinit_completion(&adc->completion);

	/* Enable a single channel */
	writew(RZT2_ADANSA0_CH_MASK(ch), adc->base + RZT2_ADANSA0_REG);

	rzt2_adc_start(adc, RZT2_ADCSR_ADCS_SINGLE);

	/*
	 * Datasheet Page 2770, Table 41.1:
	 * 0.32us per channel when sample-and-hold circuits are not in use.
	 */
	ret = wait_for_completion_timeout(&adc->completion, usecs_to_jiffies(1));
	if (!ret) {
		ret = -ETIMEDOUT;
		goto disable;
	}

	*val = readw(adc->base + RZT2_ADDR_REG(ch));
	ret = IIO_VAL_INT;

disable:
	rzt2_adc_stop(adc);

	mutex_unlock(&adc->lock);

	pm_runtime_put_autosuspend(adc->dev);

	return ret;
}

static int rzt2_adc_read_continuous_per_loop(struct rzt2_adc *adc, unsigned int ch, int *val)
{
	int ret;

	mutex_lock(&adc->lock);

	reinit_completion(&adc->completion);

	/* Enable a channel */
	writew(RZT2_ADANSA0_CH_MASK(ch), adc->base + RZT2_ADANSA0_REG);

	/*Enable scan complete irq*/
	rzt2_adc_set_bit(adc, RZT2_ADCSR_REG, RZT2_ADCSR_ADIE_MASK);
	/*
	 * Datasheet Page 2770, Table 41.1:
	 * 0.32us per channel when sample-and-hold circuits are not in use.
	 */
	ret = wait_for_completion_timeout(&adc->completion, usecs_to_jiffies(1));
	if (!ret) {
		ret = -ETIMEDOUT;
		rzt2_adc_stop(adc);

		mutex_unlock(&adc->lock);

		pm_runtime_put_autosuspend(adc->dev);

		return ret;
	}

	*val = (readw(adc->base + RZT2_ADDR_REG(ch)));
	ret = IIO_VAL_INT;

	/*Disable Scan End IRQ*/
	rzt2_adc_clear_bit(adc, RZT2_ADCSR_REG, RZT2_ADCSR_ADIE_MASK);

	mutex_unlock(&adc->lock);

	return ret;
}

static int rzt2_adc_read_continuous_loop(struct rzt2_adc *adc, unsigned int ch, int *val)
{
	int max_time_out, ret;

	max_time_out = 100;

	ret = pm_runtime_resume_and_get(adc->dev);
	if (ret)
		return ret;

	writew(0xff, adc->base + RZT2_ADANSA0_REG);
	rzt2_adc_start(adc, RZT2_ADCSR_ADCS_CONTINUOUS);
	do {
		if (!(max_time_out--)) {
			// rzt2_adc_stop(adc);
			pr_err("adc: %s stopping ADC, timed out\n", __func__);
			return ret;
		}

		ret = rzt2_adc_read_continuous_per_loop(adc, ch, val);

		pr_info("adc: Channel %d [Iteration %d]: %d\n", ch, max_time_out, *val);
		mdelay(50);
	} while (rzt2_adc_get_bit(adc, RZT2_ADCSR_REG, RZT2_ADCSR_ADST_MASK));

	rzt2_adc_stop(adc);
	// pm_runtime_put_autosuspend(adc->dev);
	return ret;
}

static int rzt2_adc_stop_scan(struct rzt2_adc *adc)
{
	rzt2_adc_stop(adc);
	dev_info(adc->dev, "In STOP SCAN mode\n");
	return -EINVAL;
}

static void rzt2_adc_set_cal(struct rzt2_adc *adc, bool cal)
{
	u16 val;

	val = readw(adc->base + RZT2_ADCALCTL_REG);
	if (cal)
		val |= RZT2_ADCALCTL_CAL_MASK;
	else
		val &= ~RZT2_ADCALCTL_CAL_MASK;

	writew(val, adc->base + RZT2_ADCALCTL_REG);
}

static int rzt2_adc_calibrate(struct rzt2_adc *adc)
{
	u16 val;
	int ret;

	if (!adc->has_adcalctl) {
		dev_dbg(adc->dev, "Skipping calibration (no ADCALCTL)\n");
		return 0;
	}

	rzt2_adc_set_cal(adc, true);

	ret = read_poll_timeout(readw, val, val & RZT2_ADCALCTL_CAL_RDY_MASK,
				200, 1000, true, adc->base + RZT2_ADCALCTL_REG);
	if (ret) {
		dev_err(adc->dev, "Calibration timed out: %d\n", ret);
		return ret;
	}

	rzt2_adc_set_cal(adc, false);

	if (val & RZT2_ADCALCTL_CAL_ERR_MASK) {
		dev_err(adc->dev, "Calibration failed\n");
		return -EINVAL;
	}

	return 0;
}

static int rzt2_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct rzt2_adc *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (adc->scan_mode == STOP_SCAN)
			return rzt2_adc_stop_scan(adc);

		if (adc->scan_mode == SINGLE_SCAN)
			return rzt2_adc_read_single(adc, chan->channel, val);

		if (adc->scan_mode == CONTINUOUS_SCAN)
			return rzt2_adc_read_continuous_loop(adc, chan->channel, val);
		return -EINVAL;
	case IIO_CHAN_INFO_SCALE:
		*val = 1800;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info rzt2_adc_iio_info = {
	.read_raw = rzt2_adc_read_raw,
};

/* Per-compatible SoC/IP data */
struct rzt2_adc_soc_data {
	bool has_adcalctl;
};

static const struct rzt2_adc_soc_data rzt2_adc_soc_with_cal = {
	.has_adcalctl = true,
};

static const struct rzt2_adc_soc_data rzt2_adc_soc_no_cal = {
	.has_adcalctl = false,
};

static irqreturn_t rzt2_adc_isr(int irq, void *private)
{
	struct rzt2_adc *adc = private;

	complete(&adc->completion);

	return IRQ_HANDLED;
}

static const struct iio_chan_spec rzt2_adc_chan_template = {
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE),
	.type = IIO_VOLTAGE,
};

static int rzt2_adc_parse_properties(struct platform_device *pdev, struct rzt2_adc *adc)
{
	struct device *dev = &pdev->dev;
	struct iio_chan_spec *chan_array, *chan;
	int num_chan, ret;
	int max_chan_id = RZT2_ADC_MAX_CHANNELS - 1;
	const struct iio_chan_spec *template = &rzt2_adc_chan_template;
	unsigned int i, j;
	struct fwnode_handle *fwnode;

	num_chan = device_get_child_node_count(&pdev->dev);
	if (num_chan < 0)
		return dev_err_probe(adc->dev, num_chan, "Failed to read channel info");

	if (!num_chan)
		return -ENOENT;

	chan_array = devm_kcalloc(dev, num_chan, sizeof(*chan_array),
				  GFP_KERNEL);
	if (!chan_array)
		return -ENOMEM;

	chan = &chan_array[0];

	j = 0;
	device_for_each_child_node(&pdev->dev, fwnode) {
		u32 ch;

		ret = fwnode_property_read_u32(fwnode, "reg", &ch);
		if (ret)
			return ret;

		if (max_chan_id >= 0 && ch > max_chan_id)
			return -ERANGE;

		*chan = *template;
		chan->channel = ch;
		chan++;
		j++;
	}

	memcpy(&chan_array[j], &rzt2_adc_cnt_channels, sizeof(rzt2_adc_cnt_channels));
	adc->num_channels = num_chan + 1;
	adc->channels = chan_array;

	for (i = 0; i < adc->num_channels; i++)
		if (chan_array[i].channel + 1 > adc->max_channels)
			adc->max_channels = chan_array[i].channel + 1;

	return 0;
}

static int rzt2_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct rzt2_adc *adc;
	int ret, irq;
	const struct rzt2_adc_soc_data *soc;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->dev = dev;
	init_completion(&adc->completion);

	ret = devm_mutex_init(dev, &adc->lock);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, adc);

	ret = rzt2_adc_parse_properties(pdev, adc);
	if (ret)
		return ret;

	adc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	soc = of_device_get_match_data(dev);
	if (!soc) {
		dev_err(dev, "Missing SoC data from compatible\n");
		return -EINVAL;
	}

	adc->has_adcalctl = soc->has_adcalctl;

	pm_runtime_set_autosuspend_delay(dev, 300);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	irq = platform_get_irq_byname(pdev, "adi");
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rzt2_adc_isr, 0, dev_name(dev), adc);
	if (ret)
		return ret;

	indio_dev->name = "rzt2-adc";
	indio_dev->info = &rzt2_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->channels;
	indio_dev->num_channels = adc->num_channels;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id rzt2_adc_match[] = {
	{ .compatible = "renesas,rzt2h-adc", .data = &rzt2_adc_soc_with_cal },
	{ .compatible = "renesas,rzt2-adc", .data = &rzt2_adc_soc_no_cal  },
	{ }
};
MODULE_DEVICE_TABLE(of, rzt2_adc_match);

static int rzt2_adc_pm_runtime_resume(struct device *dev)
{
	struct rzt2_adc *adc = dev_get_drvdata(dev);

	/*
	 * Datasheet Page 2810, Section 41.5.6:
	 * After release from the module-stop state, wait for at least
	 * 0.5 µs before starting A/D conversion.
	 */
	fsleep(1);

	return rzt2_adc_calibrate(adc);
}

static const struct dev_pm_ops rzt2_adc_pm_ops = {
	RUNTIME_PM_OPS(NULL, rzt2_adc_pm_runtime_resume, NULL)
};

static struct platform_driver rzt2_adc_driver = {
	.probe		= rzt2_adc_probe,
	.driver		= {
		.name		= "rzt2-adc",
		.of_match_table = rzt2_adc_match,
		.pm		= pm_ptr(&rzt2_adc_pm_ops),
	},
};

module_platform_driver(rzt2_adc_driver);

MODULE_AUTHOR("Cosmin Tanislav <cosmin-gabriel.tanislav.xa@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/T2 driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DRIVER");

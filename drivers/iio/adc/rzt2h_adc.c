// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/T2H A/D Converter driver
 *
 * Author: AnhHoang <anh.hoang.yk@renesas.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/property.h>

#define DRIVER_NAME		"rzt2h-adc"

#define ADCSR				0x00
#define ADIE				BIT(12)
#define ADST				BIT(15)
#define ADCS				(0x3 << 13)
#define SINGLE_SCAN_REG			(0x0 << 13)
#define GROUP_SCAN_REG			(0x1 << 13)
#define CONTINUOUS_SCAN_REG		(0x2 << 13)
#define STOP_SCAN_REG			(0x2 << 13)

#define ADANSA0				0x04

#define ADDR(n)				(0x20 + (n) * 0x02)
/* Store in single scan mode */
#define ADDR_SINGLE_SCAN_MASK		GENMASK(11, 0)

#define ADCALCTL			0x1f0
#define CAL				BIT(0)
#define CAL_DRY				BIT(1)
#define CAL_ERR				BIT(2)

#define RZT2H_ADC_MAX_CHANNELS		16
#define RZG2L_ADC_TIMEOUT		usecs_to_jiffies(1 * 4)

struct rzt2h_adc_data {
	const struct iio_chan_spec *channels;
	u8 num_channels;
};

struct rzt2h_adc {
	void __iomem *base;
	struct clk *clk;
	struct completion completion;
	const struct rzt2h_adc_data *data;
	struct mutex lock;
	u16 last_val[RZT2H_ADC_MAX_CHANNELS];
	int scan_mode;
	int n_irq;
};

static int rzt2h_adc_set_power(struct iio_dev *indio_dev, bool on);

static const char * const rzt2h_adc_channel_name[] = {
	"adc0",
	"adc1",
	"adc2",
	"adc3",
	"adc4",
	"adc5",
	"adc6",
	"adc7",
	"adc8",
	"adc9",
	"adc10",
	"adc11",
	"adc12",
	"adc13",
	"adc14",
	"adc15",
};

static void rzt2h_adc_set_bit(struct rzt2h_adc *adc, u32 offset, u16 mask)
{
	writew(readw(adc->base + offset) | mask, adc->base + offset);
}

static void rzt2h_adc_clear_bit(struct rzt2h_adc *adc, u32 offset, u16 mask)
{
	writew(readw(adc->base + offset) & ~mask, adc->base + offset);
}

static u16 rzt2h_adc_get_bit(struct rzt2h_adc *adc, u32 offset, u16 mask)
{
	return readw(adc->base + offset) & mask;
}

static void rzt2h_adc_update_bits(struct rzt2h_adc *adc, u32 offset, u16 mask, u16 bits)
{
	writew((readw(adc->base + offset) & ~mask) | bits, adc->base + offset);
}

static const char *const rzt2h_adc_scan_mode[] = {
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

static int rzt2h_adc_get_scan_mode(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	return adc->scan_mode;
}

static int rzt2h_adc_set_scan_mode(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					unsigned int type)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	adc->scan_mode = type;
	rzt2h_adc_set_power(indio_dev, true);
	rzt2h_adc_update_bits(adc, ADCSR, ADCS, scan_mode_reg[type]);
	writew(0x00, adc->base + ADANSA0);

	if (type == CONTINUOUS_SCAN) {
		writew(0xff, adc->base + ADANSA0);
		rzt2h_adc_set_bit(adc, ADCSR, ADIE);
		rzt2h_adc_set_bit(adc, ADCSR, ADST);
		return 0;
	}

	if (type == STOP_SCAN) {
		rzt2h_adc_clear_bit(adc, ADCSR, ADST);
		rzt2h_adc_clear_bit(adc, ADCSR, ADIE);
	}
	rzt2h_adc_set_power(indio_dev, false);
	return 0;
}

static const struct iio_enum rzt2h_adc_scan_mode_en = {
	.items = rzt2h_adc_scan_mode,
	.num_items = ARRAY_SIZE(rzt2h_adc_scan_mode),
	.get = rzt2h_adc_get_scan_mode,
	.set = rzt2h_adc_set_scan_mode,
};

static const struct iio_chan_spec_ext_info rzt2h_adc_cnt_ext_info[] = {
	IIO_ENUM("scan_mode", IIO_SEPARATE, &rzt2h_adc_scan_mode_en),
	IIO_ENUM_AVAILABLE("scan_mode", IIO_SHARED_BY_TYPE, &rzt2h_adc_scan_mode_en),
	{}
};

static const struct iio_chan_spec rzt2h_adc_cnt_channels = {
	.type = IIO_COUNT,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_ENABLE),
	.ext_info = rzt2h_adc_cnt_ext_info,
	.indexed = 1,
};

static void rzt2h_adc_start_stop(struct rzt2h_adc *adc, bool start)
{
	int timeout = 5;

	if (start) {
		rzt2h_adc_set_bit(adc, ADCSR, ADST);
		rzt2h_adc_set_bit(adc, ADCSR, ADIE);
		return;
	}

	do {
		usleep_range(100, 200);
		timeout--;
		if (!timeout) {
			pr_err("%s stopping ADC timed out\n", __func__);
			break;
		}
	} while (rzt2h_adc_get_bit(adc, ADCSR, ADST));
}

static int rzt2h_adc_set_power(struct iio_dev *indio_dev, bool on)
{
	struct device *dev = indio_dev->dev.parent;

	if (on)
		return pm_runtime_resume_and_get(dev);

	return pm_runtime_put_sync(dev);
}

static int rzt2h_adc_conversion(struct iio_dev *indio_dev, struct rzt2h_adc *adc, u8 ch)
{
	int ret;

	ret = rzt2h_adc_set_power(indio_dev, true);
	if (ret)
		return ret;

	/* Select trigger mode: software trigger. */
	/* Select type of scan mode: single scan mode. */

	reinit_completion(&adc->completion);

	rzt2h_adc_start_stop(adc, true);

	if (!wait_for_completion_timeout(&adc->completion, RZG2L_ADC_TIMEOUT)) {
		rzt2h_adc_clear_bit(adc, ADCSR, ADIE);
		rzt2h_adc_start_stop(adc, false);
		rzt2h_adc_set_power(indio_dev, false);
		return -ETIMEDOUT;
	}

	if (adc->scan_mode == CONTINUOUS_SCAN)
		return 0;

	return rzt2h_adc_set_power(indio_dev, false);
}

static int rzt2h_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	int ret;
	u8 ch;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_VOLTAGE)
			return -EINVAL;

		if (adc->scan_mode == STOP_SCAN) {
			dev_info(&indio_dev->dev, "In STOP_SCAN mode\n");
			return -EINVAL;
		}

		mutex_lock(&adc->lock);
		rzt2h_adc_set_power(indio_dev, true);
		ch = chan->channel;
		writew(BIT(ch), adc->base + ADANSA0);
		rzt2h_adc_start_stop(adc, true);
		udelay(5);

		if (adc->scan_mode == SINGLE_SCAN) {
			ret = rzt2h_adc_conversion(indio_dev, adc, ch);
			if (ret) {
				mutex_unlock(&adc->lock);
				return ret;
			}
			*val = adc->last_val[ch];
		}

		if (adc->scan_mode == CONTINUOUS_SCAN) {
			int n = 100;

			while (n--) {
				ret = rzt2h_adc_conversion(indio_dev, adc, ch);
				if (ret) {
					mutex_unlock(&adc->lock);
					return ret;
				}
				*val = adc->last_val[ch];
				pr_info("Channel %d [Iteration %d]: %d\n", ch, n, *val);
				mdelay(50);
			}
		}

		mutex_unlock(&adc->lock);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info rzt2h_adc_iio_info = {
	.read_raw = rzt2h_adc_read_raw,
};

static irqreturn_t rzt2h_adc_isr(int irq, void *dev_id)
{
	struct rzt2h_adc *adc = dev_id;
	int ch;

	ch = ffs(readw(adc->base + ADANSA0)) - 1;
	adc->last_val[ch] = readw(adc->base + ADDR(ch)) & ADDR_SINGLE_SCAN_MASK;

	/*clear interrupt*/
	rzt2h_adc_clear_bit(adc, ADCSR, ADIE);
	complete(&adc->completion);

	return IRQ_HANDLED;
}

static int rzt2h_adc_parse_properties(struct platform_device *pdev, struct rzt2h_adc *adc)
{
	struct iio_chan_spec *chan_array;
	struct fwnode_handle *fwnode;
	struct rzt2h_adc_data *data;
	unsigned int channel;
	int num_channels;
	int ret;
	u8 i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	num_channels = device_get_child_node_count(&pdev->dev);
	if (!num_channels) {
		dev_err(&pdev->dev, "no channel children\n");
		return -ENODEV;
	}

	if (num_channels > RZT2H_ADC_MAX_CHANNELS) {
		dev_err(&pdev->dev, "num of channel children out of range\n");
		return -EINVAL;
	}

	chan_array = devm_kcalloc(&pdev->dev, num_channels, sizeof(*chan_array),
				  GFP_KERNEL);
	if (!chan_array)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node(&pdev->dev, fwnode) {
		ret = fwnode_property_read_u32(fwnode, "reg", &channel);
		if (ret) {
			fwnode_handle_put(fwnode);
			return ret;
		}

		if (channel >= RZT2H_ADC_MAX_CHANNELS) {
			fwnode_handle_put(fwnode);
			return -EINVAL;
		}

		chan_array[i].type = IIO_VOLTAGE;
		chan_array[i].indexed = 1;
		chan_array[i].channel = channel;
		chan_array[i].info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		chan_array[i].datasheet_name = rzt2h_adc_channel_name[channel];
		i++;
	}

	memcpy(&chan_array[i], &rzt2h_adc_cnt_channels, sizeof(rzt2h_adc_cnt_channels));
	data->num_channels = num_channels + 1;
	data->channels = chan_array;
	adc->data = data;

	return 0;
}

static int rzt2h_adc_hw_init(struct rzt2h_adc *adc)
{
	int timeout = 5;
	int ret;

	ret = clk_prepare_enable(adc->clk);
	if (ret)
		return ret;

	writew(CAL, adc->base + ADCALCTL);
	while (!rzt2h_adc_get_bit(adc, ADCALCTL, CAL_DRY)) {
		if (!timeout) {
			ret = -ETIMEDOUT;
			clk_disable_unprepare(adc->clk);
			return ret;
		}
		timeout--;
		usleep_range(100, 200);
	}

	if (rzt2h_adc_get_bit(adc, ADCALCTL, CAL_ERR)) {
		pr_err("adc: Calibration error\n");
		/* Clear CAL bit to 0 */
		writew(0, adc->base + ADCALCTL);
		clk_disable_unprepare(adc->clk);
	}

	return 0;
}

static void rzt2h_adc_pm_runtime_disable(void *data)
{
	struct device *dev = data;

	pm_runtime_disable(dev->parent);
}

static void rzt2h_adc_pm_runtime_set_suspended(void *data)
{
	struct device *dev = data;

	pm_runtime_set_suspended(dev->parent);
}

static int rzt2h_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct rzt2h_adc *adc;
	int ret;
	int irq;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);

	ret = rzt2h_adc_parse_properties(pdev, adc);
	if (ret)
		return ret;

	mutex_init(&adc->lock);

	adc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	adc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(adc->clk)) {
		dev_err(dev, "Failed to get adc clk");
		return PTR_ERR(adc->clk);
	}

	ret = rzt2h_adc_hw_init(adc);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize ADC HW, %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rzt2h_adc_isr,
			       0, dev_name(dev), adc);
	if (ret < 0)
		return ret;

	init_completion(&adc->completion);

	platform_set_drvdata(pdev, indio_dev);
	indio_dev->name = DRIVER_NAME;
	indio_dev->info = &rzt2h_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->data->channels;
	indio_dev->num_channels = adc->data->num_channels;
	pm_runtime_set_suspended(dev);
	ret = devm_add_action_or_reset(&pdev->dev,
				       rzt2h_adc_pm_runtime_set_suspended, &indio_dev->dev);
	if (ret)
		return ret;

	pm_runtime_enable(dev);
	ret = devm_add_action_or_reset(&pdev->dev,
				       rzt2h_adc_pm_runtime_disable, &indio_dev->dev);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);

	return ret;
}

static const struct of_device_id rzt2h_adc_match[] = {
	{ .compatible = "renesas,rzt2h-adc",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzt2h_adc_match);

static int __maybe_unused rzt2h_adc_pm_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	clk_disable_unprepare(adc->clk);

	return 0;
}

static int __maybe_unused rzt2h_adc_pm_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = clk_prepare_enable(adc->clk);

	return ret;
}

static const struct dev_pm_ops rzt2h_adc_pm_ops = {
	SET_RUNTIME_PM_OPS(rzt2h_adc_pm_runtime_suspend,
			   rzt2h_adc_pm_runtime_resume,
			   NULL)
};

static struct platform_driver rzt2h_adc_driver = {
	.probe		= rzt2h_adc_probe,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table = rzt2h_adc_match,
		.pm		= &rzt2h_adc_pm_ops,
	},
};

module_platform_driver(rzt2h_adc_driver);

MODULE_DESCRIPTION("Renesas RZ/G2L ADC driver");
MODULE_AUTHOR("AnhHoang <anh.hoang.yk@renesas.com>");
MODULE_LICENSE("GPL v2");

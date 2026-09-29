// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/iio/adc-helpers.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kthread.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/regmap.h>

#define RZT2H_ADCSR_REG			0x00
#define RZT2H_ADCSR_ADIE_MASK		BIT(12)
#define RZT2H_ADCSR_ADCS_MASK		GENMASK(14, 13)
#define RZT2H_ADCSR_ADCS_SINGLE		0b00
#define RZT2H_ADCSR_ADCS_CONTINUOUS	0b10
#define RZT2H_ADCSR_ADST_MASK		BIT(15)

#define RZT2H_ADANSA0_REG		0x04
#define RZT2H_ADANSA0_CH_MASK(x)	BIT(x)

#define RZT2H_ADDR_REG(x)		(0x20 + 0x2 * (x))

#define RZT2H_ADCALCTL_REG		0x1f0
#define RZT2H_ADCALCTL_CAL_MASK		BIT(0)
#define RZT2H_ADCALCTL_CAL_RDY_MASK	BIT(1)
#define RZT2H_ADCALCTL_CAL_ERR_MASK	BIT(2)

#define RZT2H_ADC_CHANNEL_BYTES		sizeof(u16)
#define RZT2H_ADC_DMA_PERIOD_SAMPLES	128
#define RZT2H_ADC_DMA_PERIODS		64
#define RZT2H_ADC_DMA_BUFFER_SAMPLES	(RZT2H_ADC_DMA_PERIODS * \
					 RZT2H_ADC_DMA_PERIOD_SAMPLES)

#define RZT2H_ADC_NEED_CALIBRATION	BIT(0)
#define RZT2H_ADC_CFG_SYS		BIT(1)

enum rzt2h_adc_sysc_func_id {
	RZT2H_SYSC_FUNC_ID_MSTP_ADA_B,
	RZT2H_ADC_SYSC_FUNC_MAX,
};

struct rzt2h_adc_sysc_function {
	u32 offset;
	u32 mask;
};

struct rzt2h_adc_sysc_info {
	const struct rzt2h_adc_sysc_function
		functions[RZT2H_ADC_SYSC_FUNC_MAX];
};

struct rzt2h_adc_sysc {
	struct regmap *regmap;
	const struct rzt2h_adc_sysc_info *info;
};

struct rzt2h_adc_data {
	u32 flags;
	unsigned int max_channels;
	unsigned long timeout;
	const struct iio_chan_spec *channel_template;
	const struct rzt2h_adc_ops *ops;
	const struct rzt2h_adc_sysc_info sysc_info;
};

struct rzt2h_adc_dma {
	struct dma_chan *chan;
	u16 *buf;
	dma_addr_t addr;
	size_t buf_size;

	unsigned int period_index;
	unsigned int period_bytes;
	unsigned int first_chan;
	unsigned int sample_chans;

	u8 *gather;
	unsigned int gather_len;

	atomic_t pending_periods;

	wait_queue_head_t wq;
	struct task_struct *thread;
};

struct rzt2h_adc {
	void __iomem *base;
	struct device *dev;
	struct reset_control *rstc;
	phys_addr_t phys_base;
	struct rzt2h_adc_dma dma;
	struct completion completion;
	/* lock to protect against multiple access to the device */
	struct mutex lock;

	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	u16 *buf;
	const struct rzt2h_adc_data *data;
	struct rzt2h_adc_sysc *sysc;

	int irq;
};

struct rzt2h_adc_ops {
	int (*init)(struct rzt2h_adc *adc);
	int (*deinit)(struct rzt2h_adc *adc);
};

static int rzt2h_sysc_config_func(struct rzt2h_adc_sysc *sysc,
				  enum rzt2h_adc_sysc_func_id fid, u32 val)
{
	if (!sysc)
		return 0;
	else {
		const struct rzt2h_adc_sysc_info *info = sysc->info;
		const struct rzt2h_adc_sysc_function *functions = info->functions;

		if (fid >= RZT2H_ADC_SYSC_FUNC_MAX)
			return -EINVAL;

		if (!functions[fid].mask)
			return 0;

		return regmap_update_bits(sysc->regmap, functions[fid].offset,
					  functions[fid].mask,
					  field_prep(functions[fid].mask, val));
	}
}

static void rzt2h_adc_start(struct rzt2h_adc *adc, unsigned int conversion_type)
{
	u16 reg;

	reg = readw(adc->base + RZT2H_ADCSR_REG);

	/* Set conversion type */
	FIELD_MODIFY(RZT2H_ADCSR_ADCS_MASK, &reg, conversion_type);

	/* Set end of conversion interrupt and start bit. */
	reg |= RZT2H_ADCSR_ADIE_MASK | RZT2H_ADCSR_ADST_MASK;

	writew(reg, adc->base + RZT2H_ADCSR_REG);
}

static void rzt2h_adc_stop(struct rzt2h_adc *adc)
{
	u16 reg;

	reg = readw(adc->base + RZT2H_ADCSR_REG);

	/* Clear end of conversion interrupt and start bit. */
	reg &= ~(RZT2H_ADCSR_ADIE_MASK | RZT2H_ADCSR_ADST_MASK);

	writew(reg, adc->base + RZT2H_ADCSR_REG);
}

static int rzt2h_adc_read_single(struct rzt2h_adc *adc, unsigned int ch, int *val)
{
	const struct rzt2h_adc_data *data = adc->data;
	int ret;

	ret = pm_runtime_resume_and_get(adc->dev);
	if (ret)
		return ret;

	mutex_lock(&adc->lock);

	reinit_completion(&adc->completion);

	/* Enable a single channel */
	writew(RZT2H_ADANSA0_CH_MASK(ch), adc->base + RZT2H_ADANSA0_REG);

	rzt2h_adc_start(adc, RZT2H_ADCSR_ADCS_SINGLE);

	ret = wait_for_completion_timeout(&adc->completion,
					usecs_to_jiffies(data->timeout));
	if (!ret) {
		ret = -ETIMEDOUT;
		goto disable;
	}

	*val = readw(adc->base + RZT2H_ADDR_REG(ch));
	ret = IIO_VAL_INT;

disable:
	rzt2h_adc_stop(adc);

	mutex_unlock(&adc->lock);

	pm_runtime_put_autosuspend(adc->dev);

	return ret;
}

static void rzt2h_adc_set_cal(struct rzt2h_adc *adc, bool cal)
{
	u16 val;

	val = readw(adc->base + RZT2H_ADCALCTL_REG);
	if (cal)
		val |= RZT2H_ADCALCTL_CAL_MASK;
	else
		val &= ~RZT2H_ADCALCTL_CAL_MASK;

	writew(val, adc->base + RZT2H_ADCALCTL_REG);
}

static int rzt2h_adc_calibrate(struct rzt2h_adc *adc)
{
	u16 val;
	int ret;

	rzt2h_adc_set_cal(adc, true);

	ret = read_poll_timeout(readw, val, val & RZT2H_ADCALCTL_CAL_RDY_MASK,
				200, 1000, true, adc->base + RZT2H_ADCALCTL_REG);
	if (ret) {
		dev_err(adc->dev, "Calibration timed out: %d\n", ret);
		return ret;
	}

	rzt2h_adc_set_cal(adc, false);

	if (val & RZT2H_ADCALCTL_CAL_ERR_MASK) {
		dev_err(adc->dev, "Calibration failed\n");
		return -EINVAL;
	}

	return 0;
}

static void rzt2h_adc_push_period(struct iio_dev *indio_dev, u16 *period,
				  dma_addr_t addr)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	u16 *dst = adc->buf;
	u16 *src = period;
	unsigned int sample;
	unsigned int i;

	dma_sync_single_for_cpu(adc->dev, addr, adc->dma.period_bytes,
				DMA_FROM_DEVICE);

	for (sample = 0; sample < RZT2H_ADC_DMA_PERIOD_SAMPLES; sample++) {
		for (i = 0; i < adc->dma.gather_len; i++)
			dst[i] = src[adc->dma.gather[i]];

		src += adc->dma.sample_chans;

		iio_push_to_buffers(indio_dev, adc->buf);
	}
}

static void rzt2h_adc_advance_period_index(struct rzt2h_adc *adc, unsigned int i)
{
	adc->dma.period_index += i;
	adc->dma.period_index %= RZT2H_ADC_DMA_PERIODS;
}

static void rzt2h_adc_dma_thread_loop(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	int pending, drop;
	dma_addr_t addr;
	u16 *period;
	unsigned int i;

	pending = atomic_xchg(&adc->dma.pending_periods, 0);

	if (pending >= RZT2H_ADC_DMA_PERIODS) {
		/*
		 * The consumer fell a full buffer behind and the oldest periods
		 * have already been overwritten by the cyclic DMA transfer.
		 * Drop them and jump straight to the oldest period that has
		 * not been overwritten.
		 */
		drop = pending - RZT2H_ADC_DMA_PERIODS + 1;

		rzt2h_adc_advance_period_index(adc, drop);
		pending -= drop;
	}

	for (i = 0; i < pending; i++) {
		unsigned int backlog = atomic_read(&adc->dma.pending_periods) +
				       pending - i;

		/*
		 * Bail if enough new periods have completed since reading the
		 * pending_periods that the next period about to be read is at
		 * risk of being overwritten.
		 */
		if (backlog >= RZT2H_ADC_DMA_PERIODS) {
			rzt2h_adc_advance_period_index(adc, pending - i);
			break;
		}

		period = adc->dma.buf + adc->dma.period_index *
			 RZT2H_ADC_DMA_PERIOD_SAMPLES * adc->dma.sample_chans;
		addr = adc->dma.addr + adc->dma.period_index *
		       adc->dma.period_bytes;

		rzt2h_adc_push_period(indio_dev, period, addr);
		rzt2h_adc_advance_period_index(adc, 1);
	}
}

static int rzt2h_adc_dma_thread(void *data)
{
	struct iio_dev *indio_dev = data;
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	while (!kthread_should_stop()) {
		wait_event_interruptible(adc->dma.wq,
					 atomic_read(&adc->dma.pending_periods) ||
					 kthread_should_stop());

		if (kthread_should_stop())
			break;

		rzt2h_adc_dma_thread_loop(indio_dev);
	}

	return 0;
}

static void rzt2h_adc_dma_callback(void *data)
{
	struct iio_dev *indio_dev = data;
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	atomic_inc(&adc->dma.pending_periods);
	wake_up(&adc->dma.wq);
}

static void rzt2h_adc_dma_calc_layout(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	unsigned int hi = 0;
	unsigned int lo = adc->data->max_channels - 1;
	const struct iio_chan_spec *chan;
	unsigned int sample_chans;
	unsigned int first_chan;
	unsigned int scan_index;
	unsigned int swap;
	unsigned int idx;

	/* Find the lowest and highest enabled channel. */
	iio_for_each_active_channel(indio_dev, scan_index) {
		chan = &indio_dev->channels[scan_index];

		lo = min_t(unsigned int, lo, chan->channel);
		hi = max_t(unsigned int, hi, chan->channel);
	}

	/*
	 * The DMA has no scatter-gather and transfers must have a power-of-two
	 * width, so pick the smallest power-of-two-aligned block of channels
	 * that covers all enabled channels.
	 */
	for (sample_chans = 1; sample_chans <= adc->data->max_channels;
	     sample_chans <<= 1) {
		first_chan = round_down(lo, sample_chans);

		if (first_chan + sample_chans > hi)
			break;
	}

	/*
	 * Build a table to map each enabled channel to its position in the
	 * transferred block, it will be used later to extract only the enabled
	 * channels out of it.
	 * The DMA moves data in 32-bit words, which swaps each pair of adjacent
	 * 16-bit channels. Undo it.
	 */
	adc->dma.gather_len = 0;
	swap = sample_chans > 1;
	iio_for_each_active_channel(indio_dev, scan_index) {
		chan = &indio_dev->channels[scan_index];
		idx = chan->channel - first_chan;

		adc->dma.gather[adc->dma.gather_len++] = idx ^ swap;
	}

	adc->dma.first_chan = first_chan;
	adc->dma.sample_chans = sample_chans;
	adc->dma.period_bytes = RZT2H_ADC_DMA_PERIOD_SAMPLES * sample_chans *
				RZT2H_ADC_CHANNEL_BYTES;
}

static int rzt2h_adc_start_dma(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config;
	unsigned int buffer_bytes;
	dma_cookie_t cookie;
	int ret;

	rzt2h_adc_dma_calc_layout(indio_dev);

	config = (struct dma_slave_config) {
		.src_addr = adc->phys_base + RZT2H_ADDR_REG(adc->dma.first_chan),
		.src_addr_width = adc->dma.sample_chans * RZT2H_ADC_CHANNEL_BYTES,
	};

	buffer_bytes = RZT2H_ADC_DMA_PERIODS * adc->dma.period_bytes;

	ret = dmaengine_slave_config(adc->dma.chan, &config);
	if (ret)
		return ret;

	desc = dmaengine_prep_dma_cyclic(adc->dma.chan, adc->dma.addr,
					 buffer_bytes, adc->dma.period_bytes,
					 DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
	if (!desc)
		return -EBUSY;

	desc->callback = rzt2h_adc_dma_callback;
	desc->callback_param = indio_dev;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(adc->dma.chan);
		return ret;
	}

	adc->dma.thread = kthread_run(rzt2h_adc_dma_thread, indio_dev,
				      "rzt2h-adc-dma");
	if (IS_ERR(adc->dma.thread)) {
		dmaengine_terminate_sync(adc->dma.chan);
		return PTR_ERR(adc->dma.thread);
	}

	disable_irq(adc->irq);

	dma_async_issue_pending(adc->dma.chan);

	return 0;
}

static int rzt2h_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	const struct iio_chan_spec *chan;
	struct device *dev = adc->dev;
	unsigned int scan_index;
	u16 val = 0;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	iio_for_each_active_channel(indio_dev, scan_index) {
		chan = &indio_dev->channels[scan_index];
		val |= RZT2H_ADANSA0_CH_MASK(chan->channel);
	}

	writew(val, adc->base + RZT2H_ADANSA0_REG);

	adc->dma.period_index = 0;
	atomic_set(&adc->dma.pending_periods, 0);

	ret = rzt2h_adc_start_dma(indio_dev);
	if (ret) {
		pm_runtime_put_autosuspend(dev);
		return ret;
	}

	rzt2h_adc_start(adc, RZT2H_ADCSR_ADCS_CONTINUOUS);

	return 0;
}

static int rzt2h_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;

	rzt2h_adc_stop(adc);

	dmaengine_terminate_sync(adc->dma.chan);

	kthread_stop(adc->dma.thread);

	enable_irq(adc->irq);

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int rzt2h_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		return rzt2h_adc_read_single(adc, chan->channel, val);
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 1800;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_buffer_setup_ops rzt2h_adc_buffer_setup_ops = {
	.postenable = rzt2h_adc_buffer_postenable,
	.predisable = rzt2h_adc_buffer_predisable,
};

static const struct iio_info rzt2h_adc_iio_info = {
	.read_raw = rzt2h_adc_read_raw,
};

static irqreturn_t rzt2h_adc_isr(int irq, void *private)
{
	struct rzt2h_adc *adc = private;

	complete(&adc->completion);

	return IRQ_HANDLED;
}

static int rzt2h_adc_parse_properties(struct rzt2h_adc *adc)
{
	const struct rzt2h_adc_data *data = adc->data;
	struct iio_chan_spec *chan_array;
	int ret;

	ret = devm_iio_adc_device_alloc_chaninfo_se(adc->dev,
						    data->channel_template,
						    data->max_channels - 1,
						    &chan_array);
	if (ret < 0)
		return dev_err_probe(adc->dev, ret, "Failed to read channel info");

	adc->num_channels = ret;
	adc->channels = chan_array;

	for (unsigned int i = 0; i < adc->num_channels; i++)
		chan_array[i].scan_index = i;

	return 0;
}

static void rzt2h_adc_free_dma_buf(void *p)
{
	struct rzt2h_adc *adc = p;

	dma_free_noncoherent(adc->dev, adc->dma.buf_size,
			     adc->dma.buf, adc->dma.addr, DMA_FROM_DEVICE);
}

static int rzt2h_adc_setup_dma(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;
	int ret;

	adc->dma.chan = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(adc->dma.chan)) {
		ret = PTR_ERR(adc->dma.chan);
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret, "DMA channel request failed\n");

		adc->dma.chan = NULL;
		return 0;
	}

	adc->buf = devm_kcalloc(dev, adc->data->max_channels, sizeof(*adc->buf),
				 GFP_KERNEL);
	if (!adc->buf)
		return -ENOMEM;

	adc->dma.gather = devm_kcalloc(dev, adc->data->max_channels,
				       sizeof(*adc->dma.gather), GFP_KERNEL);
	if (!adc->dma.gather)
		return -ENOMEM;

	adc->dma.buf_size = RZT2H_ADC_DMA_BUFFER_SAMPLES *
				    adc->data->max_channels *
				    RZT2H_ADC_CHANNEL_BYTES;
	adc->dma.buf = dma_alloc_noncoherent(dev, adc->dma.buf_size,
					     &adc->dma.addr, DMA_FROM_DEVICE,
					     GFP_KERNEL);
	if (!adc->dma.buf)
		return -ENOMEM;

	dma_sync_single_for_device(dev, adc->dma.addr, adc->dma.buf_size,
				   DMA_FROM_DEVICE);

	ret = devm_add_action_or_reset(dev, rzt2h_adc_free_dma_buf, adc);
	if (ret)
		return ret;

	return devm_iio_kfifo_buffer_setup_ext(dev, indio_dev,
					       &rzt2h_adc_buffer_setup_ops, NULL);
}

static void rzt2h_adc_reset_assert(void *data)
{
	struct rzt2h_adc *adc = data;

	reset_control_assert(adc->rstc);
}

static int rzv2h_adc_init(struct rzt2h_adc *adc)
{
	return rzt2h_sysc_config_func(adc->sysc,
			      RZT2H_SYSC_FUNC_ID_MSTP_ADA_B, 0);
}

static int rzv2h_adc_deinit(struct rzt2h_adc *adc)
{
	return rzt2h_sysc_config_func(adc->sysc,
			      RZT2H_SYSC_FUNC_ID_MSTP_ADA_B, 1);
}

static int rzt2h_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *sysc_np __free(device_node) = NULL;
	struct iio_dev *indio_dev;
	struct rzt2h_adc *adc;
	const struct rzt2h_adc_data *data;
	struct rzt2h_adc_sysc *sysc;
	struct resource *res;
	int ret;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->dev = dev;
	adc->data = data;
	init_completion(&adc->completion);
	init_waitqueue_head(&adc->dma.wq);

	ret = devm_mutex_init(dev, &adc->lock);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, adc);

	ret = rzt2h_adc_parse_properties(adc);
	if (ret)
		return ret;

	if (data->flags & RZT2H_ADC_CFG_SYS) {
		sysc_np = of_parse_phandle(dev->of_node, "renesas,sysc", 0);
		if (!sysc_np)
			return -ENODEV;

		adc->sysc = devm_kzalloc(dev, sizeof(*adc->sysc), GFP_KERNEL);
		if (!adc->sysc) {
			of_node_put(sysc_np);
			return -ENOMEM;
		}

		sysc = adc->sysc;
		sysc->info = &adc->data->sysc_info;
		sysc->regmap = syscon_node_to_regmap(sysc_np);
		of_node_put(sysc_np);
		if (IS_ERR(sysc->regmap))
			return dev_err_probe(dev, PTR_ERR(sysc->regmap),
					     "failed to get sysc regmap\n");
	}

	adc->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	adc->phys_base = res->start;

	adc->rstc = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(adc->rstc))
		return dev_err_probe(dev, PTR_ERR(adc->rstc), "failed to get reset\n");

	reset_control_deassert(adc->rstc);

	ret = devm_add_action_or_reset(dev, rzt2h_adc_reset_assert, adc);
	if (ret)
		return dev_err_probe(dev, ret,
					"failed to register reset assert devm action\n");

	if (data->ops && data->ops->init) {
		ret = data->ops->init(adc);
		if (ret != 0)
			return dev_err_probe(dev, ret, "failed to init ADC\n");
	}

	pm_runtime_set_autosuspend_delay(dev, 300);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	adc->irq = platform_get_irq_byname(pdev, "adi");
	if (adc->irq < 0)
		return adc->irq;

	ret = devm_request_irq(dev, adc->irq, rzt2h_adc_isr, 0, dev_name(dev), adc);
	if (ret)
		return ret;

	indio_dev->name = "rzt2h-adc";
	indio_dev->info = &rzt2h_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->channels;
	indio_dev->num_channels = adc->num_channels;

	ret = rzt2h_adc_setup_dma(indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static void rzt2h_adc_remove(struct platform_device *pdev)
{
	struct rzt2h_adc *adc = dev_get_drvdata(&pdev->dev);
	const struct rzt2h_adc_data *data = adc->data;

	if (data->ops && data->ops->deinit)
		data->ops->deinit(adc);
}

static const struct iio_chan_spec rzt2h_adc_chan_template = {
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE),
	.type = IIO_VOLTAGE,
	.scan_type = {
		.sign = 'u',
		.realbits = 12,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
};

static struct rzt2h_adc_data rzt2h_pdata = {
	.flags = RZT2H_ADC_NEED_CALIBRATION,
	.max_channels = 16,
	/*
	 * Datasheet Page 2770, Table 41.1:
	 * 0.32us per channel when sample-and-hold circuits are not in use.
	 */
	.timeout = 1,
	.channel_template = &rzt2h_adc_chan_template,
};

static const struct iio_chan_spec rzv2h_adc_chan_template = {
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.type = IIO_VOLTAGE,
	.scan_type = {
		.sign = 'u',
		.realbits = 12,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
};

static const struct rzt2h_adc_ops rzv2h_ops = {
	.init = rzv2h_adc_init,
	.deinit = rzv2h_adc_deinit,
};

static struct rzt2h_adc_data rzv2h_pdata = {
	.flags = RZT2H_ADC_CFG_SYS,
	.max_channels = 8,
	.timeout = 100,
	.channel_template = &rzv2h_adc_chan_template,
	.ops = &rzv2h_ops,
	.sysc_info = {
		.functions = {
			[RZT2H_SYSC_FUNC_ID_MSTP_ADA_B] = {
				.offset = 0x1600,
				.mask = BIT(0),
			},
		},
	},
};

static const struct of_device_id rzt2h_adc_match[] = {
	{ .compatible = "renesas,r9a09g077-adc", .data = &rzt2h_pdata},
	{ .compatible = "renesas,r9a09g057-adc", .data = &rzv2h_pdata},
	{ }
};
MODULE_DEVICE_TABLE(of, rzt2h_adc_match);

static int rzt2h_adc_pm_runtime_resume(struct device *dev)
{
	struct rzt2h_adc *adc = dev_get_drvdata(dev);
	const struct rzt2h_adc_data *data = adc->data;
	int ret = 0;

	if (data->flags & RZT2H_ADC_NEED_CALIBRATION) {
		/*
		 * Datasheet Page 2810, Section 41.5.6:
		 * After release from the module-stop state, wait for at least
		 * 0.5 µs before starting A/D conversion.
		 */
		fsleep(1);
		ret = rzt2h_adc_calibrate(adc);
	}

	return ret;
}

static int __maybe_unused rzt2h_adc_sys_suspend(struct device *dev)
{
	struct rzt2h_adc *adc = dev_get_drvdata(dev);
	const struct rzt2h_adc_data *data = adc->data;
	int ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	reset_control_assert(adc->rstc);

	if (data->ops && data->ops->deinit)
		data->ops->deinit(adc);

	return 0;
}

static int __maybe_unused rzt2h_adc_sys_resume(struct device *dev)
{
	struct rzt2h_adc *adc = dev_get_drvdata(dev);
	const struct rzt2h_adc_data *data = adc->data;
	int ret;

	if (data->ops && data->ops->init)
		data->ops->init(adc);

	ret = reset_control_deassert(adc->rstc);
	if (ret)
		return ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	return 0;
}

static const struct dev_pm_ops rzt2h_adc_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, rzt2h_adc_pm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(rzt2h_adc_sys_suspend, rzt2h_adc_sys_resume)
};

static struct platform_driver rzt2h_adc_driver = {
	.probe		= rzt2h_adc_probe,
	.remove_new	= rzt2h_adc_remove,
	.driver		= {
		.name		= "rzt2h-adc",
		.of_match_table = rzt2h_adc_match,
		.pm		= pm_ptr(&rzt2h_adc_pm_ops),
	},
};

module_platform_driver(rzt2h_adc_driver);

MODULE_AUTHOR("Cosmin Tanislav <cosmin-gabriel.tanislav.xa@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/T2H / RZ/N2H ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DRIVER");

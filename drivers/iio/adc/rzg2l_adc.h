#ifndef __ADC_H__
#define __ADC_H__

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
#include <linux/sys_soc.h>

#ifdef CONFIG_RZG2L_ADC

#define RZG2L_ADC_MAX_CHANNELS		8

struct rzg2l_adc_data {
	const struct iio_chan_spec *channels;
	u8 num_channels;
};

struct rzg2l_adc {
	void __iomem *base;
	struct device *dev;
	struct clk *pclk;
	struct clk *adclk;
	struct reset_control *presetn;
	struct reset_control *adrstn;
	struct completion completion;
	const struct rzg2l_adc_data *data;
	struct mutex lock;
	u16 last_val[RZG2L_ADC_MAX_CHANNELS];
};

extern int rzg2l_adc_read_tsu(struct device *dev);

#else

#endif

#endif /* __ADC_H__ */

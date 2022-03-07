// SPDX-License-Identifier: GPL-2.0
/*
 * Pin Function controller, GPIO controller and Interrupt controller for RZV2M
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/psci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include "core.h"

struct rzv2m_pfc *pfc;
static u32 rzv2m_pfc_read_raw_reg(void __iomem *mapped_reg,
				  unsigned int reg_width)
{
	switch (reg_width) {
	case 8:
		return ioread8(mapped_reg);
	case 16:
		return ioread16(mapped_reg);
	case 32:
		return ioread32(mapped_reg);
	};
	BUG();
	return 0;
}

static void rzv2m_pfc_write_raw_reg(void __iomem *mapped_reg,
				    unsigned int reg_width,
				    u32 data)
{
	switch (reg_width) {
	case 8:
		iowrite8(data, mapped_reg);
		return;
	case 16:
		iowrite16(data, mapped_reg);
		return;
	case 32:
		iowrite32(data, mapped_reg);
		return;
	};
	BUG();
}

static void __iomem *rzv2m_pfc_check_reg(struct rzv2m_pfc *pfc, u32 offset)
{
	if (offset > pfc->size)
		return NULL;
	return pfc->base + offset;
}

static void rzv2m_pfc_write(struct rzv2m_pfc *pfc, u32 reg,
			    unsigned int reg_width, u32 data)
{
	void __iomem *mem = rzv2m_pfc_check_reg(pfc, reg);

	if (mem == NULL)
		BUG();
	else
		rzv2m_pfc_write_raw_reg(mem, reg_width, data);
}

static u32 rzv2m_pfc_read(struct rzv2m_pfc *pfc, u32 reg,
			  unsigned int reg_width)
{
	void __iomem *mem = rzv2m_pfc_check_reg(pfc, reg);

	if (mem != NULL)
		return rzv2m_pfc_read_raw_reg(mem, reg_width);
	BUG();
	return -1;
}

static int rzv2m_pfc_write_multi_bit(struct rzv2m_pfc *pfc,
			       struct rzv2m_pfc_reg_data reg,
			       unsigned int bit_start,
			       unsigned int data_width,
			       unsigned int value)
{
	u32 data = rzv2m_pfc_read(pfc, reg.addr, 32);
	// Mask 1 for filter value
	u32 mask = ~(0xffffffff << data_width);
	// Filter value and move bits data to the position of bit written
	value = (value & mask) << bit_start;
	if (reg.en_bit == BIT_WRITE_ENABLE) {
		if ((bit_start + data_width) <= 16)
			value |= (mask << (16 + bit_start));
		else
			return -EINVAL;
	}

	// Mask 0 for clear old bits data on position of bit written
	mask = ~(mask << bit_start);
	// Clear the old bits data
	data = data & mask;
	// Apply the new bits data
	data = data | value;
	rzv2m_pfc_write(pfc, reg.addr, 32, data);
	return 0;
}

static u32 rzv2m_pfc_read_multi_bit(struct rzv2m_pfc *pfc,
			     struct rzv2m_pfc_reg_data reg,
			     unsigned int bit_start,
			     unsigned int data_width)
{
	u32 data, mask;

	if (reg.en_bit == BIT_WRITE_ENABLE &&
	    (bit_start + data_width) > 16)
		return -1;

	// Read all value of register
	data = rzv2m_pfc_read(pfc, reg.addr, 32);
	// Make mask 1 for filter value
	mask = ~(0xffffffff << data_width);
	// Clear all value and keep only the number of bits we need
	data &= (mask << bit_start);
	// Move the bits data to LSB position for usage.
	data = data >> bit_start;
	return data;
}

int rzv2m_pfc_pin_from_mark(struct rzv2m_pfc *pfc, unsigned mark)
{
	const struct rzv2m_pfc_info *info = pfc->info;
	const struct rzv2m_group_desc *group;
	int i, j;

	for (i = 0; i < info->nr_groups; i++) {
		group = &info->groups[i];
		for (j = 0; j < group->nr_pins; j++) {
			if (group->pin_desc[j].mux == mark)
				return group->pin_desc[j].pin;
		}
	}
	return -1;
}

int rzv2m_pfc_get_pin_index(struct rzv2m_pfc *pfc, unsigned int _pin)
{
	unsigned int i;

	for (i = 0; i < pfc->info->nr_pins; i++) {
		if (pfc->info->pins[i].pin == _pin)
			return i;
	}
	return -1;
}

const struct rzv2m_pfc_config_reg *
rzv2m_pfc_config_reg_helper(struct rzv2m_pfc *pfc, unsigned type,
			    u16 pin, int *pos_mark)
{
	const struct rzv2m_pfc_info *info = pfc->info;
	const struct rzv2m_pfc_config_reg *info_reg;
	const struct rzv2m_pfc_config_reg *reg;
	unsigned int i, j, num_regs = 0;

	info_reg = info->config_regs;
	num_regs = info->nr_config_regs;

	for (i = 0; i < num_regs; i++) {
		reg = &info_reg[i];
		if (type == reg->type) {
			for (j = 0; j < reg->num_marks; j++) {
				if (reg->pin_or_mark[j] == pin) {
					if  (pos_mark)
						*pos_mark = j;
					return reg;
				}
			}
		}
	}
	return NULL;
}

int rzv2m_set_di_mask(struct rzv2m_pfc *pfc, unsigned int pin, u32 value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit;

	reg = rzv2m_pfc_config_reg_helper(pfc, DI_MSK, pin, &bit);
	if (!reg)
		return -EINVAL;
	bit = bit / reg->var_field;
	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, bit, reg->field_width,
					 value);
}

int rzv2m_set_en_mask(struct rzv2m_pfc *pfc, unsigned int pin, u32 value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit = -1;

	reg = rzv2m_pfc_config_reg_helper(pfc, EN_MSK, pin, &bit);
	if (!reg || bit < 0)
		return -EINVAL;

	bit = bit / reg->var_field;

	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, bit, reg->field_width,
					 value);
}

int rzv2m_pfc_config_mux(struct rzv2m_pfc *pfc, unsigned mux,
			 unsigned int _pin)
{
	const struct rzv2m_pfc_info *info = pfc->info;
	const struct rzv2m_pfc_config_reg *reg;
	unsigned int bit;
	int i, j, ret;

	ret = -EINVAL;
	for (i = 0; i < info->nr_config_regs; i++) {
		reg = &info->config_regs[i];
		if (reg->type != SEL)
			continue;
		for (j = 0; j < reg->num_marks; j++) {
			if (reg->pin_or_mark[j] == mux) {
				bit = (j / reg->var_field) * reg->field_width;
				ret = rzv2m_set_di_mask(pfc, _pin, 1);
				if (ret)
					return -EINVAL;
				ret = rzv2m_set_en_mask(pfc, _pin, 1);
				if (ret)
					goto release_di;
				ret = rzv2m_pfc_write_multi_bit(pfc, reg->reg,
							bit, reg->field_width,
							j % reg->var_field);
				/* We no need to check the result, previous
				 * setting step have checked*/
				ret = rzv2m_set_en_mask(pfc, _pin, 0);
release_di:
				ret = rzv2m_set_di_mask(pfc, _pin, 0);
				if (ret)
					goto done;
			}
		}
	}
done:
	return ret;
}

static int rzv2m_pfc_set_bias(struct rzv2m_pfc *pfc,
			      unsigned int pin, enum pin_config_param param)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit, arg;

	switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			arg = BIT(0);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			arg = BIT(1);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			arg = 0;
			break;
		default: return -EINVAL;
	};

	reg = rzv2m_pfc_config_reg_helper(pfc, PUPD, pin, &bit);
	if (!reg)
		return -EINVAL;
	bit = bit * reg->field_width;

	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, bit, reg->field_width,
					 arg);
}

static int rzv2m_pfc_get_bias(struct rzv2m_pfc *pfc, unsigned int pin,
			      unsigned int *param)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit;
	u32 arg;

	reg = rzv2m_pfc_config_reg_helper(pfc, PUPD, pin, &bit);
	if (!reg)
		return -EINVAL;

	bit = bit * reg->field_width;

	arg = rzv2m_pfc_read_multi_bit(pfc, reg->reg, bit, reg->field_width);
	if (arg < 0)
		return -EINVAL;

	if (arg & BIT(0)) {	// arg is b'x1
		*param = PIN_CONFIG_BIAS_DISABLE;
	} else {		// arg is b'x0
		if (arg & BIT(1))	// arg is b'10
			*param = PIN_CONFIG_BIAS_PULL_UP;
		else			// arg is b'00
			*param = PIN_CONFIG_BIAS_PULL_DOWN;
	}

	return 0;
}

static int rzv2m_pfc_set_str(struct rzv2m_pfc *pfc,
			     unsigned int pin, unsigned int arg)
{
	const struct rzv2m_pfc_config_reg *reg;
	unsigned int drv_support[] = {1, 2, 4, 6};
	int i, bit;
	
	for (i = 0; i < ARRAY_SIZE(drv_support); i++)
		if (arg == drv_support[i])
			break;
	if (i == ARRAY_SIZE(drv_support))
		return -EINVAL;
	arg = arg >> 1;
	reg = rzv2m_pfc_config_reg_helper(pfc, DRV_STR, pin, &bit);
	if (!reg)
		return -EINVAL;

	bit = bit * reg->field_width;
	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, bit, reg->field_width,
					 arg);
}

static int rzv2m_pfc_get_str(struct rzv2m_pfc *pfc, unsigned int pin,
			     u32 *value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit;
	u32 arg;

	reg = rzv2m_pfc_config_reg_helper(pfc, DRV_STR, pin, &bit);
	if (!reg)
		return -EINVAL;
	bit = bit * reg->field_width;

	arg = rzv2m_pfc_read_multi_bit(pfc, reg->reg, bit, reg->field_width);
	arg = arg << 1;
	if (arg == 0)
		arg = 1;
	*value = arg;
	return 0;
}

static int rzv2m_pfc_set_slew(struct rzv2m_pfc *pfc,
			      unsigned int pin, unsigned int arg)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit;

	if (arg < 0 && arg > 1)
		return -EINVAL;

	reg = rzv2m_pfc_config_reg_helper(pfc, SLEW, pin, &bit);
	if (!reg)
		return -EINVAL;
	bit = bit * reg->field_width;

	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, bit, reg->field_width,
					 arg);
}

static int rzv2m_pfc_get_slew(struct rzv2m_pfc *pfc,
			      unsigned int pin, u32 *value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit;

	reg = rzv2m_pfc_config_reg_helper(pfc, SLEW, pin, &bit);
	if (!reg)
		return -EINVAL;

	*value = rzv2m_pfc_read_multi_bit(pfc, reg->reg, bit, reg->field_width);
	return 0;
}

static int rzv2m_pfc_ops_valid(struct rzv2m_pfc *pfc,
			       unsigned int _pin, enum pin_config_param param)
{
	const struct rzv2m_pfc_info *info = pfc->info;
	int pin = rzv2m_pfc_get_pin_index(pfc, _pin);

	if (pin < 0)
		return -EINVAL;
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return info->pins[pin].configs & RZV2M_PIN_CFG_DISPULL;
	case PIN_CONFIG_BIAS_PULL_UP:
		return info->pins[pin].configs & RZV2M_PIN_CFG_PULLUP;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return info->pins[pin].configs & RZV2M_PIN_CFG_PULLDOWN;
	case PIN_CONFIG_DRIVE_STRENGTH:
		return info->pins[pin].configs & RZV2M_PIN_CFG_DRV_STR;
	case PIN_CONFIG_SLEW_RATE:
		return info->pins[pin].configs & RZV2M_PIN_CFG_SLEW;
	default:
		return -ENOTSUPP;
	};
}

static int rzv2m_pfc_get_value(struct rzv2m_pfc *pfc, unsigned int pin,
			       u32 *value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit, tmp;

	reg = rzv2m_pfc_config_reg_helper(pfc, GP_OE, pin, &bit);
	if (!reg)
		return -EINVAL;

	tmp = rzv2m_pfc_read_multi_bit(pfc, reg->reg, bit, reg->field_width);
	if (rzv2m_pfc_read_multi_bit(pfc, reg->reg, bit, reg->field_width)){
		reg = rzv2m_pfc_config_reg_helper(pfc, GP_DO, pin, &bit);
	}	
	else{
		reg = rzv2m_pfc_config_reg_helper(pfc, DI_MON, pin, &bit);
	}
	if (!reg)
		return -EINVAL;
	bit = bit * reg->field_width;

	*value = rzv2m_pfc_read_multi_bit(pfc, reg->reg, bit, reg->field_width);
	return 0;
}

static int rzv2m_pfc_gpio_set_value(struct rzv2m_pfc *pfc, unsigned int pin,
				    int value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int bit;

	reg = rzv2m_pfc_config_reg_helper(pfc, GP_DO, pin, &bit);
	if (!reg)
		return -EINVAL;
	bit = bit * reg->field_width;

	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, bit, reg->field_width,
					 value);
}

static int rzv2m_pfc_gpio_direction(struct rzv2m_pfc *pfc, unsigned int pin,
				    bool input)
{
	const struct rzv2m_pfc_config_reg *reg_in, *reg_out;
	int bit, ret = 0;
	u32 value = 0;

	reg_in = rzv2m_pfc_config_reg_helper(pfc, GP_IE, pin, &bit);
	if (!reg_in)
		return -EINVAL;
	bit = bit * reg_in->field_width;

	reg_out = rzv2m_pfc_config_reg_helper(pfc, GP_OE, pin, &bit);
	if (!reg_out)
		return -EINVAL;

	if (input)
		value = 1;

	ret = rzv2m_set_di_mask(pfc, pin, 1);
	if (ret)
		return ret;

	ret = rzv2m_set_en_mask(pfc, pin, 1);
	if (ret)
		goto release_di;

	ret = rzv2m_pfc_write_multi_bit(pfc, reg_in->reg, bit, reg_in->field_width,
				  value);
	if (ret)
		goto release_en;
	ret = rzv2m_pfc_write_multi_bit(pfc, reg_out->reg, bit, reg_out->field_width,
				  ~value);

release_en:
	rzv2m_set_en_mask(pfc, pin, 0);
release_di:
	rzv2m_set_di_mask(pfc, pin, 0);

	return ret;
}

static int rzv2m_set_interrupt_mask(struct rzv2m_pfc *pfc, unsigned id,
				    u32 value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int reg_id = id / NUM_IRQ_PER_REG;

	if (reg_id > pfc->info->nr_irq_regs / 2)
		return -1;

	reg = &pfc->info->irq_regs[reg_id + (pfc->info->nr_irq_regs / 2)];
	if (reg->type != INT_MSK) {
		dev_err(pfc->dev,
			"SoC info describe wrong for interrupt register\n");
		return -1;
	}
	/* Get bit index of IRQ id*/
	id = id % NUM_IRQ_PER_REG;
	if (id > reg->num_marks)
		return -1;

	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, id, reg->field_width,
					 value);
}

static int rzv2m_set_interrupt_invert(struct rzv2m_pfc *pfc, unsigned id,
				      u32 value)
{
	const struct rzv2m_pfc_config_reg *reg;
	int reg_id = id / NUM_IRQ_PER_REG;

	if (reg_id > pfc->info->nr_irq_regs / 2)
		return -1;

	reg = &pfc->info->irq_regs[reg_id];
	if (reg->type != INT_INV) {
		dev_err(pfc->dev,
			"SoC info describe wrong for interrupt register\n");
		return -1;
	}
	/* Get bit index of IRQ id*/
	id = id % NUM_IRQ_PER_REG;
	if (id > reg->num_marks)
		return -1;

	return rzv2m_pfc_write_multi_bit(pfc, reg->reg, id, reg->field_width,
					 value);
}

struct rzv2m_pfc_ops rzv2m_pfc_ops_map = {
	.set_bias = rzv2m_pfc_set_bias,
	.get_bias = rzv2m_pfc_get_bias,
	.set_drv_str = rzv2m_pfc_set_str,
	.get_drv_str = rzv2m_pfc_get_str,
	.set_slew = rzv2m_pfc_set_slew,
	.get_slew = rzv2m_pfc_get_slew,
	.get_value = rzv2m_pfc_get_value,
	.set_value = rzv2m_pfc_gpio_set_value,
	.gpio_direction = rzv2m_pfc_gpio_direction,
	.valid = rzv2m_pfc_ops_valid,
	.irq_msk = rzv2m_set_interrupt_mask,
	.irq_inv = rzv2m_set_interrupt_invert,
};

static const struct of_device_id rzv2m_pfc_of_table[] = {
	{
		.compatible = "renesas,pfc-r8arzv2m",
		.data = &r8arzv2m_pinmux_info,
	},
};

static int rzv2m_pfc_init_ranges(struct rzv2m_pfc *pfc)
{
	struct rzv2m_pfc_gpio_range *range;
	unsigned int nr_ranges;
	unsigned int i;

	if (pfc->info->pins[0].pin == (u16)-1) {
		/* Pin number -1 denotes that the SoC doesn't report pin numbers
		 * in its pin arrays yet. Consider the pin numbers range as
		 * continuous and allocate a single range.
		 */
		pfc->nr_ranges = 1;
		pfc->ranges = devm_kzalloc(pfc->dev, sizeof(*pfc->ranges),
		GFP_KERNEL);
		if (pfc->ranges == NULL)
			return -ENOMEM;

		pfc->ranges->start = 0;
		pfc->ranges->end = pfc->info->nr_pins - 1;
		pfc->nr_gpio_pins = pfc->info->nr_pins;

		return 0;
	}

	/* Count, allocate and fill the ranges. The PFC SoC data pins array must
	 * be sorted by pin numbers, and pins without a GPIO port must come
	 * last.
	 */
	for (i = 1, nr_ranges = 1; i < pfc->info->nr_pins; ++i) {
		if (pfc->info->pins[i-1].pin != pfc->info->pins[i].pin - 1)
			nr_ranges++;
	}

	pfc->nr_ranges = nr_ranges;
	pfc->ranges = devm_kcalloc(pfc->dev, nr_ranges, sizeof(*pfc->ranges),
				   GFP_KERNEL);
	if (pfc->ranges == NULL)
		return -ENOMEM;

	range = pfc->ranges;
	range->start = pfc->info->pins[0].pin;

	for (i = 1; i < pfc->info->nr_pins; ++i) {
		if (pfc->info->pins[i-1].pin == pfc->info->pins[i].pin - 1)
			continue;

		range->end = pfc->info->pins[i-1].pin;
		if (!(pfc->info->pins[i-1].configs & RZV2M_PIN_CFG_NO_GPIO))
			pfc->nr_gpio_pins = range->end + 1;

		range++;
		range->start = pfc->info->pins[i].pin;
	}

	range->end = pfc->info->pins[i-1].pin;
	if (!(pfc->info->pins[i-1].configs & RZV2M_PIN_CFG_NO_GPIO))
		pfc->nr_gpio_pins = range->end + 1;

	return 0;
}

static struct rzv2m_irq_priv *irq_data_to_priv(struct irq_data *data)
{
	return data->domain->host_data;
}

static void rzv2m_irq_mask_enable(struct irq_data *data)
{
	struct rzv2m_irq_priv *irq_priv = irq_data_to_priv(data);
	struct rzv2m_pfc *pfc = irq_priv->pfc;
	int hwirq = irqd_to_hwirq(data);

	pfc->ops->irq_msk(pfc, hwirq, 1);
}

static void rzv2m_irq_mask_disable(struct irq_data *data)
{
	struct rzv2m_irq_priv *irq_priv = irq_data_to_priv(data);
	struct rzv2m_pfc *pfc = irq_priv->pfc;
	int hwirq = irqd_to_hwirq(data);

	pfc->ops->irq_msk(pfc, hwirq, 0);
}

static int rzv2m_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct rzv2m_irq_priv *irq_priv = irq_data_to_priv(data);
	struct rzv2m_pfc *pfc = irq_priv->pfc;
	u32 value = flow_type & IRQ_TYPE_LEVEL_LOW ? 1 : 0;
	int hwirq = irqd_to_hwirq(data);

	return pfc->ops->irq_inv(pfc, hwirq, value);
}

static irqreturn_t rzv2m_irq_handler(int irq, void *dev_priv)
{
	struct rzv2m_irq_chip *irq_chip = dev_priv;
	struct rzv2m_irq_priv *irq_priv = irq_chip->irq_priv;
	struct rzv2m_pfc *pfc = irq_priv->pfc;
	int i, value, ret;

	for (i = 0; i < irq_chip->nr_irqs; i++) {
		if (irq_chip->hw_irq[i] != irq)
			continue;
		ret = rzv2m_pfc_get_value(pfc, irq_chip->pins[i], &value);
		if (ret)
			return IRQ_NONE;
		generic_handle_irq(irq_find_mapping(irq_priv->irq_domain,
						    irq_chip->index * NUM_IRQ_PER_REG + i));
	}
	return IRQ_HANDLED;
}

int rzv2m_pfc_register_irq(struct platform_device *pdev,
				  struct rzv2m_pfc *pfc)
{
	struct rzv2m_irq_priv *irq_priv;
	struct resource *irq;
	struct irq_chip_generic *gc;
	struct rzv2m_irq_chip *chip;
	const struct rzv2m_pfc_config_reg *irq_reg;
	unsigned int i, j, num_irqs, nr_ex_irqs;
	int ret;

	ret = 0;
	nr_ex_irqs = 0;
	for (i = 0 ; i < pfc->info->nr_irq_regs; i++)
		nr_ex_irqs += pfc->info->irq_regs[i].num_marks;
	nr_ex_irqs /= 2; /* This divide base on number of register control IRQ*/

	num_irqs = platform_irq_count(pdev);
	if (num_irqs == 0) {
		dev_err(&pdev->dev, "No interrupt is provided\n");
		return -1;
	} else if (num_irqs < nr_ex_irqs) {
		dev_err(&pdev->dev,
			"Not enough interrupt, need %u, provided: %d\n",
			nr_ex_irqs, num_irqs);
		return -1;
	}


	pfc->irq_priv = devm_kzalloc(&pdev->dev, sizeof(*pfc->irq_priv),
				     GFP_KERNEL);
	if (!pfc->irq_priv) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Can't allocate irq priv for pfc\n");
		goto err_alloc_priv;
	}

	irq_priv = pfc->irq_priv;
	irq_priv->pfc = pfc;
	irq_priv->nr_chips = nr_ex_irqs / NUM_IRQ_PER_REG + 1;
	irq_priv->irqs = devm_kcalloc(&pdev->dev, irq_priv->nr_chips,
				      sizeof(*irq_priv->irqs),
				      GFP_KERNEL | __GFP_ZERO);

	if (!irq_priv->irqs) {
		ret = -ENOMEM;
		goto err_alloc_irq_chip;
	}

	for (i = 0; i < irq_priv->nr_chips; i++) {
		chip = &irq_priv->irqs[i];
		chip->irq_priv = irq_priv;
		nr_ex_irqs = (num_irqs - (i * NUM_IRQ_PER_REG) > NUM_IRQ_PER_REG)? NUM_IRQ_PER_REG : num_irqs % NUM_IRQ_PER_REG;
		chip->index = i;
		chip->nr_irqs = nr_ex_irqs;
		chip->hw_irq = devm_kcalloc(&pdev->dev, chip->nr_irqs,
					    sizeof(*chip->hw_irq),
					    GFP_KERNEL | __GFP_ZERO);
		chip->pins = devm_kcalloc(&pdev->dev, chip->nr_irqs,
				  sizeof(*chip->pins),
				  GFP_KERNEL | __GFP_ZERO);

		if (!chip->hw_irq || !chip->pins) {
			ret = -ENOMEM;
			goto err_irq_chip_init;
		}

		irq_reg = &pfc->info->irq_regs[i];
		for (j = 0; j < chip->nr_irqs; j++) {
			int ret;
	
			irq = platform_get_resource(pdev, IORESOURCE_IRQ, i * NUM_IRQ_PER_REG + j);
			if (!irq) {
				ret = -ENXIO;
				goto err_irq_chip_init;
			}
	
			chip->hw_irq[j] = irq->start;
	
			/* Get pin number that matched with irq mark. */
			ret = rzv2m_pfc_pin_from_mark(pfc,
						      irq_reg->pin_or_mark[j]);
			if (ret < 0) {
				ret = -1;
				dev_err(&pdev->dev,
					"Wrong define in SoC describe\n");
				goto err_irq_chip_init;
			}
			chip->pins[j] = ret;
		}
	}
	/* Create IRQ domain for contain number of interrupt source base on
	 * number of external interrupt that driver handle.
	 */
	irq_priv->irq_domain = irq_domain_add_linear(pdev->dev.of_node,
						     num_irqs,
						     &irq_generic_chip_ops,
						     irq_priv);
	if (!irq_priv->irq_domain) {
		dev_err(&pdev->dev, "Can initialize irq domain\n");
		ret = -ENXIO;
		goto err_irq_chip_init;
	}

	/* Allocate the number of IRQ chip that handle the provided number of
	 * interrupt source for each chip. This function will calculate how
	 * many chip IRQ domain needed.
	 */
	ret = irq_alloc_domain_generic_chips(irq_priv->irq_domain,
					     NUM_IRQ_PER_REG, 1,
					     pdev->name, handle_level_irq,
					     0, 0, IRQ_GC_INIT_NESTED_LOCK);
	if (ret) {
		dev_err(&pdev->dev, "Can not allocate generic chip\n");
		goto err_generic_chip;
	}

	for (i = 0 ; i < irq_priv->nr_chips; i++) {
		gc = irq_get_domain_generic_chip(irq_priv->irq_domain, i*NUM_IRQ_PER_REG);
		gc->chip_types[0].chip.irq_mask = rzv2m_irq_mask_enable;
		gc->chip_types[0].chip.irq_unmask = rzv2m_irq_mask_disable;
		gc->chip_types[0].chip.irq_set_type  = rzv2m_irq_set_type;
		gc->chip_types[0].chip.flags =  IRQCHIP_SET_TYPE_MASKED |
						IRQCHIP_ONOFFLINE_ENABLED |
						IRQCHIP_SKIP_SET_WAKE;
	}

	/* Request Interrupt Handler for each interrupt with shared flags, due
	 * to many interrupt source will use the same interrupt signal.
	 */
	for (i = 0; i < irq_priv->nr_chips; i++) {
		for (j = 0; j < irq_priv->irqs[i].nr_irqs; j++) {
			if (devm_request_irq(&pdev->dev, irq_priv->irqs[i].hw_irq[j],
					       rzv2m_irq_handler, IRQF_SHARED,
					       pdev->name, &irq_priv->irqs[i])) {
				ret = -ENOENT;
				goto err_request_irq;
			}
		}
	}

	return 0;

err_request_irq:
	for (i = 0; i < irq_priv->nr_chips; i++)
		for (j = 0; j < irq_priv->irqs[i].nr_irqs; j++)
			devm_free_irq(&pdev->dev, irq_priv->irqs[i].hw_irq[j],
				      &irq_priv->irqs[i]);
err_generic_chip:
	irq_domain_remove(irq_priv->irq_domain);
err_irq_chip_init:
	for (i = 0; i < irq_priv->nr_chips; i++) {
		if (irq_priv->irqs[i].hw_irq)
			kfree(irq_priv->irqs[i].hw_irq);
		if (irq_priv->irqs[i].pins)
			kfree(irq_priv->irqs[i].pins);
	}
	kfree(irq_priv->irqs);
err_alloc_irq_chip:
	kfree(irq_priv);
err_alloc_priv:
	return ret;
}

/* Function name: rzv2m_pfc_info_remap
 * Modify data on SoC info structure for using in pinctrl framework.
 * Parse interrupt resource for gpio interrupt
 */
static void rzv2m_pfc_info_remap(struct rzv2m_pfc *pfc)
{
	const struct rzv2m_pfc_info *info = pfc->info;
	const struct rzv2m_group_desc *group;
	unsigned int i, j;

	for (i = 0; i < info->nr_groups; i++) {
		group = &info->groups[i];
		for (j = 0; j < group->nr_pins; j++) {
			*(unsigned int *)&group->pins[j] =
					 group->pin_desc[j].pin;
		}
	}
}

static int rzv2m_pfc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rzv2m_pfc_info *info;
	int ret;

	pfc = devm_kzalloc(&pdev->dev, sizeof(*pfc), GFP_KERNEL);
	if (!pfc)
		return -ENOMEM;

	pfc->dev = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pfc->phys = res->start;
	pfc->size = resource_size(res);
	pfc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pfc->base))
		return -ENOMEM;

	pfc->ops = &rzv2m_pfc_ops_map;

	info = (struct rzv2m_pfc_info *)of_device_get_match_data(&pdev->dev);
	if (info == NULL) {
		dev_err(pfc->dev, "fail to map data\n");
		return -EINVAL;
	}

	pfc->info = info;

	rzv2m_pfc_info_remap(pfc);
	ret = rzv2m_pfc_init_ranges(pfc);
	if (ret)
		return ret;
	spin_lock_init(&pfc->lock);
	ret = rzv2m_pfc_pinctrl_register(pfc);
	if (ret)
		return ret;

	ret = rzv2m_pfc_register_irq(pdev, pfc);
	if (ret)
		dev_err(pfc->dev, "Init external interrupt controller fail\n");
	else
		dev_info(pfc->dev, "Init external interrupt controller success.\n");

	platform_set_drvdata(pdev, pfc);
	dev_info(pfc->dev,
		"RZ/V pin controller successfully registered\n");
	return 0;
}

static struct platform_driver rzv2m_pfc_driver = {
	.probe		= rzv2m_pfc_probe,
	.driver		= {
		.name   = DRV_NAME,
		.of_match_table = of_match_ptr(rzv2m_pfc_of_table),
		.pm     = NULL,
	},
};

static int __init rzv2m_pfc_init(void)
{
	return platform_driver_register(&rzv2m_pfc_driver);
}
static void __exit rzv2m_pfc_cleanup(void)
{
	return platform_driver_unregister(&rzv2m_pfc_driver);
}
static int __init rzv2m_gpio_init(void)
{
	int ret;

	if (!pfc)
		return -ENXIO;
	ret = rzv2m_pfc_gpio_register(pfc);
        if (ret == 0)
                dev_info(pfc->dev, "No GPIO node controller found\n");
        else if (ret < 0)
                dev_err(pfc->dev, "Register GPIO controller fail\n");
        else {
                dev_info(pfc->dev, "%d GPIO nodes are registered\n", ret);
		ret = 0;
	}
	return ret;
}

postcore_initcall(rzv2m_pfc_init);
subsys_initcall(rzv2m_gpio_init);

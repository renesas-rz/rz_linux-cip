// SPDX-License-Identifier: GPL-2.0
/* Renesas RZ/G2L Pin controller and GPIO drivers
 *
 * Copyright (C) 2020 Renesas Electronics Corporation.
 *
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include "pinctrl-rzg2l.h"

#define DRV_NAME "rzg2l-pinctrl"

static void rzg2l_pinctrl_set_pfc_mode(struct rzg2l_pinctrl *pctrl,
				       int pins, unsigned long pfc_mode)
{
	u32 port = RZG2L_PIN_ID_TO_PORT(pins);
	u8 bit = RZG2L_PIN_ID_TO_PIN(pins);
	u32 mask32;
	u16 mask16;
	u32 reg32;
	u16 reg16;
	u8 reg8;
	unsigned long flags;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Set pin to 'Non-use (Hi-z input protection)'  */
	reg16 = readw(pctrl->base + PM(port));
	mask16 = PM_MASK << (bit * 2);
	reg16 = reg16 & ~mask16;
	writew(reg16, pctrl->base + PM(port));

	/* Temporarily switch to GPIO mode with PMC register */
	reg8 = readb(pctrl->base + PMC(port));
	writeb(reg8 & ~BIT(bit), pctrl->base + PMC(port));

	/* Set the PWPR register to allow PFC register to write */
	writel(0x00, pctrl->base + PWPR);        /* B0WI=0, PFCWE=0 */
	writel(PWPR_PFCWE, pctrl->base + PWPR);  /* B0WI=0, PFCWE=1 */

	/* Select Pin function mode with PFC register */
	reg32 = readl(pctrl->base + PFC(port));
	mask32 = PFC_MASK << (bit * 4);
	reg32 = reg32 & ~mask32;
	pfc_mode = pfc_mode << (bit * 4);
	writel(reg32 | pfc_mode, pctrl->base + PFC(port));

	/* SEt the PWPR register to be write-protected */
	writel(0x00, pctrl->base + PWPR);        /* B0WI=0, PFCWE=0 */
	writel(PWPR_B0WI, pctrl->base + PWPR);  /* B0WI=1, PFCWE=0 */

	/* Switch to Peripheral pin function with PMC register */
	reg8 = readb(pctrl->base + PMC(port));
	writeb(reg8 | BIT(bit), pctrl->base + PMC(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
};

static int rzg2l_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *group;
	int i;
	int *pins;
	unsigned long data;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;
	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group)
		return -EINVAL;

	pins = group->pins;
	data = (unsigned long) group->data;

	dev_dbg(pctldev->dev, "enable function %s group %s\n",
		func->name, group->name);

	for (i = 0; i < group->num_pins; i++) {
		rzg2l_pinctrl_set_pfc_mode(pctrl, *(pins + i), data);
	};

	return 0;
};

/* Check whether the requested parameter is supported for a pin. */
static bool rzg2l_pinctrl_validate_pinconf(u32 configs,
					   enum pin_config_param param)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return configs & PIN_CFG_PULL_UP_DOWN;
	case PIN_CONFIG_DRIVE_STRENGTH:
		return configs & PIN_CFG_DRIVE_STRENGTH;
	case PIN_CONFIG_SLEW_RATE:
		return configs & PIN_CFG_SLEW_RATE;
	case PIN_CONFIG_INPUT_ENABLE:
		return configs & PIN_CFG_INPUT_ENABLE;
	case PIN_CONFIG_POWER_SOURCE:
		return configs & PIN_CFG_IO_VOLTAGE;
	default:
		return false;
	}
};

static int rzg2l_pinctrl_pinconf_get(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *config)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	const struct pinctrl_pin_desc *pin = &pctrl->pctrl_desc.pins[_pin];
	unsigned int arg = 0;
	u64 reg;
	u32 port;
	u64 bit;
	void __iomem *addr;
	u32 configs;
	unsigned long flags;


	if (_pin < pctrl->nports * 8) {
		port = RZG2L_PIN_ID_TO_PORT(_pin);
		bit = RZG2L_PIN_ID_TO_PIN(_pin);
		configs = (uintptr_t) (pin->drv_data);
		addr = pctrl->base + 0x80;
	} else {
		struct pin_data *pin_data =
				(struct pin_data *) pin->drv_data;
		port = pin_data->port;
		bit  = pin_data->bit;
		configs = pin_data->configs;
		addr = pctrl->base;
	}

	if (!rzg2l_pinctrl_validate_pinconf(configs, param))
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN: {
		spin_lock_irqsave(&pctrl->lock, flags);

		reg = readq(addr + PUPD(port)) & (PUPD_MASK << (bit * 8));
		reg = reg >> (bit * 8);

		spin_unlock_irqrestore(&pctrl->lock, flags);

		if ((reg == 0 && param != PIN_CONFIG_BIAS_DISABLE) ||
		    (reg == 0x1 && param != PIN_CONFIG_BIAS_PULL_UP) ||
		    (reg == 0x2 && param != PIN_CONFIG_BIAS_PULL_DOWN))
			return -EINVAL;

		break;
	}

	case PIN_CONFIG_DRIVE_STRENGTH: {
		spin_lock_irqsave(&pctrl->lock, flags);

		reg = readq(addr + IOLH(port)) & (IOLH_MASK << (bit * 8));
		reg = reg >> (bit * 8);
		arg = (reg == 0) ? 2 : reg * 4;

		spin_unlock_irqrestore(&pctrl->lock, flags);
		break;
	}

	case PIN_CONFIG_SLEW_RATE: {
		spin_lock_irqsave(&pctrl->lock, flags);

		reg = readq(addr + SR(port)) & (SR_MASK << (bit * 8));
		reg = reg >> (bit * 8);
		arg = reg;

		spin_unlock_irqrestore(&pctrl->lock, flags);
		break;
	}

	case PIN_CONFIG_INPUT_ENABLE: {
		spin_lock_irqsave(&pctrl->lock, flags);

		reg = readq(addr + IEN(port)) & (IEN_MASK << (bit * 8));
		reg = reg >> (bit * 8);
		arg = reg;

		spin_unlock_irqrestore(&pctrl->lock, flags);
		break;
	}

	case PIN_CONFIG_POWER_SOURCE: {
		u32 io_reg;

		spin_lock_irqsave(&pctrl->lock, flags);

		addr = pctrl->base;

		configs = configs & PIN_CFG_IO_VOLTAGE;
		switch (configs) {
		case PIN_CFG_IO_VOLTAGE_SD0:
		case PIN_CFG_IO_VOLTAGE_SD1:
		case PIN_CFG_IO_VOLTAGE_QSPI: {
			if (configs == PIN_CFG_IO_VOLTAGE_SD0)
				io_reg = readl(addr + SD_CH(0)) & PVDD_MASK;
			else if (configs == PIN_CFG_IO_VOLTAGE_SD1)
				io_reg = readl(addr + SD_CH(1)) & PVDD_MASK;
			else
				io_reg = readl(addr + QSPI) & PVDD_MASK;

			arg = io_reg ? 1800 : 3300;

			break;
		}

		case PIN_CFG_IO_VOLTAGE_ETH0:
		case PIN_CFG_IO_VOLTAGE_ETH1: {
			if (configs == PIN_CFG_IO_VOLTAGE_ETH0)
				io_reg = readl(addr + ETH_CH(0))
					& ETH_PVDD_MASK;
			else
				io_reg = readl(addr + ETH_CH(1))
					& ETH_PVDD_MASK;
			arg = io_reg ? ((io_reg == ETH_PVDD_1800) ? 1800
								  : 2500)
								  : 3300;
			break;
		}

		default:
			break;
		}

		spin_unlock_irqrestore(&pctrl->lock, flags);
		break;
	}

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
};

static int rzg2l_pinctrl_pinconf_set(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *_configs,
				     unsigned int num_configs)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	const struct pinctrl_pin_desc *pin = &pctrl->pctrl_desc.pins[_pin];
	int i;
	u64 reg, mask;
	u32 port;
	u64 bit;
	void __iomem *addr;
	u32 configs;
	unsigned long flags;

	if (_pin < pctrl->nports * 8) {
		port = RZG2L_PIN_ID_TO_PORT(_pin);
		bit = RZG2L_PIN_ID_TO_PIN(_pin);
		configs = (uintptr_t) (pin->drv_data);
		addr = pctrl->base + 0x80;
	} else {
		struct pin_data *pin_data = (struct pin_data *) pin->drv_data;

		port = pin_data->port;
		bit  = pin_data->bit;
		configs = pin_data->configs;
		addr = pctrl->base;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(_configs[i]);
		if (!rzg2l_pinctrl_validate_pinconf(configs, param))
			return -ENOTSUPP;

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN: {
			spin_lock_irqsave(&pctrl->lock, flags);

			reg = readq(addr + PUPD(port));
			mask = PUPD_MASK << (bit * 8);
			reg = reg & ~mask;
			if (param == PIN_CONFIG_BIAS_DISABLE)
				writeq(reg | (0x0 << (bit * 8)),
					addr + PUPD(port));
			else if (param == PIN_CONFIG_BIAS_PULL_UP)
				writeq(reg | (0x1 << (bit * 8)),
					addr + PUPD(port));
			else
				writeq(reg | (0x2 << (bit * 8)),
					addr + PUPD(port));

			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_DRIVE_STRENGTH: {
			unsigned int arg =
					pinconf_to_config_argument(_configs[i]);
			unsigned int mA[4] = {2, 4, 8, 12};
			u64 val;

			if ((arg != mA[0]) && (arg != mA[1]) && (arg != mA[2])
				&& (arg != mA[3]))
				return -EINVAL;

			spin_lock_irqsave(&pctrl->lock, flags);

			reg = readq(addr + IOLH(port));
			mask = IOLH_MASK << (bit * 8);
			reg = reg & ~mask;
			val = arg == 2 ? 0 : arg/4;
			writeq(reg | (val << (bit * 8)), addr + IOLH(port));

			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_SLEW_RATE: {
			unsigned int arg =
					pinconf_to_config_argument(_configs[i]);
			if (arg > 1)
				return -EINVAL;

			spin_lock_irqsave(&pctrl->lock, flags);

			reg = readq(addr + SR(port));
			mask = SR_MASK << (bit * 8);
			reg = reg & ~mask;

			writeq(reg | (arg << (bit * 8)), addr + SR(port));

			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_INPUT_ENABLE: {
			unsigned int arg =
					pinconf_to_config_argument(_configs[i]);

			spin_lock_irqsave(&pctrl->lock, flags);

			reg = readq(addr + IEN(port));
			mask = IEN_MASK << (bit * 8);
			reg = reg & ~mask;
			writeq(reg | (arg << (bit * 8)), addr + IEN(port));

			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_POWER_SOURCE: {
			u32 io_reg;
			unsigned int mV =
					pinconf_to_config_argument(_configs[i]);

			addr = pctrl->base;

			configs = configs & PIN_CFG_IO_VOLTAGE;

			if (mV != 1800 && mV != 3300) {
				if ((configs != PIN_CFG_IO_VOLTAGE_ETH0) &&
				    (configs != PIN_CFG_IO_VOLTAGE_ETH1))
					return -EINVAL;
				else if (mV != 2500)
					return -EINVAL;
			}

			spin_lock_irqsave(&pctrl->lock, flags);

			switch (configs) {
			case PIN_CFG_IO_VOLTAGE_SD0:
			case PIN_CFG_IO_VOLTAGE_SD1:
			case PIN_CFG_IO_VOLTAGE_QSPI: {
				io_reg = (mV == 1800) ? PVDD_1800 : PVDD_3300;
				if (configs == PIN_CFG_IO_VOLTAGE_SD0)
					writel(io_reg, addr + SD_CH(0));
				else if (configs == PIN_CFG_IO_VOLTAGE_SD1)
					writel(io_reg, addr + SD_CH(1));
				else
					writel(io_reg, addr + QSPI);
				break;
			}

			case PIN_CFG_IO_VOLTAGE_ETH0:
			case PIN_CFG_IO_VOLTAGE_ETH1: {
				io_reg = (mV == 3300) ? ETH_PVDD_3300 :
					((mV == 2500) ? ETH_PVDD_2500 :
							ETH_PVDD_1800);
				if (configs == PIN_CFG_IO_VOLTAGE_ETH0)
					writel(io_reg, addr + ETH_CH(0));
				else
					writel(io_reg, addr + ETH_CH(1));

				break;
			}

			default:
				break;
			}

			spin_unlock_irqrestore(&pctrl->lock, flags);

			break;
		}
		default:
			return -ENOTSUPP;
		}
	}

	return 0;
};

static int rzg2l_pinctrl_pinconf_group_set(struct pinctrl_dev *pctldev,
					  unsigned int group,
					  unsigned long *configs,
					  unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rzg2l_pinctrl_pinconf_set(pctldev, pins[i], configs,
						num_configs);
		if (ret)
			return ret;
	}

	return 0;
};

static int rzg2l_pinctrl_pinconf_group_get(struct pinctrl_dev *pctldev,
					   unsigned int group,
					   unsigned long *config)
{
	const unsigned int *pins;
	unsigned int i, npins, prev_config = 0;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rzg2l_pinctrl_pinconf_get(pctldev, pins[i], config);
		if (ret)
			return ret;


		/* Check config matching between to pin  */
		if (i && prev_config != *config)
			return -ENOTSUPP;

		prev_config = *config;
	}

	return 0;
};

/* -----------------------------------------------------------------------------
 * Switch between MII / RGMII of ETH
 * Ethernet driver API
 */

int rzg2l_pinctrl_eth_mode_set(struct device *dev,
			       phy_interface_t interface,
			       unsigned int eth_channel)
{
	struct rzg2l_pinctrl *pctrl = dev_get_drvdata(dev);
	u32 reg32;
	unsigned long flags;

	if (eth_channel < 2)
		reg32 = readl(pctrl->base + ETH_MODE_CTRL) & ~BIT(eth_channel);
	else
		return -EINVAL;

	switch (interface) {
	case PHY_INTERFACE_MODE_MII: {
		spin_lock_irqsave(&pctrl->lock, flags);
		writel(reg32 | ETH_MII_CH(eth_channel),
						pctrl->base + ETH_MODE_CTRL);
		spin_unlock_irqrestore(&pctrl->lock, flags);
		break;
	}
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID: {
		spin_lock_irqsave(&pctrl->lock, flags);
		writel(reg32 | ETH_RGMII_CH(eth_channel),
						pctrl->base + ETH_MODE_CTRL);
		spin_unlock_irqrestore(&pctrl->lock, flags);
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
};

static const struct pinctrl_ops rzg2l_pinctrl_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const struct pinmux_ops rzg2l_pinctrl_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rzg2l_pinctrl_set_mux,
	.strict = true,
};

static const struct pinconf_ops rzg2l_pinctrl_confops = {
	.is_generic = true,
	.pin_config_get = rzg2l_pinctrl_pinconf_get,
	.pin_config_set = rzg2l_pinctrl_pinconf_set,
	.pin_config_group_set = rzg2l_pinctrl_pinconf_group_set,
	.pin_config_group_get = rzg2l_pinctrl_pinconf_group_get,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static int rzg2l_pinctrl_add_groups(struct rzg2l_pinctrl *pctrl)
{
	int ret, i;

	for (i = 0; i < pctrl->psoc->ngroups; i++) {
		const struct group_desc *group = pctrl->psoc->groups + i;

		ret = pinctrl_generic_add_group(pctrl->pctrl_dev, group->name,
						group->pins, group->num_pins,
						group->data);
		if (ret < 0) {
			dev_err(pctrl->dev, "Failed to register group %s\n",
				group->name);
			return ret;
		}
	}

	return 0;
}

static int rzg2l_pinctrl_add_functions(struct rzg2l_pinctrl *pctrl)
{
	int ret, i;

	for (i = 0; i < pctrl->psoc->nfuncs; i++) {
		const struct function_desc *func = pctrl->psoc->funcs + i;

		ret = pinmux_generic_add_function(pctrl->pctrl_dev, func->name,
						   func->group_names,
						   func->num_group_names,
						   func->data);
		if (ret < 0) {
			dev_err(pctrl->dev, "Failed to register function %s\n",
				func->name);
			return ret;
		}
	}

	return 0;
}

static int rzg2l_gpio_irq_validate_id(struct rzg2l_pinctrl *pctrl, u32 port,
				      u32 bit)
{
	const struct rzg2l_pin_info *pin_info = pctrl->psoc->pin_info;
	int i;

	for (i = 0; i <= pctrl->psoc->ngpioints; i++) {
		if (port == pin_info->port && bit == pin_info->bit)
			return pin_info->gpio_irq_id;

		pin_info++;
	}

	return i;
}

static int rzg2l_gpio_irq_request_tint_slot(struct rzg2l_pinctrl *pctrl)
{
	int i;

	for (i = 0; i <= pctrl->psoc->nirqs; i++) {
		if (pctrl->tint[i] == 0)
			break;
	}

	return i;
}

static int rzg2l_gpio_irq_check_tint_slot(struct rzg2l_pinctrl *pctrl,
					  u32 gpio_id)
{
	int i;

	for (i = 0; i <= pctrl->psoc->nirqs; i++) {
		if (pctrl->tint[i] == (BIT(16) | gpio_id))
			break;
	}

	return i;
}

static void rzg2l_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	int hw_irq = irqd_to_hwirq(d);
	u32 port = RZG2L_PIN_ID_TO_PORT(hw_irq);
	u8 bit = RZG2L_PIN_ID_TO_PIN(hw_irq);
	u32 gpioint;
	u32 tint_slot;
	unsigned long flags;
	u64 reg64;
	u32 reg32;

	gpioint = rzg2l_gpio_irq_validate_id(pctrl, port, bit);
	if (gpioint == pctrl->psoc->ngpioints)
		return;

	tint_slot = rzg2l_gpio_irq_check_tint_slot(pctrl, gpioint);
	if (tint_slot ==  pctrl->psoc->nirqs)
		return;


	spin_lock_irqsave(&pctrl->lock, flags);

	reg64 = readq(pctrl->base + ISEL(port));
	reg64 &= ~BIT(bit * 8);
	writeq(reg64, pctrl->base + ISEL(port));

	reg32 = readl(pctrl->base_tint + TSSR(tint_slot / 4));
	reg32 &= ~(GENMASK(7, 0) << (tint_slot % 4));
	writel(reg32, pctrl->base_tint + TSSR(tint_slot / 4));

	spin_unlock_irqrestore(&pctrl->lock, flags);

	pctrl->tint[tint_slot] = 0;
}

static void rzg2l_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	int hw_irq = irqd_to_hwirq(d);
	u32 port = RZG2L_PIN_ID_TO_PORT(hw_irq);
	u8 bit = RZG2L_PIN_ID_TO_PIN(hw_irq);
	u32 gpioint;
	u32 tint_slot;
	unsigned long flags;
	u64 reg64;
	u32 reg32;

	gpioint = rzg2l_gpio_irq_validate_id(pctrl, port, bit);
	if (gpioint == pctrl->psoc->ngpioints)
		return;

	tint_slot = rzg2l_gpio_irq_check_tint_slot(pctrl, hw_irq);
	if (tint_slot ==  pctrl->psoc->nirqs)
		return;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg64 = readq(pctrl->base + ISEL(port));
	reg64 |= BIT(bit * 8);
	writeq(reg64, pctrl->base + ISEL(port));

	reg32 = readl(pctrl->base_tint + TSSR(tint_slot / 4));
	reg32 |= (BIT(7) | gpioint) << (8 * (tint_slot % 4));
	writel(reg32, pctrl->base_tint + TSSR(tint_slot / 4));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	int hw_irq = irqd_to_hwirq(d);
	u32 port = RZG2L_PIN_ID_TO_PORT(hw_irq);
	u8 bit = RZG2L_PIN_ID_TO_PIN(hw_irq);
	u32 gpioint;
	u32 tint_slot;
	unsigned long flags;
	u32 irq_type;
	u32 reg32;
	u8 reg8;

	gpioint = rzg2l_gpio_irq_validate_id(pctrl, port, bit);
	if (gpioint == pctrl->psoc->ngpioints)
		return -EINVAL;


	tint_slot = rzg2l_gpio_irq_request_tint_slot(pctrl);
	if (tint_slot ==  pctrl->psoc->nirqs)
		return -EINVAL;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	/*
	 * Currently we just support interrupt edge type.
	 * About level type, we do not support because we can not clear
	 * after triggering.
	 */
	case IRQ_TYPE_EDGE_RISING:
		irq_type = RISING_EDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type = FALLING_EDGE;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Select GPIO mode in PMC Register before enabling interrupt mode */
	reg8 = readb(pctrl->base + PMC(port));
	reg8 &= ~BIT(bit);
	writeb(reg8, pctrl->base + PMC(port));

	pctrl->tint[tint_slot] = BIT(16) | hw_irq;

	if (tint_slot > 15) {
		reg32 = readl(pctrl->base_tint + TITSR1);
		reg32 &= ~(IRQ_MASK << (tint_slot * 2));
		reg32 |= irq_type << (tint_slot * 2);
		writel(reg32, pctrl->base_tint + TITSR1);
	} else {
		reg32 = readl(pctrl->base_tint + TITSR0);
		reg32 &= ~(IRQ_MASK << ((tint_slot - 16) * 2));
		reg32 |= irq_type << ((tint_slot - 16) * 2);
		writel(reg32, pctrl->base_tint + TITSR0);
	}

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static irqreturn_t rzg2l_pinctrl_irq_handler(int irq, void *dev_id)
{
	struct rzg2l_pinctrl *pctrl = dev_id;
	unsigned int offset = irq - pctrl->irq_start;
	u32 reg32;

	reg32 = readl(pctrl->base_tint + TSCR);
	writel(reg32 & ~BIT(offset), pctrl->base_tint + TSCR);

	generic_handle_irq(irq_find_mapping(pctrl->gpio_chip.irq.domain,
					    pctrl->tint[offset] & ~BIT(16)));

	return IRQ_HANDLED;
}

static int rzg2l_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	u8 reg8;
	unsigned long flags;
	int ret;

	ret = pinctrl_gpio_request(chip->base + offset);
	if (ret)
		return ret;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Select GPIO mode in PMC Register */
	reg8 = readb(pctrl->base + PMC(port));
	reg8 &= ~BIT(bit);
	writeb(reg8, pctrl->base + PMC(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void rzg2l_gpio_set_direction(struct rzg2l_pinctrl *pctrl, u32 port,
				     u8 bit, bool output)
{
	u16 reg16;
	unsigned long flags;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg16 = readw(pctrl->base + PM(port));
	reg16 = reg16 & ~(PM_MASK << (bit * 2));

	if (output)
		writew(reg16 | (PM_OUTPUT << (bit * 2)),
		       pctrl->base + PM(port));
	else
		writew(reg16 | (PM_INPUT << (bit * 2)),
		       pctrl->base + PM(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);

	if (!(readb(pctrl->base + PMC(port)) & BIT(bit))) {
		u16 reg16;

		reg16 = readw(pctrl->base + PM(port));
		reg16 = (reg16 >> (bit * 2)) & PM_MASK;
		if (reg16 == PM_OUTPUT)
			return GPIOF_OUTPUT;
		else if (reg16 == PM_INPUT)
			return GPIOF_INPUT;
		else if (reg16 == PM_OUTPUT_INPUT)
			return GPIOF_BIDIRECTION;
		else
			return GPIOF_HI_Z;
	} else {
		return -EINVAL;
	}
}

static int rzg2l_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);

	rzg2l_gpio_set_direction(pctrl, port, bit, false);

	return 0;
}

static void rzg2l_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	u8 reg8;
	unsigned long flags;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg8 = readb(pctrl->base + P(port));

	if (value)
		writeb(reg8 | BIT(bit), pctrl->base + P(port));
	else
		writeb(reg8 & ~BIT(bit), pctrl->base + P(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);


	rzg2l_gpio_set_direction(pctrl, port, bit, true);
	rzg2l_gpio_set(chip, offset, value);

	return 0;
}

static int rzg2l_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	u16 reg16;

	reg16 = readw(pctrl->base + PM(port));
	reg16 = (reg16 >> (bit * 2)) & PM_MASK;

	if (reg16 == PM_INPUT || reg16 == PM_OUTPUT_INPUT)
		return !!(readb(pctrl->base + PIN(port)) & BIT(bit));
	else if (reg16 == PM_OUTPUT)
		return !!(readb(pctrl->base + P(port)) & BIT(bit));
	else
		return -EINVAL;
}

static void rzg2l_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip->base + offset);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	rzg2l_gpio_direction_input(chip, offset);
}

static int rzg2l_pinctrl_add_gpiochip(struct rzg2l_pinctrl *pctrl)
{
	struct gpio_chip *chip = &pctrl->gpio_chip;
	struct irq_chip *irq_chip = &pctrl->irq_chip;
	struct device_node *np = pctrl->dev->of_node;
	struct of_phandle_args args;
	int ret;
	unsigned int npins;
	const char *name = dev_name(pctrl->dev);

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &args);
	if (ret) {
		dev_err(pctrl->dev, "Unable to parse gpio-ranges\n");
		return ret;
	}

	npins = args.args[2];

	chip->label = name;
	chip->parent = pctrl->dev;
	chip->base = -1;
	chip->ngpio = npins;
	chip->request = rzg2l_gpio_request;
	chip->get_direction = rzg2l_gpio_get_direction;
	chip->direction_input = rzg2l_gpio_direction_input;
	chip->get = rzg2l_gpio_get;
	chip->direction_output = rzg2l_gpio_direction_output;
	chip->set = rzg2l_gpio_set;
	chip->free = rzg2l_gpio_free;
	chip->owner = THIS_MODULE;

	irq_chip->name = name;
	irq_chip->irq_disable = rzg2l_gpio_irq_disable;
	irq_chip->irq_enable = rzg2l_gpio_irq_enable;
	irq_chip->irq_set_type = rzg2l_gpio_irq_set_type;
	irq_chip->flags = IRQCHIP_SET_TYPE_MASKED;

	ret = devm_gpiochip_add_data(pctrl->dev, chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO controller\n");
		return ret;
	}

	ret = gpiochip_irqchip_add(chip, irq_chip, 0, handle_level_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(pctrl->dev, "cannot add irqchip\n");
		return ret;
	}

	return 0;
}

static int rzg2l_pinctrl_probe(struct platform_device *pdev)
{
	struct resource *res, *irq;
	struct rzg2l_pinctrl *pctrl;
	const struct rzg2l_pin_soc *psoc;
	int i;
	int ret;

	psoc = of_device_get_match_data(&pdev->dev);

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (pctrl == NULL)
		return -ENOMEM;

	pctrl->psoc = psoc;
	pctrl->nports = psoc->nports;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing IO resource\n");
		return -ENXIO;
	}

	pctrl->dev = &pdev->dev;
	pctrl->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "missing IO resource\n");
		return -ENXIO;
	}

	pctrl->base_tint = ioremap_nocache(res->start, resource_size(res));
	if (IS_ERR(pctrl->base_tint))
		return PTR_ERR(pctrl->base_tint);

	spin_lock_init(&pctrl->lock);

	pctrl->pctrl_desc.name = DRV_NAME;
	pctrl->pctrl_desc.pins = pctrl->psoc->pins;
	pctrl->pctrl_desc.npins = pctrl->psoc->npins;
	pctrl->pctrl_desc.pctlops = &rzg2l_pinctrl_pctlops;
	pctrl->pctrl_desc.pmxops = &rzg2l_pinctrl_pmxops;
	pctrl->pctrl_desc.confops = &rzg2l_pinctrl_confops;
	pctrl->pctrl_desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(pctrl->dev, &pctrl->pctrl_desc,
					     pctrl, &pctrl->pctrl_dev);
	if (ret) {
		dev_err(pctrl->dev, "could not register: %i\n", ret);
		return ret;
	};

	ret = rzg2l_pinctrl_add_groups(pctrl);
	if (ret)
		return ret;

	ret = rzg2l_pinctrl_add_functions(pctrl);
	if (ret)
		return ret;

	ret = pinctrl_enable(pctrl->pctrl_dev);
	if (ret)
		return ret;

	ret = rzg2l_pinctrl_add_gpiochip(pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO chip: %i\n", ret);
		return ret;
	};

	pctrl->clk = devm_clk_get(pctrl->dev, NULL);
	if (IS_ERR(pctrl->clk)) {
		ret = PTR_ERR(pctrl->clk);
		dev_err(pctrl->dev, "failed to get GPIO clk : %i\n", ret);
		return ret;
	};

	ret = clk_prepare_enable(pctrl->clk);
	if (ret) {
		dev_err(pctrl->dev, "failed to enable GPIO clk: %i\n", ret);
		return ret;
	};

	for (i = 0; i < psoc->nirqs; i++) {
		char *irqstr[psoc->nirqs];

		irq = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!irq) {
			dev_err(pctrl->dev, "missing IRQ\n");
			return -EINVAL;
		};

		if (i == 0)
			pctrl->irq_start = irq->start;

		irqstr[i] = kasprintf(GFP_KERNEL, "tint%d", i);

		if (devm_request_irq(pctrl->dev, irq->start,
				     rzg2l_pinctrl_irq_handler, IRQF_SHARED,
				     irqstr[i], pctrl)) {
			dev_err(pctrl->dev, "failed to request IRQ\n");
			return -ENOENT;
		}
	}

	platform_set_drvdata(pdev, pctrl);

	dev_info(pctrl->dev, "%s support registered\n", DRV_NAME);
	return 0;
}

static int rzg2l_pinctrl_remove(struct platform_device *pdev)
{
	struct rzg2l_pinctrl *pctrl = platform_get_drvdata(pdev);

	gpiochip_remove(&pctrl->gpio_chip);

	iounmap(pctrl->base_tint);

	return 0;
}

static const struct of_device_id rzg2l_pinctrl_of_table[] = {
#ifdef CONFIG_PINCTRL_R9A07G044L
	{
		.compatible = "renesas,r9a07g044l-pinctrl",
		.data = &r9a07g044l_pinctrl_data,
	},
#endif
	{},
};

static struct platform_driver rzg2l_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rzg2l_pinctrl_of_table),
	},
	.probe = rzg2l_pinctrl_probe,
	.remove = rzg2l_pinctrl_remove,
};

static int __init rzg2l_pinctrl_init(void)
{
	return platform_driver_register(&rzg2l_pinctrl_driver);
}
subsys_initcall_sync(rzg2l_pinctrl_init);

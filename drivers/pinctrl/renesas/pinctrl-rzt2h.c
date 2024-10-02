// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/T2H Pin Control and GPIO driver core
 *
 * Copyright (C) 2021 Renesas Electronics Corporation.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

#include <dt-bindings/pinctrl/rzt2h-pinctrl.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define DRV_NAME	"pinctrl-rzt2h"

/*
 * Use 16 lower bits [15:0] for pin identifier
 * Use 16 higher bits [31:16] for pin mux function
 */
#define MUX_PIN_ID_MASK		GENMASK(15, 0)
#define MUX_FUNC_MASK		GENMASK(31, 16)
#define MUX_FUNC_OFFS		16
#define MUX_FUNC(pinconf)	(((pinconf) & MUX_FUNC_MASK) >> MUX_FUNC_OFFS)

/*
 * n indicates number of pins in the port, a is the register index
 * and f is pin configuration capabilities supported.
 */
#define RZT2H_GPIO_PORT_PACK(n, a, f)	(((n) << 28) | ((a) << 20) | (f))
#define RZT2H_GPIO_PORT_GET_PINCNT(x)	(((x) & GENMASK(30, 28)) >> 28)
#define RZT2H_GPIO_PORT_GET_INDEX(x)	(((x) & GENMASK(26, 20)) >> 20)
#define RZT2H_GPIO_PORT_GET_CFGS(x)	((x) & GENMASK(19, 0))

/*
 * BIT(31) indicates dedicated pin, p is the register index while
 * referencing to SR/IEN/IOLH/FILxx registers, b is the register bits
 * (b * 8) and f is the pin configuration capabilities supported.
 */
#define RZT2H_SINGLE_PIN		BIT(31)
#define RZT2H_SINGLE_PIN_PACK(p, b, f)	(RZT2H_SINGLE_PIN | \
					 ((p) << 24) | ((b) << 20) | (f))
#define RZT2H_SINGLE_PIN_GET_PORT_OFFSET(x)	(((x) & GENMASK(30, 24)) >> 24)
#define RZT2H_SINGLE_PIN_GET_BIT(x)	(((x) & GENMASK(22, 20)) >> 20)
#define RZT2H_SINGLE_PIN_GET_CFGS(x)	((x) & GENMASK(19, 0))

#define P(n)			(0x001 * (n))
#define PM(n)			(0x200 + 0x002 * (n))
#define PMC(n)			(0x400 + 0x001 * (n))
#define PFC(n)			(0x600 + 0x008 * (n))
#define PIN(n)			(0x800 + 0x001 * (n))
#define DRCTL(n)		(0xA00 + 0x008 * (n))

#define RSELPSR			0x1F04

#define PM_MASK			0x03
#define PFC_MASK		0x3FULL
#define PM_INPUT		0x1
#define PM_OUTPUT		0x2
#define SR_MASK			0x01
#define SCHMITT_MASK		0x01
#define IOLH_MASK		0x03
#define PUPD_MASK		0x03

#define RZT2H_PIN_ID_TO_PORT(id)	((id) / RZT2H_PINS_PER_PORT)
#define RZT2H_PIN_ID_TO_PORT_OFFSET(id)	(RZT2H_PIN_ID_TO_PORT(id) + 0x10)
#define RZT2H_PIN_ID_TO_PIN(id)		((id) % RZT2H_PINS_PER_PORT)

struct rzt2h_dedicated_configs {
	const char *name;
	u32 config;
};

struct rzt2h_pinctrl_data {
	const char * const *port_pins;
	unsigned int n_port_pins;
};

struct rzt2h_pinctrl {
	struct pinctrl_dev		*pctl;
	struct pinctrl_desc		desc;
	struct pinctrl_pin_desc		*pins;

	const struct rzt2h_pinctrl_data	*data;
	void __iomem			*base, *base1;
	struct device			*dev;
	struct clk			*clk;
	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	gpio_range;

	int safety_region;
	spinlock_t			lock;
	struct mutex                    mutex;
};

static void rzt2h_pinctrl_writeb(struct rzt2h_pinctrl *pctrl, u8 port, u8 val, u16 offset)
{
	if (port > 12)
		writeb(val, pctrl->base + offset);
	else
		writeb(val, pctrl->base1 + offset);

};

static void rzt2h_pinctrl_writew(struct rzt2h_pinctrl *pctrl, u8 port, u16 val, u16 offset)
{
	if (port > 12)
		writew(val, pctrl->base + offset);
	else
		writew(val, pctrl->base1 + offset);
};

static void rzt2h_pinctrl_writel(struct rzt2h_pinctrl *pctrl, u8 port, u32 val, u16 offset)
{
	if (port > 12)
		writel(val, pctrl->base + offset);
	else
		writel(val, pctrl->base1 + offset);
};

static void rzt2h_pinctrl_writeq(struct rzt2h_pinctrl *pctrl, u8 port, u64 val, u16 offset)
{
	if (port > 12)
		writeq(val, pctrl->base + offset);
	else
		writeq(val, pctrl->base1 + offset);

};

static u8 rzt2h_pinctrl_readb(struct rzt2h_pinctrl *pctrl, u8 port, u16 offset)
{
	if (port < 13)
		return readb(pctrl->base1 + offset);
	else
		return readb(pctrl->base + offset);
};

static u16 rzt2h_pinctrl_readw(struct rzt2h_pinctrl *pctrl, u8 port, u16 offset)
{
	if (port < 13)
		return readw(pctrl->base1 + offset);
	else
		return readw(pctrl->base + offset);
};

static u32 rzt2h_pinctrl_readl(struct rzt2h_pinctrl *pctrl, u8 port, u16 offset)
{
	if (port < 13)
		return readl(pctrl->base1 + offset);
	else
		return readl(pctrl->base + offset);
};

static u64 rzt2h_pinctrl_readq(struct rzt2h_pinctrl *pctrl, u8 port, u16 offset)
{
	if (port < 13)
		return readq(pctrl->base1 + offset);
	else
		return readq(pctrl->base + offset);
};

static void rzt2h_pinctrl_set_pfc_mode(struct rzt2h_pinctrl *pctrl,
				       u8 port, u8 pin, u64 func)
{
	unsigned long flags;
	u32 reg;
	u64 reg_pfc;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Set pin to 'Non-use (Hi-Z input protection)'  */
	reg = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg &= ~(PM_MASK << (pin * 2));
	rzt2h_pinctrl_writew(pctrl, port, reg, PM(port));

	/* Temporarily switch to GPIO mode with PMC register */
	reg = rzt2h_pinctrl_readb(pctrl, port, PMC(port));
	rzt2h_pinctrl_writeb(pctrl, port, reg & ~BIT(pin), PMC(port));

	/* Select Pin function mode with PFC register */
	reg_pfc = rzt2h_pinctrl_readq(pctrl, port, PFC(port));
	reg_pfc &= ~(PFC_MASK << (pin * 8));
	rzt2h_pinctrl_writeq(pctrl, port, reg_pfc | (func << (pin * 8)), PFC(port));

	/* Switch to Peripheral pin function with PMC register */
	reg = rzt2h_pinctrl_readb(pctrl, port, PMC(port));
	rzt2h_pinctrl_writeb(pctrl, port, reg | BIT(pin), PMC(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
};

static int rzt2h_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	unsigned int i, *psel_val;
	struct group_desc *group;
	const unsigned int *pins;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;
	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group)
		return -EINVAL;

	psel_val = func->data;
	pins = group->grp.pins;

	for (i = 0; i < group->grp.npins; i++) {
		dev_dbg(pctrl->dev, "port:%u pin: %u PSEL:%u\n",
			RZT2H_PIN_ID_TO_PORT(pins[i]), RZT2H_PIN_ID_TO_PIN(pins[i]),
			psel_val[i]);
		rzt2h_pinctrl_set_pfc_mode(pctrl, RZT2H_PIN_ID_TO_PORT(pins[i]),
					   RZT2H_PIN_ID_TO_PIN(pins[i]), psel_val[i]);
	}

	return 0;
};

static int rzt2h_map_add_config(struct pinctrl_map *map,
				const char *group_or_pin,
				enum pinctrl_map_type type,
				unsigned long *configs,
				unsigned int num_configs)
{
	unsigned long *cfgs;

	cfgs = kmemdup(configs, num_configs * sizeof(*cfgs),
		       GFP_KERNEL);
	if (!cfgs)
		return -ENOMEM;

	map->type = type;
	map->data.configs.group_or_pin = group_or_pin;
	map->data.configs.configs = cfgs;
	map->data.configs.num_configs = num_configs;

	return 0;
}

static int rzt2h_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **map,
				   unsigned int *num_maps,
				   unsigned int *index)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_map *maps = *map;
	unsigned int nmaps = *num_maps;
	unsigned long *configs = NULL;
	unsigned int *pins, *psel_val;
	unsigned int num_pinmux = 0;
	unsigned int idx = *index;
	unsigned int num_pins, i;
	unsigned int num_configs;
	struct property *pinmux;
	struct property *prop;
	int ret, gsel, fsel;
	const char **pin_fn;
	const char *pin;

	pinmux = of_find_property(np, "pinmux", NULL);
	if (pinmux)
		num_pinmux = pinmux->length / sizeof(u32);

	ret = of_property_count_strings(np, "pins");
	if (ret == -EINVAL) {
		num_pins = 0;
	} else if (ret < 0) {
		dev_err(pctrl->dev, "Invalid pins list in DT\n");
		return ret;
	} else {
		num_pins = ret;
	}

	if (!num_pinmux && !num_pins)
		return 0;

	if (num_pinmux && num_pins) {
		dev_err(pctrl->dev,
			"DT node must contain either a pinmux or pins and not both\n");
		return -EINVAL;
	}

	ret = pinconf_generic_parse_dt_config(np, NULL, &configs, &num_configs);
	if (ret < 0)
		return ret;

	if (num_pins && !num_configs) {
		dev_err(pctrl->dev, "DT node must contain a config\n");
		ret = -ENODEV;
		goto done;
	}

	if (num_pinmux)
		nmaps += 1;

	if (num_pins)
		nmaps += num_pins;

	if (configs && num_pinmux)
		nmaps += 1;

	maps = krealloc_array(maps, nmaps, sizeof(*maps), GFP_KERNEL);
	if (!maps) {
		ret = -ENOMEM;
		goto done;
	}

	*map = maps;
	*num_maps = nmaps;
	if (num_pins) {
		of_property_for_each_string(np, "pins", prop, pin) {
			ret = rzt2h_map_add_config(&maps[idx], pin,
						   PIN_MAP_TYPE_CONFIGS_PIN,
						   configs, num_configs);
			if (ret < 0)
				goto done;

			idx++;
		}
		ret = 0;
		goto done;
	}

	pins = devm_kcalloc(pctrl->dev, num_pinmux, sizeof(*pins), GFP_KERNEL);
	psel_val = devm_kcalloc(pctrl->dev, num_pinmux, sizeof(*psel_val),
				GFP_KERNEL);
	pin_fn = devm_kzalloc(pctrl->dev, sizeof(*pin_fn), GFP_KERNEL);
	if (!pins || !psel_val || !pin_fn) {
		ret = -ENOMEM;
		goto done;
	}

	/* Collect pin locations and mux settings from DT properties */
	for (i = 0; i < num_pinmux; ++i) {
		u32 value;

		ret = of_property_read_u32_index(np, "pinmux", i, &value);
		if (ret)
			goto done;
		pins[i] = value & MUX_PIN_ID_MASK;
		psel_val[i] = MUX_FUNC(value);
	}

	mutex_lock(&pctrl->mutex);

	/* Register a single pin group listing all the pins we read from DT */
	gsel = pinctrl_generic_add_group(pctldev, np->name, pins, num_pinmux, NULL);
	if (gsel < 0) {
		ret = gsel;
		goto unlock;
	}

	/*
	 * Register a single group function where the 'data' is an array PSEL
	 * register values read from DT.
	 */
	pin_fn[0] = np->name;
	fsel = pinmux_generic_add_function(pctldev, np->name, pin_fn, 1,
					   psel_val);
	if (fsel < 0) {
		ret = fsel;
		goto remove_group;
	}

	mutex_unlock(&pctrl->mutex);

	maps[idx].type = PIN_MAP_TYPE_MUX_GROUP;
	maps[idx].data.mux.group = np->name;
	maps[idx].data.mux.function = np->name;
	idx++;

	if (num_configs) {
		ret = rzt2h_map_add_config(&maps[idx], np->name,
					   PIN_MAP_TYPE_CONFIGS_GROUP,
					   configs, num_configs);
		if (ret < 0)
			goto remove_group;

		idx++;
	};

	dev_dbg(pctrl->dev, "Parsed %pOF with %d pins\n", np, num_pinmux);
	ret = 0;
	goto done;

remove_group:
	pinctrl_generic_remove_group(pctldev, gsel);

unlock:
	mutex_unlock(&pctrl->mutex);

done:
	*index = idx;
	kfree(configs);
	return ret;
}

static void rzt2h_dt_free_map(struct pinctrl_dev *pctldev,
			      struct pinctrl_map *map,
			      unsigned int num_maps)
{
	unsigned int i;

	if (!map)
		return;

	for (i = 0; i < num_maps; ++i) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP ||
		    map[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(map[i].data.configs.configs);
	}
	kfree(map);
}

static int rzt2h_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np,
				struct pinctrl_map **map,
				unsigned int *num_maps)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device_node *child;
	unsigned int index;
	int ret;

	*map = NULL;
	*num_maps = 0;
	index = 0;

	for_each_child_of_node(np, child) {
		ret = rzt2h_dt_subnode_to_map(pctldev, child, map,
					      num_maps, &index);
		if (ret < 0) {
			of_node_put(child);
			goto done;
		}
	}

	if (*num_maps == 0) {
		ret = rzt2h_dt_subnode_to_map(pctldev, np, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps)
		return 0;

	dev_err(pctrl->dev, "no mapping found in node %pOF\n", np);
	ret = -EINVAL;

done:
	rzt2h_dt_free_map(pctldev, *map, *num_maps);

	return ret;
}

static int rzt2h_pinctrl_pinconf_get(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *config)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int arg = 0;
	u32 port = RZT2H_PIN_ID_TO_PORT(_pin);
	u8 bit = RZT2H_PIN_ID_TO_PIN(_pin);
	u32 addr = 0;

	if (bit >= 4) {
		bit -= 4;
		addr += 4;
	}

	arg = rzt2h_pinctrl_readl(pctrl, port, DRCTL(port) + addr);

	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH: {
		arg = (arg >> (8 * bit)) & IOLH_MASK;
		break;
	}

	case PIN_CONFIG_SLEW_RATE: {
		arg = (arg >> (8 * bit + 5)) & SR_MASK;
		break;
	}

	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN: {
		arg = (arg  >> (8 * bit + 2)) & PUPD_MASK;
		if ((arg == 0 && param != PIN_CONFIG_BIAS_DISABLE) ||
		    (arg == 0x1 && param != PIN_CONFIG_BIAS_PULL_UP) ||
		    (arg == 0x2 && param != PIN_CONFIG_BIAS_PULL_DOWN))
			return -EINVAL;

		break;
	}

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE: {
		arg = (arg >> (8 * bit + 4)) & SCHMITT_MASK;
		if (!arg)
			return -EINVAL;
		break;
	}

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
};

static int rzt2h_pinctrl_pinconf_set(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *_configs,
				     unsigned int num_configs)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int i;
	u32 port = RZT2H_PIN_ID_TO_PORT(_pin);
	u8 bit = RZT2H_PIN_ID_TO_PIN(_pin);
	u32 drctl;
	u32 addr = 0;
	unsigned long flags;

	if (bit >= 4) {
		bit -= 4;
		addr += 4;
	}

	for (i = 0; i < num_configs; i++) {
		drctl = rzt2h_pinctrl_readl(pctrl, port, DRCTL(port) + addr);
		param = pinconf_to_config_param(_configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH: {
			unsigned int arg =
					pinconf_to_config_argument(_configs[i]);

			if (arg > IOLH_MASK)
				return -EINVAL;

			drctl = drctl & ~(IOLH_MASK << (bit * 8));
			drctl |= (arg << (bit * 8));
			spin_lock_irqsave(&pctrl->lock, flags);
			rzt2h_pinctrl_writel(pctrl, port, drctl,
					     DRCTL(port) + addr);
			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_SLEW_RATE: {
			unsigned int arg =
					pinconf_to_config_argument(_configs[i]);

			drctl = drctl & ~(SR_MASK << ((bit * 8) + 5));
			drctl |= (!!arg << (bit * 8 + 5));
			spin_lock_irqsave(&pctrl->lock, flags);
			rzt2h_pinctrl_writel(pctrl, port, drctl,
					     DRCTL(port) + addr);
			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN: {
			u32 bias;

			if (param == PIN_CONFIG_BIAS_DISABLE)
				bias = 0;
			else if (param == PIN_CONFIG_BIAS_PULL_UP)
				bias = 1;
			else if (param == PIN_CONFIG_BIAS_PULL_DOWN)
				bias = 2;
			else
				return -EOPNOTSUPP;

			drctl = drctl & ~(PUPD_MASK << ((bit * 8) + 2));
			drctl |= (bias << (bit * 8 + 2));
			spin_lock_irqsave(&pctrl->lock, flags);
			rzt2h_pinctrl_writel(pctrl, port, drctl,
					     DRCTL(port) + addr);
			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE: {
			u32 arg = pinconf_to_config_argument(_configs[i]);

			drctl = drctl & ~(SCHMITT_MASK << ((bit * 8) + 4));
			drctl |= (!!arg << (bit * 8 + 4));
			spin_lock_irqsave(&pctrl->lock, flags);
			rzt2h_pinctrl_writel(pctrl, port, drctl,
					     DRCTL(port) + addr);
			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		}

		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}


static int rzt2h_pinctrl_pinconf_group_set(struct pinctrl_dev *pctldev,
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
		ret = rzt2h_pinctrl_pinconf_set(pctldev, pins[i], configs,
						num_configs);
		if (ret)
			return ret;
	}

	return 0;
};

static int rzt2h_pinctrl_pinconf_group_get(struct pinctrl_dev *pctldev,
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
		ret = rzt2h_pinctrl_pinconf_get(pctldev, pins[i], config);
		if (ret) {
			return ret;
		}

		/* Check config matching between to pin  */
		if (i && prev_config != *config)
			return -EOPNOTSUPP;

		prev_config = *config;
	}

	return 0;
};

static const struct pinctrl_ops rzt2h_pinctrl_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = rzt2h_dt_node_to_map,
	.dt_free_map = rzt2h_dt_free_map,
};

static const struct pinmux_ops rzt2h_pinctrl_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rzt2h_pinctrl_set_mux,
	.strict = true,
};

static const struct pinconf_ops rzt2h_pinctrl_confops = {
	.is_generic = true,
	.pin_config_get = rzt2h_pinctrl_pinconf_get,
	.pin_config_set = rzt2h_pinctrl_pinconf_set,
	.pin_config_group_set = rzt2h_pinctrl_pinconf_group_set,
	.pin_config_group_get = rzt2h_pinctrl_pinconf_group_get,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static int rzt2h_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u8 reg8;
	int ret;

	ret = pinctrl_gpio_request(chip, offset);
	if (ret)
		return ret;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Select GPIO mode in PMC Register */
	reg8 = rzt2h_pinctrl_readb(pctrl, port, PMC(port));
	reg8 &= ~BIT(bit);
	rzt2h_pinctrl_writeb(pctrl, port, reg8, PMC(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void rzt2h_gpio_set_direction(struct rzt2h_pinctrl *pctrl, u32 port,
				     u8 bit, bool output)
{
	unsigned long flags;
	u16 reg16;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg16 = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg16 &= ~(PM_MASK << (bit * 2));

	reg16 |= (output ? PM_OUTPUT : PM_INPUT) << (bit * 2);
	rzt2h_pinctrl_writew(pctrl, port, reg16, PM(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzt2h_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);

	if (!(rzt2h_pinctrl_readb(pctrl, port, PMC(port)) & BIT(bit))) {
		u16 reg16;

		reg16 = rzt2h_pinctrl_readw(pctrl, port, PM(port));
		reg16 = (reg16 >> (bit * 2)) & PM_MASK;
		if (reg16 == PM_OUTPUT)
			return GPIO_LINE_DIRECTION_OUT;
	}

	return GPIO_LINE_DIRECTION_IN;
}

static int rzt2h_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);

	rzt2h_gpio_set_direction(pctrl, port, bit, false);

	return 0;
}

static void rzt2h_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u8 reg8;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg8 = rzt2h_pinctrl_readb(pctrl, port, P(port));

	if (value)
		rzt2h_pinctrl_writeb(pctrl, port, reg8 | BIT(bit), P(port));
	else
		rzt2h_pinctrl_writeb(pctrl, port, reg8 & ~BIT(bit), P(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzt2h_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);

	rzt2h_gpio_set(chip, offset, value);
	rzt2h_gpio_set_direction(pctrl, port, bit, true);

	return 0;
}

static int rzt2h_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	u16 reg16;

	reg16 = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg16 = (reg16 >> (bit * 2)) & PM_MASK;

	if (reg16 == PM_INPUT)
		return !!(rzt2h_pinctrl_readb(pctrl, port, PIN(port)) & BIT(bit));
	else if (reg16 == PM_OUTPUT)
		return !!(rzt2h_pinctrl_readb(pctrl, port, P(port)) & BIT(bit));
	else
		return -EINVAL;
}

static void rzt2h_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip, offset);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	rzt2h_gpio_direction_input(chip, offset);
}

static const char * const rzt2h_gpio_names[] = {
	"P0_0", "P0_1", "P0_2", "P0_3", "P0_4", "P0_5", "P0_6", "P0_7",
	"P1_0", "P1_1", "P1_2", "P1_3", "P1_4", "P1_5", "P1_6", "P1_7",
	"P2_0", "P2_1", "P2_2", "P2_3", "P2_4", "P2_5", "P2_6", "P2_7",
	"P3_0", "P3_1", "P3_2", "P3_3", "P3_4", "P3_5", "P3_6", "P3_7",
	"P4_0", "P4_1", "P4_2", "P4_3", "P4_4", "P4_5", "P4_6", "P4_7",
	"P5_0", "P5_1", "P5_2", "P5_3", "P5_4", "P5_5", "P5_6", "P5_7",
	"P6_0", "P6_1", "P6_2", "P6_3", "P6_4", "P6_5", "P6_6", "P6_7",
	"P7_0", "P7_1", "P7_2", "P7_3", "P7_4", "P7_5", "P7_6", "P7_7",
	"P8_0", "P8_1", "P8_2", "P8_3", "P8_4", "P8_5", "P8_6", "P8_7",
	"P9_0", "P9_1", "P9_2", "P9_3", "P9_4", "P9_5", "P9_6", "P9_7",
	"P10_0", "P10_1", "P10_2", "P10_3", "P10_4", "P10_5", "P10_6", "P10_7",
	"P11_0", "P11_1", "P11_2", "P11_3", "P11_4", "P11_5", "P11_6", "P11_7",
	"P12_0", "P12_1", "P12_2", "P12_3", "P12_4", "P12_5", "P12_6", "P12_7",
	"P13_0", "P13_1", "P13_2", "P13_3", "P13_4", "P13_5", "P13_6", "P13_7",
	"P14_0", "P14_1", "P14_2", "P14_3", "P14_4", "P14_5", "P14_6", "P14_7",
	"P15_0", "P15_1", "P15_2", "P15_3", "P15_4", "P15_5", "P15_6", "P15_7",
	"P16_0", "P16_1", "P16_2", "P16_3", "P16_4", "P16_5", "P16_6", "P16_7",
	"P17_0", "P17_1", "P17_2", "P17_3", "P17_4", "P17_5", "P17_6", "P17_7",
	"P18_0", "P18_1", "P18_2", "P18_3", "P18_4", "P18_5", "P18_6", "P18_7",
	"P19_0", "P19_1", "P19_2", "P19_3", "P19_4", "P19_5", "P19_6", "P19_7",
	"P20_0", "P20_1", "P20_2", "P20_3", "P20_4", "P20_5", "P20_6", "P20_7",
	"P21_0", "P21_1", "P21_2", "P21_3", "P21_4", "P21_5", "P21_6", "P21_7",
	"P22_0", "P22_1", "P22_2", "P22_3", "P22_4", "P22_5", "P22_6", "P22_7",
	"P23_0", "P23_1", "P23_2", "P23_3", "P23_4", "P23_5", "P23_6", "P23_7",
	"P24_0", "P24_1", "P24_2", "P24_3", "P24_4", "P24_5", "P24_6", "P24_7",
	"P25_0", "P25_1", "P25_2", "P25_3", "P25_4", "P25_5", "P25_6", "P25_7",
	"P26_0", "P26_1", "P26_2", "P26_3", "P26_4", "P26_5", "P26_6", "P26_7",
	"P27_0", "P27_1", "P27_2", "P27_3", "P27_4", "P27_5", "P27_6", "P27_7",
	"P28_0", "P28_1", "P28_2", "P28_3", "P28_4", "P28_5", "P28_6", "P28_7",
	"P29_0", "P29_1", "P29_2", "P29_3", "P29_4", "P29_5", "P29_6", "P29_7",
	"P30_0", "P30_1", "P30_2", "P30_3", "P30_4", "P30_5", "P30_6", "P30_7",
	"P31_0", "P31_1", "P31_2", "P31_3", "P31_4", "P31_5", "P31_6", "P31_7",
	"P32_0", "P32_1", "P32_2", "P32_3", "P32_4", "P32_5", "P32_6", "P32_7",
	"P33_0", "P33_1", "P33_2", "P33_3", "P33_4", "P33_5", "P33_6", "P33_7",
	"P34_0", "P34_1", "P34_2", "P34_3", "P34_4", "P34_5", "P34_6", "P34_7",
	"P35_0", "P35_1", "P35_2", "P35_3", "P35_4", "P35_5", "P35_6",
};

static int rzt2h_gpio_register(struct rzt2h_pinctrl *pctrl)
{
	struct device_node *np = pctrl->dev->of_node;
	struct gpio_chip *chip = &pctrl->gpio_chip;
	const char *name = dev_name(pctrl->dev);
	struct of_phandle_args of_args;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &of_args);
	if (ret) {
		dev_err(pctrl->dev, "Unable to parse gpio-ranges\n");
		return ret;
	}

	if (of_args.args[0] != 0 || of_args.args[1] != 0 ||
	    of_args.args[2] != pctrl->data->n_port_pins) {
		dev_err(pctrl->dev, "gpio-ranges does not match selected SOC\n");
		return -EINVAL;
	}

	chip->names = pctrl->data->port_pins;
	chip->request = rzt2h_gpio_request;
	chip->free = rzt2h_gpio_free;
	chip->get_direction = rzt2h_gpio_get_direction;
	chip->direction_input = rzt2h_gpio_direction_input;
	chip->direction_output = rzt2h_gpio_direction_output;
	chip->get = rzt2h_gpio_get;
	chip->set = rzt2h_gpio_set;
	chip->label = name;
	chip->parent = pctrl->dev;
	chip->owner = THIS_MODULE;
	chip->base = -1;
	chip->ngpio = of_args.args[2];

	pctrl->gpio_range.id = 0;
	pctrl->gpio_range.pin_base = 0;
	pctrl->gpio_range.base = 0;
	pctrl->gpio_range.npins = chip->ngpio;
	pctrl->gpio_range.name = chip->label;
	pctrl->gpio_range.gc = chip;
	ret = devm_gpiochip_add_data(pctrl->dev, chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO controller\n");
		return ret;
	}

	dev_dbg(pctrl->dev, "Registered gpio controller\n");

	return 0;
}

static int rzt2h_pinctrl_register(struct rzt2h_pinctrl *pctrl)
{
	struct pinctrl_pin_desc *pins;
	unsigned int i, j;
	u32 *pin_data;
	int ret;

	pctrl->desc.name = DRV_NAME;
	pctrl->desc.npins = pctrl->data->n_port_pins;
	pctrl->desc.pctlops = &rzt2h_pinctrl_pctlops;
	pctrl->desc.pmxops = &rzt2h_pinctrl_pmxops;
	pctrl->desc.confops = &rzt2h_pinctrl_confops;
	pctrl->desc.owner = THIS_MODULE;

	pins = devm_kcalloc(pctrl->dev, pctrl->desc.npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pin_data = devm_kcalloc(pctrl->dev, pctrl->desc.npins,
				sizeof(*pin_data), GFP_KERNEL);
	if (!pin_data)
		return -ENOMEM;

	pctrl->pins = pins;
	pctrl->desc.pins = pins;

	for (i = 0, j = 0; i < pctrl->data->n_port_pins; i++) {
		pins[i].number = i;
		pins[i].name = pctrl->data->port_pins[i];
		if (i && !(i % RZT2H_PINS_PER_PORT))
			j++;
	}

	ret = devm_pinctrl_register_and_init(pctrl->dev, &pctrl->desc, pctrl,
					     &pctrl->pctl);
	if (ret) {
		dev_err(pctrl->dev, "pinctrl registration failed\n");
		return ret;
	}

	ret = pinctrl_enable(pctrl->pctl);
	if (ret) {
		dev_err(pctrl->dev, "pinctrl enable failed\n");
		return ret;
	}

	ret = rzt2h_gpio_register(pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO chip: %i\n", ret);
		return ret;
	}

	return 0;
}

static void rzt2h_pinctrl_clk_disable(void *data)
{
	clk_disable_unprepare(data);
}

static int rzt2h_pinctrl_probe(struct platform_device *pdev)
{
	struct rzt2h_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = &pdev->dev;

	pctrl->data = of_device_get_match_data(&pdev->dev);
	if (!pctrl->data)
		return -EINVAL;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	pctrl->base1 = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(pctrl->base1))
		return PTR_ERR(pctrl->base1);

	pctrl->clk = devm_clk_get_optional(pctrl->dev, NULL);
	if (IS_ERR(pctrl->clk)) {
		ret = PTR_ERR(pctrl->clk);
		dev_err(pctrl->dev, "failed to get GPIO clk : %i\n", ret);
		return ret;
	}

	spin_lock_init(&pctrl->lock);
	mutex_init(&pctrl->mutex);
	platform_set_drvdata(pdev, pctrl);

	ret = clk_prepare_enable(pctrl->clk);
	if (ret) {
		dev_err(pctrl->dev, "failed to enable GPIO clk: %i\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&pdev->dev, rzt2h_pinctrl_clk_disable,
				       pctrl->clk);
	if (ret) {
		dev_err(pctrl->dev,
			"failed to register GPIO clk disable action, %i\n",
			ret);
		return ret;
	}

	ret = rzt2h_pinctrl_register(pctrl);
	if (ret)
		return ret;

	dev_info(pctrl->dev, "%s support registered\n", DRV_NAME);
	return 0;
}

static struct rzt2h_pinctrl_data r9a09g077_data = {
	.port_pins = rzt2h_gpio_names,
	.n_port_pins = ARRAY_SIZE(rzt2h_gpio_names),
};

static const struct of_device_id rzt2h_pinctrl_of_table[] = {
	{
		.compatible = "renesas,r9a09g077-pinctrl",
		.data = &r9a09g077_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver rzt2h_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rzt2h_pinctrl_of_table),
	},
	.probe = rzt2h_pinctrl_probe,
};

static int __init rzt2h_pinctrl_init(void)
{
	return platform_driver_register(&rzt2h_pinctrl_driver);
}
core_initcall(rzt2h_pinctrl_init);

MODULE_DESCRIPTION("Pin and gpio controller driver for RZ/T2H family");
MODULE_LICENSE("GPL v2");

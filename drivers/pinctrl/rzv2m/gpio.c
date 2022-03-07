// SPDX-License-Identifier: GPL-2.0
/*
 * RZV2M Pin Function Controller GPIO driver.
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "core.h"

struct rzv2m_gpio_chip {
	struct rzv2m_pfc *pfc;
	struct gpio_chip *chip;
	struct rzv2m_pfc_gpio_range *ranges;
	u32 nr_chips;
	u32 nr_ranges;
};

static struct rzv2m_pfc *gpio_to_pfc(struct gpio_chip *chip)
{
	struct rzv2m_gpio_chip *gc = gpiochip_get_data(chip);

	return gc->pfc;
}

static int gpio_pin_get(struct gpio_chip *chip, unsigned offset)
{
	int ret, value;
	struct rzv2m_gpio_chip *gc = gpiochip_get_data(chip);
	struct rzv2m_pfc *pfc = gc->pfc;;
	
	value = -EINVAL;
	if (pfc->ops && pfc->ops->get_value) {
		ret = pfc->ops->get_value(pfc, chip->base + offset, &value);
		if (ret)
			return ret;
	}
	return value;
}

static void gpio_pin_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct rzv2m_pfc *pfc = gpio_to_pfc(chip);
	int ret = -EINVAL;

	if (pfc->ops && pfc->ops->get_value)
		ret = pfc->ops->set_value(pfc, chip->base + offset, value);
}

static int gpio_rzv2m_direction_output(struct gpio_chip *chip,
				       unsigned offset, int value)
{
	struct rzv2m_pfc *pfc = gpio_to_pfc(chip);

	if (pfc->ops && pfc->ops->set_value) {
		pfc->ops->set_value(pfc, chip->base + offset, value);
	}
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static int gpio_rzv2m_direction_input(struct gpio_chip *chip,
				      unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int gpio_request_pfc_pin(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_request(chip->base + offset);
}

static void gpio_free_pfc_pin(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_gpio_free(chip->base + offset);
}

static int gpio_parse_dt_node(struct rzv2m_gpio_chip *gp_chip,
			      struct device_node *np, unsigned int *start,
			      u16 *npins)
{
	struct of_phandle_args args;
	int i, ret = 0;

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &args);
	if (ret) {
		goto done;
	}
	ret = -EINVAL;
	for (i = 0; i < gp_chip->nr_ranges; i++) {
		if (args.args[1] >= gp_chip->ranges[i].start &&
		    ((args.args[1] + args.args[2] - 1) <=
		     gp_chip->ranges[i].end)) {
			ret = 0;
			*start = args.args[1];
			*npins = args.args[2];
			goto done;
		}
	}
done:
	return ret;
}

static int rzv2m_gpio_setup_chip(struct rzv2m_pfc *pfc, struct device_node *np)
{
	struct rzv2m_gpio_chip *gp_chip = pfc->gp_chip;
	struct gpio_chip *chip;
	int pin_base = 0, ret = 0;
	u16 nr_pins = 0;

	if (of_find_property(np, "gpio-controller", NULL)) {
		ret = gpio_parse_dt_node(gp_chip, np, &pin_base, &nr_pins);
		if (ret) {
			return ret;
		}
		chip = &gp_chip->chip[gp_chip->nr_chips];
		chip->of_node = np;
		chip->request = gpio_request_pfc_pin;
		chip->free = gpio_free_pfc_pin;
		chip->set = gpio_pin_set;
		chip->get = gpio_pin_get;
		chip->direction_input = gpio_rzv2m_direction_input;
		chip->direction_output = gpio_rzv2m_direction_output;
		chip->label = np->name;
		chip->owner = THIS_MODULE;
		chip->base = pin_base;
		chip->ngpio = nr_pins;
		chip->parent = pfc->dev;
		ret = gpiochip_add_data(chip, gp_chip);
		if (ret) {
			return ret;
		}
		gp_chip->nr_chips++;
	}
	return 0;
}

int rzv2m_pfc_gpio_register(struct rzv2m_pfc *pfc)
{
	struct device_node *np = pfc->dev->of_node;
	struct device_node *child;
	struct rzv2m_gpio_chip *gp_chip;
	int num_gp = 0, ret = 0;


	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			num_gp++;
		
	}
	if (num_gp > 0) {
		gp_chip = devm_kzalloc(pfc->dev, sizeof(*gp_chip), GFP_KERNEL);
		if (!gp_chip) {
			return -ENOMEM;
		}
		gp_chip->pfc = pfc;
		gp_chip->ranges = pfc->ranges;
		gp_chip->nr_ranges = pfc->nr_ranges;
		pfc->gp_chip = gp_chip;
		gp_chip->chip = devm_kcalloc(pfc->dev, num_gp,
						    sizeof(*gp_chip->chip),
						    GFP_KERNEL);
		if (!gp_chip->chip) {
			return -ENOMEM;
		}

		gp_chip->nr_chips = 0;

		for_each_child_of_node(np, child) {
			ret = rzv2m_gpio_setup_chip(pfc, child);
			if (ret) {
				return ret;
			}
		}
	}
	return num_gp;
}

void rzv2m_pfc_gpio_remove(struct rzv2m_pfc *pfc)
{
	struct rzv2m_gpio_chip *gp_chip  = pfc->gp_chip;
	struct gpio_chip *chip;
	int i;

	for (i = 0 ; i < gp_chip->nr_chips; i++)
	{
		chip = &gp_chip->chip[i];
		gpiochip_remove(chip);
	}
}

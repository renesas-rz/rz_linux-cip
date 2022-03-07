// SPDX-License-Identifier: GPL-2.0
/*
 * RZV2M Pin Function Controller pinmux support.
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "core.h"
#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinconf.h"

struct rzv2m_pinctrl {
	struct pinctrl_dev *pctl;
	struct pinctrl_desc *pctrl_desc;
	struct rzv2m_pfc *pfc;
	struct pinctrl_pin_desc *pins;
	unsigned int *configs;
};

enum {
	PINMUX_NONE,
	PINMUX_GPIO,
	PINMUX_FUNCTION,
};

static int rzv2m_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	return pin_ctl->pfc->info->nr_groups;
};

static const char *rzv2m_get_group_name(struct pinctrl_dev *pctldev,
					unsigned selector)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	return pin_ctl->pfc->info->groups[selector].name;
}

static int rzv2m_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
				const unsigned **pins, unsigned *num_pins)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pin_ctl->pfc->info->groups[selector].pins;
	*num_pins = pin_ctl->pfc->info->groups[selector].nr_pins;
	return 0;
}

static void rzv2m_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			       unsigned offset)
{
	seq_puts(s, "pin controller");
}

static int rzv2m_pfc_map_add_config(struct pinctrl_map *map,
				    const char *group_or_pin,
				    enum pinctrl_map_type type,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	unsigned long *cfgs;

	cfgs = kmemdup(configs, num_configs * sizeof(*cfgs),
		       GFP_KERNEL);
	if (cfgs == NULL)
		return -ENOMEM;

	map->type = type;
	map->data.configs.group_or_pin = group_or_pin;
	map->data.configs.configs = cfgs;
	map->data.configs.num_configs = num_configs;

	return 0;
}

static int rzv2m_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **map,
				   unsigned int *num_maps, unsigned int *index)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pin_ctl->pfc->dev;
	struct pinctrl_map *maps = *map;
	unsigned int nmaps = *num_maps;
	unsigned int idx = *index;
	unsigned int num_configs;
	const char *function = NULL;
	unsigned long *configs;
	struct property *prop;
	unsigned int num_groups;
	unsigned int num_pins;
	const char *group;
	const char *pin;
	int ret;

	/* Parse the function and configuration properties. At least a function
	 * or one configuration must be specified.
	 */
	ret = of_property_read_string(np, "function", &function);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "Invalid function in DT\n");
		return ret;
	}

	ret = pinconf_generic_parse_dt_config(np, NULL, &configs, &num_configs);
	if (ret < 0)
		return ret;

	if (!function && num_configs == 0) {
		dev_err(dev,
			"DT node must contain at least a function or config\n");
		ret = -ENODEV;
		goto done;
	}

	/* Count the number of pins and groups and reallocate mappings. */
	ret = of_property_count_strings(np, "pins");
	if (ret == -EINVAL) {
		num_pins = 0;
	} else if (ret < 0) {
		dev_err(dev, "Invalid pins list in DT\n");
		goto done;
	} else {
		num_pins = ret;
	}
	ret = of_property_count_strings(np, "groups");
	if (ret == -EINVAL) {
		num_groups = 0;
	} else if (ret < 0) {
		dev_err(dev, "Invalid pin groups list in DT\n");
		goto done;
	} else {
		num_groups = ret;
	}

	if (!num_pins && !num_groups) {
		dev_err(dev, "No pin or group provided in DT node\n");
		ret = -ENODEV;
		goto done;
	}

	if (function)
		nmaps += num_groups;
	if (configs)
		nmaps += num_pins + num_groups;

	maps = krealloc(maps, sizeof(*maps) * nmaps, GFP_KERNEL);
	if (maps == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	*map = maps;
	*num_maps = nmaps;


	/* Iterate over pins and groups and create the mappings. */
	of_property_for_each_string(np, "groups", prop, group) {
		if (function) {
			maps[idx].type = PIN_MAP_TYPE_MUX_GROUP;
		        maps[idx].data.mux.group = group;
		        maps[idx].data.mux.function = function;
		        idx++;
		}

		if (configs) {
			ret = rzv2m_pfc_map_add_config(&maps[idx], group,
						    PIN_MAP_TYPE_CONFIGS_GROUP,
						    configs, num_configs);
			if (ret < 0)
				goto done;

			idx++;
		}
	}

	if (!configs) {
		ret = 0;
		goto done;
	}

	of_property_for_each_string(np, "pins", prop, pin) {
		ret = rzv2m_pfc_map_add_config(&maps[idx], pin,
					    PIN_MAP_TYPE_CONFIGS_PIN,
					    configs, num_configs);

		if (ret < 0)
			goto done;

		idx++;
	}
	
done:
	*index = idx;
	kfree(configs);
	return ret;
}


static int rzv2m_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np,
				struct pinctrl_map **map, unsigned *num_maps)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct device_node *child;
	unsigned int index = 0;
	int ret = 0;

	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np, child) {
		ret = rzv2m_dt_subnode_to_map(pctldev, child, map, num_maps,
					      &index);
		if (ret) {
			of_node_put(child);
			goto done;
		}
	}

	if (*num_maps == 0) {
		ret = rzv2m_dt_subnode_to_map(pctldev, np, map, num_maps,
					      &index);
		if (ret)
			goto done;
	}

	if (*num_maps)
		return 0;

	dev_err(pin_ctl->pfc->dev, "no mapping found in node %pOF\n", np);
	ret = -EINVAL;
done:
	if (ret < 0)
		pinctrl_utils_free_map(pctldev, *map, *num_maps);
	return ret;
}

static const struct pinctrl_ops rzv2m_pinctrl_ops = {
	.get_groups_count	= rzv2m_get_groups_count,
	.get_group_name		= rzv2m_get_group_name,
	.get_group_pins		= rzv2m_get_group_pins,
	.pin_dbg_show		= rzv2m_pin_dbg_show,
	.dt_node_to_map		= rzv2m_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
};


static int rzv2m_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);

	return pin_ctl->pfc->info->nr_funcs;
}

static const char *rzv2m_get_func_name(struct pinctrl_dev *pctldev,
			       unsigned selector)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);

	return pin_ctl->pfc->info->funcs[selector].name;
}

static int rzv2m_get_groups(struct pinctrl_dev *pctldev,
			    unsigned int selector,
			    const char * const **groups,
			    unsigned int * const num_groups)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pin_ctl->pfc->info->funcs[selector].groups;
	*num_groups = pin_ctl->pfc->info->funcs[selector].nr_groups;
	return 0;
}


/* rzv2m_func_set_mux
 *  Function to set mux for each pin of selected group.
 *  Pinmux subsystem will handle the conflict pin configuration, we only need
 *  to set the mux setting for each pin in this function.
 *  Pinmux subsystem give the group selector parameter mapped with our group
 *  order, so we only need to use it without function selector.
 */
static int rzv2m_func_set_mux(struct pinctrl_dev *pctldev, unsigned selector,
			      unsigned group)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzv2m_pfc *pfc = pin_ctl->pfc;
	const struct rzv2m_group_desc *_group = &pfc->info->groups[group];
	unsigned long flags;
	unsigned int i;
	int ret = 0;

	spin_lock_irqsave(&pfc->lock, flags);
	for (i = 0; i < _group->nr_pins; i++) {
		ret = rzv2m_pfc_config_mux(pfc, _group->pin_desc[i].mux,
					   _group->pin_desc[i].pin);
		if (ret)
			break;
		pin_ctl->configs[_group->pin_desc[i].pin] = PINMUX_FUNCTION;
	}
	spin_unlock_irqrestore(&pfc->lock, flags);
	return ret;
}

static int rzv2m_gpio_set_direction(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned pin, bool input)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzv2m_pfc *pfc = pin_ctl->pfc;

	if (!pfc->ops || !pfc->ops->gpio_direction)
		return -EINVAL;

	return pfc->ops->gpio_direction(pfc, pin, input);
}

static int rzv2m_gpio_request_enable(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned pin)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzv2m_pfc *pfc = pin_ctl->pfc;
	const struct rzv2m_pfc_info *info = pfc->info;
	const struct rzv2m_pin_desc *pin_desc = NULL;
	unsigned long flags;
	unsigned int i, j, index;
	int ret = 0;

	spin_lock_irqsave(&pfc->lock, flags);
	index = rzv2m_pfc_get_pin_index(pfc, pin);

	if (index >= pfc->info->nr_pins) {
		ret = -EINVAL;
		goto done;
	}

	if (pin_ctl->configs[index] != PINMUX_NONE) {
		ret = -EBUSY;
		goto done;
	}

	for (i = 0; i < info->nr_groups; i++) {
		if (strncmp(info->groups[i].name, "gpio", 4) != 0)
			continue;
		for (j = 0; j < info->groups[i].nr_pins; j++) {
			pin_desc = &info->groups[i].pin_desc[j];
			if (pin_desc->pin == pin)
				goto done_search;
			else
				pin_desc = NULL;
		}
	}

done_search:
	if (!pin_desc) {
		ret = -EINVAL;
		goto done;
	}

	ret = rzv2m_pfc_config_mux(pfc, pin_desc->mux, pin);
	if (!ret)
		pin_ctl->configs[index] = PINMUX_GPIO;

done:
	spin_unlock_irqrestore(&pfc->lock, flags);
	return ret;
}

static void rzv2m_gpio_disable_free(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned pin)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzv2m_pfc *pfc = pin_ctl->pfc;
	unsigned long flags;
	unsigned int index = rzv2m_pfc_get_pin_index(pfc, pin);

	spin_lock_irqsave(&pfc->lock, flags);
	if (index < pfc->info->nr_pins)
		pin_ctl->configs[index] = PINMUX_NONE;
	spin_unlock_irqrestore(&pfc->lock, flags);
}

static const struct pinmux_ops rzv2m_pinmux_ops = {
	.get_functions_count	= rzv2m_get_funcs_count,
	.get_function_name	= rzv2m_get_func_name,
	.get_function_groups	= rzv2m_get_groups,
	.set_mux		= rzv2m_func_set_mux,
	.gpio_set_direction	= rzv2m_gpio_set_direction,
	.gpio_request_enable	= rzv2m_gpio_request_enable,
	.gpio_disable_free	= rzv2m_gpio_disable_free,
	.strict			= true,
};

static int rzv2m_pinconf_get(struct pinctrl_dev *pctldev, unsigned _pin,
			     unsigned long *config)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzv2m_pfc *pfc = pin_ctl->pfc;
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned long flags;
	unsigned int arg;
	int ret;

	if (!pfc->ops || !pfc->ops->valid ||
	    pfc->ops->valid(pfc, _pin, param) < 0) {
		return -ENOTSUPP;
	}
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!pfc->ops->get_bias)
			return -ENOTSUPP;

		spin_lock_irqsave(&pfc->lock, flags);
		ret = pfc->ops->get_bias(pfc, _pin, &arg);
		spin_unlock_irqrestore(&pfc->lock, flags);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (!pfc->ops->get_drv_str)
			return -ENOTSUPP;
		spin_lock_irqsave(&pfc->lock, flags);
		ret = pfc->ops->get_drv_str(pfc, _pin, &arg);
		spin_unlock_irqrestore(&pfc->lock, flags);
		if (ret < 0)
			return -EINVAL;
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (!pfc->ops->get_slew)
			return -ENOTSUPP;
		spin_lock_irqsave(&pfc->lock, flags);
		ret = pfc->ops->get_drv_str(pfc, _pin, &arg);
		spin_unlock_irqrestore(&pfc->lock, flags);
		if (ret < 0)
			return -EINVAL;
		break;
	default:
			return -ENOTSUPP;
	};
	*config = pinconf_to_config_packed(param, arg);
	return 0;
}


static int rzv2m_pinconf_set(struct pinctrl_dev *pctldev, unsigned _pin,
			     unsigned long *configs, unsigned num_configs)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzv2m_pfc *pfc = pin_ctl->pfc;
	enum pin_config_param param;
	unsigned long flags;
	unsigned int arg;
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		if (!pfc->ops || !pfc->ops->valid ||
		    pfc->ops->valid(pfc, _pin, param) < 0)
			return -ENOTSUPP;
		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (!pfc->ops->set_bias)
				return -ENOTSUPP;
			spin_lock_irqsave(&pfc->lock, flags);
			ret = pfc->ops->set_bias(pfc, _pin, param);
			spin_unlock_irqrestore(&pfc->lock, flags);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (!pfc->ops->set_drv_str)
				return -ENOTSUPP;
			arg = pinconf_to_config_argument(configs[i]);
			spin_lock_irqsave(&pfc->lock, flags);
			ret = pfc->ops->set_drv_str(pfc, _pin, arg);
			spin_unlock_irqrestore(&pfc->lock, flags);
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (!pfc->ops->set_slew)
				return -ENOTSUPP;
			arg = pinconf_to_config_argument(configs[i]);
			spin_lock_irqsave(&pfc->lock, flags);
			ret = pfc->ops->set_slew(pfc, _pin, arg);
			spin_unlock_irqrestore(&pfc->lock, flags);
			break;
		default:
			return -ENOTSUPP;
		};
	}
	return 0;
}

static int rzv2m_pinconf_group_set(struct pinctrl_dev *pctldev, unsigned group,
				   unsigned long *configs, unsigned num_configs)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins;
	unsigned int n_pins;
	unsigned int i, ret;

	pins = pin_ctl->pfc->info->groups[group].pins;
	n_pins = pin_ctl->pfc->info->groups[group].nr_pins;
	for (i = 0; i < n_pins; i++) {
		ret = rzv2m_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}
	return 0;
}

static int rzv2m_pinconf_group_get(struct pinctrl_dev *pctldev,
				   unsigned int group,
				   unsigned long *config)
{
	struct rzv2m_pinctrl *pin_ctl = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins;
	unsigned int n_pins;
	unsigned int i, ret;
	unsigned long old = 0;

	pins = pin_ctl->pfc->info->groups[group].pins;
	n_pins = pin_ctl->pfc->info->groups[group].nr_pins;
	for (i = 0; i < n_pins; i++) {
		ret = rzv2m_pinconf_get(pctldev, pins[i], config);
		if (ret)
			return ret;
		if (i && (old != *config))
			return -ENOTSUPP;

		old = *config;
	}
	return 0;
}

static const struct pinconf_ops rzv2m_pinconf_ops = {
	.is_generic			= true,
	.pin_config_get			= rzv2m_pinconf_get,
	.pin_config_set			= rzv2m_pinconf_set,
	.pin_config_group_get		= rzv2m_pinconf_group_get,
	.pin_config_group_set		= rzv2m_pinconf_group_set,
	.pin_config_config_dbg_show	= pinconf_generic_dump_config,
};

static struct pinctrl_desc rzv2m_pinctrl_desc = {
	.name		= DRV_NAME,
	.owner		= THIS_MODULE,
	.pctlops	= &rzv2m_pinctrl_ops,
	.pmxops		= &rzv2m_pinmux_ops,
	.confops	= &rzv2m_pinconf_ops,
};

static int rzv2m_pfc_map_pin(struct rzv2m_pfc *pfc,
			     struct rzv2m_pinctrl *pin_ctl)
{
	unsigned int i;

	pin_ctl->pins =	devm_kcalloc(pfc->dev,
			pfc->info->nr_pins, sizeof(*pin_ctl->pins),
			GFP_KERNEL);
	if (unlikely(!pin_ctl->pins))
		return -ENOMEM;

	pin_ctl->configs = devm_kcalloc(pfc->dev,
			   pfc->info->nr_pins, sizeof(*pin_ctl->configs),
			   GFP_KERNEL);

	if (unlikely(!pin_ctl->configs))
		return -ENOMEM;

	for (i = 0; i < pfc->info->nr_pins; i++) {
		pin_ctl->pins[i].number = pfc->info->pins[i].pin;
		pin_ctl->pins[i].name = pfc->info->pins[i].name;
		pin_ctl->configs[i] = PINMUX_NONE;
	}

	return 0;
}

int rzv2m_pfc_pinctrl_register(struct rzv2m_pfc *pfc)
{
	struct rzv2m_pinctrl *pin_ctl;
	int ret;

	pin_ctl = devm_kzalloc(pfc->dev, sizeof(*pin_ctl), GFP_KERNEL);
	if (unlikely(!pin_ctl))
		return -ENOMEM;

	pin_ctl->pfc = pfc;

	ret = rzv2m_pfc_map_pin(pfc, pin_ctl);
	if (ret < 0)
		return ret;

	rzv2m_pinctrl_desc.pins = pin_ctl->pins;
	rzv2m_pinctrl_desc.npins = pfc->info->nr_pins;
	pin_ctl->pctrl_desc = &rzv2m_pinctrl_desc;

	ret = devm_pinctrl_register_and_init(pfc->dev, pin_ctl->pctrl_desc,
					     pin_ctl, &pin_ctl->pctl);
	
	if (ret) {
		dev_err(pfc->dev, "could not register: %d\n", ret);
		return ret;
	}
	return pinctrl_enable(pin_ctl->pctl);
}

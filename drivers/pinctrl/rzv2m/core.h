// SPDX-License-Identifier: GPL-2.0
/*
 * RZV2M Pin Function Controller support.
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __RZV2M_CORE_H__
#define __RZV2M_CORE_H__

#include "pfc.h"

#define DRV_NAME	"PFC_RZV2M"
#define PORT_NUMS		22
#define PINS_PER_PORT		16
#define NUM_IRQ_PER_REG		16

struct rzv2m_pfc;
struct rzv2m_irq_priv;

/* struct rzv2m_pfc_ops:
 *	(*set_bias):	Function to set bias for pin
 *	(*get_bias):	Function to get bias from pin
 *	(*set_drv_str):	Function to set driven strength capability of pin
 *	(*get_drv_str): Function to get driven strength capability of pin
 *	(*set_slew):	Function to set slew rate of pin
 *	(*get_slew):	Function to get slew rate of pin
 *	(*valid):	Function to check if pin can handle config "param"
 */
struct rzv2m_pfc_ops {
	int (*set_bias)(struct rzv2m_pfc *pfc, unsigned int pin,
			enum pin_config_param param);
	int (*get_bias)(struct rzv2m_pfc *pfc, unsigned int pin, u32 *value);
	int (*set_drv_str)(struct rzv2m_pfc *pfc, unsigned int pin,
			   enum pin_config_param param);
	int (*get_drv_str)(struct rzv2m_pfc *pfc, unsigned int pin,
			   u32 *value);
	int (*set_slew)(struct rzv2m_pfc *pfc, unsigned int pin,
			enum pin_config_param param);
	int (*get_slew)(struct rzv2m_pfc *pfc, unsigned int pin, u32 *value);
	int (*set_value)(struct rzv2m_pfc *pfc, unsigned int pin, int value);
	int (*get_value)(struct rzv2m_pfc *pfc, unsigned int pin, u32 *value);
	int (*gpio_direction)(struct rzv2m_pfc *pfc, unsigned int pin,
			      bool input);
	int (*valid)(struct rzv2m_pfc *pfc, unsigned int pin,
		     unsigned param);
	int (*irq_msk)(struct rzv2m_pfc *pfc, unsigned mark, unsigned value);
	int (*irq_inv)(struct rzv2m_pfc *pfc, unsigned mark, unsigned value);
};

struct rzv2m_pfc_gpio_range {
	u16 start;
	u16 end;
};


struct rzv2m_irq_chip {
	u32 *hw_irq;
	u16 *pins;
	u16 nr_irqs;
	u16 index;
	struct rzv2m_irq_priv *irq_priv;
};

struct rzv2m_irq_priv {
	struct rzv2m_pfc *pfc;
	struct irq_domain *irq_domain;
	struct rzv2m_irq_chip *irqs;
	u32 nr_chips;
};

struct rzv2m_pfc {
	struct device *dev;
	phys_addr_t phys;
	void __iomem *base;
	unsigned long size;

	const struct rzv2m_pfc_info *info;
	const struct rzv2m_pfc_ops *ops;
	struct rzv2m_pfc_gpio_range *ranges;
	struct rzv2m_gpio_chip *gp_chip;
	struct rzv2m_irq_priv *irq_priv;

	u32 nr_ranges;
	u32 nr_gpio_pins;
	spinlock_t lock;
};


int rzv2m_pfc_get_pin_index(struct rzv2m_pfc *pfc, unsigned int _pin);
int rzv2m_pfc_config_mux(struct rzv2m_pfc *pfc, unsigned int mux,
			 unsigned int _pin);
int rzv2m_pfc_pinctrl_register(struct rzv2m_pfc *pfc);
int rzv2m_pfc_gpio_register(struct rzv2m_pfc *pfc);
void rzv2m_pfc_gpio_remove(struct rzv2m_pfc *pfc);
extern const struct rzv2m_pfc_info r8arzv2m_pinmux_info;
extern struct rzv2m_pfc_ops rzv2m_pfc_ops_map;
#endif

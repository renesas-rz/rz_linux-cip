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

#ifndef __PFC_H__
#define __PFC_H__
#include <linux/spinlock.h>

#define PORT_PF0(fn, x, cfg)	fn(x, 0, cfg)
#define PORT_PF1(fn, x, cfg)		\
		PORT_PF0(fn, x, cfg),	\
		fn(x, 1, cfg)
#define PORT_PF2(fn, x, cfg)		\
		PORT_PF1(fn, x, cfg),	\
		fn(x, 2, cfg)
#define PORT_PF3(fn, x, cfg)		\
		PORT_PF2(fn, x, cfg),	\
		fn(x, 3, cfg)
#define PORT_PF4(fn, x, cfg)		\
		PORT_PF3(fn, x, cfg),	\
		fn(x, 4, cfg)
#define PORT_PF5(fn, x, cfg)		\
		PORT_PF4(fn, x, cfg),	\
		fn(x, 5, cfg)
#define PORT_PF6(fn, x, cfg)		\
		PORT_PF5(fn, x, cfg),	\
		fn(x, 6, cfg)
#define PORT_PF7(fn, x, cfg)		\
		PORT_PF6(fn, x, cfg),	\
		fn(x, 7, cfg)
#define PORT_PF8(fn, x, cfg)		\
		PORT_PF7(fn, x, cfg),	\
		fn(x, 8, cfg)
#define PORT_PF9(fn, x, cfg)		\
		PORT_PF8(fn, x, cfg),	\
		fn(x, 9, cfg)
#define PORT_PF10(fn, x, cfg)		\
		PORT_PF9(fn, x, cfg),	\
		fn(x, 10, cfg)
#define PORT_PF11(fn, x, cfg)		\
		PORT_PF10(fn, x, cfg),	\
		fn(x, 11, cfg)
#define PORT_PF12(fn, x, cfg)		\
		PORT_PF11(fn, x, cfg),	\
		fn(x, 12, cfg)
#define PORT_PF13(fn, x, cfg)		\
		PORT_PF12(fn, x, cfg),	\
		fn(x, 13, cfg)
#define PORT_PF14(fn, x, cfg)		\
		PORT_PF13(fn, x, cfg),	\
		fn(x, 14, cfg)
#define PORT_PF15(fn, x, cfg)		\
		PORT_PF14(fn, x, cfg),	\
		fn(x, 15, cfg)

#define RZV2M_PIN(port, pf, ...)	((PINS_PER_PORT * port) + (pf))

#define RZV2M_PIN_GP(port, pf, config)	\
			RZV2M_PINCTRL(__stringify(GP##port##_##pf), port, pf, \
					config)
#define RZV2M_PIN_NOGP(name, port, pf, config)	\
			RZV2M_PINCTRL(__stringify(name), port, pf,	\
					config | RZV2M_PIN_CFG_NO_GPIO)

#define RZV2M_PFC_PIN(port, pf, mark)		\
	{					\
		.pin = RZV2M_PIN(port, pf),	\
		.mux = mark,			\
		.name = __stringify(mark),	\
	}

#define RZV2M_PINCTRL(pin_name, port, pf, config)	\
	{					\
		.pin = RZV2M_PIN(port, pf),	\
		.configs = config,		\
		.name = pin_name,		\
	}

#define RZV2M_GROUP(x)					\
	{						\
		.name = #x,				\
		.pin_desc = x##_desc,			\
		.nr_pins = ARRAY_SIZE(x##_desc),	\
		.pins = (unsigned int[ARRAY_SIZE(x##_desc)]) {},	\
	}

#define RZV2M_FUNCTION(x)				\
	{						\
		.name = #x,				\
		.groups = x##_groups,			\
		.nr_groups = ARRAY_SIZE(x##_groups),	\
	}

#define RZV2M_REG_DATA(reg_offset, en)	\
	{				\
		.addr = reg_offset,	\
		.en_bit = en,		\
	}

/* MACRO for define configuration register
 *  reg_type: type of reg
 *  offset: offset of register to base register
 *  en_bit: BIT_WRITE_NONE (writable without set bit+16) or BIT_WRITE_ENABLE
 *  f_width: data width of each field to be set.
 *  num_vals: describe how many function of each field can handle
 *	      (should 1 for register other "SEL" register)
 *  nums: number of pin or number of functions
 */
#define RZV2M_CONFIG_REG(reg_type, offset, en_bit, f_width, num_vals, nums) \
			.type = reg_type,			\
			.reg = RZV2M_REG_DATA(offset, en_bit),	\
			.field_width = f_width,			\
			.var_field = num_vals,			\
			.num_marks = nums,			\
			.pin_or_mark = (u16 [])

#define RZV2M_PIN_CFG_INPUT             (1<<0)
#define RZV2M_PIN_CFG_OUTPUT            (1<<1)
#define RZV2M_PIN_CFG_DISPULL           (1<<2)
#define RZV2M_PIN_CFG_PULLUP            (1<<3)
#define RZV2M_PIN_CFG_PULLDOWN          (1<<4)
#define RZV2M_PIN_CFG_PULL              (RZV2M_PIN_CFG_PULLUP | \
                                         RZV2M_PIN_CFG_PULLDOWN)
#define RZV2M_PIN_CFG_DRV_STR_1         (1<<5)
#define RZV2M_PIN_CFG_DRV_STR_2         (1<<6)
#define RZV2M_PIN_CFG_DRV_STR_4         (1<<7)
#define RZV2M_PIN_CFG_DRV_STR_6         (1<<8)
#define RZV2M_PIN_CFG_DRV_STR           (RZV2M_PIN_CFG_DRV_STR_1 | \
                                         RZV2M_PIN_CFG_DRV_STR_2 | \
                                         RZV2M_PIN_CFG_DRV_STR_4 | \
                                         RZV2M_PIN_CFG_DRV_STR_6)
#define RZV2M_PIN_CFG_SLEW_FAST         (1<<9)
#define RZV2M_PIN_CFG_SLEW_SLOW         (1<<10)
#define RZV2M_PIN_CFG_SLEW              (RZV2M_PIN_CFG_SLEW_FAST | \
                                         RZV2M_PIN_CFG_SLEW_SLOW)
#define RZV2M_PIN_CFG_NO_GPIO           (1<<31)

struct rzv2m_pin_pinctrl {
	u16 pin;
	u16 configs;
	const char *name;
};

struct rzv2m_pin_desc {
	u16 pin;
	unsigned mux;
	const char *name;
};

struct rzv2m_group_desc {
	const char *name;
	unsigned int *pins;
	const struct rzv2m_pin_desc *pin_desc;
	unsigned int nr_pins;
};

struct rzv2m_func_desc {
	const char *name;
	const char * const *groups;
	unsigned int nr_groups;
};

struct rzv2m_pfc_ranges {
	u16 start;
	u16 end;
};

/* structure rzv2m_pfc_reg_data
 *  addr : contain offset of register to base address
 *  en_bit : enum value BIT_WRITE_ENABLE if register need set 1 to "bit + 16"
 *	     to be writable for "bit"
 */

struct rzv2m_pfc_reg_data {
	u32 addr;
	enum {
		BIT_WRITE_NONE,
		BIT_WRITE_ENABLE,
	} en_bit;
};

/* structure rzv2m_pfc_config_reg:
 *  reg : contain information of register
 *  pin_or_mark : contain number of pins/marks for number of field that register
 *		handle
 *  field_width : number of bit data that each field (contain reserve bit)
 *  var_field :	number of pins/mark that each field can handle
 *  num_marks :	total pins/mark of register
 *  name :	Type of register, should follow below requirement
 *			SEL	: For register select functionability
 *			SLEW	: For register setting slew rate
 *			PUPD	: For register setting Pull Up/Pull Down
 *			DRV_STR : For register setting driven strength
 *				      capability
 *			DI_MSK	: For register setting Mask Input of pins
 *			EN_MSK	: For register setting Enable output of pins
 *			GP_IE	: For register setting Enable input for GPIOs
 *			GP_OE	: For register setting Enable output for GPIOs
 *			GP_DO	: For register setting output value of GPIOs
 *			DI_MON	: For register monitor input value of pins
 *			INT_INV	: For register setting invert of external
 *				      interrupt
 *			INT_MSK	: For register seting Interrupt Mask
 */

struct rzv2m_pfc_config_reg {
	struct rzv2m_pfc_reg_data reg;
	u16 *pin_or_mark;
	u8 field_width;
	u8 var_field;
	u8 num_marks;
	enum {
		GP_DO,
		GP_OE,
		GP_IE,
		SEL,
		DI_MON,
		PUPD,
		DRV_STR,
		SLEW,
		DI_MSK,
		EN_MSK,
		INT_INV,
		INT_MSK,	
	} type;
};

struct rzv2m_pfc_info {
	const struct rzv2m_pfc_config_reg *config_regs;
	const struct rzv2m_pfc_config_reg *irq_regs;
	const struct rzv2m_pin_pinctrl *pins;
	const struct rzv2m_group_desc *groups;
	const struct rzv2m_func_desc *funcs;
	const struct rzv2m_pfc_ops *ops;

	u32 nr_config_regs;
	u32 nr_irq_regs;
	u32 nr_pins;
	u32 nr_groups;
	u32 nr_funcs;
};
#endif

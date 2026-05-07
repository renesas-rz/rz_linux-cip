// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Port Output Enable 3 (POE3) driver
 *
 * This program is free software; you can redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/reset.h>

#define ICSR1	0x00 /* Input level control/status register 1 */
#define OCSR1	0x02 /* Output level control/status register 1 */
#define ICSR2	0x04 /* Input level control/status register 2 */
#define OCSR2	0x06 /* Output level control/status register 1 */
#define ICSR3	0x08 /* Input level control/status register 3 */
#define SPOER	0x0A /* Software port output enable register */
#define POECR1	0x0B /* Port output enable control register 1 */
#define POECR2	0x0C /* Port output enable control register 2 */
#define POECR4	0x10 /* Port output enable control register 4 */
#define POECR5	0x12 /* Port output enable control register 5 */
#define ICSR4	0x16 /* Input Level Control/Status Register 4 */
#define ICSR5	0x18 /* Input Level Control/Status Register 5 */
#define ALR1	0x1A /* Active Level Setting Register 1 */
#define ICSR6   0x1C /* Input Level Control/Status Register 6 */
#define ICSR7   0x1E /* Input Level Control/Status Register 7 */
#define M0SELR1	0x24 /* MTU0 Pin Select Register 1 */
#define M0SELR2 0x25 /* MTU0 Pin Select Register 2 */
#define M3SELR	0x26 /* MTU3 Pin Select Register */
#define M4SELR1	0x27 /* MTU4 Pin Select Register 1 */
#define M4SELR2	0x28 /* MTU4 Pin Select Register 2 */
#define M6SELR	0x2A /* MTU6 Pin Select Register */
#define M7SELR1	0x2B /* MTU7 Pin Select Register 1 */
#define M7SELR2 0x2C /* MTU7 Pin Select Register 2 */
#define ICSR8   0x30 /* Input Level Control/Status Register 8 */
#define ICSR9   0x32 /* Input Level Control/Status Register 9 */
#define ICSR10	0x34 /* Input Level Control/Status Register 10 */
#define POECR7	0x36 /* Port Output Enable Control Register 7 */
#define POECR8  0x38 /* Port Output Enable Control Register 8 */
#define POECR9  0x3A /* Port Output Enable Control Register 9 */
#define POECR10	0x3C /* Port Output Enable Control Register 10 */
#define POECR11 0x3E /* Port Output Enable Control Register 11 */
#define POECR12 0x40 /* Port Output Enable Control Register 12 */

#define OCSR_OCE	BIT(9) /* Output Short High-Impedance Enable */
#define OCSR_OIE	BIT(8) /* Output Short Interrupt */
#define OCSR_OSF	BIT(15) /* Output Short Flag */
#define ICSR6_OSTSTE	BIT(9) /* Oscillation Stop High-Impedance Enable */
#define ICSR6_OSTSTF	BIT(12) /* Oscillation Stop High-Impedance Flag */
#define ICSR_PIE	BIT(8) /* Port Interrupt Enable */
#define ICSR_POEF	BIT(12) /* High-impedance request by input */
#define ICSR345_POEE	BIT(9) /* High-impedance Enable of ICSR3, ICSR4 and ICSR5 */
#define ALR1_OLSEN	BIT(7) /* Active Level Setting Enable */
#define ALR1_OLSG0AB	0x0003 /* MTIOC3BD Active Level Active-high */
#define ALR1_OLSG1AB	0x000C /* MTIOC4AC Active Level Active-high */
#define ALR1_OLSG2AB	0x0030 /* MTIOC4BD Active Level Active-high */
#define SPOER_MTUCH0HIZ		BIT(2) /* Place MTU0 output in High-Z state */
#define SPOER_MTUCH67HIZ	BIT(1) /* Place MTU67 output in High-Z state */
#define SPOER_MTUCH34HIZ	BIT(0) /* Place MTU34 output in High-Z state */
#define POECR1_MTU0AZE		BIT(0) /* MTIOC0A High-impedance Enable */
#define POECR1_MTU0BZE		BIT(1) /* MTIOC0B High-Impedance Enable */
#define POECR1_MTU0CZE		BIT(2) /* MTIOC0C High-Impedance Enable */
#define POECR1_MTU0DZE		BIT(3) /* MTIOC0D High-Impedance Enable */
#define POECR2_MTU7BDZE		BIT(0) /* MTIOC7B/D High-impedance Enable */
#define POECR2_MTU7ACZE		BIT(1) /* MTIOC7A/C High-impedance Enable */
#define POECR2_MTU6BDZE		BIT(2) /* MTIOC6B/D High-impedance Enable */
#define POECR2_MTU4BDZE		BIT(8) /* MTIOC4B/D High-impedance Enable */
#define POECR2_MTU4ACZE		BIT(9) /* MTIOC4A/C High-impedance Enable */
#define POECR2_MTU3BDZE		BIT(10) /* MTIOC3B/D High-impedance Enable */
#define POECR4_IC2ADDMT34	BIT(2) /* MTU34 High-impedance add POE4F */
#define POECR4_IC3ADDMT34	BIT(3) /* MTU34 High-impedance add POE8F */
#define POECR4_IC4ADDMT34	BIT(4) /* MTU34 High-impedance add POE10F */
#define POECR4_IC5ADDMT34	BIT(5) /* MTU34 High-impedance add POE11F */
#define POECR4_IC1ADDMT67	BIT(9) /* MTU67 High-impedance add POE0F */
#define POECR4_IC3ADDMT67	BIT(11) /* MTU67 High-impedance add POE8F */
#define POECR4_IC4ADDMT67	BIT(12) /* MTU67 High-impedance add POE10F */
#define POECR4_IC5ADDMT67	BIT(13) /* MTU67 High-impedance add POE11F */
#define POECR5_IC1ADDMT0	BIT(1) /* MTU0 High-impedance add POE0F */
#define POECR5_IC2ADDMT0	BIT(2) /* MTU0 High-impedance add POE4F */
#define POECR5_IC4ADDMT0	BIT(4) /* MTU0 High-impedance add POE10F */
#define POECR5_IC5ADDMT0	BIT(5) /* MTU0 High-impedance add POE11F */

/*  RZ/T2H specific */
#define DSMIF_ERROR_MASK		GENMASK(9, 0)

enum mtu3_channel_port_output {
	MTU3_CHANNEL_0,
	MTU3_CHANNEL_34,
	MTU3_CHANNEL_67,
};

enum renesas_poe3_soc_type {
	POE3_SOC_G2L,
	POE3_SOC_T2H,
};

struct renesas_poe3_soc_data {
	enum renesas_poe3_soc_type type;
};

struct renesas_poe3 {
	struct platform_device *pdev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	struct mutex mutex;
	int dev_base;
	const struct renesas_poe3_soc_data *soc_data;
	u8 spoer;
};

struct renesas_poe3_port_output {
	u16 hiz_mask;
	u16 poecr_reg;
	u16 poecr_shift;
	u16 ocsr_reg;
};

static const struct renesas_poe3_port_output poe3_port_out[] = {
	[MTU3_CHANNEL_0] = {
		.hiz_mask = SPOER_MTUCH0HIZ,
		.poecr_reg = POECR5,
		.poecr_shift = 0x0001,
	},
	[MTU3_CHANNEL_34] = {
		.hiz_mask = SPOER_MTUCH34HIZ,
		.poecr_reg = POECR4,
		.poecr_shift = 0x0001,
		.ocsr_reg = OCSR1,
	},
	[MTU3_CHANNEL_67] = {
		.hiz_mask = SPOER_MTUCH67HIZ,
		.poecr_reg = POECR4,
		.poecr_shift = 0x0100,
		.ocsr_reg = OCSR2,
	},
};

struct renesas_poe3_icsr_info {
	u16 ch;
	u16 reg;
	u16 poee_mask;
};

static const struct renesas_poe3_icsr_info poe3_icsr_info[] = {
	{  0, ICSR1, 0 },
	{  4, ICSR2, 0 },
	{  8, ICSR3, ICSR345_POEE },
	{ 10, ICSR4, ICSR345_POEE },
	{ 11, ICSR5, ICSR345_POEE },
};

struct renesas_poe3_poecr_info {
	const char *ch;
	u16 input_pin;
	u16 reg;
	u16 bitmask;
};

static const struct renesas_poe3_poecr_info poe3_poecr_info[] = {
	{ "mtu3_ch0",   0, POECR5, POECR5_IC1ADDMT0 },
	{ "mtu3_ch0",   4, POECR5, POECR5_IC2ADDMT0 },
	{ "mtu3_ch0",  10, POECR5, POECR5_IC4ADDMT0 },
	{ "mtu3_ch0",  11, POECR5, POECR5_IC5ADDMT0 },
	{ "mtu3_ch34",  4, POECR4, POECR4_IC2ADDMT34 },
	{ "mtu3_ch34",  8, POECR4, POECR4_IC3ADDMT34 },
	{ "mtu3_ch34", 10, POECR4, POECR4_IC4ADDMT34 },
	{ "mtu3_ch34", 11, POECR4, POECR4_IC5ADDMT34 },
	{ "mtu3_ch67",  0, POECR4, POECR4_IC1ADDMT67 },
	{ "mtu3_ch67",  8, POECR4, POECR4_IC3ADDMT67 },
	{ "mtu3_ch67", 10, POECR4, POECR4_IC4ADDMT67 },
	{ "mtu3_ch67", 11, POECR4, POECR4_IC5ADDMT67 },
};

struct renesas_poe3_pin_select_info {
	const char *child_name;
	const char *prop_name;
	u16 reg;
	u8 shift;
	u8 max_sel;
};

static const struct renesas_poe3_pin_select_info poe3_pin_select_info[] = {
	{ "mtu3_ch0",  "mtioc0a_pin_select", M0SELR1, 0, 2 },
	{ "mtu3_ch0",  "mtioc0b_pin_select", M0SELR1, 4, 2 },
	{ "mtu3_ch0",  "mtioc0c_pin_select", M0SELR2, 0, 1 },
	{ "mtu3_ch0",  "mtioc0d_pin_select", M0SELR2, 4, 1 },
	{ "mtu3_ch34", "mtioc3b_pin_select", M3SELR,  0, 1 },
	{ "mtu3_ch34", "mtioc3d_pin_select", M3SELR,  4, 1 },
	{ "mtu3_ch34", "mtioc4a_pin_select", M4SELR1, 0, 1 },
	{ "mtu3_ch34", "mtioc4c_pin_select", M4SELR1, 4, 1 },
	{ "mtu3_ch34", "mtioc4b_pin_select", M4SELR2, 0, 1 },
	{ "mtu3_ch34", "mtioc4d_pin_select", M4SELR2, 4, 1 },
	{ "mtu3_ch67", "mtioc6b_pin_select", M6SELR,  0, 2 },
	{ "mtu3_ch67", "mtioc6d_pin_select", M6SELR,  4, 2 },
	{ "mtu3_ch67", "mtioc7a_pin_select", M7SELR1, 0, 2 },
	{ "mtu3_ch67", "mtioc7c_pin_select", M7SELR1, 4, 2 },
	{ "mtu3_ch67", "mtioc7b_pin_select", M7SELR2, 0, 2 },
	{ "mtu3_ch67", "mtioc7d_pin_select", M7SELR2, 4, 2 },
};

static inline bool renesas_poe3_is_t2h(struct renesas_poe3 *poe3)
{
	return poe3->soc_data &&
	       poe3->soc_data->type == POE3_SOC_T2H;
}

static const unsigned int poe3_8bit_regs[] = {
	SPOER, POECR1, M0SELR1, M0SELR2,
	M3SELR, M4SELR1, M4SELR2,
	M6SELR, M7SELR1, M7SELR2,
};

static inline bool is_8bit_register(unsigned int reg_nr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(poe3_8bit_regs); i++) {
		if (poe3_8bit_regs[i] == reg_nr)
			return true;
	}

	return false;
}

static inline unsigned int renesas_poe3_read(struct renesas_poe3 *poe3,
					     unsigned int reg_nr)
{
	if (is_8bit_register(reg_nr))
		return ioread8(poe3->base + reg_nr);
	return ioread16(poe3->base + reg_nr);
}

static inline void renesas_poe3_write(struct renesas_poe3 *poe3,
				      unsigned int reg_nr, u16 value)
{
	if (is_8bit_register(reg_nr))
		iowrite8((u8)value, poe3->base + reg_nr);
	else
		iowrite16((u16)value, poe3->base + reg_nr);
}

static void renesas_poe3_clear_poef(struct renesas_poe3 *poe3, u16 reg, u32 offset)
{
	int i, val;
	unsigned int icsr;

	val = renesas_poe3_read(poe3, reg);
	for (i = 1; i <= 5; i++) {
		if (val & (offset << i)) {
			switch (i) {
			case 1:
				icsr = ICSR1;
				break;
			case 2:
				icsr = ICSR2;
				break;
			case 3:
				icsr = ICSR3;
				break;
			case 4:
				icsr = ICSR4;
				break;
			case 5:
				icsr = ICSR5;
				break;
			default:
				continue;
			}

			val = renesas_poe3_read(poe3, icsr);
			if (val & ICSR_POEF)
				renesas_poe3_write(poe3, icsr, val & 0x0FFF);
		}
	}
}

static int renesas_poe3_clear_Hi_z_state(struct renesas_poe3 *poe3,
					 enum mtu3_channel_port_output ch)
{
	const struct renesas_poe3_port_output *port_out;
	u32 val;

	if (ch >= ARRAY_SIZE(poe3_port_out))
		return -EINVAL;

	port_out = &poe3_port_out[ch];

	/* Clear Hi-Z */
	val = renesas_poe3_read(poe3, SPOER);
	if (val & port_out->hiz_mask)
		renesas_poe3_write(poe3, SPOER, val & ~port_out->hiz_mask);

	/* Clear POExF */
	renesas_poe3_clear_poef(poe3, port_out->poecr_reg, port_out->poecr_shift);

	/* Clear OSF */
	if (port_out->ocsr_reg) {
		val = renesas_poe3_read(poe3, port_out->ocsr_reg);
		if (val & OCSR_OSF)
			renesas_poe3_write(poe3, port_out->ocsr_reg, val & 0x0FFF);
	}

	/* Clear OSTSTF */
	if (renesas_poe3_is_t2h(poe3)) {
		val = renesas_poe3_read(poe3, ICSR6);
		if (val & ICSR6_OSTSTF)
			renesas_poe3_write(poe3, ICSR6, val & 0x0FFF);
	}

	return 0;
}

static ssize_t mtu0_output_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct renesas_poe3 *poe3 = platform_get_drvdata(pdev);
	unsigned int val1, val2;
	int ret;

	ret = kstrtouint(buf, 0, &val1);
	if (ret == 1)
		return ret;

	mutex_lock(&poe3->mutex);

	val2 = renesas_poe3_read(poe3, SPOER);
	if (val1 == 0)
		renesas_poe3_write(poe3, SPOER,
				  (val2 | SPOER_MTUCH0HIZ));
	else if (val1 == 1)
		ret = renesas_poe3_clear_Hi_z_state(poe3, MTU3_CHANNEL_0);

	mutex_unlock(&poe3->mutex);

	return ret ? : size;
}

static DEVICE_ATTR_WO(mtu0_output_enable);

static ssize_t mtu34_output_enable_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{

	struct platform_device *pdev = to_platform_device(dev);
	struct renesas_poe3 *poe3 = platform_get_drvdata(pdev);
	unsigned int val1, val2;
	int ret;

	ret = kstrtouint(buf, 0, &val1);
	if (ret == 1)
		return ret;

	mutex_lock(&poe3->mutex);

	val2 = renesas_poe3_read(poe3, SPOER);
	if (val1 == 0)
		renesas_poe3_write(poe3, SPOER,
				  (val2 | SPOER_MTUCH34HIZ));
	else if (val1 == 1)
		ret = renesas_poe3_clear_Hi_z_state(poe3, MTU3_CHANNEL_34);

	mutex_unlock(&poe3->mutex);

	return ret ? : size;
}

static DEVICE_ATTR_WO(mtu34_output_enable);

static ssize_t mtu67_output_enable_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{

	struct platform_device *pdev = to_platform_device(dev);
	struct renesas_poe3 *poe3 = platform_get_drvdata(pdev);
	unsigned int val1, val2;
	int ret;

	ret = kstrtouint(buf, 0, &val1);
	if (ret == 1)
		return ret;

	mutex_lock(&poe3->mutex);

	val2 = renesas_poe3_read(poe3, SPOER);
	if (val1 == 0)
		renesas_poe3_write(poe3, SPOER,
				  (val2 | SPOER_MTUCH67HIZ));
	else if (val1 == 1)
		ret = renesas_poe3_clear_Hi_z_state(poe3, MTU3_CHANNEL_67);

	mutex_unlock(&poe3->mutex);

	return ret ? : size;
}

static DEVICE_ATTR_WO(mtu67_output_enable);

static void renesas_poe3_additional_inputs(struct device_node *child,
					   struct renesas_poe3 *poe3,
					   unsigned int *poecr4_val,
					   unsigned int *poecr5_val)
{
	u32 input_pin, len;
	int i, j;
	bool found;

	if (!of_get_property(child, "addition_poe3_inputs", &len))
		return;

	len /= sizeof(u32);

	for (i = 0; i < len; i++) {
		if (of_property_read_u32_index(child, "addition_poe3_inputs", i, &input_pin))
			continue;

		found = false;

		for (j = 0; j < ARRAY_SIZE(poe3_poecr_info); j++) {
			if (strcmp(child->name, poe3_poecr_info[j].ch))
				continue;

			if (input_pin != poe3_poecr_info[j].input_pin)
				continue;

			if (poe3_poecr_info[j].reg == POECR5)
				*poecr5_val |= poe3_poecr_info[j].bitmask;
			else
				*poecr4_val |= poe3_poecr_info[j].bitmask;

			found = true;
			break;
		}

		if (!found)
			dev_warn(&poe3->pdev->dev,
				 "Invalid addition_poe3_inputs value %u for %s\n",
				  input_pin, child->name);
	}
}

static void renesas_poe3_setup_pin_select(struct device_node *child,
					 struct renesas_poe3 *poe3)
{
	u16 regs[] = { M0SELR1, M0SELR2, M3SELR, M4SELR1, M4SELR2, M6SELR,
		       M7SELR1, M7SELR2 };
	u8 val[ARRAY_SIZE(regs)] = {};
	bool used[ARRAY_SIZE(regs)] = {};
	const struct renesas_poe3_pin_select_info *info;
	u32 sel;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(poe3_pin_select_info); i++) {
		info = &poe3_pin_select_info[i];

		if (strcmp(child->name, info->child_name))
			continue;

		if (of_property_read_u32(child, info->prop_name, &sel))
			continue;

		if (sel > info->max_sel) {
			dev_warn(&poe3->pdev->dev,
				 "Invalid %s=%u for %s, valid range is 0..%u\n",
				 info->prop_name, sel, child->name, info->max_sel);
			continue;
		}

		for (j = 0; j < ARRAY_SIZE(regs); j++) {
			if (regs[j] != info->reg)
				continue;

			val[j] &= ~(0x3 << info->shift);
			val[j] |= (sel & 0x3) << info->shift;
			used[j] = true;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (!used[i])
			continue;

		renesas_poe3_write(poe3, regs[i], val[i]);
	}
}

static void renesas_poe3_dsmif_error_detection(struct device_node *child,
					       struct renesas_poe3 *poe3)
{
	if (!strcmp(child->name, "mtu3_ch0")) {
		renesas_poe3_write(poe3, POECR7, DSMIF_ERROR_MASK);
		renesas_poe3_write(poe3, POECR8, DSMIF_ERROR_MASK);
	} else if (!strcmp(child->name, "mtu3_ch34")) {
		renesas_poe3_write(poe3, POECR9, DSMIF_ERROR_MASK);
		renesas_poe3_write(poe3, POECR10, DSMIF_ERROR_MASK);
	} else if (!strcmp(child->name, "mtu3_ch67")) {
		renesas_poe3_write(poe3, POECR11, DSMIF_ERROR_MASK);
		renesas_poe3_write(poe3, POECR12, DSMIF_ERROR_MASK);
	}
}

static void renesas_poe3_setup(struct renesas_poe3 *poe3, bool device_file_created)
{
	struct device *dev = &poe3->pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	unsigned int poecr1_val, poecr2_val, poecr4_val, poecr5_val, val;
	u32 tmp, num, ch;
	int i, j, ret;
	bool found;

	if (!of_get_property(np, "poe3_pins_mode", &tmp))
		goto poe3_assign;

	num = tmp/(sizeof(u32)*2);

	for (i = 0; i < num; i++) {
		of_property_read_u32_index(np, "poe3_pins_mode",
					   i*2, &ch);
		of_property_read_u32_index(np, "poe3_pins_mode",
					   i*2 + 1, &tmp);
		if (tmp < 4) {
			found = false;

			for (j = 0; j < ARRAY_SIZE(poe3_icsr_info); j++) {
				if (poe3_icsr_info[j].ch == ch) {
					val = renesas_poe3_read(poe3, poe3_icsr_info[j].reg);
					val = (val | ICSR_PIE |
					       poe3_icsr_info[j].poee_mask | tmp) & 0x0FFF;
					renesas_poe3_write(poe3, poe3_icsr_info[j].reg, val);
					found = true;
					break;
				}
			}

			if (!found)
				dev_err(&poe3->pdev->dev, "Invalid pin POE%d: %d\n",
					ch, -EINVAL);
		} else
			dev_err(&poe3->pdev->dev,
				"Invalid mode %d, must be smaller than 4: %d\n",
				tmp, -EINVAL);
	}

poe3_assign:
	poecr1_val = 0;
	poecr2_val = 0;
	poecr4_val = 0;
	poecr5_val = 0;

	for_each_child_of_node(np, child) {
		if (of_get_property(child, "mtu3_outputs", &tmp)) {
			num = tmp/sizeof(u32);
			for (i = 0; i < num; i++) {
				of_property_read_u32_index(child,
					"mtu3_outputs", i, &tmp);
				if (!strcmp(child->name, "mtu3_ch0") &&
				    (tmp < 4))
					poecr1_val |= (POECR1_MTU0AZE << tmp);
				else if (!strcmp(child->name, "mtu3_ch34") &&
					(tmp < 3))
					poecr2_val |= (POECR2_MTU3BDZE >> tmp);
				else if (!strcmp(child->name, "mtu3_ch67") &&
					(tmp < 3))
					poecr2_val |= (POECR2_MTU6BDZE >>  tmp);
			}

			renesas_poe3_additional_inputs(child, poe3, &poecr4_val, &poecr5_val);

			if (renesas_poe3_is_t2h(poe3)) {
				renesas_poe3_dsmif_error_detection(child, poe3);
				renesas_poe3_setup_pin_select(child, poe3);
			}
		}

		if (!strcmp(child->name, "mtu3_ch34"))
			renesas_poe3_write(poe3, OCSR1, OCSR_OCE | OCSR_OIE);
		else if (!strcmp(child->name, "mtu3_ch67"))
			renesas_poe3_write(poe3, OCSR2, OCSR_OCE | OCSR_OIE);

		if (!device_file_created) {
			if (!strcmp(child->name, "mtu3_ch0"))
				ret = device_create_file(&poe3->pdev->dev,
						&dev_attr_mtu0_output_enable);
			else if (!strcmp(child->name, "mtu3_ch34")) {
				ret = device_create_file(&poe3->pdev->dev,
						&dev_attr_mtu34_output_enable);
			} else if (!strcmp(child->name, "mtu3_ch67")) {
				ret = device_create_file(&poe3->pdev->dev,
						&dev_attr_mtu67_output_enable);
			} else
				ret = 0;

			if (ret < 0)
				dev_err(&poe3->pdev->dev, "Failed to create poe3 sysfs for %s\n",
					child->name);
		}
	}

	renesas_poe3_write(poe3, POECR1, poecr1_val);
	renesas_poe3_write(poe3, POECR2, poecr2_val);
	renesas_poe3_write(poe3, POECR4, poecr4_val);
	renesas_poe3_write(poe3, POECR5, poecr5_val);

	if (renesas_poe3_is_t2h(poe3)) {
		renesas_poe3_write(poe3, ICSR9, DSMIF_ERROR_MASK);
		renesas_poe3_write(poe3, ICSR10, DSMIF_ERROR_MASK);
	}
}

static int renesas_poe3_probe(struct platform_device *pdev)
{
	struct renesas_poe3 *poe3;
	struct resource *res;
	int ret;

	poe3 = devm_kzalloc(&pdev->dev, sizeof(*poe3), GFP_KERNEL);
	if (poe3 == NULL)
		return -ENOMEM;

	poe3->pdev = pdev;

	poe3->soc_data = of_device_get_match_data(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	poe3->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(poe3->base))
		return PTR_ERR(poe3->base);

	poe3->rstc = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (IS_ERR(poe3->rstc)) {
		dev_err(&pdev->dev, "failed to get reset control\n");
		return PTR_ERR(poe3->rstc);
	}
	reset_control_deassert(poe3->rstc);

	poe3->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(poe3->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(poe3->clk);
	}

	ret = clk_prepare_enable(poe3->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return ret;
	}

	renesas_poe3_setup(poe3, false);

	platform_set_drvdata(pdev, poe3);
	dev_info(&pdev->dev, "Renesas POE3 driver probed\n");
	return 0;
}

static int renesas_poe3_pm_suspend(struct device *dev)
{
	struct renesas_poe3 *poe3 = dev_get_drvdata(dev);
	/* Save SPOER value */
	poe3->spoer = renesas_poe3_read(poe3, SPOER);
	clk_disable_unprepare(poe3->clk);
	reset_control_assert(poe3->rstc);

	return 0;
}

static int renesas_poe3_pm_resume(struct device *dev)
{
	struct renesas_poe3 *poe3 = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(poe3->rstc);
	if (ret) {
		dev_err(dev, "failed to deassert reset control\n");
		reset_control_assert(poe3->rstc);
		return ret;
	}

	ret = clk_prepare_enable(poe3->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		clk_disable_unprepare(poe3->clk);
		reset_control_assert(poe3->rstc);
		return ret;
	}

	renesas_poe3_setup(poe3, true);
	/* Restore SPOER value */
	renesas_poe3_write(poe3, SPOER, poe3->spoer);

	return 0;
}

static const struct dev_pm_ops renesas_poe3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(renesas_poe3_pm_suspend,
				renesas_poe3_pm_resume)
};

static void renesas_poe3_remove(struct platform_device *pdev)
{
	struct renesas_poe3 *poe = platform_get_drvdata(pdev);

	clk_disable_unprepare(poe->clk);
}

static const struct renesas_poe3_soc_data renesas_poe3_g2l_data = {
	.type = POE3_SOC_G2L,
};

static const struct renesas_poe3_soc_data renesas_poe3_t2h_data = {
	.type = POE3_SOC_T2H,
};

static const struct of_device_id renesas_poe3_of_table[] = {
	{ .compatible = "renesas,poe3", .data = &renesas_poe3_g2l_data },
	{ .compatible = "renesas,rz-poe3", .data = &renesas_poe3_g2l_data },
	{ .compatible = "renesas,r9a09g077-poe3", .data = &renesas_poe3_t2h_data },
	{ },
};

MODULE_DEVICE_TABLE(of, poe3_of_table);

static struct platform_driver renesas_poe3_device_driver = {
	.probe		= renesas_poe3_probe,
	.remove		= renesas_poe3_remove,
	.driver		= {
		.name	= "renesas_poe3",
		.of_match_table = of_match_ptr(renesas_poe3_of_table),
		.pm = &renesas_poe3_pm_ops,
	}
};

module_platform_driver(renesas_poe3_device_driver);

MODULE_DESCRIPTION("Renesas POE3 Driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL v2");

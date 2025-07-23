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
#define ICSR4	0x16 /* Input Level Control/Status Register 4 */
#define ICSR5   0x18 /* Input Level Control/Status Register 5 */
#define SPOER	0x0A /* Software port output enable register */
#define POECR1	0x0B /* Port output enable control register 1 */
#define POECR2	0x0C /* Port output enable control register 2 */
#define POECR4	0x10 /* Port output enable control register 4 */
#define POECR5	0x12 /* Port output enable control register 5 */
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
#define MxSELR_MxxSEL_VAL_LO(n)		(((n) & 0x3) << 0)
#define MxSELR_MxxSEL_VAL_HI(n)		(((n) & 0x3) << 4)

#define DSMIF_ERROR_MASK		GENMASK(9, 0)

enum renesas_poe3_board_id {
	POE3_BOARD_UNKNOWN,
	POE3_BOARD_RZG2L,
	POE3_BOARD_RZT2H,
};

struct renesas_poe3_soc_info {
	enum renesas_poe3_board_id board_id;
};

enum mtu3_channel_port_output {
	MTU3_CHANNEL_0,
	MTU3_CHANNEL_34,
	MTU3_CHANNEL_67,
};

static const unsigned int poe3_8bit_regs[] = {
	SPOER, POECR1, M0SELR1, M0SELR2,
	M3SELR, M4SELR1, M4SELR2,
	M6SELR, M7SELR1, M7SELR2,
};

struct poe3_channel_info {
	u16 hiz_mask;
	u16 poecr_reg;
	u16 poecr_shift;
	u16 ocsr_reg;
};

static const struct poe3_channel_info poe3_chan_info[] = {
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

struct poe3_icsr_entry {
	u16 input_pin;
	u16 reg;
	u16 poee_mask;
};

static const struct poe3_icsr_entry icsr_table[] = {
	{  0, ICSR1, 0 },
	{  4, ICSR2, 0 },
	{  8, ICSR3, ICSR345_POEE },
	{ 10, ICSR4, ICSR345_POEE },
	{ 11, ICSR5, ICSR345_POEE },
};

struct pin_select {
	const char *pin_name;
	const char *mtu_channel;
	int max_pin_select;
	int required_output_val;
	bool select_low_bits;
	u16 pin_select_reg;
	bool first_setup;
};

static const struct pin_select psl[] = {
	{ "mtioc0a_pin_select", "mtu3_ch0", 3, 0, true, M0SELR1, true },
	{ "mtioc0b_pin_select", "mtu3_ch0", 3, 1, false, M0SELR1, true },
	{ "mtioc0c_pin_select", "mtu3_ch0", 2, 2, true, M0SELR2, true },
	{ "mtioc0d_pin_select", "mtu3_ch0", 2, 3, false, M0SELR2, true },
	{ "mtioc3b_pin_select", "mtu3_ch34", 2, 0, true, M3SELR, true },
	{ "mtioc3d_pin_select", "mtu3_ch34", 2, 0, false, M3SELR, false },
	{ "mtioc4a_pin_select", "mtu3_ch34", 2, 1, true, M4SELR1, true },
	{ "mtioc4c_pin_select", "mtu3_ch34", 2, 1, false, M4SELR1, false },
	{ "mtioc4b_pin_select", "mtu3_ch34", 2, 2, true, M4SELR2, true },
	{ "mtioc4d_pin_select", "mtu3_ch34", 2, 2, false, M4SELR2, false },
	{ "mtioc6b_pin_select", "mtu3_ch67", 3, 0, true, M6SELR, true },
	{ "mtioc6d_pin_select", "mtu3_ch67", 3, 0, false, M6SELR, false },
	{ "mtioc7a_pin_select", "mtu3_ch67", 3, 1, true, M7SELR1, true },
	{ "mtioc7c_pin_select", "mtu3_ch67", 3, 1, false, M7SELR1, false},
	{ "mtioc7b_pin_select", "mtu3_ch67", 3, 2, true, M7SELR2, true },
	{ "mtioc7d_pin_select", "mtu3_ch67", 3, 2, false, M7SELR2, false },
};

struct poe3_poecr_entry {
	const char *mtu_channel;
	u16 input_pin;
	u16 reg;
	u16 bitmask;
};

static const struct poe3_poecr_entry poecr_table[] = {
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

struct renesas_poe3 {
	struct platform_device *pdev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	struct mutex mutex;
	int dev_base;
	enum renesas_poe3_board_id board_id;
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
		iowrite16(value, poe3->base + reg_nr);
}

static void poe3_clear_poef(struct renesas_poe3 *poe3, u16 reg, u32 offset)
{
	int i, val;
	u16 icsr;

	for (i = 1; i <= 5; i++) {
		val = renesas_poe3_read(poe3, reg);
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
	u32 val;
	const struct poe3_channel_info *info;

	if (ch >= ARRAY_SIZE(poe3_chan_info))
		return -EINVAL;

	info = &poe3_chan_info[ch];

	/* Clear Hi-Z */
	val = renesas_poe3_read(poe3, SPOER);
	if (val & info->hiz_mask)
		renesas_poe3_write(poe3, SPOER, val & ~info->hiz_mask);

	/* Clear POExF */
	poe3_clear_poef(poe3, info->poecr_reg, info->poecr_shift);

	/* Clear OSF */
	if (info->ocsr_reg) {
		val = renesas_poe3_read(poe3, info->ocsr_reg);
		if (val & OCSR_OSF)
			renesas_poe3_write(poe3, info->ocsr_reg, val & 0x0FFF);
	}

	/* Clear OSTSTF */
	if (poe3->board_id == POE3_BOARD_RZT2H) {
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
	if (ret)
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

static void renesas_poe3_highz_pin_selection(struct device_node *child,
					    struct renesas_poe3 *poe3)
{
	unsigned int poecr1_val = 0, poecr2_val = 0, alr1 = 0, val = 0;
	unsigned int m0selr1 = 0, m0selr2 = 0, m3selr = 0, m4selr1 = 0;
	unsigned int m4selr2 = 0, m6selr = 0, m7selr1 = 0, m7selr2 = 0;
	u32 pin_index, output_value, active_level;
	u32 num, total, i, j;

	of_get_property(child, "mtu3_outputs", &total);
	num = total / sizeof(u32);

	for (j = 0; j < ARRAY_SIZE(psl); j++) {
		const struct pin_select *p = &psl[j];

		if (strcmp(child->name, p->mtu_channel))
			continue;
		
		for (i = 0; i < num; i++) {
			of_property_read_u32_index(child, "mtu3_outputs", i, &output_value);
			of_property_read_u32_index(child, "active_level", i, &active_level);

			if (output_value != p->required_output_val)
				continue;

			if (p->first_setup) {
				if (!strcmp(p->mtu_channel, "mtu3_ch0"))
					poecr1_val |= (POECR1_MTU0AZE << output_value);
				else if (!strcmp(p->mtu_channel, "mtu3_ch34")) {
					poecr2_val |= (POECR2_MTU3BDZE >> output_value);
					if ((poe3->board_id == POE3_BOARD_RZT2H) && active_level == 1) {
						switch (output_value) {
						case 0:
							alr1 |= ALR1_OLSG0AB;
							break;
						case 1:
							alr1 |= ALR1_OLSG1AB;
							break;
						case 2:
							alr1 |= ALR1_OLSG2AB;
							break;
						default:
							continue;
						}
	
					}
				} else if (!strcmp(child->name, "mtu3_ch67"))
					poecr2_val |= (POECR2_MTU6BDZE >> output_value);
			}

			if (of_property_read_u32(child, p->pin_name, &pin_index))
				continue;

			if (pin_index >= p->max_pin_select)
				continue;

			if (p->select_low_bits)
				val |= MxSELR_MxxSEL_VAL_LO(pin_index);
			else
				val |= MxSELR_MxxSEL_VAL_HI(pin_index);

			switch (p->pin_select_reg) {
			case M0SELR1:
				m0selr1 |= val;
				break;
			case M0SELR2:
				m0selr2 |= val;
				break;
			case M3SELR:
				m3selr |= val;
				break;
			case M4SELR1:
				m4selr1 |= val;
				break;
			case M4SELR2:
				m4selr2 |= val;
				break;
			case M6SELR:
				m6selr |= val;
				break;
			case M7SELR1:
				m7selr1 |= val;
				break;
			case M7SELR2:
				m7selr2 |= val;
				break;
			}
		}
	}

	renesas_poe3_write(poe3, POECR1, poecr1_val);
	renesas_poe3_write(poe3, POECR2, poecr2_val);
	if (poe3->board_id == POE3_BOARD_RZT2H) {
		renesas_poe3_write(poe3, ALR1, alr1 | ALR1_OLSEN);
		renesas_poe3_write(poe3, M0SELR1, m0selr1);
		renesas_poe3_write(poe3, M0SELR2, m0selr2);
		renesas_poe3_write(poe3, M3SELR, m3selr);
		renesas_poe3_write(poe3, M4SELR1, m4selr1);
		renesas_poe3_write(poe3, M4SELR2, m4selr2);
		renesas_poe3_write(poe3, M6SELR, m6selr);
		renesas_poe3_write(poe3, M7SELR1, m7selr1);
		renesas_poe3_write(poe3, M7SELR2, m7selr2);
	}
}

static void renesas_poe3_additional_inputs(struct device_node *child,
					   struct renesas_poe3 *poe3)
{
	unsigned int poecr4_val = 0, poecr5_val = 0;
	u32 input_pin, num_inputs;
	int i, j;

	if (!of_get_property(child, "addition_poe3_inputs", &input_pin))
		return;

	num_inputs = input_pin / sizeof(u32);

	for (i = 0; i < num_inputs; i++) {
		if (of_property_read_u32_index(child, "addition_poe3_inputs", i, &input_pin))
			continue;

		for (j = 0; j < ARRAY_SIZE(poecr_table); j++) {
			if (!strcmp(child->name, poecr_table[j].mtu_channel)
						&& input_pin == poecr_table[j].input_pin) {
				if (poecr_table[j].reg == POECR5)
					poecr5_val |= poecr_table[j].bitmask;
				else
					poecr4_val |= poecr_table[j].bitmask;
				break;
			}
		}
	}

	renesas_poe3_write(poe3, POECR4, poecr4_val);
	renesas_poe3_write(poe3, POECR5, poecr5_val);
}

static void renesas_poe3_dsmif_error_detection(struct device_node *child,
					       struct renesas_poe3 *poe3)
{
	renesas_poe3_write(poe3, ICSR9, DSMIF_ERROR_MASK);
	renesas_poe3_write(poe3, ICSR10, DSMIF_ERROR_MASK);

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

static void renesas_poe3_setup(struct renesas_poe3 *poe3)
{
	struct device *dev = &poe3->pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	u32 tmp, num, input_pin, mode;
	u16 val;
	bool found = false;
	int i, j, ret;

	if (!of_get_property(np, "poe3_pins_mode", &tmp))
		goto poe3_assign;

	num = tmp/(sizeof(u32)*2);

	for (i = 0; i < num; i++) {
		if (of_property_read_u32_index(np, "poe3_pins_mode", i * 2, &input_pin) ||
		    of_property_read_u32_index(np, "poe3_pins_mode", i * 2 + 1, &mode)) {
			dev_err(dev, "Failed to read poe3_pins_mode index %d\n", i);
			continue;
		}

		if (mode >= 4) {
			dev_err(dev, "Invalid mode %u at index %d: must be < 4\n", mode, i);
			continue;
		}

		for (j = 0; j < ARRAY_SIZE(icsr_table); j++) {
			if (icsr_table[j].input_pin == input_pin) {
				val = renesas_poe3_read(poe3, icsr_table[j].reg);
				val = (val | ICSR_PIE | icsr_table[j].poee_mask | mode) & 0x0FFF;
				renesas_poe3_write(poe3, icsr_table[j].reg, val);
				found = true;
				break;
			}
		}

		if (!found)
			dev_err(dev, "Invalid POE channel %u\n", input_pin);
	}

poe3_assign:
	for_each_child_of_node(np, child) {
		if (!of_get_property(child, "mtu3_outputs", NULL))
			return;

		renesas_poe3_highz_pin_selection(child, poe3);

		renesas_poe3_additional_inputs(child, poe3);

		if ((poe3->board_id == POE3_BOARD_RZT2H))
			renesas_poe3_dsmif_error_detection(child, poe3);
		if (!strcmp(child->name, "mtu3_ch0"))
			ret = device_create_file(&poe3->pdev->dev,
					&dev_attr_mtu0_output_enable);
		else if (!strcmp(child->name, "mtu3_ch34")) {
			renesas_poe3_write(poe3, OCSR1, OCSR_OCE | OCSR_OIE);
			ret = device_create_file(&poe3->pdev->dev,
					&dev_attr_mtu34_output_enable);
		} else if (!strcmp(child->name, "mtu3_ch67")) {
			renesas_poe3_write(poe3, OCSR2, OCSR_OCE | OCSR_OIE);
			ret = device_create_file(&poe3->pdev->dev,
					&dev_attr_mtu67_output_enable);
		} else
			ret = 0;

		if (ret < 0)
			dev_err(&poe3->pdev->dev, "Failed to create poe3 sysfs for %s\n",
				child->name);
	}
}

static const struct renesas_poe3_soc_info poe3_info_rzg2l = {
	.board_id = POE3_BOARD_RZG2L,
};

static const struct renesas_poe3_soc_info poe3_info_rzt2h = {
	.board_id = POE3_BOARD_RZT2H,
};

static const struct of_device_id renesas_poe3_of_table[] = {
	{ .compatible = "renesas,poe3", .data = &poe3_info_rzg2l},
	{ .compatible = "renesas,rz-poe3", .data = &poe3_info_rzg2l},
	{ .compatible = "renesas,r9a09g077-poe3", .data = &poe3_info_rzt2h},
	{ },
};

static int renesas_poe3_probe(struct platform_device *pdev)
{
	struct renesas_poe3 *poe3;
	const struct renesas_poe3_soc_info *info;
	struct resource *res;
	int ret;

	poe3 = devm_kzalloc(&pdev->dev, sizeof(*poe3), GFP_KERNEL);
	if (poe3 == NULL)
		return -ENOMEM;

	poe3->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	poe3->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(poe3->base))
		return PTR_ERR(poe3->base);

	info = of_device_get_match_data(&pdev->dev);
	if (info)
		poe3->board_id = info->board_id;
	else {
		dev_warn(&pdev->dev, "Unknown board type, assuming default\n");
		poe3->board_id = POE3_BOARD_UNKNOWN;
	}

	if (poe3->board_id == POE3_BOARD_RZG2L) {
		poe3->rstc = devm_reset_control_get(&pdev->dev, NULL);
		if (IS_ERR(poe3->rstc)) {
			dev_err(&pdev->dev, "failed to get reset control\n");
			return PTR_ERR(poe3->rstc);
		}
		reset_control_deassert(poe3->rstc);
	}

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

	renesas_poe3_setup(poe3);

	platform_set_drvdata(pdev, poe3);
	dev_info(&pdev->dev, "Renesas POE3 driver probed\n");
	return 0;
}

static void renesas_poe3_remove(struct platform_device *pdev)
{
	struct renesas_poe3 *poe = platform_get_drvdata(pdev);

	clk_disable_unprepare(poe->clk);
}

MODULE_DEVICE_TABLE(of, poe3_of_table);

static struct platform_driver renesas_poe3_device_driver = {
	.probe		= renesas_poe3_probe,
	.remove		= renesas_poe3_remove,
	.driver		= {
		.name	= "renesas_poe3",
		.of_match_table = of_match_ptr(renesas_poe3_of_table),
	}
};

module_platform_driver(renesas_poe3_device_driver);

MODULE_DESCRIPTION("Renesas POE3 Driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL v2");

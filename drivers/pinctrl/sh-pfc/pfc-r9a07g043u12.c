// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R9A07G043U (Type2,PKG-A) SoC Pin Controller and GPIO support
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#include "pinctrl-rzg2l.h"

static struct pin_data TMS_SWDIO_data[] = {
	{2, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE)},
};

static struct pin_data TDO_SWO_data[] = {
	{3, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE)},
};

static struct pin_data AUDIO_CLK1_data[] = {
	{4, 0, PIN_CFG_INPUT_ENABLE},
};

static struct pin_data AUDIO_CLK2_data[] = {
	{4, 1, PIN_CFG_INPUT_ENABLE},
};

static struct pin_data SD0_CLK_data[] = {
	{6, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_CMD_data[] = {
	{6, 1, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_RST_N_data[] = {
	{6, 2, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA0_data[] = {
	{7, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA1_data[] = {
	{7, 1, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA2_data[] = {
	{7, 2, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA3_data[] = {
	{7, 3, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA4_data[] = {
	{7, 4, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA5_data[] = {
	{7, 5, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA6_data[] = {
	{7, 6, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD0_DATA7_data[] = {
	{7, 7, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD0)},
};

static struct pin_data SD1_CLK_data[] = {
	{8, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_SD1)},
};

static struct pin_data SD1_CMD_data[] = {
	{8, 1, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD1)},
};

static struct pin_data SD1_DATA0_data[] = {
	{9, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD1)},
};

static struct pin_data SD1_DATA1_data[] = {
	{9, 1, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD1)},
};

static struct pin_data SD1_DATA2_data[] = {
	{9, 2, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD1)},
};

static struct pin_data SD1_DATA3_data[] = {
	{9, 3, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_INPUT_ENABLE
	      | PIN_CFG_IO_VOLTAGE_SD1)},
};

static struct pin_data QSPI0_SPCLK_data[] = {
	{10, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI0_IO0_data[] = {
	{10, 1, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI0_IO1_data[] = {
	{10, 2, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI0_IO2_data[] = {
	{10, 3, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI0_IO3_data[] = {
	{10, 4, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI0_SSL_data[] = {
	{10, 5, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI_RESET_N_data[] = {
	{12, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data QSPI_WP_N_data[] = {
	{12, 1, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE
	      | PIN_CFG_IO_VOLTAGE_QSPI)},
};

static struct pin_data WDTOVF_PERROUT_data[] = {
	{13, 0, (PIN_CFG_DRIVE_STRENGTH
	      | PIN_CFG_SLEW_RATE)},
};

static struct pin_data RIIC0_SDA_data[] = {
	{14, 0, PIN_CFG_INPUT_ENABLE},
};

static struct pin_data RIIC0_SCL_data[] = {
	{14, 1, PIN_CFG_INPUT_ENABLE},
};

static struct pin_data RIIC1_SDA_data[] = {
	{14, 2, PIN_CFG_INPUT_ENABLE},
};

static struct pin_data RIIC1_SCL_data[] = {
	{14, 3, PIN_CFG_INPUT_ENABLE},
};

static const struct {
	struct pinctrl_pin_desc pin_gpio[152];
	struct pinctrl_pin_desc pin_no_gpio[34];
} r9a07g043u12_pins = {
	.pin_gpio = {
		RZ_G2L_PINCTRL_PIN_GPIO(0,  (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(1,  PIN_CFG_IO_VOLTAGE_ETH0),
		RZ_G2L_PINCTRL_PIN_GPIO(2,  PIN_CFG_IO_VOLTAGE_ETH0),
		RZ_G2L_PINCTRL_PIN_GPIO(3,  PIN_CFG_IO_VOLTAGE_ETH0),
		RZ_G2L_PINCTRL_PIN_GPIO(4,  PIN_CFG_IO_VOLTAGE_ETH0),
		RZ_G2L_PINCTRL_PIN_GPIO(5,  (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(6,  (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(7,  PIN_CFG_IO_VOLTAGE_ETH1),
		RZ_G2L_PINCTRL_PIN_GPIO(8,  PIN_CFG_IO_VOLTAGE_ETH1),
		RZ_G2L_PINCTRL_PIN_GPIO(9,  PIN_CFG_IO_VOLTAGE_ETH1),
		RZ_G2L_PINCTRL_PIN_GPIO(10, PIN_CFG_IO_VOLTAGE_ETH1),
		RZ_G2L_PINCTRL_PIN_GPIO(11, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(12, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(13, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(14, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(15, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(16, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(17, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
		RZ_G2L_PINCTRL_PIN_GPIO(18, (PIN_CFG_DRIVE_STRENGTH
					   | PIN_CFG_SLEW_RATE)),
	},
	.pin_no_gpio = {
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 0,  TMS_SWDIO),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 1,  TDO_SWO),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 2,  AUDIO_CLK1),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 3,  AUDIO_CLK2),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 4,  SD0_CLK),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 5,  SD0_CMD),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 6,  SD0_RST_N),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 7,  SD0_DATA0),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 8,  SD0_DATA1),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 9,  SD0_DATA2),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 10, SD0_DATA3),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 11, SD0_DATA4),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 12, SD0_DATA5),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 13, SD0_DATA6),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 14, SD0_DATA7),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 15, SD1_CLK),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 16, SD1_CMD),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 17, SD1_DATA0),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 18, SD1_DATA1),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 19, SD1_DATA2),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 20, SD1_DATA3),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 21, QSPI0_SPCLK),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 22, QSPI0_IO0),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 23, QSPI0_IO1),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 24, QSPI0_IO2),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 25, QSPI0_IO3),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 26, QSPI0_SSL),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 27, QSPI_RESET_N),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 28, QSPI_WP_N),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 29, WDTOVF_PERROUT),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 30, RIIC0_SDA),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 31, RIIC0_SCL),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 32, RIIC1_SDA),
		RZ_G2L_PINCTRL_PIN_NO_GPIO(
			ARRAY_SIZE(r9a07g043u12_pins.pin_gpio), 33, RIIC1_SCL),
	}
};

/* External IRQ*/
static int irq0_a_pins[] = {
	RZ_G2L_PIN(12, 0),
};

static int irq1_a_pins[] = {
	RZ_G2L_PIN(12, 1),
};

static int irq4_c_pins[] = {
	RZ_G2L_PIN(15, 0),
};

static int irq5_c_pins[] = {
	RZ_G2L_PIN(15, 1),
};

static int irq6_c_pins[] = {
	RZ_G2L_PIN(15, 2),
};

static int irq7_d_pins[] = {
	RZ_G2L_PIN(15, 3),
};

/* SCI0 */
static int sci0_data_a_pins[] = {
	/* RX, TX */
	RZ_G2L_PIN(12, 0), RZ_G2L_PIN(12, 1),
};

static int sci0_data_b_pins[] = {
	/* RX, TX */
	RZ_G2L_PIN(14, 0), RZ_G2L_PIN(14, 1),
};

static int sci0_clk_b_pins[] = {
	/* SCK */
	RZ_G2L_PIN(14, 2),
};

static int sci0_ctrl_b_pins[] = {
	/* CTS_RTS */
	RZ_G2L_PIN(16, 0),
};

/* SCI1 */
static int sci1_data_b_pins[] = {
	/* RX, TX */
	RZ_G2L_PIN(11, 0), RZ_G2L_PIN(11, 1),
};

static int sci1_clk_b_pins[] = {
	/* SCK */
	RZ_G2L_PIN(11, 2),
};

static int sci1_ctrl_b_pins[] = {
	/* CTS_RTS */
	RZ_G2L_PIN(11, 3),
};

/* SCIF0 */
static int scif0_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(13, 0), RZ_G2L_PIN(13, 1),
};

static int scif0_clk_pins[] = {
	/* SCK */
	RZ_G2L_PIN(13, 2),
};

static int scif0_ctrl_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(13, 3), RZ_G2L_PIN(13, 4),
};

/* SCIF1 */
static int scif1_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(14, 0), RZ_G2L_PIN(14, 1),
};

static int scif1_clk_pins[] = {
	/* SCK */
	RZ_G2L_PIN(14, 2),
};

static int scif1_ctrl_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(16, 0), RZ_G2L_PIN(16, 1),
};

/* SCIF2 */
static int scif2_data_a_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(5, 0), RZ_G2L_PIN(5, 1),
};

static int scif2_clk_a_pins[] = {
	/* SCK */
	RZ_G2L_PIN(5, 2),
};

static int scif2_ctrl_a_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(5, 3), RZ_G2L_PIN(5, 4),
};

static int scif2_data_d_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(6, 0), RZ_G2L_PIN(6, 1),
};

static int scif2_clk_d_pins[] = {
	/* SCK */
	RZ_G2L_PIN(6, 2),
};

static int scif2_ctrl_d_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(6, 3), RZ_G2L_PIN(6, 4),
};

/* SCIF3 */
static int scif3_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(12, 0), RZ_G2L_PIN(12, 1),
};

/* USB channel 0 */
static int usb0_a_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(5, 0), RZ_G2L_PIN(5, 2),
};

static int usb0_vbusen_a_pins[] = {
	/* VBUS */
	RZ_G2L_PIN(5, 0),
};

static int usb0_ovrcur_a_pins[] = {
	/* OVC */
	RZ_G2L_PIN(5, 2),
};

static int usb0_otg_id_a_pins[] = {
	/* OTG_ID */
	RZ_G2L_PIN(5, 3),
};

static int usb0_otg_exicen_a_pins[] = {
	/* OTG_EXICEN */
	RZ_G2L_PIN(5, 4),
};

/* USB channel 1 */
static int usb1_c_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(13, 0), RZ_G2L_PIN(13, 1),
};

static int usb1_vbusen_c_pins[] = {
	/* VBUS */
	RZ_G2L_PIN(13, 0),
};

static int usb1_ovrcur_c_pins[] = {
	/* OVC */
	RZ_G2L_PIN(13, 1),
};

static int usb1_d_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(6, 0), RZ_G2L_PIN(6, 1),
};

static int usb1_vbusen_d_pins[] = {
	/* VBUS */
	RZ_G2L_PIN(6, 0),
};

static int usb1_ovrcur_d_pins[] = {
	/* OVC */
	RZ_G2L_PIN(6, 1),
};

/* MTU channel 0 */
static int mtu0_a_pins[] = {
	/* MTIOC0A, MTIOC0B  */
	RZ_G2L_PIN(12, 0), RZ_G2L_PIN(12, 1),
};

/* MTU channel 1 */
static int mtu1_c_pins[] = {
	/* MTIOC1A, MTIOC1B */
	RZ_G2L_PIN(0, 2), RZ_G2L_PIN(0, 3),
};

/* MTU channel 2 */
static int mtu2_c_pins[] = {
	/* MTIOC2A, MTIOC2B */
	RZ_G2L_PIN(0, 0), RZ_G2L_PIN(0, 1),
};

/* MTU channel 3 */
static int mtu3_b_pins[] = {
	/* MTIOC3A, MTIOC3B, MTIOC3C, MTIOC3D */
	RZ_G2L_PIN(17, 0), RZ_G2L_PIN(17, 1),
	RZ_G2L_PIN(17, 2), RZ_G2L_PIN(17, 3),
};

/* MTU channel 4 */
static int mtu4_pins[] = {
	/* MTIOC4A, MTIOC4B, MTIOC4C, MTIOC4D */
	RZ_G2L_PIN(13, 0), RZ_G2L_PIN(13, 1),
	RZ_G2L_PIN(13, 2), RZ_G2L_PIN(13, 3),
};

/* MTU channel 5 */
static int mtu5_b_pins[] = {
	/* MTIC5U, MTIC5V, MTIC5W */
	RZ_G2L_PIN(14, 0), RZ_G2L_PIN(14, 1), RZ_G2L_PIN(14, 2),
};

/* MTU channel 7 */
static int mtu7_a_pins[] = {
	/* MTIOC7A, MTIOC7B, MTIOC7C, MTIOC7D */
	RZ_G2L_PIN(5, 0), RZ_G2L_PIN(5, 1),
	RZ_G2L_PIN(5, 2), RZ_G2L_PIN(5, 3),
};

static int mtu7_b_pins[] = {
	/* MTIOC7A, MTIOC7B, MTIOC7C, MTIOC7D */
	RZ_G2L_PIN(6, 0), RZ_G2L_PIN(6, 1),
	RZ_G2L_PIN(6, 2), RZ_G2L_PIN(6, 3),
};

/* MTU channel 8 */
static int mtu8_a_pins[] = {
	/* MTIOC8A, MTIOC8B, MTIOC8C, MTIOC8D */
	RZ_G2L_PIN(4, 0), RZ_G2L_PIN(4, 1),
	RZ_G2L_PIN(4, 2), RZ_G2L_PIN(4, 3),
};

static int mtu8_b_pins[] = {
	/* MTIOC8A, MTIOC8B, MTIOC8C, MTIOC8D */
	RZ_G2L_PIN(15, 0), RZ_G2L_PIN(15, 1),
	RZ_G2L_PIN(15, 2), RZ_G2L_PIN(15, 3),
};

/* I2C channel 2 */
static int i2c2_b_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(0, 2), RZ_G2L_PIN(0, 3),
};

static int i2c2_c_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(6, 3), RZ_G2L_PIN(6, 4),
};

/* I2C channel 3 */
static int i2c3_b_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(0, 0), RZ_G2L_PIN(0, 1),
};

/* SSI channel 0 */
static int ssi0_ctrl_b_pins[] = {
	/* BCK, RCK */
	RZ_G2L_PIN(2, 0), RZ_G2L_PIN(2, 1),
};

static int ssi0_data_b_pins[] = {
	/* TXD, RXD */
	RZ_G2L_PIN(2, 2), RZ_G2L_PIN(2, 3),
};

static int ssi0_ctrl_d_pins[] = {
	/* BCK, RCK */
	RZ_G2L_PIN(11, 0), RZ_G2L_PIN(11, 1),
};

static int ssi0_data_d_pins[] = {
	/* TXD, RXD */
	RZ_G2L_PIN(11, 2), RZ_G2L_PIN(11, 3),
};

/* SSI channel 1 */
static int ssi1_ctrl_b_pins[] = {
	/* BCK, RCK */
	RZ_G2L_PIN(3, 0), RZ_G2L_PIN(3, 1),
};

static int ssi1_data_b_pins[] = {
	/* TXD, RXD */
	RZ_G2L_PIN(3, 2), RZ_G2L_PIN(3, 3),
};

static int ssi1_ctrl_c_pins[] = {
	/* BCK, RCK */
	RZ_G2L_PIN(17, 0), RZ_G2L_PIN(17, 1),
};

static int ssi1_data_c_pins[] = {
	/* TXD, RXD */
	RZ_G2L_PIN(17, 2), RZ_G2L_PIN(17, 3),
};

/* SSI channel 2 */
static int ssi2_ctrl_a_pins[] = {
	/* BCK, RCK */
	RZ_G2L_PIN(5, 2), RZ_G2L_PIN(5, 3),
};

static int ssi2_data_a_pins[] = {
	/* DATA */
	RZ_G2L_PIN(5, 4),
};

/* RSPI channel 0 */
static int rspi0_clk_a_pins[] = {
	/* CK */
	RZ_G2L_PIN(1, 0),
};

static int rspi0_data_a_pins[] = {
	/* MOSI, MISO */
	RZ_G2L_PIN(1, 1), RZ_G2L_PIN(1, 2),
};

static int rspi0_ssl_a_pins[] = {
	/* SSL */
	RZ_G2L_PIN(1, 3),
};

static int rspi0_clk_b_pins[] = {
	/* CK */
	RZ_G2L_PIN(15, 0),
};

static int rspi0_data_b_pins[] = {
	/* MOSI, MISO */
	RZ_G2L_PIN(15, 1), RZ_G2L_PIN(15, 2),
};

static int rspi0_ssl_b_pins[] = {
	/* SSL */
	RZ_G2L_PIN(15, 3),
};

/* RSPI channel 1 */
static int rspi1_clk_a_pins[] = {
	/* CK */
	RZ_G2L_PIN(4, 0),
};

static int rspi1_data_a_pins[] = {
	/* MOSI, MISO */
	RZ_G2L_PIN(4, 1), RZ_G2L_PIN(4, 2),
};

static int rspi1_ssl_a_pins[] = {
	/* SSL */
	RZ_G2L_PIN(4, 3),
};

static int rspi1_clk_b_pins[] = {
	/* CK */
	RZ_G2L_PIN(17, 0),
};

static int rspi1_data_b_pins[] = {
	/* MOSI, MISO */
	RZ_G2L_PIN(17, 1), RZ_G2L_PIN(17, 2),
};

static int rspi1_ssl_b_pins[] = {
	/* SSL */
	RZ_G2L_PIN(17, 3),
};

/* RSPI channel 2 */
static int rspi2_clk_b_pins[] = {
	/* CK */
	RZ_G2L_PIN(6, 0),
};

static int rspi2_data_b_pins[] = {
	/* MOSI, MISO */
	RZ_G2L_PIN(6, 1), RZ_G2L_PIN(6, 2),
};

static int rspi2_ssl_b_pins[] = {
	/* SSL */
	RZ_G2L_PIN(6, 3),
};

/* CAN clock */
static int can_clk_b_pins[] = {
	/* CLK */
	RZ_G2L_PIN(1, 0),
};

static int can_clk_c_pins[] = {
	/* CLK */
	RZ_G2L_PIN(13, 0),
};

static int can_clk_d_pins[] = {
	/* CLK */
	RZ_G2L_PIN(6, 0),
};

/* CAN channel 0 */
static int can0_tx_b_pins[] = {
	/* TX */
	RZ_G2L_PIN(1, 1),
};

static int can0_rx_b_pins[] = {
	/* RX */
	RZ_G2L_PIN(1, 2),
};

static int can0_tx_datarate_en_b_pins[] = {
	/* TX_DATARATE_EN */
	RZ_G2L_PIN(1, 3),
};

static int can0_rx_datarate_en_b_pins[] = {
	/* RX_DATARATE_EN */
	RZ_G2L_PIN(1, 4),
};

static int can0_tx_c_pins[] = {
	/* TX */
	RZ_G2L_PIN(13, 1),
};

static int can0_rx_c_pins[] = {
	/* RX */
	RZ_G2L_PIN(13, 2),
};

static int can0_tx_datarate_en_c_pins[] = {
	/* TX_DATARATE_EN */
	RZ_G2L_PIN(13, 3),
};

static int can0_rx_datarate_en_c_pins[] = {
	/* RX_DATARATE_EN */
	RZ_G2L_PIN(13, 4),
};

static int can0_tx_d_pins[] = {
	/* TX */
	RZ_G2L_PIN(6, 1),
};

static int can0_rx_d_pins[] = {
	/* RX */
	RZ_G2L_PIN(6, 2),
};

static int can0_tx_datarate_en_d_pins[] = {
	/* TX_DATARATE_EN */
	RZ_G2L_PIN(6, 3),
};

static int can0_rx_datarate_en_d_pins[] = {
	/* RX_DATARATE_EN */
	RZ_G2L_PIN(6, 4),
};

/* CAN channel 1 */
static int can1_tx_b_pins[] = {
	/* TX */
	RZ_G2L_PIN(2, 0),
};

static int can1_rx_b_pins[] = {
	/* RX */
	RZ_G2L_PIN(2, 1),
};

static int can1_tx_datarate_en_b_pins[] = {
	/* TX_DATARATE_EN */
	RZ_G2L_PIN(2, 2),
};

static int can1_rx_datarate_en_b_pins[] = {
	/* RX_DATARATE_EN */
	RZ_G2L_PIN(2, 3),
};

static int can1_tx_c_pins[] = {
	/* TX */
	RZ_G2L_PIN(14, 0),
};

static int can1_rx_c_pins[] = {
	/* RX */
	RZ_G2L_PIN(14, 1),
};

static int can1_tx_datarate_en_c_pins[] = {
	/* TX_DATARATE_EN */
	RZ_G2L_PIN(14, 2),
};

static int can1_rx_datarate_en_c_pins[] = {
	/* RX_DATARATE_EN */
	RZ_G2L_PIN(16, 0),
};

static int can1_tx_d_pins[] = {
	/* TX */
	RZ_G2L_PIN(17, 0),
};

static int can1_rx_d_pins[] = {
	/* RX */
	RZ_G2L_PIN(17, 1),
};

static int can1_tx_datarate_en_d_pins[] = {
	/* TX_DATARATE_EN */
	RZ_G2L_PIN(17, 2),
};

static int can1_rx_datarate_en_d_pins[] = {
	/* RX_DATARATE_EN */
	RZ_G2L_PIN(17, 3),
};

/* POE */
static int poe_b_pins[] = {
	/* POE0_N, POE4_N, POE8_N, POE10_N */
	RZ_G2L_PIN(3, 0), RZ_G2L_PIN(3, 1),
	RZ_G2L_PIN(3, 2), RZ_G2L_PIN(3, 3),
};

static int poe_c_pins[] = {
	/* POE0_N, POE4_N, POE8_N, POE10_N */
	RZ_G2L_PIN(11, 0), RZ_G2L_PIN(11, 1),
	RZ_G2L_PIN(11, 2), RZ_G2L_PIN(11, 3),
};

/* SDHI channel 0 */
static int sdhi0_cd_a_pins[] = {
	/* CD */
	RZ_G2L_PIN(0, 0),
};

static int sdhi0_wp_a_pins[] = {
	/* WP */
	RZ_G2L_PIN(0, 1),
};

/* SDHI channel 1 */
static int sdhi1_cd_b_pins[] = {
	/* CD */
	RZ_G2L_PIN(0, 2),
};

static int sdhi1_wp_b_pins[] = {
	/* WP */
	RZ_G2L_PIN(0, 3),
};

/* MTCLK*/
static int mtclka_b_pins[] = {
	/* MTCLKA */
	RZ_G2L_PIN(2, 0),
};

static int mtclkb_b_pins[] = {
	/* MTCLKB */
	RZ_G2L_PIN(2, 1),
};

static int mtclkc_b_pins[] = {
	/* MTCLKC */
	RZ_G2L_PIN(2, 2),
};

static int mtclkd_b_pins[] = {
	/* MTCLKD */
	RZ_G2L_PIN(2, 3),
};

/* Ethernet channel 0 */
static int eth0_link_pins[] = {
	/* ETH0_LINKSTA */
	RZ_G2L_PIN(4, 5),
};

static int eth0_mdio_pins[] = {
	/* ETH0_MDC, ETH0_MDIO */
	RZ_G2L_PIN(4, 3), RZ_G2L_PIN(4, 4),
};

static int eth0_mii_pins[] = {
	RZ_G2L_PIN(1, 0),	/* ETH0_TXC_TX_CLK */
	RZ_G2L_PIN(1, 1),	/* ETH0_TX_CTL_TX_EN */
	RZ_G2L_PIN(1, 2),	/* ETH0_TXD0 */
	RZ_G2L_PIN(1, 3),	/* ETH0_TXD1 */
	RZ_G2L_PIN(1, 4),	/* ETH0_TXD2 */
	RZ_G2L_PIN(2, 0),	/* ETH0_TXD3 */
	RZ_G2L_PIN(2, 1),	/* ETH0_TX_ERR */
	RZ_G2L_PIN(2, 2),	/* ETH0_TX_COL */
	RZ_G2L_PIN(2, 3),	/* ETH0_TX_CRS */
	RZ_G2L_PIN(3, 0),	/* ETH0_RXC_RX_CLK */
	RZ_G2L_PIN(3, 1),	/* ETH0_RX_CTL_RX_DV */
	RZ_G2L_PIN(3, 2),	/* ETH0_RXD0 */
	RZ_G2L_PIN(3, 3),	/* ETH0_RXD1 */
	RZ_G2L_PIN(4, 0),	/* ETH0_RXD2 */
	RZ_G2L_PIN(4, 1),	/* ETH0_RXD3 */
	RZ_G2L_PIN(4, 2),	/* ETH0_RX_ERR */
};

static int eth0_rgmii_pins[] = {
	RZ_G2L_PIN(1, 0),	/* ETH0_TXC_TX_CLK */
	RZ_G2L_PIN(1, 1),	/* ETH0_TX_CTL_TX_EN */
	RZ_G2L_PIN(1, 2),	/* ETH0_TXD0 */
	RZ_G2L_PIN(1, 3),	/* ETH0_TXD1 */
	RZ_G2L_PIN(1, 4),	/* ETH0_TXD2 */
	RZ_G2L_PIN(2, 0),	/* ETH0_TXD3 */
	RZ_G2L_PIN(3, 0),	/* ETH0_RXC_RX_CLK */
	RZ_G2L_PIN(3, 1),	/* ETH0_RX_CTL_RX_DV */
	RZ_G2L_PIN(3, 2),	/* ETH0_RXD0 */
	RZ_G2L_PIN(3, 3),	/* ETH0_RXD1 */
	RZ_G2L_PIN(4, 0),	/* ETH0_RXD2 */
	RZ_G2L_PIN(4, 1),	/* ETH0_RXD3 */
};

static struct group_desc r9a07g043u12_groups[] = {
	RZ_G2L_PINCTRL_PIN_GROUP(irq0_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(irq1_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(irq4_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(irq5_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(irq6_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(irq7_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_vbusen_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_ovrcur_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_otg_exicen_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_otg_id_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_c, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_vbusen_c, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_ovrcur_c, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_d, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_vbusen_d, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_ovrcur_d, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(sci0_data_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(sci0_data_b, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(sci0_clk_b,  5),
	RZ_G2L_PINCTRL_PIN_GROUP(sci0_ctrl_b, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(sci1_data_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(sci1_clk_b,  3),
	RZ_G2L_PINCTRL_PIN_GROUP(sci1_ctrl_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(scif0_data, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif0_clk,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif0_ctrl, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif1_data, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif1_clk,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif1_ctrl, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_a,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_d,  4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif3_data, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu0_a, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu1_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu2_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu3_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu4,   4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu5_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu7_a, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu7_b, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu8_a, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(mtu8_b, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_c, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c3_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi0_data_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi0_ctrl_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi0_data_d, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi0_ctrl_d, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi1_data_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi1_ctrl_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi1_data_c, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi1_ctrl_c, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi2_data_a, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(ssi2_ctrl_a, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi0_clk_a,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi0_data_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi0_ssl_a,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi0_clk_b,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi0_data_b, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi0_ssl_b,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi1_clk_a,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi1_data_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi1_ssl_a,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi1_clk_b,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi1_data_b, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi1_ssl_b,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi2_clk_b,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi2_data_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(rspi2_ssl_b,  2),
	RZ_G2L_PINCTRL_PIN_GROUP(can_clk_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can_clk_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can_clk_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_tx_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_rx_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_tx_datarate_en_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_rx_datarate_en_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_tx_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_rx_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_tx_datarate_en_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_rx_datarate_en_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_tx_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_rx_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_tx_datarate_en_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can0_rx_datarate_en_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_tx_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_rx_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_tx_datarate_en_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_rx_datarate_en_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_tx_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_rx_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_tx_datarate_en_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_rx_datarate_en_c, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_tx_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_rx_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_tx_datarate_en_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(can1_rx_datarate_en_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(poe_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(poe_c, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(sdhi0_cd_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(sdhi0_wp_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(sdhi1_cd_b, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(sdhi1_wp_b, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(mtclka_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtclkb_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtclkc_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(mtclkd_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(eth0_link, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(eth0_mdio, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(eth0_mii,  1),
	RZ_G2L_PINCTRL_PIN_GROUP(eth0_rgmii, 1),
};

static const char *r9a07g043u12_irq_groups[] = {
	"irq0_a",
	"irq1_a",
	"irq4_c",
	"irq5_c",
	"irq6_c",
	"irq7_d",
};

static const char *r9a07g043u12_sci0_groups[] = {
	"sci0_data_a",
	"sci0_data_b", "sci0_clk_b", "sci0_ctrl_b",
};

static const char *r9a07g043u12_sci1_groups[] = {
	"sci1_data_b", "sci1_clk_b", "sci1_ctrl_b",
};

static const char *r9a07g043u12_scif0_groups[] = {
	"scif0_data",   "scif0_clk",   "scif0_ctrl",
};

static const char *r9a07g043u12_scif1_groups[] = {
	"scif1_data",   "scif1_clk",   "scif1_ctrl",
};

static const char *r9a07g043u12_scif2_groups[] = {
	"scif2_data_a", "scif2_clk_a", "scif2_ctrl_a",
	"scif2_data_d", "scif2_clk_d", "scif2_ctrl_d",
};

static const char *r9a07g043u12_scif3_groups[] = {
	"scif3_data",
};

static const char *r9a07g043u12_mtu0_groups[] = {
	"mtu0_a",
};

static const char *r9a07g043u12_mtu1_groups[] = {
	"mtu1_c",
};

static const char *r9a07g043u12_mtu2_groups[] = {
	"mtu2_c",
};

static const char *r9a07g043u12_mtu3_groups[] = {
	"mtu3_b",
};

static const char *r9a07g043u12_mtu4_groups[] = {
	"mtu4",
};

static const char *r9a07g043u12_mtu5_groups[] = {
	"mtu5_b",
};

static const char *r9a07g043u12_mtu7_groups[] = {
	"mtu7_a", "mtu7_b",
};

static const char *r9a07g043u12_mtu8_groups[] = {
	"mtu8_a", "mtu8_b",
};

static const char *r9a07g043u12_usb0_groups[] = {
	"usb0_a", "usb0_vbusen_a", "usb0_ovrcur_a",
	"usb0_otg_exicen_a", "usb0_otg_id_a",
};

static const char *r9a07g043u12_usb1_groups[] = {
	"usb1_c", "usb1_vbusen_c", "usb1_ovrcur_c",
	"usb1_d", "usb1_vbusen_d", "usb1_ovrcur_d",
};

static const char *r9a07g043u12_i2c2_groups[] = {
	"i2c2_b", "i2c2_c",
};

static const char *r9a07g043u12_i2c3_groups[] = {
	"i2c3_b",
};

static const char *r9a07g043u12_ssi0_groups[] = {
	"ssi0_data_b", "ssi0_ctrl_b",
	"ssi0_data_d", "ssi0_ctrl_d",
};

static const char *r9a07g043u12_ssi1_groups[] = {
	"ssi1_data_b", "ssi1_ctrl_b",
	"ssi1_data_c", "ssi1_ctrl_c",
};

static const char *r9a07g043u12_ssi2_groups[] = {
	"ssi2_data_a", "ssi2_ctrl_a",
};

static const char *r9a07g043u12_rspi0_groups[] = {
	"rspi0_clk_a", "rspi0_data_a", "rspi0_ssl_a",
	"rspi0_clk_b", "rspi0_data_b", "rspi0_ssl_b",
};

static const char *r9a07g043u12_rspi1_groups[] = {
	"rspi1_clk_a", "rspi1_data_a", "rspi1_ssl_a",
	"rspi1_clk_b", "rspi1_data_b", "rspi1_ssl_b",
};

static const char *r9a07g043u12_rspi2_groups[] = {
	"rspi2_clk_b", "rspi2_data_b", "rspi2_ssl_b",
};

static const char *r9a07g043u12_can_clk_groups[] = {
	"can_clk_b", "can_clk_c", "can_clk_d",
};

static const char *r9a07g043u12_can0_groups[] = {
	"can0_tx_b", "can0_rx_b",
	"can0_tx_datarate_en_b", "can0_rx_datarate_en_b",
	"can0_tx_c", "can0_rx_c",
	"can0_tx_datarate_en_c", "can0_rx_datarate_en_c",
	"can0_tx_d", "can0_rx_d",
	"can0_tx_datarate_en_d", "can0_rx_datarate_en_d",
};

static const char *r9a07g043u12_can1_groups[] = {
	"can1_tx_b", "can1_rx_b",
	"can1_tx_datarate_en_b", "can1_rx_datarate_en_b",
	"can1_tx_c", "can1_rx_c",
	"can1_tx_datarate_en_c", "can1_rx_datarate_en_c",
	"can1_tx_d", "can1_rx_d",
	"can1_tx_datarate_en_d", "can1_rx_datarate_en_d",
};

static const char *r9a07g043u12_poe_groups[] = {
	"poe_b", "poe_c",
};

static const char *r9a07g043u12_sdhi0_groups[] = {
	"sdhi0_cd_a", "sdhi0_wp_a",
};

static const char *r9a07g043u12_sdhi1_groups[] = {
	"sdhi1_cd_b", "sdhi1_wp_b",
};

static const char *r9a07g043u12_mtclk_groups[] = {
	"mtclka_b", "mtclkb_b", "mtclkc_b", "mtclkd_b",
};

static const char *r9a07g043u12_eth0_groups[] = {
	"eth0_link", "eth0_mdio", "eth0_mii", "eth0_rgmii",
};

static const struct function_desc r9a07g043u12_funcs[] = {
	{"irq", r9a07g043u12_irq_groups,
		ARRAY_SIZE(r9a07g043u12_irq_groups)},
	{"sci0", r9a07g043u12_sci0_groups,
		ARRAY_SIZE(r9a07g043u12_sci0_groups)},
	{"sci1", r9a07g043u12_sci1_groups,
		ARRAY_SIZE(r9a07g043u12_sci1_groups)},
	{"scif0", r9a07g043u12_scif0_groups,
		ARRAY_SIZE(r9a07g043u12_scif0_groups)},
	{"scif1", r9a07g043u12_scif1_groups,
		ARRAY_SIZE(r9a07g043u12_scif1_groups)},
	{"scif2", r9a07g043u12_scif2_groups,
		ARRAY_SIZE(r9a07g043u12_scif2_groups)},
	{"scif3", r9a07g043u12_scif3_groups,
		ARRAY_SIZE(r9a07g043u12_scif3_groups)},
	{"mtu0", r9a07g043u12_mtu0_groups,
		ARRAY_SIZE(r9a07g043u12_mtu0_groups)},
	{"mtu1", r9a07g043u12_mtu1_groups,
		ARRAY_SIZE(r9a07g043u12_mtu1_groups)},
	{"mtu2", r9a07g043u12_mtu2_groups,
		ARRAY_SIZE(r9a07g043u12_mtu2_groups)},
	{"mtu3", r9a07g043u12_mtu3_groups,
		ARRAY_SIZE(r9a07g043u12_mtu3_groups)},
	{"mtu4", r9a07g043u12_mtu4_groups,
		ARRAY_SIZE(r9a07g043u12_mtu4_groups)},
	{"mtu5", r9a07g043u12_mtu5_groups,
		ARRAY_SIZE(r9a07g043u12_mtu5_groups)},
	{"mtu7", r9a07g043u12_mtu7_groups,
		ARRAY_SIZE(r9a07g043u12_mtu7_groups)},
	{"mtu8", r9a07g043u12_mtu8_groups,
		ARRAY_SIZE(r9a07g043u12_mtu8_groups)},
	{"usb0", r9a07g043u12_usb0_groups,
		ARRAY_SIZE(r9a07g043u12_usb0_groups)},
	{"usb1", r9a07g043u12_usb1_groups,
		ARRAY_SIZE(r9a07g043u12_usb1_groups)},
	{"i2c2", r9a07g043u12_i2c2_groups,
		ARRAY_SIZE(r9a07g043u12_i2c2_groups)},
	{"i2c3", r9a07g043u12_i2c3_groups,
		ARRAY_SIZE(r9a07g043u12_i2c2_groups)},
	{"ssi0", r9a07g043u12_ssi0_groups,
		ARRAY_SIZE(r9a07g043u12_ssi0_groups)},
	{"ssi1", r9a07g043u12_ssi1_groups,
		ARRAY_SIZE(r9a07g043u12_ssi1_groups)},
	{"ssi2", r9a07g043u12_ssi2_groups,
		ARRAY_SIZE(r9a07g043u12_ssi2_groups)},
	{"rspi0", r9a07g043u12_rspi0_groups,
		ARRAY_SIZE(r9a07g043u12_rspi0_groups)},
	{"rspi1", r9a07g043u12_rspi1_groups,
		ARRAY_SIZE(r9a07g043u12_rspi1_groups)},
	{"rspi2", r9a07g043u12_rspi2_groups,
		ARRAY_SIZE(r9a07g043u12_rspi2_groups)},
	{"can_clk", r9a07g043u12_can_clk_groups,
		ARRAY_SIZE(r9a07g043u12_can_clk_groups)},
	{"can0", r9a07g043u12_can0_groups,
		ARRAY_SIZE(r9a07g043u12_can0_groups)},
	{"can1", r9a07g043u12_can1_groups,
		ARRAY_SIZE(r9a07g043u12_can1_groups)},
	{"poe", r9a07g043u12_poe_groups,
		ARRAY_SIZE(r9a07g043u12_poe_groups)},
	{"sdhi0", r9a07g043u12_sdhi0_groups,
		ARRAY_SIZE(r9a07g043u12_sdhi0_groups)},
	{"sdhi1", r9a07g043u12_sdhi1_groups,
		ARRAY_SIZE(r9a07g043u12_sdhi1_groups)},
	{"mtclk", r9a07g043u12_mtclk_groups,
		ARRAY_SIZE(r9a07g043u12_mtclk_groups)},
	{"eth0", r9a07g043u12_eth0_groups,
		ARRAY_SIZE(r9a07g043u12_eth0_groups)},
};

/* R9A07G043UL (Type2,PKG-A) support 82 interrupt input pins */
static struct rzg2l_pin_info r9a07g043u12_pin_info[] = {
	{0, 0, 0}, {0, 1, 1}, {0, 2, 2}, {0, 3, 3},
	{1, 0, 4}, {1, 1, 5}, {1, 2, 6}, {1, 3, 7}, {1, 4, 8},
	{2, 0, 9}, {2, 1, 10}, {2, 2, 11}, {2, 3, 12},
	{3, 0, 13}, {3, 1, 14}, {3, 2, 15}, {3, 3, 16},
	{4, 0, 17}, {4, 1, 18}, {4, 2, 19}, {4, 3, 20}, {4, 4, 21}, {4, 5, 22},
	{5, 0, 23}, {5, 1, 24}, {5, 2, 25}, {5, 3, 26}, {5, 4, 27},
	{6, 0, 28}, {6, 1, 29}, {6, 2, 30}, {6, 3, 31}, {6, 4, 32},
	{7, 0, 33}, {7, 1, 34}, {7, 2, 35}, {7, 3, 36}, {7, 4, 37},
	{8, 0, 38}, {8, 1, 39}, {8, 2, 40}, {8, 3, 41}, {8, 4, 42},
	{9, 0, 43}, {9, 1, 44}, {9, 2, 45}, {9, 3, 46},
	{10, 0, 47}, {10, 1, 48}, {10, 2, 49}, {10, 3, 50}, {10, 4, 51},
	{11, 0, 52}, {11, 1, 53}, {11, 2, 54}, {11, 3, 55},
	{12, 0, 56}, {12, 1, 57},
	{13, 0, 58}, {13, 1, 59}, {13, 2, 60}, {13, 3, 61}, {13, 4, 62},
	{14, 0, 63}, {14, 1, 64}, {14, 2, 65},
	{15, 0, 66}, {15, 1, 67}, {15, 2, 68}, {15, 3, 69},
	{16, 0, 70}, {16, 1, 71},
	{17, 0, 72}, {17, 1, 73}, {17, 2, 74}, {17, 3, 75},
	{18, 0, 76}, {18, 1, 77}, {18, 2, 78}, {18, 3, 79}, {18, 4, 80}, {18, 5, 81},
};

const struct rzg2l_pin_soc r9a07g043u12_pinctrl_data = {
	.pins =	r9a07g043u12_pins.pin_gpio,
	.npins = ARRAY_SIZE(r9a07g043u12_pins.pin_gpio) +
		 ARRAY_SIZE(r9a07g043u12_pins.pin_no_gpio),
	.groups	= r9a07g043u12_groups,
	.ngroups = ARRAY_SIZE(r9a07g043u12_groups),
	.funcs = r9a07g043u12_funcs,
	.nfuncs	= ARRAY_SIZE(r9a07g043u12_funcs),
	.nports = 19,
	.nirqs = 32,
	.pin_info = r9a07g043u12_pin_info,
	.ngpioints = ARRAY_SIZE(r9a07g043u12_pin_info),
};

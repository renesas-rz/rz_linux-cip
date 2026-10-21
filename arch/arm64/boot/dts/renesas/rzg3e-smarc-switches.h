/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Switch and peripheral selections for the Renesas RZ/G3E SMARC Module
 * and RZ SMARC Carrier II boards.
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 *
 */

#ifndef __RZG3E_SMARC_SWITCHES_H__
#define __RZG3E_SMARC_SWITCHES_H__

/*
 * On-board switches' states:
 * @SW_OFF: switch's state is OFF
 * @SW_ON:  switch's state is ON
 */
#define SW_OFF		0
#define SW_ON		1

/*
 * Module routing selections:
 * @SW_SD0_DEV_SEL (SYS.1):
 *	SW_OFF - SD0 is connected to eMMC
 *	SW_ON  - SD0 is connected to uSD0 card
 * @SW_LCD_EN (SYS.5):
 *	SW_OFF - Select miscellaneous signals, including PMOD GPIO/UART
 *	SW_ON  - Select LCD
 * @SW_PDM_EN (BOOT.6):
 *	SW_OFF - Select CAN routing
 *	SW_ON  - Select PDM
 * @SW_HDMI_EN (when MIPI DSI is enabled):
 *	SW_OFF - Route MIPI DSI to carrier HDMI
 *	SW_ON  - Route MIPI DSI to the module DSI connector
 */
#define SW_SD0_DEV_SEL  SW_OFF
#define SW_LCD_EN       SW_OFF
#define SW_PDM_EN       SW_OFF
#define SW_HDMI_EN      SW_OFF

/*
 * SW_OPT_MUX[x] switches' states:
 * @SW_SDIO_M2E (SW_OPT_MUX.1):
 *	SW_OFF - SMARC SDIO is connected to uSD1
 *	SW_ON  - SMARC SDIO is connected to M.2 Key E
 * @SW_SER0_PMOD (SW_OPT_MUX.4):
 *	SW_OFF - SER0 is routed to M.2 Key E UART
 *	SW_ON  - SER0 is routed to PMOD1
 * @SW_SER2_EN:
 *	SW_OFF - Disable SER2
 *	SW_ON  - Enable SER2 when SER0 is routed to PMOD1
 */
#define SW_SDIO_M2E     SW_OFF
#define SW_SER0_PMOD    SW_ON
#define SW_SER2_EN      SW_ON

/*
 * SW_GPIO[x]_CAN[x]_STB switches' states:
 * @SW_GPIO8_CAN0_STB:
 *	SW_OFF - GPIO8 is routed to PMOD
 *	SW_ON  - GPIO8 is routed to CAN0 transceiver standby
 * @SW_GPIO9_CAN1_STB:
 *	SW_OFF - GPIO9 is routed to PMOD
 *	SW_ON  - GPIO9 is routed to CAN1 transceiver standby
 */
#define SW_GPIO8_CAN0_STB	SW_OFF
#define SW_GPIO9_CAN1_STB	SW_OFF

/*
 * Display selections:
 * @RZG3E_SMARC_MIPI_DSI_ENABLE: enable MIPI DSI support
 * @RZG3E_SMARC_LVDS_ENABLE:     enable LVDS support
 *
 * Parallel RGB is selected by SW_LCD_EN.
 */
#define RZG3E_SMARC_MIPI_DSI_ENABLE	SW_ON
#define RZG3E_SMARC_LVDS_ENABLE		SW_OFF

/*
 * PMOD0 GPIO selections:
 * @PMOD_GPIO4: release GPIO4 for PMOD0 use
 * @PMOD_GPIO6: release GPIO6 for PMOD0 use
 * @PMOD_GPIO7: release GPIO7 for PMOD0 use
 */
#define PMOD_GPIO4	SW_OFF
#define PMOD_GPIO6	SW_OFF
#define PMOD_GPIO7	SW_OFF

#endif /* __RZG3E_SMARC_SWITCHES_H__ */

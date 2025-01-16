/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * On-board switches for the Renesas RZ/G3S SMARC Module and RZ SMARC Carrier II
 * boards.
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __RZG3S_SMARC_SWITCHES_H__
#define __RZG3S_SMARC_SWITCHES_H__

/*
 * On-board switches' states:
 * @SW_OFF: switch's state is OFF
 * @SW_ON:  switch's state is ON
 */
#define SW_OFF		0
#define SW_ON		1

/*
 * SW_CONFIG[x] switches' states:
 * @SW_CONFIG2:
 *	SW_OFF - SD0 is connected to eMMC
 *	SW_ON  - SD0 is connected to uSD0 card
 * @SW_CONFIG3:
 *	SW_OFF - SD2 is connected to SoC
 *	SW_ON  - SCIF1, SSI0, IRQ0, IRQ1 connected to SoC
 */
#define SW_CONFIG2	SW_OFF
#define SW_CONFIG3	SW_ON

/*
 * SW_GPIO_CAN_PMOD[x]: switch between CAN and PMOD pins
 * @SW_GPIO_CAN_PMOD1:
 *     SW_OFF - GPIO8 is connected to GPIO8_PMOD
 *     SW_ON  - GPIO8 is connected to GPIO8_CAN0_STB
 * @SW_GPIO_CAN_PMOD2:
 *     SW_OFF - GPIO9 is connected to GPIO9_PMOD
 *     SW_ON  - GPIO9 is connected to GPIO9_CAN1_STB
 */
#define SW_GPIO_CAN_PMOD1      SW_OFF
#define SW_GPIO_CAN_PMOD2      SW_OFF

/*
 * SW_OPT_MUX[x] switches' states:
 * @SW_OPT_MUX4:
 *	SW_OFF - The SMARC SER0 signals are routed to M.2 Key E UART
 *	SW_ON  - The SMARC SER0 signals are routed to PMOD1
 */
#define SW_OPT_MUX4	SW_ON

/*
 * Select between SCIF1 and SPDIF due to pins multiplex:
 * @SPDIF_SEL:
 *     SW_OFF - SCIF1 pins are selected (default)
 *     SW_ON  - SPDIF pins are selected
 */
#define SPDIF_SEL SW_OFF

#endif /* __RZG3S_SMARC_SWITCHES_H__ */

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
 * Select between SCIF1 and SPDIF due to pins multiplex:
 * @SPDIF_SEL:
 *     SW_OFF - SCIF1 pins are selected (default)
 *     SW_ON  - SPDIF pins are selected
 */
#define SPDIF_SEL	SW_OFF

/*
 * SW_OPT_MUX[x] switches' states:
 * @SW_OPT_MUX4:
 *	SW_OFF - The SMARC SER0 signals are routed to M.2 Key E UART
 *	SW_ON  - The SMARC SER0 signals are routed to PMOD1
 */
#define SW_OPT_MUX4	SW_ON

/*
 * Please set the switch position SW_GPIO_CAN_PMOD on the carrier board and the
 * corresponding macro SW_GPIO8_CAN0_STB/SW_GPIO8_CAN0_STB on the board DTS:
 *
 * SW_GPIO8_CAN0_STB:
 *	0 - Connect to GPIO8 PMOD (default)
 *	1 - Connect to CAN0 transceiver STB pin
 *
 * SW_GPIO9_CAN1_STB:
 *	0 - Connect to GPIO9 PMOD (default)
 *	1 - Connect to CAN1 transceiver STB pin
 */

#define SW_GPIO8_CAN0_STB	0
#define SW_GPIO9_CAN1_STB	0

#endif /* __RZG3S_SMARC_SWITCHES_H__ */

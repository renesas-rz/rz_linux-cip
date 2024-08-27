/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/V2H family pinctrl bindings.
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_RZV2H_PINCTRL_H
#define __DT_BINDINGS_RZV2H_PINCTRL_H

#define RZV2H_PINS_PER_PORT	8

#define PORT_0		0
#define PORT_1		1
#define PORT_2		2
#define PORT_3		3
#define PORT_4		4
#define PORT_5		5
#define PORT_6		6
#define PORT_7		7
#define PORT_8		8
#define PORT_9		9
#define PORT_A		10
#define PORT_B		11
#define PORT_C		12
#define PORT_D		13
#define PORT_E		14
#define PORT_F		15
#define PORT_G		16
#define PORT_H		17
#define PORT_I		18
#define PORT_J		19
#define PORT_K		20
#define PORT_L		21
#define PORT_M		22
#define PORT_N		23
#define PORT_O		24
#define PORT_P		25
#define PORT_Q		26
#define PORT_R		27
#define PORT_S		28
#define PORT_T		29
#define PORT_U		30
#define PORT_V		31
#define PORT_W		32
#define PORT_X		33
#define PORT_Y		34
#define PORT_Z		35

/*
 * Create the pin index from its bank and position numbers and store in
 * the upper 16 bits the alternate function identifier
 */
#define RZV2H_PORT_PINMUX(b, p, f)	(PORT_##b * RZV2H_PINS_PER_PORT + (p) | ((f) << 16))

/* Convert a port and pin label to its global pin index */
#define RZV2H_GPIO(port, pin)		(PORT_##port * RZV2H_PINS_PER_PORT + (pin))

#endif /* __DT_BINDINGS_RZV2H_PINCTRL_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Defines macros and constants for Renesas RZ/G2L pin controller
 * GPIO function
 */

#ifndef __DT_BINDINGS_PINCTRL_RZG2L_H
#define __DT_BINDINGS_PINCTRL_RZG2L_H

#define RZG2L_PINS_PER_PORT	8

#define RZG2L_GPIO(port, pos)	((port) * RZG2L_PINS_PER_PORT + (pos))

#endif /* __DT_BINDINGS_PINCTRL_RZG2L_H */

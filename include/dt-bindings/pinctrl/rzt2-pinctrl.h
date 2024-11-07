/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/T2 family pinctrl bindings.
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_RZT2_PINCTRL_H
#define __DT_BINDINGS_RZT2_PINCTRL_H

#define RZT2_PINS_PER_PORT	8

/*
 * Create the pin index from its bank and position numbers and store in
 * the upper 16 bits the alternate function identifier
 */
#define RZT2_PORT_PINMUX(b, p, f)	((b) * RZT2_PINS_PER_PORT + (p) | ((f) << 16))

/* Convert a port and pin label to its global pin index */
#define RZT2_GPIO(port, pin)	((port) * RZT2_PINS_PER_PORT + (pin))

#endif /* __DT_BINDINGS_RZT2_PINCTRL_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_SERIAL_H
#define __ASM_GENERIC_SERIAL_H

/*
 * This should not be an architecture specific #define, oh well.
 *
 * Traditionally, it just describes i8250 and related serial ports
 * that have this clock rate.
 */
#if defined(CONFIG_ARCH_R9A09G011GBG)
	#define BASE_BAUD (48000000 / 16)
#else  /* defined(CONFIG_ARCH_R9A09G011GBG) */
	#define BASE_BAUD (1843200 / 16)
#endif /* defined(CONFIG_ARCH_R9A09G011GBG) */

#endif /* __ASM_GENERIC_SERIAL_H */

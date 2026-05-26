/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ/G2L Interrupt Control Unit (ICU)
 *
 * Copyright (C) 2026 Renesas Electronics Corporation.
 */

#ifndef __LINUX_IRQ_RENESAS_RZG2L
#define __LINUX_IRQ_RENESAS_RZG2L

#ifdef CONFIG_RENESAS_RZG2L_IRQC
int rzg3l_irqc_gpt_ovfunf_mapping(struct device_node *np,
				  unsigned int channel,
				  bool is_overflow);
#else
static inline int rzg3l_irqc_gpt_ovfunf_mapping(struct device_node *np,
						unsigned int channel,
						bool is_overflow)
{
	return -ENODEV;
}
#endif

#endif /* __LINUX_IRQ_RENESAS_RZG2L */

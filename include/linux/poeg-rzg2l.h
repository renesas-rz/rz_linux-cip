/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef __POEG_RZG2L_H__
#define __POEG_RZG2L_H__

#define RZG2L_POEGG		0x0000
#define RZG2L_POEGG_PIDF	BIT(0)
#define RZG2L_POEGG_IOCF	BIT(1)
#define RZG2L_POEGG_SSF		BIT(3)
#define RZG2L_POEGG_PIDE	BIT(4)
#define RZG2L_POEGG_IOCE	BIT(5)
#define RZG2L_POEGG_ST		BIT(16)
#define RZG2L_POEGG_EN_NFEN	BIT(29)

void rzg2l_poeg_clear_bit_export(struct platform_device *poeg_dev, u32 data, unsigned int offset);

#endif /* __POEG_RZG2L_H__ */

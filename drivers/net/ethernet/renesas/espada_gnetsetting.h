/* Renesas Ethernet AVB device driver
 *
 * Copyright (C) 2014-2015 Renesas Electronics Corporation
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Copyright (C) 2015 Cogent Embedded, Inc. <source@cogentembedded.com>
 *
 * Based on the SuperH Ethernet driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */

#ifndef __ESPADA_GNETSETTING_H__
#define __ESPADA_GNETSETTING_H__

#include <linux/io.h>

enum espada_gnetsetting_reg {
	CSFR00		= 0x000,
	CSFR01		= 0x004,
	CSFR02_0	= 0x008,
	CSFR02_1	= 0x00C,
	CSFR02_2	= 0x010,
	CSFR02_3	= 0x014,
	CSFR03_U	= 0x018,
	CSFR03_L	= 0x01C,
	CSFR04		= 0x020,
	CSFR10_0	= 0x030,
	CSFR10_1	= 0x034,
	CSFR10_2	= 0x038,
	CSFR10_3	= 0x03C,
	CSFR10		= 0x040,
	CSFR11_0	= 0x044,
	CSFR11_1	= 0x048,
	CSFR11_2	= 0x04C,
	CSFR11_3	= 0x050,
	CSFR11		= 0x054,
	CSFR12_0	= 0x058,
	CSFR12_1	= 0x05C,
	CSFR12_2	= 0x060,
	CSFR12_3	= 0x064,
	CSFR12_4	= 0x068,
	CSFR12_5	= 0x06C,
	CSFR12_6	= 0x070,
	CSFR12_7	= 0x074,
	CSFR12_8	= 0x078,
	CSFR12_9	= 0x07C,
	CSFR12_10	= 0x080,
	CSFR12_11	= 0x084,
	CSFR12		= 0x088,
	CSFR13_U	= 0x08C,
	CSFR13_L	= 0x090,
	CSFR14_U	= 0x094,
	CSFR14_L	= 0x098,
	CSFR15_U_0	= 0x09C,
	CSFR15_L_0	= 0x0A0,
	CSFR15_U_1	= 0x0A4,
	CSFR15_L_1	= 0x0A8,
	CSFR15_U_2	= 0x0AC,
	CSFR15_L_2	= 0x0B0,
	CSFR15_U_3	= 0x0B4,
	CSFR15_L_3	= 0x0B8,
	CSFR15_U_4	= 0x0BC,
	CSFR15_L_4	= 0x0C0,
	CSFR15_U_5	= 0x0C4,
	CSFR15_L_5	= 0x0C8,
	CSFR15_U_6	= 0x0CC,
	CSFR15_L_6	= 0x0D0,
	CSFR15_U_7	= 0x0D4,
	CSFR15_L_7	= 0x0D8,
	CSFR15_U_8	= 0x0DC,
	CSFR15_L_8	= 0x0E0,
	CSFR15_U_9	= 0x0E4,
	CSFR15_L_9	= 0x0E8,
	CSFR15_U_10	= 0x0EC,
	CSFR15_L_10	= 0x0F0,
	CSFR15_U_11	= 0x0F4,
	CSFR15_L_11	= 0x0F8,
	CSFR15_U_12	= 0x0FC,
	CSFR15_L_12	= 0x100,
	CSFR15_U_13	= 0x104,
	CSFR15_L_13	= 0x108,
	CSFR15_U_14	= 0x10C,
	CSFR15_L_14	= 0x110,
	CSFR15_U_15	= 0x114,
	CSFR15_L_15	= 0x118,
	CSFR15_U_16	= 0x11C,
	CSFR15_L_16	= 0x120,
	CSFR15_U_17	= 0x124,
	CSFR15_L_17	= 0x128,
	CSFR15_U_18	= 0x12C,
	CSFR15_L_18	= 0x130,
	CSFR15_U_19	= 0x134,
	CSFR15_L_19	= 0x138,
	CSFR15		= 0x13C,
	CSFR16_0	= 0x140,
	CSFR16_1	= 0x144,
	CSFR16_2	= 0x148,
	CSFR16		= 0x14C,
	CSFR20		= 0x160,
	CSFR21		= 0x164,
	CSFR30		= 0x170,
	CSFR31		= 0x174,
	CSFR40		= 0x180,
	CSFR41		= 0x184,
};

static const u32 espada_gnetsetting_filtering_reg[] = {
	CSFR00,
	CSFR01,
	CSFR02_0,
	CSFR02_1,
	CSFR02_2,
	CSFR02_3,
	CSFR03_U,
	CSFR03_L,
	CSFR04,
	CSFR10_0,
	CSFR10_1,
	CSFR10_2,
	CSFR10_3,
	CSFR10,
	CSFR11_0,
	CSFR11_1,
	CSFR11_2,
	CSFR11_3,
	CSFR11,
	CSFR12_0,
	CSFR12_1,
	CSFR12_2,
	CSFR12_3,
	CSFR12_4,
	CSFR12_5,
	CSFR12_6,
	CSFR12_7,
	CSFR12_8,
	CSFR12_9,
	CSFR12_10,
	CSFR12_11,
	CSFR12,
	CSFR13_U,
	CSFR13_L,
	CSFR14_U,
	CSFR14_L,
	CSFR15_U_0,
	CSFR15_L_0,
	CSFR15_U_1,
	CSFR15_L_1,
	CSFR15_U_2,
	CSFR15_L_2,
	CSFR15_U_3,
	CSFR15_L_3,
	CSFR15_U_4,
	CSFR15_L_4,
	CSFR15_U_5,
	CSFR15_L_5,
	CSFR15_U_6,
	CSFR15_L_6,
	CSFR15_U_7,
	CSFR15_L_7,
	CSFR15_U_8,
	CSFR15_L_8,
	CSFR15_U_9,
	CSFR15_L_9,
	CSFR15_U_10,
	CSFR15_L_10,
	CSFR15_U_11,
	CSFR15_L_11,
	CSFR15_U_12,
	CSFR15_L_12,
	CSFR15_U_13,
	CSFR15_L_13,
	CSFR15_U_14,
	CSFR15_L_14,
	CSFR15_U_15,
	CSFR15_L_15,
	CSFR15_U_16,
	CSFR15_L_16,
	CSFR15_U_17,
	CSFR15_L_17,
	CSFR15_U_18,
	CSFR15_L_18,
	CSFR15_U_19,
	CSFR15_L_19,
	CSFR15,
	CSFR16_0,
	CSFR16_1,
	CSFR16_2,
	CSFR16,
	CSFR20,
	CSFR21
};

static const u32 espada_gnetsetting_autoresponse_reg[] = {
	CSFR30,
	CSFR31,
	CSFR40,
	CSFR41,
};

enum espada_gnetsetting_gberam_offset {
	GBE_RAM_TIME		= 0x00000,
	GBE_RAM_SIZE		= 0x00004,
	GBE_RAM_SUSPEND		= 0x00100,
	GBE_RAM_SEND_DATA	= 0x18000,
};

static inline u32 espada_gnetsetting_read(void __iomem *baseaddr,
					  unsigned int reg)
{
	return ioread32(baseaddr + reg);
}

static inline void espada_gnetsetting_write(void __iomem *baseaddr,
					    u32 data, unsigned int reg)
{
	iowrite32(data, baseaddr + reg);
}

#define ESPADA_GNETSETTING_FILTERING_REG_OFFSET		0
#define ESPADA_GNETSETTING_AUTORESPONSE_REG_OFFSET (CSFR30 - CSFR00)

#endif	/* #ifndef __ESPADA_GNETSETTING_H__ */

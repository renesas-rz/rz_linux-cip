// SPDX-License-Identifier: GPL-2.0
/*
 * R8ARZV2M processor support - PFC hardware block.
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include "core.h"
#include "pfc.h"

#define P0_PF0	FN(GPIO0_00)	FN(NADAT0)	FN(MMDAT0)	F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF1	FN(GPIO0_01)	FN(NADAT1)	FN(MMDAT1)	F_(0)   	F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF2	FN(GPIO0_02)	FN(NADAT2)	FN(MMDAT2)	F_(0)   	F_(0)   F_(0)		F_(0)		F_(0)
#define P0_PF3	FN(GPIO0_03)	FN(NADAT3)	FN(MMDAT3)	F_(0)   	F_(0)   F_(0)		F_(0)		F_(0)
#define P0_PF4	FN(GPIO0_04)	FN(NADAT4)	FN(MMDAT4)	F_(0)   	F_(0)   F_(0)		F_(0)		F_(0)
#define P0_PF5	FN(GPIO0_05)	FN(NADAT5)	FN(MMDAT5)	F_(0)   	F_(0)   F_(0)		F_(0)		F_(0)
#define P0_PF6	FN(GPIO0_06)	FN(NADAT6)	FN(MMDAT6)	F_(0)   	F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF7	FN(GPIO0_07)	FN(NADAT7)	FN(MMDAT7)	F_(0)   	F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF8	FN(GPIO0_08)	FN(NACEN)	FN(MMDATCMD)	F_(0)   	F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF9	FN(GPIO0_09)	FN(NAREN)	FN(MMDATCLK)	F_(0)   	F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF10	FN(GPIO0_10)	FN(NAWEN)	F_(0)		F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF11	FN(GPIO0_11)	FN(NACLE)	F_(0)		F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF12	FN(GPIO0_12)	FN(NAALE)	F_(0)		F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P0_PF13	FN(GPIO0_13)	FN(NARBN)	F_(0)		F_(0)		F_(0)	F_(0)		F_(0)		F_(0)

#define P1_PF0	FN(GPIO01_00)	FN(PWM0)	FN(INEXINT8)	F_(0)		F_(0)	FN(DRPAEXT0_01)	FN(DRPBEXT0_01)	FN(RESIG8_01)
#define P1_PF1	FN(GPIO01_01)	FN(PWM1)	FN(INEXINT9)	F_(0)		F_(0)	FN(DRPAEXT1_01)	FN(DRPBEXT1_01)	FN(RESIG9_01)
#define P1_PF2	FN(GPIO01_02)	FN(PWM2)	FN(INEXINT10)	F_(0)		F_(0)	FN(DRPAEXT2_01)	FN(DRPBEXT2_01)	FN(RESIG10_01)
#define P1_PF3	FN(GPIO01_03)	FN(PWM3)	FN(INEXINT11)	F_(0)		F_(0)	FN(DRPAEXT3_01)	FN(DRPBEXT3_01)	FN(RESIG11_01)
#define P1_PF4	FN(GPIO01_04)	FN(PWM4)	FN(INEXINT12)	F_(0)		F_(0)	F_(0)		F_(0)		FN(SDTCS1)
#define P1_PF5	FN(GPIO01_05)	FN(PWM5)	FN(INEXINT13)	FN(GFPLS0)	F_(0)	F_(0)		F_(0)		FN(SDTCS2)
#define P1_PF6	FN(GPIO01_06)	FN(PWM6)	FN(INEXINT14)	FN(GMCLK0_01_06)	F_(0)	F_(0)		F_(0)		FN(SDTCS3)
#define P1_PF7	FN(GPIO01_07)	FN(PWM7)	FN(INEXINT15)	FN(GMCLK1_01_07)	F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF8	FN(GPIO01_08)	FN(PWM8)	FN(INEXINT16)	F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF9	FN(GPIO01_09)	FN(PWM9)	FN(INEXINT17)	F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF10	FN(GPIO01_10)	FN(PWM10)	FN(INEXINT18)	F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF11	FN(GPIO01_11)	FN(PWM11)	FN(INEXINT19)	F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF12	FN(GPIO01_12)	FN(PWM12)	FN(INEXINT20)	F_(0)		F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF13	FN(GPIO01_13)	FN(PWM13)	FN(INEXINT21)	FN(GFPLS1)	F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF14	FN(GPIO01_14)	FN(PWM14)	FN(INEXINT22)	FN(GMCLK0_01_14)	F_(0)	F_(0)		F_(0)		F_(0)
#define P1_PF15	FN(GPIO01_15)	FN(PWM15)	FN(INEXINT23)	FN(GMCLK1_01_15)	F_(0)	F_(0)		F_(0)		F_(0)

#define P2_PF0	FN(GPIO02_00)	F_(0)		FN(INEXINT0)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF1	FN(GPIO02_01)	F_(0)		FN(INEXINT1)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF2	FN(GPIO02_02)	F_(0)		FN(INEXINT2)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF3	FN(GPIO02_03)	F_(0)		FN(INEXINT3)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF4	FN(GPIO02_04)	F_(0)		FN(INEXINT4)	FN(JGPLSA0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF5	FN(GPIO02_05)	F_(0)		FN(INEXINT5)	FN(JGPLSB0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF6	FN(GPIO02_06)	F_(0)		FN(INEXINT6)	FN(JGPLSA1)	F_(0)	F_(0)	F_(0)	F_(0)
#define P2_PF7	FN(GPIO02_07)	F_(0)		FN(INEXINT7)	FN(JGPLSB1)	F_(0)	F_(0)	F_(0)	F_(0)

#define P3_PF0	FN(GPIO03_00)	FN(CSTXD0_03_00)	FN(UATX0)		FN(CSRXD0_03_00)	F_(0)		F_(0)		F_(0)		F_(0)
#define P3_PF1	FN(GPIO03_01)	FN(CSRXD0_03_01)	FN(UARX0)		F_(0)			F_(0)		F_(0)		F_(0)		F_(0)
#define P3_PF2	FN(GPIO03_02)	FN(CSSCLK0)		FN(UACTS0N)		F_(0)			F_(0)		F_(0)		F_(0)		F_(0)
#define P3_PF3	FN(GPIO03_03)	FN(CSCS0)		FN(UARTS0N)		F_(0)			F_(0)		F_(0)		F_(0)		F_(0)
#define P3_PF4	FN(GPIO03_04)	FN(CSTXD1_03_04)	FN(UATX1)		FN(CSRXD1_03_04)	F_(0)		F_(0)		F_(0)		F_(0)
#define P3_PF5	FN(GPIO03_05)	FN(CSRXD1_03_05)	FN(UARX1)		F_(0)			F_(0)		F_(0)		F_(0)		F_(0)
#define P3_PF6	FN(GPIO03_06)	FN(CSSCLK1)		FN(UACTS1N)		F_(0)			FN(TRDAT15_03)	F_(0)		F_(0)		F_(0)
#define P3_PF7	FN(GPIO03_07)	FN(CSCS1)		FN(UARTS1N)		F_(0)			FN(TRDAT14_03)	F_(0)		F_(0)		F_(0)
#define P3_PF8	FN(GPIO03_08)	FN(CSTXD2_03_08)	FN(I2SDA2)		FN(CSRXD2_03_08)	FN(TRDAT13_03)	FN(DRPAEXT4_03)	FN(DRPBEXT4_03)	FN(RESIG12_03)
#define P3_PF9	FN(GPIO03_09)	FN(CSRXD2_03_09)	FN(I2SCL2)		F_(0)			FN(TRDAT12_03)	FN(DRPAEXT5_03)	FN(DRPBEXT5_03)	FN(RESIG13_03)
#define P3_PF10	FN(GPIO03_10)	FN(CSSCLK2)		FN(I2SDA3)		F_(0)			FN(TRDAT11_03)	FN(DRPAEXT6_03)	FN(DRPBEXT6_03)	FN(RESIG14_03)
#define P3_PF11	FN(GPIO03_11)	FN(CSCS2)		FN(I2SCL3)		F_(0)			FN(TRDAT10_03)	FN(DRPAEXT7_03)	FN(DRPBEXT7_03)	FN(RESIG15_03)
#define P3_PF12	FN(GPIO03_12)	FN(CSTXD3_03_12)	FN(CSRXD3_03_12)	F_(0)			FN(TRDAT9_03)	F_(0)		F_(0)		FN(SDTTXD)
#define P3_PF13	FN(GPIO03_13)	FN(CSRXD3_03_13)	F_(0)			F_(0)			FN(TRDAT8_03)	F_(0)		F_(0)		FN(SDTRXD)
#define P3_PF14	FN(GPIO03_14)	FN(CSSCLK3)		F_(0)			F_(0)			FN(TRDAT7_03)	F_(0)		F_(0)		FN(SDTSCLK)
#define P3_PF15	FN(GPIO03_15)	FN(CSCS3)		F_(0)			F_(0)			FN(TRDAT6_03)	F_(0)		F_(0)		FN(SDTCS0)

#define P4_PF0	FN(GPIO04_00)	FN(CSTXD4_04_00)	FN(CSRXD4_04_00)	F_(0)	FN(TRDAT5_04)	F_(0)		F_(0)		F_(0)
#define P4_PF1	FN(GPIO04_01)	FN(CSRXD4_04_01)	F_(0)			F_(0)	FN(TRDAT4_04)	F_(0)		F_(0)		F_(0)
#define P4_PF2	FN(GPIO04_02)	FN(CSSCLK4)		F_(0)			F_(0)	FN(TRDAT3_04)	F_(0)		F_(0)		F_(0)
#define P4_PF3	FN(GPIO04_03)	FN(CSCS4)		F_(0)			F_(0)	FN(TRDAT2_04)	F_(0)		F_(0)		F_(0)
#define P4_PF4	FN(GPIO04_04)	FN(CSTXD5_04_04)	FN(CSRXD5_04_04)	F_(0)	FN(TRDAT0_04)	F_(0)		F_(0)		F_(0)
#define P4_PF5	FN(GPIO04_05)	FN(CSRXD5_04_05)	F_(0)			F_(0)	FN(TRDAT1_04)	F_(0)		F_(0)		F_(0)
#define P4_PF6	FN(GPIO04_06)	FN(CSSCLK5)		F_(0)			F_(0)	FN(TRCLK_04)	F_(0)		F_(0)		F_(0)
#define P4_PF7	FN(GPIO04_07)	FN(CSCS5)		F_(0)			F_(0)	FN(TRCTL_04)	F_(0)		F_(0)		F_(0)

#define P5_PF0	FN(GPIO05_00)	F_(0)		FN(I2SDA0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P5_PF1	FN(GPIO05_01)	F_(0)		FN(I2SCL0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P5_PF2	FN(GPIO05_02)	F_(0)		FN(I2SDA1)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P5_PF3	FN(GPIO05_03)	F_(0)		FN(I2SCL1)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P6_PF0	FN(GPIO06_00)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT8_06)		FN(DRPBEXT8_06)		FN(RESIG0_06)
#define P6_PF1	FN(GPIO06_01)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT9_06)		FN(DRPBEXT9_06)		FN(RESIG1_06)
#define P6_PF2	FN(GPIO06_02)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT10_06)	FN(DRPBEXT10_06)	FN(RESIG2_06)
#define P6_PF3	FN(GPIO06_03)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT11_06)	FN(DRPBEXT11_06)	FN(RESIG3_06)
#define P6_PF4	FN(GPIO06_04)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT12_06)	FN(DRPBEXT12_06)	FN(RESIG4_06)
#define P6_PF5	FN(GPIO06_05)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT13_06)	FN(DRPBEXT13_06)	FN(RESIG5_06)
#define P6_PF6	FN(GPIO06_06)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT14_06)	FN(DRPBEXT14_06)	FN(RESIG6_06)
#define P6_PF7	FN(GPIO06_07)	F_(0)	F_(0)	F_(0)	F_(0)	FN(DRPAEXT15_06)	FN(DRPBEXT15_06)	FN(RESIG7_06)
#define P6_PF8	FN(GPIO06_08)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)					FN(RESIG8_06)
#define P6_PF9	FN(GPIO06_09)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)					FN(RESIG9_06)
#define P6_PF10	FN(GPIO06_10)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)					FN(RESIG10_06)
#define P6_PF11	FN(GPIO06_11)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)					FN(RESIG11_06)

#define P7_PF0	FN(GPIO07_00)	FN(AULRCK)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P7_PF1	FN(GPIO07_01)	FN(AUBICK)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P7_PF2	FN(GPIO07_02)	FN(AUDI)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P7_PF3	FN(GPIO07_03)	FN(AUDO)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P7_PF4	FN(GPIO07_04)	FN(AUMCLK)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P7_PF5	FN(GPIO07_05)	FN(AUPLLCLK)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P8_PF0	FN(GPIO08_00)	FN(SD0CMD)	FN(PCDEBUGP0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF1	FN(GPIO08_01)	FN(SD0CLK)	FN(PCDEBUGP1)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF2	FN(GPIO08_02)	FN(SD0DAT0)	FN(PCDEBUGP2)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF3	FN(GPIO08_03)	FN(SD0DAT1)	FN(PCDEBUGP3)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF4	FN(GPIO08_04)	FN(SD0DAT2)	FN(PCDEBUGP4)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF5	FN(GPIO08_05)	FN(SD0DAT3)	FN(PCDEBUGP5)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF6	FN(GPIO08_06)	FN(SD0WP)	FN(PCDEBUGP6)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P8_PF7	FN(GPIO08_07)	FN(SD0CD)	FN(PCDEBUGP7)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P9_PF0	FN(GPIO09_00)	FN(SD1CMD)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF1	FN(GPIO09_01)	FN(SD1CLK)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF2	FN(GPIO09_02)	FN(SD1DAT0)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF3	FN(GPIO09_03)	FN(SD1DAT1)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF4	FN(GPIO09_04)	FN(SD1DAT2)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF5	FN(GPIO09_05)	FN(SD1DAT3)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF6	FN(GPIO09_06)	FN(SD1WP)	FN(INEXINT24)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P9_PF7	FN(GPIO09_07)	FN(SD1CD)	FN(INEXINT25)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P10_PF0	FN(GPIO10_00)	FN(IM0VS)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF1	FN(GPIO10_01)	FN(IM0HS)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF2	FN(GPIO10_02)	FN(IM0CS)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF3	FN(GPIO10_03)	FN(IM0TXD)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF4	FN(GPIO10_04)	FN(IM0RXD)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF5	FN(GPIO10_05)	FN(IM0SCLK)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF6	FN(GPIO10_06)	FN(IM0SIG0)	FN(INEXINT26)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF7	FN(GPIO10_07)	FN(IM0SIG1)	FN(INEXINT27)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P10_PF8	FN(GPIO10_08)	FN(IM0SIG2)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P11_PF0	FN(GPIO11_00)	FN(IM1VS)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF1	FN(GPIO11_01)	FN(IM1HS)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF2	FN(GPIO11_02)	FN(IM1CS)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF3	FN(GPIO11_03)	FN(IM1TXD)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF4	FN(GPIO11_04)	FN(IM1RXD)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF5	FN(GPIO11_05)	FN(IM1SCLK)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF6	FN(GPIO11_06)	FN(IM1SIG0)	FN(INEXINT28)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF7	FN(GPIO11_07)	FN(IM1SIG1)	FN(INEXINT29)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P11_PF8	FN(GPIO11_08)	FN(IM1SIG2)	F_(0)		F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P12_PF0	FN(GPIO12_00)	FN(IMSHUT0)	FN(INEXINT30)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P12_PF1	FN(GPIO12_01)	FN(IMSHUT1)	FN(INEXINT31)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P12_PF2	FN(GPIO12_02)	FN(IMSTSIG0)	FN(INEXINT32)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P12_PF3	FN(GPIO12_03)	FN(IMSTSIG1)	FN(INEXINT33)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P13_PF0 FN(GPIO13_00)	F_(0)	F_(0)		FN(MTDRV0)	FN(LVRXTMO_DAT0)	F_(0)	F_(0)	F_(0)
#define P13_PF1 FN(GPIO13_01)	F_(0)	F_(0)		FN(MTDRV1)	FN(LVRXTMO_DAT1)	F_(0)	F_(0)	F_(0)
#define P13_PF2 FN(GPIO13_02)	F_(0)	F_(0)		FN(MTDRV2)	FN(LVRXTMO_DAT2)	F_(0)	F_(0)	F_(0)
#define P13_PF3 FN(GPIO13_03)	F_(0)	F_(0)		FN(MTDRV3)	FN(LVRXTMO_DAT3)	F_(0)	F_(0)	F_(0)
#define P13_PF4 FN(GPIO13_04)	F_(0)	F_(0)		FN(MTDRV4)	FN(LVRXTMO_DAT4)	F_(0)	F_(0)	F_(0)
#define P13_PF5 FN(GPIO13_05)	F_(0)	F_(0)		FN(MTDRV5)	FN(LVRXTMO_DAT5)	F_(0)	F_(0)	F_(0)
#define P13_PF6 FN(GPIO13_06)	F_(0)	F_(0)		FN(MTDRV6)	FN(LVRXTMO_DAT6)	F_(0)	F_(0)	F_(0)
#define P13_PF7 FN(GPIO13_07)	F_(0)	F_(0)		FN(MTDRV7)	FN(LVRXTMO_DAT7)	F_(0)	F_(0)	F_(0)
#define P13_PF8 FN(GPIO13_08)	F_(0)	F_(0)		FN(MTDCPLS0)	FN(LVRXTMO_DAT8)	F_(0)	F_(0)	F_(0)
#define P13_PF9 FN(GPIO13_09)	F_(0)	FN(INEXINT34)	FN(MTDCPLS1)	FN(LVRXTMO_DAT9)	F_(0)	F_(0)	F_(0)
#define P13_PF10 FN(GPIO13_10)	F_(0)	FN(INEXINT35)	FN(MTDCPLS2)	FN(LVRXTMO_DAT10)	F_(0)	F_(0)	F_(0)
#define P13_PF11 FN(GPIO13_11)	F_(0)	FN(INEXINT36)	FN(MTDCPLS3)	FN(LVRXTMO_DAT11)	F_(0)	F_(0)	F_(0)

#define P14_PF0	FN(GPIO14_00)	F_(0)	F_(0)		FN(MTCS0)	FN(LVRXTMO_DAT12)	F_(0)	F_(0)	F_(0)
#define P14_PF1	FN(GPIO14_01)	F_(0)	F_(0)		FN(MTTXD0)	FN(LVRXTMO_DAT13)	F_(0)	F_(0)	F_(0)
#define P14_PF2	FN(GPIO14_02)	F_(0)	FN(INEXINT37)	FN(MTRXD0)	FN(LVRXTMO_DAT14)	F_(0)	F_(0)	F_(0)
#define P14_PF3	FN(GPIO14_03)	F_(0)	F_(0)		FN(MTSCLK0)	FN(LVRXTMO_DAT15)	F_(0)	F_(0)	F_(0)
#define P14_PF4	FN(GPIO14_04)	F_(0)	F_(0)		FN(MTCS1)	F_(0)	F_(0)	F_(0)	F_(0)
#define P14_PF5	FN(GPIO14_05)	F_(0)	F_(0)		FN(MTTXD1)	F_(0)	F_(0)	F_(0)	F_(0)
#define P14_PF6	FN(GPIO14_06)	F_(0)	FN(INEXINT38)	FN(MTRXD1)	F_(0)	F_(0)	F_(0)	F_(0)
#define P14_PF7	FN(GPIO14_07)	F_(0)	F_(0)		FN(MTSCLK1)	F_(0)	F_(0)	F_(0)	F_(0)

#define P15_PF0	FN(GPIO15_00)	FN(GETXC)	FN(LDDAT0)	F_(0)	FN(TRCLK_15)	FN(DRPAEXT1_15)		FN(DRPBEXT1_15)		FN(RESIG9_15)
#define P15_PF1	FN(GPIO15_01)	FN(GETXEN)	FN(LDDAT1)	F_(0)	FN(TRCTL_15)	FN(DRPAEXT2_15)		FN(DRPBEXT2_15)		FN(RESIG10_15)
#define P15_PF2	FN(GPIO15_02)	FN(GETXER)	FN(LDDAT2)	F_(0)	F_(0)		FN(DRPAEXT3_15)		FN(DRPBEXT3_15)		FN(RESIG11_15)
#define P15_PF3	FN(GPIO15_03)	FN(GETXD0)	FN(LDDAT3)	F_(0)	F_(0)		FN(DRPAEXT4_15)		FN(DRPBEXT4_15)		FN(RESIG12_15)
#define P15_PF4	FN(GPIO15_04)	FN(GETXD1)	FN(LDDAT4)	F_(0)	F_(0)		FN(DRPAEXT5_15)		FN(DRPBEXT5_15)		FN(RESIG13_15)
#define P15_PF5	FN(GPIO15_05)	FN(GETXD2)	FN(LDDAT5)	F_(0)	F_(0)		FN(DRPAEXT6_15)		FN(DRPBEXT6_15)		FN(RESIG14_15)
#define P15_PF6	FN(GPIO15_06)	FN(GETXD3)	FN(LDDAT6)	F_(0)	F_(0)		FN(DRPAEXT7_15)		FN(DRPBEXT7_15)		FN(RESIG15_15)
#define P15_PF7	FN(GPIO15_07)	FN(GETXD4)	FN(LDDAT7)	F_(0)	F_(0)		FN(DRPAEXT8_15)		FN(DRPBEXT8_15)		F_(0)
#define P15_PF8	FN(GPIO15_08)	FN(GETXD5)	FN(LDDAT8)	F_(0)	FN(TRDAT3_15)	FN(DRPAEXT9_15)		FN(DRPBEXT9_15)		F_(0)
#define P15_PF9	FN(GPIO15_09)	FN(GETXD6)	FN(LDDAT9)	F_(0)	FN(TRDAT2_15)	FN(DRPAEXT10_15)	FN(DRPBEXT10_15)	F_(0)
#define P15_PF10 FN(GPIO15_10)	FN(GETXD7)	FN(LDDAT10)	F_(0)	FN(TRDAT1_15)	FN(DRPAEXT11_15)	FN(DRPBEXT11_15)	F_(0)
#define P15_PF11 FN(GPIO15_11)	FN(GERXC)	FN(LDDAT11)	F_(0)	FN(TRDAT0_15)	FN(DRPAEXT12_15)	FN(DRPBEXT12_15)	F_(0)
#define P15_PF12 FN(GPIO15_12)	FN(GERXDV)	FN(LDDAT12)	F_(0)	FN(TRDAT4_15)	FN(DRPAEXT13_15)	FN(DRPBEXT13_15)	F_(0)
#define P15_PF13 FN(GPIO15_13)	FN(GERXER)	FN(LDDAT13)	F_(0)	FN(TRDAT5_15)	FN(DRPAEXT14_15)	FN(DRPBEXT14_15)	F_(0)
#define P15_PF14 FN(GPIO15_14)	FN(GERXD0)	FN(LDDAT14)	F_(0)	FN(TRDAT6_15)	FN(DRPAEXT15_15)	FN(DRPBEXT15_15)	F_(0)
#define P15_PF15 FN(GPIO15_15)	FN(GERXD1)	FN(LDDAT15)	F_(0)	FN(TRDAT7_15)	FN(DRPAEXT16_15)	FN(DRPBEXT16_15)	FN(RESIG16_15)

#define P16_PF0	FN(GPIO16_00)	FN(GERXD2)	FN(LDDAT16)	FN(MTDRV8)	FN(TRDAT8_16)	FN(DRPAEXT17_16)	FN(DRPBEXT17_16)	FN(RESIG17_16)
#define P16_PF1	FN(GPIO16_01)	FN(GERXD3)	FN(LDDAT17)	FN(MTDRV9)	FN(TRDAT9_16)	FN(DRPAEXT18_16)	FN(DRPBEXT18_16)	FN(RESIG18_16)
#define P16_PF2	FN(GPIO16_02)	FN(GERXD4)	FN(LDDAT18)	FN(MTDRV10)	FN(TRDAT10_16)	FN(DRPAEXT19_16)	FN(DRPBEXT19_16)	FN(RESIG19_16)
#define P16_PF3	FN(GPIO16_03)	FN(GERXD5)	FN(LDDAT19)	FN(MTDRV11)	FN(TRDAT11_16)	FN(DRPAEXT20_16)	FN(DRPBEXT20_16)	FN(RESIG20_16)
#define P16_PF4	FN(GPIO16_04)	FN(GERXD6)	FN(LDDAT20)	FN(MTDRV12)	FN(TRDAT12_16)	FN(DRPAEXT21_16)	FN(DRPBEXT21_16)	FN(RESIG21_16)
#define P16_PF5	FN(GPIO16_05)	FN(GERXD7)	FN(LDDAT21)	FN(MTDRV13)	FN(TRDAT13_16)	FN(DRPAEXT22_16)	FN(DRPBEXT22_16)	FN(RESIG22_16)
#define P16_PF6	FN(GPIO16_06)	FN(GECRS)	FN(LDDAT22)	FN(MTDRV14)	FN(TRDAT14_16)	FN(DRPAEXT23_16)	FN(DRPBEXT23_16)	FN(RESIG23_16)
#define P16_PF7	FN(GPIO16_07)	FN(GECOL)	FN(LDDAT23)	FN(MTDRV15)	FN(TRDAT15_16)	FN(DRPAEXT24_16)	FN(DRPBEXT24_16)	FN(RESIG24_16)
#define P16_PF8	FN(GPIO16_08)	FN(GEMDC)	FN(LDCLK)	F_(0)		F_(0)		FN(DRPAEXT25_16)	FN(DRPBEXT25_16)	FN(RESIG25_16)
#define P16_PF9	FN(GPIO16_09)	FN(GEMDIO)	FN(LDHS)	F_(0)		F_(0)		FN(DRPAEXT26_16)	FN(DRPBEXT26_16)	FN(RESIG26_16)
#define P16_PF10 FN(GPIO16_10)	FN(GEGTXCLK)	FN(LDCS)	F_(0)		F_(0)		FN(DRPAEXT27_16)	FN(DRPBEXT27_16)	FN(RESIG27_16)
#define P16_PF11 FN(GPIO16_11)	FN(GELINK)	FN(LDDE)	F_(0)		F_(0)		FN(DRPAEXT28_16)	FN(DRPBEXT28_16)	FN(RESIG28_16)
#define P16_PF12 FN(GPIO16_12)	FN(GEINT)	FN(LDVS)	F_(0)		F_(0)		FN(DRPAEXT29_16)	FN(DRPBEXT29_16)	FN(RESIG29_16)
#define P16_PF13 FN(GPIO16_13)	FN(GECLK)	FN(LDTXD)	F_(0)		F_(0)		FN(DRPAEXT30_16)	FN(DRPBEXT30_16)	FN(RESIG30_16)

#define P17_PF0	FN(GPIO17_00)	FN(GEPPS)	FN(LDSCLK)	F_(0)		F_(0)		FN(DRPAEXT31_17)	FN(DRPBEXT31_17)	FN(RESIG31_17)

#define P20_PF0	FN(GPIO20_00)	FN(HDSCL)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P20_PF1	FN(GPIO20_01)	FN(HDSDA)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)
#define P20_PF2	FN(GPIO20_02)	FN(HDHPD)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define P21_PF0	FN(GPIO21_00)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)	F_(0)

#define PEX0	FN(NAWPN)	FN(IM0CLK)	FN(IM1CLK)	FN(DETCK)	FN(DETDI)	FN(DETDO)	FN(DETMO)	FN(DETMS) \
		FN(DETRSTN)	FN(DESRSTN)	FN(RETEST0)	FN(RETEST1)	FN(PCCLKREQB)	FN(PCRSTOUTB)	F_(0)		FN(USPWEN) \
		FN(USOVC)	F_(0)

#define PEX1	FN(MD0)	FN(MD1)	FN(MD2)	FN(MD3)	FN(MD4)	FN(MD5)	FN(MD6)	FN(MD7)

enum {
#define FN(x)	x,
#define F_(x)
	PIN_NONE,
	/* Port 0 enum*/
	P0_PF0	P0_PF1	P0_PF2	P0_PF3	P0_PF4	P0_PF5	P0_PF6	P0_PF7
	P0_PF8	P0_PF9	P0_PF10	P0_PF11	P0_PF12	P0_PF13

	/* Port 1 enum*/
	P1_PF0	P1_PF1	P1_PF2	P1_PF3	P1_PF4	P1_PF5	P1_PF6	P1_PF7
	P1_PF8	P1_PF9	P1_PF10	P1_PF11	P1_PF12	P1_PF13	P1_PF14	P1_PF15

	/* Port 2 enum*/
	P2_PF0	P2_PF1	P2_PF2	P2_PF3	P2_PF4	P2_PF5	P2_PF6	P2_PF7

	/* Port 3 enum*/
	P3_PF0	P3_PF1	P3_PF2	P3_PF3	P3_PF4	P3_PF5	P3_PF6	P3_PF7
	P3_PF8	P3_PF9	P3_PF10	P3_PF11	P3_PF12	P3_PF13	P3_PF14	P3_PF15

	/* Port 4 enum*/
	P4_PF0	P4_PF1	P4_PF2	P4_PF3	P4_PF4	P4_PF5	P4_PF6	P4_PF7

	/* Port 5 enum*/
	P5_PF0	P5_PF1	P5_PF2	P5_PF3

	/* Port 6 enum*/
	P6_PF0	P6_PF1	P6_PF2	P6_PF3	P6_PF4	P6_PF5	P6_PF6	P6_PF7
	P6_PF8	P6_PF9	P6_PF10	P6_PF11

	/* Port 7 enum*/
	P7_PF0	P7_PF1	P7_PF2	P7_PF3	P7_PF4	P7_PF5

	/* Port 8 enum*/
	P8_PF0	P8_PF1	P8_PF2	P8_PF3	P8_PF4	P8_PF5	P8_PF6	P8_PF7

	/* Port 9 enum*/
	P9_PF0	P9_PF1	P9_PF2	P9_PF3	P9_PF4	P9_PF5	P9_PF6	P9_PF7

	/* Port 10 enum*/
	P10_PF0	P10_PF1	P10_PF2	P10_PF3	P10_PF4	P10_PF5	P10_PF6	P10_PF7
	P10_PF8

	/* Port 11 enum*/
	P11_PF0	P11_PF1	P11_PF2	P11_PF3	P11_PF4	P11_PF5	P11_PF6	P11_PF7
	P11_PF8

	/* Port 12 enum*/
	P12_PF0	P12_PF1	P12_PF2	P12_PF3

	/* Port 13 enum*/
	P13_PF0	P13_PF1	P13_PF2	P13_PF3	P13_PF4	P13_PF5	P13_PF6	P13_PF7
	P13_PF8	P13_PF9	P13_PF10 P13_PF11

	/* Port 14 enum*/
	P14_PF0	P14_PF1	P14_PF2	P14_PF3 P14_PF4	P14_PF5	P14_PF6	P14_PF7
	
	/* Port 15 enum*/
	P15_PF0	P15_PF1	P15_PF2	P15_PF3	P15_PF4	P15_PF5	P15_PF6	P15_PF7
	P15_PF8	P15_PF9	P15_PF10 P15_PF11 P15_PF12 P15_PF13 P15_PF14 P15_PF15

	/* Port 16 enum*/
	P16_PF0	P16_PF1	P16_PF2	P16_PF3	P16_PF4	P16_PF5	P16_PF6	P16_PF7
	P16_PF8	P16_PF9	P16_PF10 P16_PF11 P16_PF12 P16_PF13

	/* Port 17 enum*/
	P17_PF0

	/* Port 20 enum*/
	P20_PF0 P20_PF1 P20_PF2

	/* Port 21 enum*/
	P21_PF0

	/* External port enum*/
	PEX0 PEX1
};

#define DEFAULT_CFG	\
		(RZV2M_PIN_CFG_DISPULL | RZV2M_PIN_CFG_PULL | \
		RZV2M_PIN_CFG_DRV_STR | \
        RZV2M_PIN_CFG_SLEW)

#define CPU_GP_PIN(fn, cfg)		\
		PORT_PF13(fn, 0, DEFAULT_CFG),	\
		PORT_PF15(fn, 1, DEFAULT_CFG),	\
		PORT_PF7(fn, 2, DEFAULT_CFG),	\
		PORT_PF15(fn, 3, DEFAULT_CFG),	\
		PORT_PF7(fn, 4, DEFAULT_CFG),	\
		PORT_PF3(fn, 5, DEFAULT_CFG),	\
		PORT_PF11(fn, 6, DEFAULT_CFG),	\
		PORT_PF5(fn, 7, DEFAULT_CFG),	\
		PORT_PF7(fn, 8, DEFAULT_CFG),	\
		PORT_PF7(fn, 9, DEFAULT_CFG),	\
		PORT_PF8(fn, 10, DEFAULT_CFG),	\
		PORT_PF8(fn, 11, DEFAULT_CFG),	\
		PORT_PF3(fn, 12, DEFAULT_CFG),	\
		PORT_PF11(fn, 13, DEFAULT_CFG),	\
		PORT_PF7(fn, 14, DEFAULT_CFG),	\
		PORT_PF15(fn, 15, DEFAULT_CFG),	\
		PORT_PF13(fn, 16, DEFAULT_CFG),	\
		PORT_PF0(fn, 17, DEFAULT_CFG),	\
		PORT_PF2(fn, 20, RZV2M_PIN_CFG_DRV_STR),	\
		PORT_PF0(fn, 21, RZV2M_PIN_CFG_DRV_STR | RZV2M_PIN_CFG_SLEW)

#define CPU_NO_GP_PIN(fn, cfg)				\
		fn(NAWPN, PORT_NUMS, 0, RZV2M_PIN_CFG_DISPULL | \
                                	RZV2M_PIN_CFG_DRV_STR | \
                                	RZV2M_PIN_CFG_SLEW),	\
		fn(IM0CLK, PORT_NUMS, 1, RZV2M_PIN_CFG_DISPULL | \
                                 	 RZV2M_PIN_CFG_DRV_STR | \
                                 	 RZV2M_PIN_CFG_SLEW),	\
		fn(IM1CLK, PORT_NUMS, 2, RZV2M_PIN_CFG_DISPULL | \
                                 	 RZV2M_PIN_CFG_DRV_STR | \
                                 	 RZV2M_PIN_CFG_SLEW),	\
		fn(DETCK, PORT_NUMS, 3, RZV2M_PIN_CFG_PULLDOWN | \
                                	RZV2M_PIN_CFG_DRV_STR_1 | \
                                	RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(DETDI, PORT_NUMS, 4, RZV2M_PIN_CFG_PULLUP | \
                                	RZV2M_PIN_CFG_DRV_STR_1 | \
                                	RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(DETDO, PORT_NUMS, 5, RZV2M_PIN_CFG_DISPULL | \
                                	RZV2M_PIN_CFG_DRV_STR | \
                                	RZV2M_PIN_CFG_SLEW),	\
		fn(DETMS, PORT_NUMS, 6, RZV2M_PIN_CFG_PULLUP | \
                                	RZV2M_PIN_CFG_DRV_STR | \
                                	RZV2M_PIN_CFG_SLEW),	\
		fn(DETRSTN, PORT_NUMS, 7, RZV2M_PIN_CFG_PULLUP | \
                                  	  RZV2M_PIN_CFG_DRV_STR_1 | \
                                  	  RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(DESRSTN, PORT_NUMS, 8, RZV2M_PIN_CFG_PULLUP | \
                                  	  RZV2M_PIN_CFG_DRV_STR_1 | \
                                  	  RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(RETEST0, PORT_NUMS, 9, RZV2M_PIN_CFG_PULLDOWN | \
                                  	  RZV2M_PIN_CFG_DRV_STR_1 | \
                                  	  RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(RETEST1, PORT_NUMS, 10, RZV2M_PIN_CFG_PULLDOWN | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(PCCLKREQB, PORT_NUMS, 11, RZV2M_PIN_CFG_DISPULL | \
                                     	     RZV2M_PIN_CFG_DRV_STR | \
                                     	     RZV2M_PIN_CFG_SLEW),	\
		fn(PCRSTOUTB, PORT_NUMS, 12, RZV2M_PIN_CFG_DISPULL | \
                                 	     RZV2M_PIN_CFG_DRV_STR | \
                                 	     RZV2M_PIN_CFG_SLEW),	\
		fn(USPWEN, PORT_NUMS, 14, RZV2M_PIN_CFG_DISPULL | \
                              		  RZV2M_PIN_CFG_DRV_STR | \
                              		  RZV2M_PIN_CFG_SLEW),	\
		fn(USOVC, PORT_NUMS, 15, RZV2M_PIN_CFG_DISPULL | \
                                 	 RZV2M_PIN_CFG_DRV_STR_1 | \
                                 	 RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD0, (PORT_NUMS +1), 1, RZV2M_PIN_CFG_DISPULL | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD1, (PORT_NUMS +1), 2, RZV2M_PIN_CFG_DISPULL | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD2, (PORT_NUMS +1), 3, RZV2M_PIN_CFG_DISPULL | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD3, (PORT_NUMS +1), 4, RZV2M_PIN_CFG_DISPULL | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD4, (PORT_NUMS +1), 5, RZV2M_PIN_CFG_DISPULL | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD5, (PORT_NUMS +1), 6, RZV2M_PIN_CFG_PULLUP | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD6, (PORT_NUMS +1), 7, RZV2M_PIN_CFG_PULLUP | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW),	\
		fn(MD7, (PORT_NUMS +1), 8, RZV2M_PIN_CFG_DISPULL | \
                                   	   RZV2M_PIN_CFG_DRV_STR_1 | \
                                   	   RZV2M_PIN_CFG_SLEW_SLOW)

static const struct rzv2m_pin_pinctrl pinctrl_pins[] = {
	CPU_GP_PIN(RZV2M_PIN_GP, DEFAULT_CFG),
	CPU_NO_GP_PIN(RZV2M_PIN_NOGP, 0),
};

static const struct rzv2m_pin_desc gpio00_desc[] = {
	RZV2M_PFC_PIN(0, 0, GPIO0_00),
	RZV2M_PFC_PIN(0, 1, GPIO0_01),
	RZV2M_PFC_PIN(0, 2, GPIO0_02),
	RZV2M_PFC_PIN(0, 3, GPIO0_03),
	RZV2M_PFC_PIN(0, 4, GPIO0_04),
	RZV2M_PFC_PIN(0, 5, GPIO0_05),
	RZV2M_PFC_PIN(0, 6, GPIO0_06),
	RZV2M_PFC_PIN(0, 7, GPIO0_07),
	RZV2M_PFC_PIN(0, 8, GPIO0_08),
	RZV2M_PFC_PIN(0, 9, GPIO0_09),
	RZV2M_PFC_PIN(0, 10, GPIO0_10),
	RZV2M_PFC_PIN(0, 11, GPIO0_11),
	RZV2M_PFC_PIN(0, 12, GPIO0_12),
	RZV2M_PFC_PIN(0, 13, GPIO0_13),
};

static const struct rzv2m_pin_desc gpio01_desc[] = {
	RZV2M_PFC_PIN(1, 0, GPIO01_00),
	RZV2M_PFC_PIN(1, 1, GPIO01_01),
	RZV2M_PFC_PIN(1, 2, GPIO01_02),
	RZV2M_PFC_PIN(1, 3, GPIO01_03),
	RZV2M_PFC_PIN(1, 4, GPIO01_04),
	RZV2M_PFC_PIN(1, 5, GPIO01_05),
	RZV2M_PFC_PIN(1, 6, GPIO01_06),
	RZV2M_PFC_PIN(1, 7, GPIO01_07),
	RZV2M_PFC_PIN(1, 8, GPIO01_08),
	RZV2M_PFC_PIN(1, 9, GPIO01_09),
	RZV2M_PFC_PIN(1, 10, GPIO01_10),
	RZV2M_PFC_PIN(1, 11, GPIO01_11),
	RZV2M_PFC_PIN(1, 12, GPIO01_12),
	RZV2M_PFC_PIN(1, 13, GPIO01_13),
	RZV2M_PFC_PIN(1, 14, GPIO01_14),
	RZV2M_PFC_PIN(1, 15, GPIO01_15),
};

static const struct rzv2m_pin_desc gpio02_desc[] = {
	RZV2M_PFC_PIN(2, 0, GPIO02_00),
	RZV2M_PFC_PIN(2, 1, GPIO02_01),
	RZV2M_PFC_PIN(2, 2, GPIO02_02),
	RZV2M_PFC_PIN(2, 3, GPIO02_03),
	RZV2M_PFC_PIN(2, 4, GPIO02_04),
	RZV2M_PFC_PIN(2, 5, GPIO02_05),
	RZV2M_PFC_PIN(2, 6, GPIO02_06),
	RZV2M_PFC_PIN(2, 7, GPIO02_07),
};

static const struct rzv2m_pin_desc gpio03_desc[] = {
	RZV2M_PFC_PIN(3, 0, GPIO03_00),
	RZV2M_PFC_PIN(3, 1, GPIO03_01),
	RZV2M_PFC_PIN(3, 2, GPIO03_02),
	RZV2M_PFC_PIN(3, 3, GPIO03_03),
	RZV2M_PFC_PIN(3, 4, GPIO03_04),
	RZV2M_PFC_PIN(3, 5, GPIO03_05),
	RZV2M_PFC_PIN(3, 6, GPIO03_06),
	RZV2M_PFC_PIN(3, 7, GPIO03_07),
	RZV2M_PFC_PIN(3, 8, GPIO03_08),
	RZV2M_PFC_PIN(3, 9, GPIO03_09),
	RZV2M_PFC_PIN(3, 10, GPIO03_10),
	RZV2M_PFC_PIN(3, 11, GPIO03_11),
	RZV2M_PFC_PIN(3, 12, GPIO03_12),
	RZV2M_PFC_PIN(3, 13, GPIO03_13),
	RZV2M_PFC_PIN(3, 14, GPIO03_14),
	RZV2M_PFC_PIN(3, 15, GPIO03_15),
};

static const struct rzv2m_pin_desc gpio04_desc[] = {
	RZV2M_PFC_PIN(4, 0, GPIO04_00),
	RZV2M_PFC_PIN(4, 1, GPIO04_01),
	RZV2M_PFC_PIN(4, 2, GPIO04_02),
	RZV2M_PFC_PIN(4, 3, GPIO04_03),
	RZV2M_PFC_PIN(4, 4, GPIO04_04),
	RZV2M_PFC_PIN(4, 5, GPIO04_05),
	RZV2M_PFC_PIN(4, 6, GPIO04_06),
	RZV2M_PFC_PIN(4, 7, GPIO04_07),
};


static const struct rzv2m_pin_desc gpio05_desc[] = {
	RZV2M_PFC_PIN(5, 0, GPIO05_00),
	RZV2M_PFC_PIN(5, 1, GPIO05_01),
	RZV2M_PFC_PIN(5, 2, GPIO05_02),
	RZV2M_PFC_PIN(5, 3, GPIO05_03),
};

static const struct rzv2m_pin_desc gpio06_desc[] = {
	RZV2M_PFC_PIN(6, 0, GPIO06_00),
	RZV2M_PFC_PIN(6, 1, GPIO06_01),
	RZV2M_PFC_PIN(6, 2, GPIO06_02),
	RZV2M_PFC_PIN(6, 3, GPIO06_03),
	RZV2M_PFC_PIN(6, 4, GPIO06_04),
	RZV2M_PFC_PIN(6, 5, GPIO06_05),
	RZV2M_PFC_PIN(6, 6, GPIO06_06),
	RZV2M_PFC_PIN(6, 7, GPIO06_07),
	RZV2M_PFC_PIN(6, 8, GPIO06_08),
	RZV2M_PFC_PIN(6, 9, GPIO06_09),
	RZV2M_PFC_PIN(6, 10, GPIO06_10),
	RZV2M_PFC_PIN(6, 11, GPIO06_11),
};

static const struct rzv2m_pin_desc gpio07_desc[] = {
	RZV2M_PFC_PIN(7, 0, GPIO07_00),
	RZV2M_PFC_PIN(7, 1, GPIO07_01),
	RZV2M_PFC_PIN(7, 2, GPIO07_02),
	RZV2M_PFC_PIN(7, 3, GPIO07_03),
	RZV2M_PFC_PIN(7, 4, GPIO07_04),
	RZV2M_PFC_PIN(7, 5, GPIO07_05),
};

static const struct rzv2m_pin_desc gpio08_desc[] = {
	RZV2M_PFC_PIN(8, 0, GPIO08_00),
	RZV2M_PFC_PIN(8, 1, GPIO08_01),
	RZV2M_PFC_PIN(8, 2, GPIO08_02),
	RZV2M_PFC_PIN(8, 3, GPIO08_03),
	RZV2M_PFC_PIN(8, 4, GPIO08_04),
	RZV2M_PFC_PIN(8, 5, GPIO08_05),
	RZV2M_PFC_PIN(8, 6, GPIO08_06),
	RZV2M_PFC_PIN(8, 7, GPIO08_07),
};

static const struct rzv2m_pin_desc gpio09_desc[] = {
	RZV2M_PFC_PIN(9, 0, GPIO09_00),
	RZV2M_PFC_PIN(9, 1, GPIO09_01),
	RZV2M_PFC_PIN(9, 2, GPIO09_02),
	RZV2M_PFC_PIN(9, 3, GPIO09_03),
	RZV2M_PFC_PIN(9, 4, GPIO09_04),
	RZV2M_PFC_PIN(9, 5, GPIO09_05),
	RZV2M_PFC_PIN(9, 6, GPIO09_06),
	RZV2M_PFC_PIN(9, 7, GPIO09_07),
};

static const struct rzv2m_pin_desc gpio10_desc[] = {
	RZV2M_PFC_PIN(10, 0, GPIO10_00),
	RZV2M_PFC_PIN(10, 1, GPIO10_01),
	RZV2M_PFC_PIN(10, 2, GPIO10_02),
	RZV2M_PFC_PIN(10, 3, GPIO10_03),
	RZV2M_PFC_PIN(10, 4, GPIO10_04),
	RZV2M_PFC_PIN(10, 5, GPIO10_05),
	RZV2M_PFC_PIN(10, 6, GPIO10_06),
	RZV2M_PFC_PIN(10, 7, GPIO10_07),
	RZV2M_PFC_PIN(10, 8, GPIO10_08),
};

static const struct rzv2m_pin_desc gpio11_desc[] = {
	RZV2M_PFC_PIN(11, 0, GPIO11_00),
	RZV2M_PFC_PIN(11, 1, GPIO11_01),
	RZV2M_PFC_PIN(11, 2, GPIO11_02),
	RZV2M_PFC_PIN(11, 3, GPIO11_03),
	RZV2M_PFC_PIN(11, 4, GPIO11_04),
	RZV2M_PFC_PIN(11, 5, GPIO11_05),
	RZV2M_PFC_PIN(11, 6, GPIO11_06),
	RZV2M_PFC_PIN(11, 7, GPIO11_07),
	RZV2M_PFC_PIN(11, 8, GPIO11_08),
};

static const struct rzv2m_pin_desc gpio12_desc[] = {
	RZV2M_PFC_PIN(12, 0, GPIO12_00),
	RZV2M_PFC_PIN(12, 1, GPIO12_01),
	RZV2M_PFC_PIN(12, 2, GPIO12_02),
	RZV2M_PFC_PIN(12, 3, GPIO12_03),
};

static const struct rzv2m_pin_desc gpio13_desc[] = {
	RZV2M_PFC_PIN(13, 0, GPIO13_00),
	RZV2M_PFC_PIN(13, 1, GPIO13_01),
	RZV2M_PFC_PIN(13, 2, GPIO13_02),
	RZV2M_PFC_PIN(13, 3, GPIO13_03),
	RZV2M_PFC_PIN(13, 4, GPIO13_04),
	RZV2M_PFC_PIN(13, 5, GPIO13_05),
	RZV2M_PFC_PIN(13, 6, GPIO13_06),
	RZV2M_PFC_PIN(13, 7, GPIO13_07),
	RZV2M_PFC_PIN(13, 8, GPIO13_08),
	RZV2M_PFC_PIN(13, 9, GPIO13_09),
	RZV2M_PFC_PIN(13, 10, GPIO13_10),
	RZV2M_PFC_PIN(13, 11, GPIO13_11),
};

static const struct rzv2m_pin_desc gpio14_desc[] = {
	RZV2M_PFC_PIN(14, 0, GPIO14_00),
	RZV2M_PFC_PIN(14, 1, GPIO14_01),
	RZV2M_PFC_PIN(14, 2, GPIO14_02),
	RZV2M_PFC_PIN(14, 3, GPIO14_03),
	RZV2M_PFC_PIN(14, 4, GPIO14_04),
	RZV2M_PFC_PIN(14, 5, GPIO14_05),
	RZV2M_PFC_PIN(14, 6, GPIO14_06),
	RZV2M_PFC_PIN(14, 7, GPIO14_07),
};

static const struct rzv2m_pin_desc gpio15_desc[] = {
	RZV2M_PFC_PIN(15, 0, GPIO15_00),
	RZV2M_PFC_PIN(15, 1, GPIO15_01),
	RZV2M_PFC_PIN(15, 2, GPIO15_02),
	RZV2M_PFC_PIN(15, 3, GPIO15_03),
	RZV2M_PFC_PIN(15, 4, GPIO15_04),
	RZV2M_PFC_PIN(15, 5, GPIO15_05),
	RZV2M_PFC_PIN(15, 6, GPIO15_06),
	RZV2M_PFC_PIN(15, 7, GPIO15_07),
	RZV2M_PFC_PIN(15, 8, GPIO15_08),
	RZV2M_PFC_PIN(15, 9, GPIO15_09),
	RZV2M_PFC_PIN(15, 10, GPIO15_10),
	RZV2M_PFC_PIN(15, 11, GPIO15_11),
	RZV2M_PFC_PIN(15, 12, GPIO15_12),
	RZV2M_PFC_PIN(15, 13, GPIO15_13),
	RZV2M_PFC_PIN(15, 14, GPIO15_14),
	RZV2M_PFC_PIN(15, 15, GPIO15_15),
};

static const struct rzv2m_pin_desc gpio16_desc[] = {
	RZV2M_PFC_PIN(16, 0, GPIO16_00),
	RZV2M_PFC_PIN(16, 1, GPIO16_01),
	RZV2M_PFC_PIN(16, 2, GPIO16_02),
	RZV2M_PFC_PIN(16, 3, GPIO16_03),
	RZV2M_PFC_PIN(16, 4, GPIO16_04),
	RZV2M_PFC_PIN(16, 5, GPIO16_05),
	RZV2M_PFC_PIN(16, 6, GPIO16_06),
	RZV2M_PFC_PIN(16, 7, GPIO16_07),
	RZV2M_PFC_PIN(16, 8, GPIO16_08),
	RZV2M_PFC_PIN(16, 9, GPIO16_09),
	RZV2M_PFC_PIN(16, 10, GPIO16_10),
	RZV2M_PFC_PIN(16, 11, GPIO16_11),
	RZV2M_PFC_PIN(16, 12, GPIO16_12),
	RZV2M_PFC_PIN(16, 13, GPIO16_13),
};

static const struct rzv2m_pin_desc gpio17_desc[] = {
	RZV2M_PFC_PIN(17, 0, GPIO17_00),
};

static const struct rzv2m_pin_desc gpio20_desc[] = {
	RZV2M_PFC_PIN(20, 0, GPIO20_00),
	RZV2M_PFC_PIN(20, 1, GPIO20_01),
	RZV2M_PFC_PIN(20, 2, GPIO20_02),
};

static const struct rzv2m_pin_desc gpio21_desc[] = {
	RZV2M_PFC_PIN(21, 0, GPIO21_00),
};

static const struct rzv2m_pin_desc nand_data1_desc[] = {
	RZV2M_PFC_PIN(0, 0, NADAT0),
};

static const struct rzv2m_pin_desc nand_data4_desc[] = {
	RZV2M_PFC_PIN(0, 0, NADAT0),
	RZV2M_PFC_PIN(0, 1, NADAT1),
	RZV2M_PFC_PIN(0, 2, NADAT2),
	RZV2M_PFC_PIN(0, 3, NADAT3),
};

static const struct rzv2m_pin_desc nand_data8_desc[] = {
	RZV2M_PFC_PIN(0, 0, NADAT0),
	RZV2M_PFC_PIN(0, 1, NADAT1),
	RZV2M_PFC_PIN(0, 2, NADAT2),
	RZV2M_PFC_PIN(0, 3, NADAT3),
	RZV2M_PFC_PIN(0, 4, NADAT4),
	RZV2M_PFC_PIN(0, 5, NADAT5),
	RZV2M_PFC_PIN(0, 6, NADAT6),
	RZV2M_PFC_PIN(0, 7, NADAT7),
};

static const struct rzv2m_pin_desc nand_ctl_desc[] = {
	RZV2M_PFC_PIN(0, 8, NACEN),
	RZV2M_PFC_PIN(0, 9, NAWEN),
	RZV2M_PFC_PIN(0, 10, NAREN),
	RZV2M_PFC_PIN(0, 11, NACLE),
	RZV2M_PFC_PIN(0, 12, NAALE),
	RZV2M_PFC_PIN(0, 13, NARBN),
};

static const struct rzv2m_pin_desc mmc_data1_desc[] = {
	RZV2M_PFC_PIN(0, 0, MMDAT0),
};

static const struct rzv2m_pin_desc mmc_data4_desc[] = {
	RZV2M_PFC_PIN(0, 0, MMDAT0),
	RZV2M_PFC_PIN(0, 1, MMDAT1),
	RZV2M_PFC_PIN(0, 2, MMDAT2),
	RZV2M_PFC_PIN(0, 3, MMDAT3),
};

static const struct rzv2m_pin_desc mmc_data8_desc[] = {
	RZV2M_PFC_PIN(0, 0, MMDAT0),
	RZV2M_PFC_PIN(0, 1, MMDAT1),
	RZV2M_PFC_PIN(0, 2, MMDAT2),
	RZV2M_PFC_PIN(0, 3, MMDAT3),
	RZV2M_PFC_PIN(0, 4, MMDAT4),
	RZV2M_PFC_PIN(0, 5, MMDAT5),
	RZV2M_PFC_PIN(0, 6, MMDAT6),
	RZV2M_PFC_PIN(0, 7, MMDAT7),
};

static const struct rzv2m_pin_desc mmc_ctl_desc[] = {
	RZV2M_PFC_PIN(0, 8, MMDATCMD),
	RZV2M_PFC_PIN(0, 9, MMDATCLK),
};

static const struct rzv2m_pin_desc sd0_ctl_desc[] = {
	RZV2M_PFC_PIN(8, 0, SD0CMD),
	RZV2M_PFC_PIN(8, 1, SD0CLK),
};

static const struct rzv2m_pin_desc sd0_data1_desc[] = {
	RZV2M_PFC_PIN(8, 2, SD0DAT0),
};

static const struct rzv2m_pin_desc sd0_data4_desc[] = {
	RZV2M_PFC_PIN(8, 2, SD0DAT0),
	RZV2M_PFC_PIN(8, 3, SD0DAT1),
	RZV2M_PFC_PIN(8, 4, SD0DAT2),
	RZV2M_PFC_PIN(8, 5, SD0DAT3),
};

static const struct rzv2m_pin_desc sd0_wp_desc[] = {
	RZV2M_PFC_PIN(8, 6, SD0WP),
};

static const struct rzv2m_pin_desc sd0_cd_desc[] = {
	RZV2M_PFC_PIN(8, 7, SD0CD),
};

static const struct rzv2m_pin_desc sd1_ctl_desc[] = {
	RZV2M_PFC_PIN(9, 0, SD1CMD),
	RZV2M_PFC_PIN(9, 1, SD1CLK),
};

static const struct rzv2m_pin_desc sd1_data1_desc[] = {
	RZV2M_PFC_PIN(9, 2, SD1DAT0),
};

static const struct rzv2m_pin_desc sd1_data4_desc[] = {
	RZV2M_PFC_PIN(9, 2, SD1DAT0),
	RZV2M_PFC_PIN(9, 3, SD1DAT1),
	RZV2M_PFC_PIN(9, 4, SD1DAT2),
	RZV2M_PFC_PIN(9, 5, SD1DAT3),
};

static const struct rzv2m_pin_desc sd1_wp_desc[] = {
	RZV2M_PFC_PIN(9, 6, SD1WP),
};

static const struct rzv2m_pin_desc sd1_cd_desc[] = {
	RZV2M_PFC_PIN(9, 7, SD1CD),
};

static const struct rzv2m_pin_desc pwm0_desc[] = {
	RZV2M_PFC_PIN(1, 0, PWM0),
};

static const struct rzv2m_pin_desc pwm1_desc[] = {
	RZV2M_PFC_PIN(1, 1, PWM1),
};

static const struct rzv2m_pin_desc pwm2_desc[] = {
	RZV2M_PFC_PIN(1, 2, PWM2),
};

static const struct rzv2m_pin_desc pwm3_desc[] = {
	RZV2M_PFC_PIN(1, 3, PWM3),
};

static const struct rzv2m_pin_desc pwm4_desc[] = {
	RZV2M_PFC_PIN(1, 4, PWM4),
};

static const struct rzv2m_pin_desc pwm5_desc[] = {
	RZV2M_PFC_PIN(1, 5, PWM5),
};

static const struct rzv2m_pin_desc pwm6_desc[] = {
	RZV2M_PFC_PIN(1, 6, PWM6),
};

static const struct rzv2m_pin_desc pwm7_desc[] = {
	RZV2M_PFC_PIN(1, 7, PWM7),
};

static const struct rzv2m_pin_desc pwm8_desc[] = {
	RZV2M_PFC_PIN(1, 8, PWM8),
};

static const struct rzv2m_pin_desc pwm9_desc[] = {
	RZV2M_PFC_PIN(1, 9, PWM9),
};

static const struct rzv2m_pin_desc pwm10_desc[] = {
	RZV2M_PFC_PIN(1, 10, PWM10),
};

static const struct rzv2m_pin_desc pwm11_desc[] = {
	RZV2M_PFC_PIN(1, 11, PWM11),
};

static const struct rzv2m_pin_desc pwm12_desc[] = {
	RZV2M_PFC_PIN(1, 12, PWM12),
};

static const struct rzv2m_pin_desc pwm13_desc[] = {
	RZV2M_PFC_PIN(1, 13, PWM13),
};

static const struct rzv2m_pin_desc pwm14_desc[] = {
	RZV2M_PFC_PIN(1, 14, PWM14),
};

static const struct rzv2m_pin_desc pwm15_desc[] = {
	RZV2M_PFC_PIN(1, 15, PWM15),
};

static const struct rzv2m_pin_desc irq_ex0_desc[] = {
	RZV2M_PFC_PIN(2, 0, INEXINT0),
};
static const struct rzv2m_pin_desc irq_ex1_desc[] = {
	RZV2M_PFC_PIN(2, 1, INEXINT1),
};
static const struct rzv2m_pin_desc irq_ex2_desc[] = {
	RZV2M_PFC_PIN(2, 2, INEXINT2),
};
static const struct rzv2m_pin_desc irq_ex3_desc[] = {
	RZV2M_PFC_PIN(2, 3, INEXINT3),
};
static const struct rzv2m_pin_desc irq_ex4_desc[] = {
	RZV2M_PFC_PIN(2, 4, INEXINT4),
};
static const struct rzv2m_pin_desc irq_ex5_desc[] = {
	RZV2M_PFC_PIN(2, 5, INEXINT5),
};
static const struct rzv2m_pin_desc irq_ex6_desc[] = {
	RZV2M_PFC_PIN(2, 6, INEXINT6),
};
static const struct rzv2m_pin_desc irq_ex7_desc[] = {
	RZV2M_PFC_PIN(2, 7, INEXINT7),
};
static const struct rzv2m_pin_desc irq_ex8_desc[] = {
	RZV2M_PFC_PIN(1, 0, INEXINT8),
};
static const struct rzv2m_pin_desc irq_ex9_desc[] = {
	RZV2M_PFC_PIN(1, 1, INEXINT9),
};
static const struct rzv2m_pin_desc irq_ex10_desc[] = {
	RZV2M_PFC_PIN(1, 2, INEXINT10),
};
static const struct rzv2m_pin_desc irq_ex11_desc[] = {
	RZV2M_PFC_PIN(1, 3, INEXINT11),
};
static const struct rzv2m_pin_desc irq_ex12_desc[] = {
	RZV2M_PFC_PIN(1, 4, INEXINT12),
};
static const struct rzv2m_pin_desc irq_ex13_desc[] = {
	RZV2M_PFC_PIN(1, 5, INEXINT13),
};
static const struct rzv2m_pin_desc irq_ex14_desc[] = {
	RZV2M_PFC_PIN(1, 6, INEXINT14),
};
static const struct rzv2m_pin_desc irq_ex15_desc[] = {
	RZV2M_PFC_PIN(1, 7, INEXINT15),
};
static const struct rzv2m_pin_desc irq_ex16_desc[] = {
	RZV2M_PFC_PIN(1, 8, INEXINT16),
};
static const struct rzv2m_pin_desc irq_ex17_desc[] = {
	RZV2M_PFC_PIN(1, 9, INEXINT17),
};
static const struct rzv2m_pin_desc irq_ex18_desc[] = {
	RZV2M_PFC_PIN(1, 10, INEXINT18),
};
static const struct rzv2m_pin_desc irq_ex19_desc[] = {
	RZV2M_PFC_PIN(1, 11, INEXINT19),
};
static const struct rzv2m_pin_desc irq_ex20_desc[] = {
	RZV2M_PFC_PIN(1, 12, INEXINT20),
};
static const struct rzv2m_pin_desc irq_ex21_desc[] = {
	RZV2M_PFC_PIN(1, 13, INEXINT21),
};
static const struct rzv2m_pin_desc irq_ex22_desc[] = {
	RZV2M_PFC_PIN(1, 14, INEXINT22),
};
static const struct rzv2m_pin_desc irq_ex23_desc[] = {
	RZV2M_PFC_PIN(1, 15, INEXINT23),
};
static const struct rzv2m_pin_desc irq_ex24_desc[] = {
	RZV2M_PFC_PIN(9, 6, INEXINT24),
};
static const struct rzv2m_pin_desc irq_ex25_desc[] = {
	RZV2M_PFC_PIN(9, 7, INEXINT25),
};
static const struct rzv2m_pin_desc irq_ex26_desc[] = {
	RZV2M_PFC_PIN(10, 6, INEXINT26),
};
static const struct rzv2m_pin_desc irq_ex27_desc[] = {
	RZV2M_PFC_PIN(10, 7, INEXINT27),
};
static const struct rzv2m_pin_desc irq_ex28_desc[] = {
	RZV2M_PFC_PIN(11, 6, INEXINT28),
};
static const struct rzv2m_pin_desc irq_ex29_desc[] = {
	RZV2M_PFC_PIN(11, 7, INEXINT29),
};
static const struct rzv2m_pin_desc irq_ex30_desc[] = {
	RZV2M_PFC_PIN(12, 0, INEXINT30),
};
static const struct rzv2m_pin_desc irq_ex31_desc[] = {
	RZV2M_PFC_PIN(12, 1, INEXINT31),
};
static const struct rzv2m_pin_desc irq_ex32_desc[] = {
	RZV2M_PFC_PIN(12, 2, INEXINT32),
};
static const struct rzv2m_pin_desc irq_ex33_desc[] = {
	RZV2M_PFC_PIN(12, 3, INEXINT33),
};
static const struct rzv2m_pin_desc irq_ex34_desc[] = {
	RZV2M_PFC_PIN(13, 9, INEXINT34),
};
static const struct rzv2m_pin_desc irq_ex35_desc[] = {
	RZV2M_PFC_PIN(13, 10, INEXINT35),
};
static const struct rzv2m_pin_desc irq_ex36_desc[] = {
	RZV2M_PFC_PIN(13, 11, INEXINT36),
};
static const struct rzv2m_pin_desc irq_ex37_desc[] = {
	RZV2M_PFC_PIN(14, 2, INEXINT37),
};
static const struct rzv2m_pin_desc irq_ex38_desc[] = {
	RZV2M_PFC_PIN(14, 6, INEXINT38),
};

static const struct rzv2m_pin_desc uart0_data_desc[] = {
	RZV2M_PFC_PIN(3, 0, UATX0),
	RZV2M_PFC_PIN(3, 1, UARX0),
};
static const struct rzv2m_pin_desc uart0_ctl_desc[] = {
	RZV2M_PFC_PIN(3, 2, UACTS0N),
	RZV2M_PFC_PIN(3, 3, UARTS0N),
};

static const struct rzv2m_pin_desc uart1_data_desc[] = {
	RZV2M_PFC_PIN(3, 4, UATX1),
	RZV2M_PFC_PIN(3, 5, UARX1),
};
static const struct rzv2m_pin_desc uart1_ctl_desc[] = {
	RZV2M_PFC_PIN(3, 6, UACTS1N),
	RZV2M_PFC_PIN(3, 7, UARTS1N),
};

static const struct rzv2m_pin_desc i2c0_desc[] = {
	RZV2M_PFC_PIN(5, 0, I2SDA0),
	RZV2M_PFC_PIN(5, 1, I2SCL0),
};
static const struct rzv2m_pin_desc i2c1_desc[] = {
	RZV2M_PFC_PIN(5, 2, I2SDA1),
	RZV2M_PFC_PIN(5, 3, I2SCL1),
};

static const struct rzv2m_pin_desc i2c2_desc[] = {
	RZV2M_PFC_PIN(3, 8, I2SDA2),
	RZV2M_PFC_PIN(3, 9, I2SCL2),
};
static const struct rzv2m_pin_desc i2c3_desc[] = {
	RZV2M_PFC_PIN(3, 10, I2SDA3),
	RZV2M_PFC_PIN(3, 11, I2SCL3),
};

//csi
static const struct rzv2m_pin_desc csi0rw_desc[] = {
	RZV2M_PFC_PIN(3, 0, CSTXD0_03_00),
	RZV2M_PFC_PIN(3, 1, CSRXD0_03_01),
	RZV2M_PFC_PIN(3, 2, CSSCLK0),
	RZV2M_PFC_PIN(3, 3, CSCS0)
};
static const struct rzv2m_pin_desc csi0r_desc[] = {
	RZV2M_PFC_PIN(3, 0, CSRXD0_03_00),
	RZV2M_PFC_PIN(3, 2, CSSCLK0),
	RZV2M_PFC_PIN(3, 3, CSCS0)
};

static const struct rzv2m_pin_desc csi1rw_desc[] = {
	RZV2M_PFC_PIN(3, 4, CSTXD1_03_04),
	RZV2M_PFC_PIN(3, 5, CSRXD1_03_05),
	RZV2M_PFC_PIN(3, 6, CSSCLK1),
	RZV2M_PFC_PIN(3, 7, CSCS1)
};
static const struct rzv2m_pin_desc csi1r_desc[] = {
	RZV2M_PFC_PIN(3, 4, CSRXD1_03_04),
	RZV2M_PFC_PIN(3, 6, CSSCLK1),
	RZV2M_PFC_PIN(3, 7, CSCS1)
};

static const struct rzv2m_pin_desc csi2rw_desc[] = {
	RZV2M_PFC_PIN(3, 8, CSTXD2_03_08),
	RZV2M_PFC_PIN(3, 9, CSRXD2_03_09),
	RZV2M_PFC_PIN(3, 10, CSSCLK2),
	RZV2M_PFC_PIN(3, 11, CSCS2)
};
static const struct rzv2m_pin_desc csi2r_desc[] = {
	RZV2M_PFC_PIN(3, 8, CSRXD2_03_08),
	RZV2M_PFC_PIN(3, 10, CSSCLK2),
	RZV2M_PFC_PIN(3, 11, CSCS2)
};

static const struct rzv2m_pin_desc csi3rw_desc[] = {
	RZV2M_PFC_PIN(3, 12, CSTXD3_03_12),
	RZV2M_PFC_PIN(3, 13, CSRXD3_03_13),
	RZV2M_PFC_PIN(3, 14, CSSCLK3),
	RZV2M_PFC_PIN(3, 15, CSCS3)
};
static const struct rzv2m_pin_desc csi3r_desc[] = {
	RZV2M_PFC_PIN(3, 12, CSRXD3_03_12),
	RZV2M_PFC_PIN(3, 14, CSSCLK3),
	RZV2M_PFC_PIN(3, 15, CSCS3)
};

static const struct rzv2m_pin_desc csi4rw_desc[] = {
	RZV2M_PFC_PIN(4, 0, CSTXD4_04_00),
	RZV2M_PFC_PIN(4, 1, CSRXD4_04_01),
	RZV2M_PFC_PIN(4, 2, CSSCLK4),
	RZV2M_PFC_PIN(4, 3, CSCS4)
};
static const struct rzv2m_pin_desc csi4r_desc[] = {
	RZV2M_PFC_PIN(4, 0, CSRXD4_04_00),
	RZV2M_PFC_PIN(4, 2, CSSCLK4),
	RZV2M_PFC_PIN(4, 3, CSCS4)
};

static const struct rzv2m_pin_desc csi5rw_desc[] = {
	RZV2M_PFC_PIN(4, 4, CSTXD5_04_04),
	RZV2M_PFC_PIN(4, 5, CSRXD5_04_05),
	RZV2M_PFC_PIN(4, 6, CSSCLK5),
	RZV2M_PFC_PIN(4, 7, CSCS5)
};
static const struct rzv2m_pin_desc csi5r_desc[] = {
	RZV2M_PFC_PIN(4, 4, CSRXD5_04_04),
	RZV2M_PFC_PIN(4, 6, CSSCLK5),
	RZV2M_PFC_PIN(4, 7, CSCS5)
};

//DRPA
static const struct rzv2m_pin_desc drpa0_desc[] = {
	RZV2M_PFC_PIN(1, 0, DRPAEXT0_01),
};
static const struct rzv2m_pin_desc drpa1_desc[] = {
	RZV2M_PFC_PIN(1, 1, DRPAEXT1_01),
};
static const struct rzv2m_pin_desc drpa2_desc[] = {
	RZV2M_PFC_PIN(1, 2, DRPAEXT2_01),
};
static const struct rzv2m_pin_desc drpa3_desc[] = {
	RZV2M_PFC_PIN(1, 3, DRPAEXT3_01),
};
static const struct rzv2m_pin_desc drpa4_desc[] = {
	RZV2M_PFC_PIN(3, 8, DRPAEXT4_03),
};
static const struct rzv2m_pin_desc drpa5_desc[] = {
	RZV2M_PFC_PIN(3, 9, DRPAEXT5_03),
};
static const struct rzv2m_pin_desc drpa6_desc[] = {
	RZV2M_PFC_PIN(3, 10, DRPAEXT6_03),
};
static const struct rzv2m_pin_desc drpa7_desc[] = {
	RZV2M_PFC_PIN(3, 11, DRPAEXT7_03),
};
static const struct rzv2m_pin_desc drpa8_desc[] = {
	RZV2M_PFC_PIN(6, 0, DRPAEXT8_06),
};
static const struct rzv2m_pin_desc drpa9_desc[] = {
	RZV2M_PFC_PIN(6, 1, DRPAEXT9_06),
};
static const struct rzv2m_pin_desc drpa10_desc[] = {
	RZV2M_PFC_PIN(6, 2, DRPAEXT10_06),
};
static const struct rzv2m_pin_desc drpa11_desc[] = {
	RZV2M_PFC_PIN(6, 3, DRPAEXT11_06),
};
static const struct rzv2m_pin_desc drpa12_desc[] = {
	RZV2M_PFC_PIN(6, 4, DRPAEXT12_06),
};
static const struct rzv2m_pin_desc drpa13_desc[] = {
	RZV2M_PFC_PIN(6, 5, DRPAEXT13_06),
};
static const struct rzv2m_pin_desc drpa14_desc[] = {
	RZV2M_PFC_PIN(6, 6, DRPAEXT14_06),
};
static const struct rzv2m_pin_desc drpa15_desc[] = {
	RZV2M_PFC_PIN(6, 7, DRPAEXT15_06),
};

//DRPB
static const struct rzv2m_pin_desc drpb0_desc[] = {
	RZV2M_PFC_PIN(1, 0, DRPBEXT0_01),
};
static const struct rzv2m_pin_desc drpb1_desc[] = {
	RZV2M_PFC_PIN(1, 1, DRPBEXT1_01),
};
static const struct rzv2m_pin_desc drpb2_desc[] = {
	RZV2M_PFC_PIN(1, 2, DRPBEXT2_01),
};
static const struct rzv2m_pin_desc drpb3_desc[] = {
	RZV2M_PFC_PIN(1, 3, DRPBEXT3_01),
};
static const struct rzv2m_pin_desc drpb4_desc[] = {
	RZV2M_PFC_PIN(3, 8, DRPBEXT4_03),
};
static const struct rzv2m_pin_desc drpb5_desc[] = {
	RZV2M_PFC_PIN(3, 9, DRPBEXT5_03),
};
static const struct rzv2m_pin_desc drpb6_desc[] = {
	RZV2M_PFC_PIN(3, 10, DRPBEXT6_03),
};
static const struct rzv2m_pin_desc drpb7_desc[] = {
	RZV2M_PFC_PIN(3, 11, DRPBEXT7_03),
};
static const struct rzv2m_pin_desc drpb8_desc[] = {
	RZV2M_PFC_PIN(6, 0, DRPBEXT8_06),
};
static const struct rzv2m_pin_desc drpb9_desc[] = {
	RZV2M_PFC_PIN(6, 1, DRPBEXT9_06),
};
static const struct rzv2m_pin_desc drpb10_desc[] = {
	RZV2M_PFC_PIN(6, 2, DRPBEXT10_06),
};
static const struct rzv2m_pin_desc drpb11_desc[] = {
	RZV2M_PFC_PIN(6, 3, DRPBEXT11_06),
};
static const struct rzv2m_pin_desc drpb12_desc[] = {
	RZV2M_PFC_PIN(6, 4, DRPBEXT12_06),
};
static const struct rzv2m_pin_desc drpb13_desc[] = {
	RZV2M_PFC_PIN(6, 5, DRPBEXT13_06),
};
static const struct rzv2m_pin_desc drpb14_desc[] = {
	RZV2M_PFC_PIN(6, 6, DRPBEXT14_06),
};
static const struct rzv2m_pin_desc drpb15_desc[] = {
	RZV2M_PFC_PIN(6, 7, DRPBEXT15_06),
};

//drpa_a
static const struct rzv2m_pin_desc drpa1_a_desc[] = {
	RZV2M_PFC_PIN(15, 0, DRPAEXT1_15),
};
static const struct rzv2m_pin_desc drpa2_a_desc[] = {
	RZV2M_PFC_PIN(15, 1, DRPAEXT2_15),
};
static const struct rzv2m_pin_desc drpa3_a_desc[] = {
	RZV2M_PFC_PIN(15, 2, DRPAEXT3_15),
};
static const struct rzv2m_pin_desc drpa4_a_desc[] = {
	RZV2M_PFC_PIN(15, 3, DRPAEXT4_15),
};
static const struct rzv2m_pin_desc drpa5_a_desc[] = {
	RZV2M_PFC_PIN(15, 4, DRPAEXT5_15),
};
static const struct rzv2m_pin_desc drpa6_a_desc[] = {
	RZV2M_PFC_PIN(15, 5, DRPAEXT6_15),
};
static const struct rzv2m_pin_desc drpa7_a_desc[] = {
	RZV2M_PFC_PIN(15, 6, DRPAEXT7_15),
};
static const struct rzv2m_pin_desc drpa8_a_desc[] = {
	RZV2M_PFC_PIN(15, 7, DRPAEXT8_15),
};
static const struct rzv2m_pin_desc drpa9_a_desc[] = {
	RZV2M_PFC_PIN(15, 8, DRPAEXT9_15),
};
static const struct rzv2m_pin_desc drpa10_a_desc[] = {
	RZV2M_PFC_PIN(15, 9, DRPAEXT10_15),
};
static const struct rzv2m_pin_desc drpa11_a_desc[] = {
	RZV2M_PFC_PIN(15, 10, DRPAEXT11_15),
};
static const struct rzv2m_pin_desc drpa12_a_desc[] = {
	RZV2M_PFC_PIN(15, 11, DRPAEXT12_15),
};
static const struct rzv2m_pin_desc drpa13_a_desc[] = {
	RZV2M_PFC_PIN(15, 12, DRPAEXT13_15),
};
static const struct rzv2m_pin_desc drpa14_a_desc[] = {
	RZV2M_PFC_PIN(15, 13, DRPAEXT14_15),
};
static const struct rzv2m_pin_desc drpa15_a_desc[] = {
	RZV2M_PFC_PIN(15, 14, DRPAEXT15_15),
};
static const struct rzv2m_pin_desc drpa16_a_desc[] = {
	RZV2M_PFC_PIN(15, 15, DRPAEXT16_15),
};
static const struct rzv2m_pin_desc drpa17_a_desc[] = {
	RZV2M_PFC_PIN(16, 0, DRPAEXT17_16),
};
static const struct rzv2m_pin_desc drpa18_a_desc[] = {
	RZV2M_PFC_PIN(16, 1, DRPAEXT18_16),
};
static const struct rzv2m_pin_desc drpa19_a_desc[] = {
	RZV2M_PFC_PIN(16, 2, DRPAEXT19_16),
};
static const struct rzv2m_pin_desc drpa20_a_desc[] = {
	RZV2M_PFC_PIN(16, 3, DRPAEXT20_16),
};
static const struct rzv2m_pin_desc drpa21_a_desc[] = {
	RZV2M_PFC_PIN(16, 4, DRPAEXT21_16),
};
static const struct rzv2m_pin_desc drpa22_a_desc[] = {
	RZV2M_PFC_PIN(16, 5, DRPAEXT22_16),
};
static const struct rzv2m_pin_desc drpa23_a_desc[] = {
	RZV2M_PFC_PIN(16, 6, DRPAEXT23_16),
};
static const struct rzv2m_pin_desc drpa24_a_desc[] = {
	RZV2M_PFC_PIN(16, 7, DRPAEXT24_16),
};
static const struct rzv2m_pin_desc drpa25_a_desc[] = {
	RZV2M_PFC_PIN(16, 8, DRPAEXT25_16),
};
static const struct rzv2m_pin_desc drpa26_a_desc[] = {
	RZV2M_PFC_PIN(16, 9, DRPAEXT26_16),
};
static const struct rzv2m_pin_desc drpa27_a_desc[] = {
	RZV2M_PFC_PIN(16, 10, DRPAEXT27_16),
};
static const struct rzv2m_pin_desc drpa28_a_desc[] = {
	RZV2M_PFC_PIN(16, 11, DRPAEXT28_16),
};
static const struct rzv2m_pin_desc drpa29_a_desc[] = {
	RZV2M_PFC_PIN(16, 12, DRPAEXT29_16),
};
static const struct rzv2m_pin_desc drpa30_a_desc[] = {
	RZV2M_PFC_PIN(16, 13, DRPAEXT30_16),
};
static const struct rzv2m_pin_desc drpa31_a_desc[] = {
	RZV2M_PFC_PIN(17, 0, DRPAEXT31_17),
};

//drpb_a
static const struct rzv2m_pin_desc drpb1_a_desc[] = {
	RZV2M_PFC_PIN(15, 0, DRPBEXT1_15),
};
static const struct rzv2m_pin_desc drpb2_a_desc[] = {
	RZV2M_PFC_PIN(15, 1, DRPBEXT2_15),
};
static const struct rzv2m_pin_desc drpb3_a_desc[] = {
	RZV2M_PFC_PIN(15, 2, DRPBEXT3_15),
};
static const struct rzv2m_pin_desc drpb4_a_desc[] = {
	RZV2M_PFC_PIN(15, 3, DRPBEXT4_15),
};
static const struct rzv2m_pin_desc drpb5_a_desc[] = {
	RZV2M_PFC_PIN(15, 4, DRPBEXT5_15),
};
static const struct rzv2m_pin_desc drpb6_a_desc[] = {
	RZV2M_PFC_PIN(15, 5, DRPBEXT6_15),
};
static const struct rzv2m_pin_desc drpb7_a_desc[] = {
	RZV2M_PFC_PIN(15, 6, DRPBEXT7_15),
};
static const struct rzv2m_pin_desc drpb8_a_desc[] = {
	RZV2M_PFC_PIN(15, 7, DRPBEXT8_15),
};
static const struct rzv2m_pin_desc drpb9_a_desc[] = {
	RZV2M_PFC_PIN(15, 8, DRPBEXT9_15),
};
static const struct rzv2m_pin_desc drpb10_a_desc[] = {
	RZV2M_PFC_PIN(15, 9, DRPBEXT10_15),
};
static const struct rzv2m_pin_desc drpb11_a_desc[] = {
	RZV2M_PFC_PIN(15, 10, DRPBEXT11_15),
};
static const struct rzv2m_pin_desc drpb12_a_desc[] = {
	RZV2M_PFC_PIN(15, 11, DRPBEXT12_15),
};
static const struct rzv2m_pin_desc drpb13_a_desc[] = {
	RZV2M_PFC_PIN(15, 12, DRPBEXT13_15),
};
static const struct rzv2m_pin_desc drpb14_a_desc[] = {
	RZV2M_PFC_PIN(15, 13, DRPBEXT14_15),
};
static const struct rzv2m_pin_desc drpb15_a_desc[] = {
	RZV2M_PFC_PIN(15, 14, DRPBEXT15_15),
};
static const struct rzv2m_pin_desc drpb16_a_desc[] = {
	RZV2M_PFC_PIN(15, 15, DRPBEXT16_15),
};
static const struct rzv2m_pin_desc drpb17_a_desc[] = {
	RZV2M_PFC_PIN(16, 0, DRPBEXT17_16),
};
static const struct rzv2m_pin_desc drpb18_a_desc[] = {
	RZV2M_PFC_PIN(16, 1, DRPBEXT18_16),
};
static const struct rzv2m_pin_desc drpb19_a_desc[] = {
	RZV2M_PFC_PIN(16, 2, DRPBEXT19_16),
};
static const struct rzv2m_pin_desc drpb20_a_desc[] = {
	RZV2M_PFC_PIN(16, 3, DRPBEXT20_16),
};
static const struct rzv2m_pin_desc drpb21_a_desc[] = {
	RZV2M_PFC_PIN(16, 4, DRPBEXT21_16),
};
static const struct rzv2m_pin_desc drpb22_a_desc[] = {
	RZV2M_PFC_PIN(16, 5, DRPBEXT22_16),
};
static const struct rzv2m_pin_desc drpb23_a_desc[] = {
	RZV2M_PFC_PIN(16, 6, DRPBEXT23_16),
};
static const struct rzv2m_pin_desc drpb24_a_desc[] = {
	RZV2M_PFC_PIN(16, 7, DRPBEXT24_16),
};
static const struct rzv2m_pin_desc drpb25_a_desc[] = {
	RZV2M_PFC_PIN(16, 8, DRPBEXT25_16),
};
static const struct rzv2m_pin_desc drpb26_a_desc[] = {
	RZV2M_PFC_PIN(16, 9, DRPBEXT26_16),
};
static const struct rzv2m_pin_desc drpb27_a_desc[] = {
	RZV2M_PFC_PIN(16, 10, DRPBEXT27_16),
};
static const struct rzv2m_pin_desc drpb28_a_desc[] = {
	RZV2M_PFC_PIN(16, 11, DRPBEXT28_16),
};
static const struct rzv2m_pin_desc drpb29_a_desc[] = {
	RZV2M_PFC_PIN(16, 12, DRPBEXT29_16),
};
static const struct rzv2m_pin_desc drpb30_a_desc[] = {
	RZV2M_PFC_PIN(16, 13, DRPBEXT30_16),
};
static const struct rzv2m_pin_desc drpb31_a_desc[] = {
	RZV2M_PFC_PIN(17, 0, DRPBEXT31_17),
};

//eth0
static const struct rzv2m_pin_desc eth0_getxc_desc[] = {
	RZV2M_PFC_PIN(15, 0, GETXC),
};
static const struct rzv2m_pin_desc eth0_getxen_desc[] = {
	RZV2M_PFC_PIN(15, 1, GETXEN),
};
static const struct rzv2m_pin_desc eth0_getxer_desc[] = {
	RZV2M_PFC_PIN(15, 2, GETXER),
};
static const struct rzv2m_pin_desc eth0_getxd0_desc[] = {
	RZV2M_PFC_PIN(15, 3, GETXD0),
};
static const struct rzv2m_pin_desc eth0_getxd1_desc[] = {
	RZV2M_PFC_PIN(15, 4, GETXD1),
};
static const struct rzv2m_pin_desc eth0_getxd2_desc[] = {
	RZV2M_PFC_PIN(15, 5, GETXD2),
};
static const struct rzv2m_pin_desc eth0_getxd3_desc[] = {
	RZV2M_PFC_PIN(15, 6, GETXD3),
};
static const struct rzv2m_pin_desc eth0_getxd4_desc[] = {
	RZV2M_PFC_PIN(15, 7, GETXD4),
};
static const struct rzv2m_pin_desc eth0_getxd5_desc[] = {
	RZV2M_PFC_PIN(15, 8, GETXD5),
};
static const struct rzv2m_pin_desc eth0_getxd6_desc[] = {
	RZV2M_PFC_PIN(15, 9, GETXD6),
};
static const struct rzv2m_pin_desc eth0_getxd7_desc[] = {
	RZV2M_PFC_PIN(15, 10, GETXD7),
};
static const struct rzv2m_pin_desc eth0_gerxc_desc[] = {
	RZV2M_PFC_PIN(15, 11, GERXC),
};
static const struct rzv2m_pin_desc eth0_gerxdv_desc[] = {
	RZV2M_PFC_PIN(15, 12, GERXDV),
};
static const struct rzv2m_pin_desc eth0_gerxer_desc[] = {
	RZV2M_PFC_PIN(15, 13, GERXER),
};
static const struct rzv2m_pin_desc eth0_gerxd0_desc[] = {
	RZV2M_PFC_PIN(15, 14, GERXD0),
};
static const struct rzv2m_pin_desc eth0_gerxd1_desc[] = {
	RZV2M_PFC_PIN(15, 15, GERXD1),
};
static const struct rzv2m_pin_desc eth0_gerxd2_desc[] = {
	RZV2M_PFC_PIN(16, 0, GERXD2),
};
static const struct rzv2m_pin_desc eth0_gerxd3_desc[] = {
	RZV2M_PFC_PIN(16, 1, GERXD3),
};
static const struct rzv2m_pin_desc eth0_gerxd4_desc[] = {
	RZV2M_PFC_PIN(16, 2, GERXD4),
};
static const struct rzv2m_pin_desc eth0_gerxd5_desc[] = {
	RZV2M_PFC_PIN(16, 3, GERXD5),
};
static const struct rzv2m_pin_desc eth0_gerxd6_desc[] = {
	RZV2M_PFC_PIN(16, 4, GERXD6),
};
static const struct rzv2m_pin_desc eth0_gerxd7_desc[] = {
	RZV2M_PFC_PIN(16, 5, GERXD7),
};
static const struct rzv2m_pin_desc eth0_gecrs_desc[] = {
	RZV2M_PFC_PIN(16, 6, GECRS),
};
static const struct rzv2m_pin_desc eth0_gecol_desc[] = {
	RZV2M_PFC_PIN(16, 7, GECOL),
};
static const struct rzv2m_pin_desc eth0_gemdc_desc[] = {
	RZV2M_PFC_PIN(16, 8, GEMDC),
};
static const struct rzv2m_pin_desc eth0_gemdio_desc[] = {
	RZV2M_PFC_PIN(16, 9, GEMDIO),
};
static const struct rzv2m_pin_desc eth0_gegtxclk_desc[] = {
	RZV2M_PFC_PIN(16, 10, GEGTXCLK),
};
static const struct rzv2m_pin_desc eth0_gelink_desc[] = {
	RZV2M_PFC_PIN(16, 11, GELINK),
};
static const struct rzv2m_pin_desc eth0_geint_desc[] = {
	RZV2M_PFC_PIN(16, 12, GEINT),
};
static const struct rzv2m_pin_desc eth0_geclk_desc[] = {
	RZV2M_PFC_PIN(16, 13, GECLK),
};
static const struct rzv2m_pin_desc eth0_gepps_desc[] = {
	RZV2M_PFC_PIN(17, 0, GEPPS),
};

//pci
static const struct rzv2m_pin_desc pci_pcdebugp0_desc[] = {
	RZV2M_PFC_PIN(8, 0, PCDEBUGP0),
};
static const struct rzv2m_pin_desc pci_pcdebugp1_desc[] = {
	RZV2M_PFC_PIN(8, 1, PCDEBUGP1),
};
static const struct rzv2m_pin_desc pci_pcdebugp2_desc[] = {
	RZV2M_PFC_PIN(8, 2, PCDEBUGP2),
};
static const struct rzv2m_pin_desc pci_pcdebugp3_desc[] = {
	RZV2M_PFC_PIN(8, 3, PCDEBUGP3),
};
static const struct rzv2m_pin_desc pci_pcdebugp4_desc[] = {
	RZV2M_PFC_PIN(8, 4, PCDEBUGP4),
};
static const struct rzv2m_pin_desc pci_pcdebugp5_desc[] = {
	RZV2M_PFC_PIN(8, 5, PCDEBUGP5),
};
static const struct rzv2m_pin_desc pci_pcdebugp6_desc[] = {
	RZV2M_PFC_PIN(8, 6, PCDEBUGP6),
};
static const struct rzv2m_pin_desc pci_pcdebugp7_desc[] = {
	RZV2M_PFC_PIN(8, 7, PCDEBUGP7),
};





static const struct rzv2m_group_desc groups_mux[] = {
	RZV2M_GROUP(gpio00),
	RZV2M_GROUP(gpio01),
	RZV2M_GROUP(gpio02),
	RZV2M_GROUP(gpio03),
	RZV2M_GROUP(gpio04),
	RZV2M_GROUP(gpio05),
	RZV2M_GROUP(gpio06),
	RZV2M_GROUP(gpio07),
	RZV2M_GROUP(gpio08),
	RZV2M_GROUP(gpio09),
	RZV2M_GROUP(gpio10),
	RZV2M_GROUP(gpio11),
	RZV2M_GROUP(gpio12),
	RZV2M_GROUP(gpio13),
	RZV2M_GROUP(gpio14),
	RZV2M_GROUP(gpio15),
	RZV2M_GROUP(gpio16),
	RZV2M_GROUP(gpio17),
	RZV2M_GROUP(gpio20),
	RZV2M_GROUP(gpio21),
	RZV2M_GROUP(nand_data1),
	RZV2M_GROUP(nand_data4),
	RZV2M_GROUP(nand_data8),
	RZV2M_GROUP(nand_ctl),
	RZV2M_GROUP(mmc_data1),
	RZV2M_GROUP(mmc_data4),
	RZV2M_GROUP(mmc_data8),
	RZV2M_GROUP(mmc_ctl),
	RZV2M_GROUP(sd0_ctl),
	RZV2M_GROUP(sd0_data1),
	RZV2M_GROUP(sd0_data4),
	RZV2M_GROUP(sd0_wp),
	RZV2M_GROUP(sd0_cd),
	RZV2M_GROUP(sd1_ctl),
	RZV2M_GROUP(sd1_data1),
	RZV2M_GROUP(sd1_data4),
	RZV2M_GROUP(sd1_wp),
	RZV2M_GROUP(sd1_cd),
	RZV2M_GROUP(pwm0),
	RZV2M_GROUP(pwm1),
	RZV2M_GROUP(pwm2),
	RZV2M_GROUP(pwm3),
	RZV2M_GROUP(pwm4),
	RZV2M_GROUP(pwm5),
	RZV2M_GROUP(pwm6),
	RZV2M_GROUP(pwm7),
	RZV2M_GROUP(pwm8),
	RZV2M_GROUP(pwm9),
	RZV2M_GROUP(pwm10),
	RZV2M_GROUP(pwm11),
	RZV2M_GROUP(pwm12),
	RZV2M_GROUP(pwm13),
	RZV2M_GROUP(pwm14),
	RZV2M_GROUP(pwm15),
	RZV2M_GROUP(irq_ex0),
	RZV2M_GROUP(irq_ex1),
	RZV2M_GROUP(irq_ex2),
	RZV2M_GROUP(irq_ex3),
	RZV2M_GROUP(irq_ex4),
	RZV2M_GROUP(irq_ex5),
	RZV2M_GROUP(irq_ex6),
	RZV2M_GROUP(irq_ex7),
	RZV2M_GROUP(irq_ex8),
	RZV2M_GROUP(irq_ex9),
	RZV2M_GROUP(irq_ex10),
	RZV2M_GROUP(irq_ex11),
	RZV2M_GROUP(irq_ex12),
	RZV2M_GROUP(irq_ex13),
	RZV2M_GROUP(irq_ex14),
	RZV2M_GROUP(irq_ex15),
	RZV2M_GROUP(irq_ex16),
	RZV2M_GROUP(irq_ex17),
	RZV2M_GROUP(irq_ex18),
	RZV2M_GROUP(irq_ex19),
	RZV2M_GROUP(irq_ex20),
	RZV2M_GROUP(irq_ex21),
	RZV2M_GROUP(irq_ex22),
	RZV2M_GROUP(irq_ex23),
	RZV2M_GROUP(irq_ex24),
	RZV2M_GROUP(irq_ex25),
	RZV2M_GROUP(irq_ex26),
	RZV2M_GROUP(irq_ex27),
	RZV2M_GROUP(irq_ex28),
	RZV2M_GROUP(irq_ex29),
	RZV2M_GROUP(irq_ex30),
	RZV2M_GROUP(irq_ex31),
	RZV2M_GROUP(irq_ex32),
	RZV2M_GROUP(irq_ex33),
	RZV2M_GROUP(irq_ex34),
	RZV2M_GROUP(irq_ex35),
	RZV2M_GROUP(irq_ex36),
	RZV2M_GROUP(irq_ex37),
	RZV2M_GROUP(irq_ex38),
	RZV2M_GROUP(uart0_data),
	RZV2M_GROUP(uart0_ctl),
	RZV2M_GROUP(uart1_data),
	RZV2M_GROUP(uart1_ctl),
	RZV2M_GROUP(i2c0),
	RZV2M_GROUP(i2c1),
	RZV2M_GROUP(i2c2),
	RZV2M_GROUP(i2c3),
	RZV2M_GROUP(csi0rw),
	RZV2M_GROUP(csi0r),
	RZV2M_GROUP(csi1rw),
	RZV2M_GROUP(csi1r),
	RZV2M_GROUP(csi2rw),
	RZV2M_GROUP(csi2r),
	RZV2M_GROUP(csi3rw),
	RZV2M_GROUP(csi3r),
	RZV2M_GROUP(csi4rw),
	RZV2M_GROUP(csi4r),
	RZV2M_GROUP(csi5rw),
	RZV2M_GROUP(csi5r),
	RZV2M_GROUP(drpa0),
	RZV2M_GROUP(drpa1),
	RZV2M_GROUP(drpa2),
	RZV2M_GROUP(drpa3),
	RZV2M_GROUP(drpa4),
	RZV2M_GROUP(drpa5),
	RZV2M_GROUP(drpa6),
	RZV2M_GROUP(drpa7),
	RZV2M_GROUP(drpa8),
	RZV2M_GROUP(drpa9),
	RZV2M_GROUP(drpa10),
	RZV2M_GROUP(drpa11),
	RZV2M_GROUP(drpa12),
	RZV2M_GROUP(drpa13),
	RZV2M_GROUP(drpa14),
	RZV2M_GROUP(drpa15),
	RZV2M_GROUP(drpb0),
	RZV2M_GROUP(drpb1),
	RZV2M_GROUP(drpb2),
	RZV2M_GROUP(drpb3),
	RZV2M_GROUP(drpb4),
	RZV2M_GROUP(drpb5),
	RZV2M_GROUP(drpb6),
	RZV2M_GROUP(drpb7),
	RZV2M_GROUP(drpb8),
	RZV2M_GROUP(drpb9),
	RZV2M_GROUP(drpb10),
	RZV2M_GROUP(drpb11),
	RZV2M_GROUP(drpb12),
	RZV2M_GROUP(drpb13),
	RZV2M_GROUP(drpb14),
	RZV2M_GROUP(drpb15),
	RZV2M_GROUP(drpa1_a), //drpa_a
	RZV2M_GROUP(drpa2_a),
	RZV2M_GROUP(drpa3_a),
	RZV2M_GROUP(drpa4_a),
	RZV2M_GROUP(drpa5_a),
	RZV2M_GROUP(drpa6_a),
	RZV2M_GROUP(drpa7_a),
	RZV2M_GROUP(drpa8_a),
	RZV2M_GROUP(drpa9_a),
	RZV2M_GROUP(drpa10_a),
	RZV2M_GROUP(drpa11_a),
	RZV2M_GROUP(drpa12_a),
	RZV2M_GROUP(drpa13_a),
	RZV2M_GROUP(drpa14_a),
	RZV2M_GROUP(drpa15_a),
	RZV2M_GROUP(drpa16_a),
	RZV2M_GROUP(drpa17_a),
	RZV2M_GROUP(drpa18_a),
	RZV2M_GROUP(drpa19_a),
	RZV2M_GROUP(drpa20_a),
	RZV2M_GROUP(drpa21_a),
	RZV2M_GROUP(drpa22_a),
	RZV2M_GROUP(drpa23_a),
	RZV2M_GROUP(drpa24_a),
	RZV2M_GROUP(drpa25_a),
	RZV2M_GROUP(drpa26_a),
	RZV2M_GROUP(drpa27_a),
	RZV2M_GROUP(drpa28_a),
	RZV2M_GROUP(drpa29_a),
	RZV2M_GROUP(drpa30_a),
	RZV2M_GROUP(drpa31_a),
	RZV2M_GROUP(drpb1_a), //drpb_a
	RZV2M_GROUP(drpb2_a),
	RZV2M_GROUP(drpb3_a),
	RZV2M_GROUP(drpb4_a),
	RZV2M_GROUP(drpb5_a),
	RZV2M_GROUP(drpb6_a),
	RZV2M_GROUP(drpb7_a),
	RZV2M_GROUP(drpb8_a),
	RZV2M_GROUP(drpb9_a),
	RZV2M_GROUP(drpb10_a),
	RZV2M_GROUP(drpb11_a),
	RZV2M_GROUP(drpb12_a),
	RZV2M_GROUP(drpb13_a),
	RZV2M_GROUP(drpb14_a),
	RZV2M_GROUP(drpb15_a),
	RZV2M_GROUP(drpb16_a),
	RZV2M_GROUP(drpb17_a),
	RZV2M_GROUP(drpb18_a),
	RZV2M_GROUP(drpb19_a),
	RZV2M_GROUP(drpb20_a),
	RZV2M_GROUP(drpb21_a),
	RZV2M_GROUP(drpb22_a),
	RZV2M_GROUP(drpb23_a),
	RZV2M_GROUP(drpb24_a),
	RZV2M_GROUP(drpb25_a),
	RZV2M_GROUP(drpb26_a),
	RZV2M_GROUP(drpb27_a),
	RZV2M_GROUP(drpb28_a),
	RZV2M_GROUP(drpb29_a),
	RZV2M_GROUP(drpb30_a),
	RZV2M_GROUP(drpb31_a),
	RZV2M_GROUP(eth0_getxc),
    RZV2M_GROUP(eth0_getxen),
    RZV2M_GROUP(eth0_getxer),
    RZV2M_GROUP(eth0_getxd0),
    RZV2M_GROUP(eth0_getxd1),
    RZV2M_GROUP(eth0_getxd2),
    RZV2M_GROUP(eth0_getxd3),
    RZV2M_GROUP(eth0_getxd4),
    RZV2M_GROUP(eth0_getxd5),
    RZV2M_GROUP(eth0_getxd6),
    RZV2M_GROUP(eth0_getxd7),
    RZV2M_GROUP(eth0_gerxc),
    RZV2M_GROUP(eth0_gerxdv),
    RZV2M_GROUP(eth0_gerxer),
    RZV2M_GROUP(eth0_gerxd0),
    RZV2M_GROUP(eth0_gerxd1),
    RZV2M_GROUP(eth0_gerxd2),
    RZV2M_GROUP(eth0_gerxd3),
    RZV2M_GROUP(eth0_gerxd4),
    RZV2M_GROUP(eth0_gerxd5),
    RZV2M_GROUP(eth0_gerxd6),
    RZV2M_GROUP(eth0_gerxd7),
    RZV2M_GROUP(eth0_gecrs),
    RZV2M_GROUP(eth0_gecol),
    RZV2M_GROUP(eth0_gemdc),
    RZV2M_GROUP(eth0_gemdio),
    RZV2M_GROUP(eth0_gegtxclk),
    RZV2M_GROUP(eth0_gelink),
    RZV2M_GROUP(eth0_geint),
    RZV2M_GROUP(eth0_geclk),
    RZV2M_GROUP(eth0_gepps),
	RZV2M_GROUP(pci_pcdebugp0),
    RZV2M_GROUP(pci_pcdebugp1),
    RZV2M_GROUP(pci_pcdebugp2),
    RZV2M_GROUP(pci_pcdebugp3),
    RZV2M_GROUP(pci_pcdebugp4),
    RZV2M_GROUP(pci_pcdebugp5),
    RZV2M_GROUP(pci_pcdebugp6),
    RZV2M_GROUP(pci_pcdebugp7),
};

static const char * const nand_groups[] = {
	"nand_data1",
	"nand_data4",
	"nand_data8",
	"nand_ctl",
};

static const char * const mmc_groups[] = {
	"mmc_data1",
	"mmc_data4",
	"mmc_data8",
	"mmc_ctl",
};

static const char * sd0_groups[] = {
	"sd0_ctl",
	"sd0_data1",
	"sd0_data4",
	"sd0_wp",
	"sd0_cd",
};

static const char * sd1_groups[] = {
	"sd1_ctl",
	"sd1_data1",
	"sd1_data4",
	"sd1_wp",
	"sd1_cd",
};

static const char * pwm_groups[] = {
	"pwm0",
	"pwm1"
	"pwm2",
	"pwm3",
	"pwm4",
	"pwm5",
	"pwm6",
	"pwm7",
	"pwm8",
	"pwm9",
	"pwm10",
	"pwm11",
	"pwm12",
	"pwm13",
	"pwm14",
	"pwm15",
};
static const char * const gpio_groups[] = {
	"gpio00",
	"gpio01",
	"gpio02",
	"gpio03",
	"gpio04",
	"gpio05",
	"gpio06",
	"gpio07",
	"gpio08",
	"gpio09",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
	"gpio16",
	"gpio17",
	"gpio20",
	"gpio21",
};

static const char * const irq_ex_groups[] = {
	"irq_ex0",
	"irq_ex1",
	"irq_ex2",
	"irq_ex3",
	"irq_ex4",
	"irq_ex5",
	"irq_ex6",
	"irq_ex7",
	"irq_ex8",
	"irq_ex9",
	"irq_ex10",
	"irq_ex11",
	"irq_ex12",
	"irq_ex13",
	"irq_ex14",
	"irq_ex15",
	"irq_ex16",
	"irq_ex17",
	"irq_ex18",
	"irq_ex19",
	"irq_ex20",
	"irq_ex21",
	"irq_ex22",
	"irq_ex23",
	"irq_ex24",
	"irq_ex25",
	"irq_ex26",
	"irq_ex27",
	"irq_ex28",
	"irq_ex29",
	"irq_ex30",
	"irq_ex31",
	"irq_ex32",
	"irq_ex33",
	"irq_ex34",
	"irq_ex35",
	"irq_ex36",
	"irq_ex37",
	"irq_ex38",
};

static const char * const i2c_groups[] = {
	"i2c0",
	"i2c1",
	"i2c2",
	"i2c3",
};

static const char * const uart_groups[] = {
	"uart0_data",
	"uart0_ctl",
	"uart1_data",
	"uart1_ctl",
};

//csi
static const char * const csi_groups[] = {
	"csi0rw",
	"csi0r",
	"csi1rw",
	"csi1r",
	"csi2rw",
	"csi2r",
	"csi3rw",
	"csi3r",
	"csi4rw",
	"csi4r",
	"csi5rw",
	"csi5r"
};
//drpa
static const char * const drpa_groups[] = {
	"drpa0",
	"drpa1",
	"drpa2",
	"drpa3",
	"drpa4",
	"drpa5",
	"drpa6",
	"drpa7",
	"drpa8",
	"drpa9",
	"drpa10",
	"drpa11",
	"drpa12",
	"drpa13",
	"drpa14",
	"drpa15",
};
//drpb
static const char * const drpb_groups[] = {
	"drpb0",
	"drpb1",
	"drpb2",
	"drpb3",
	"drpb4",
	"drpb5",
	"drpb6",
	"drpb7",
	"drpb8",
	"drpb9",
	"drpb10",
	"drpb11",
	"drpb12",
	"drpb13",
	"drpb14",
	"drpb15",
};

//drpa_a
static const char * const drpa_a_groups[] = {
	"drpa_a1",
	"drpa_a2",
	"drpa_a3",
	"drpa_a4",
	"drpa_a5",
	"drpa_a6",
	"drpa_a7",
	"drpa_a8",
	"drpa_a9",
	"drpa_a10",
	"drpa_a11",
	"drpa_a12",
	"drpa_a13",
	"drpa_a14",
	"drpa_a15",
	"drpa_a16",
	"drpa_a17",
	"drpa_a18",
	"drpa_a19",
	"drpa_a20",
	"drpa_a21",
	"drpa_a22",
	"drpa_a23",
	"drpa_a24",
	"drpa_a25",
	"drpa_a26",
	"drpa_a27",
	"drpa_a28",
	"drpa_a29",
	"drpa_a30",
	"drpa_a31",
};
//drpb_a
static const char * const drpb_a_groups[] = {
	"drpb_a1",
	"drpb_a2",
	"drpb_a3",
	"drpb_a4",
	"drpb_a5",
	"drpb_a6",
	"drpb_a7",
	"drpb_a8",
	"drpb_a9",
	"drpb_a10",
	"drpb_a11",
	"drpb_a12",
	"drpb_a13",
	"drpb_a14",
	"drpb_a15",
	"drpb_a16",
	"drpb_a17",
	"drpb_a18",
	"drpb_a19",
	"drpb_a20",
	"drpb_a21",
	"drpb_a22",
	"drpb_a23",
	"drpb_a24",
	"drpb_a25",
	"drpb_a26",
	"drpb_a27",
	"drpb_a28",
	"drpb_a29",
	"drpb_a30",
	"drpb_a31",
};

//eth0
static const char * const eth0_groups[] = {
    "eth0_getxc",
    "eth0_getxen",
    "eth0_getxer",
    "eth0_getxd0",
    "eth0_getxd1",
    "eth0_getxd2",
    "eth0_getxd3",
    "eth0_getxd4",
    "eth0_getxd5",
    "eth0_getxd6",
    "eth0_getxd7",
    "eth0_gerxc",
    "eth0_gerxdv",
    "eth0_gerxer",
    "eth0_gerxd0",
    "eth0_gerxd1",
    "eth0_gerxd2",
    "eth0_gerxd3",
    "eth0_gerxd4",
    "eth0_gerxd5",
    "eth0_gerxd6",
    "eth0_gerxd7",
    "eth0_gecrs",
    "eth0_gecol",
    "eth0_gemdc",
    "eth0_gemdio",
    "eth0_gegtxclk",
    "eth0_gelink",
    "eth0_geint",
    "eth0_geclk",
    "eth0_gepps",
};
//pci
static const char * const pci_groups[] = {
    "pci_pcdebugp0",
    "pci_pcdebugp1",
    "pci_pcdebugp2",
    "pci_pcdebugp3",
    "pci_pcdebugp4",
    "pci_pcdebugp5",
    "pci_pcdebugp6",
    "pci_pcdebugp7",
};

static const struct rzv2m_func_desc funcs_mux[] = {
	RZV2M_FUNCTION(gpio),
	RZV2M_FUNCTION(nand),
	RZV2M_FUNCTION(mmc),
	RZV2M_FUNCTION(sd0),
	RZV2M_FUNCTION(sd1),
	RZV2M_FUNCTION(pwm),
	RZV2M_FUNCTION(irq_ex),
	RZV2M_FUNCTION(uart),
	RZV2M_FUNCTION(i2c),
	RZV2M_FUNCTION(csi), //csi func
	RZV2M_FUNCTION(drpa), //drpa func
	RZV2M_FUNCTION(drpb), //drpb func
	RZV2M_FUNCTION(drpa_a), //drpa func
	RZV2M_FUNCTION(drpb_a), //drpb func
	RZV2M_FUNCTION(eth0), //eth0
	RZV2M_FUNCTION(pci), //pci
	
	
};

static const struct rzv2m_pfc_config_reg config_regs[] = {
#define FN(x)	x,
#define F_(x)	0,
	/*
	 *  In case the register control not continuous pin such 0,1,3, we can
	 *  use RZV2M_PIN(port, pin) instead  PORT_PFx(RZV2M_PIN, port, cfg) to
	 *  assign number of pins to mark into register, pin of port must has
	 *  index equal to its number.
	 *  Example:
	 *       RZV2M_PIN(0, 0),
	 *       RZV2M_PIN(0, 1),
	 *       -1,
	 *       RZV2M_PIN(0, 3),
	 */

	/* Define configuration register for Port 0*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x000, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x004, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x008, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x010, BIT_WRITE_ENABLE, 4, 8, 32) {
		P0_PF0
		P0_PF1
		P0_PF2
		P0_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x014, BIT_WRITE_ENABLE, 4, 8, 32) {
		P0_PF4
		P0_PF5
		P0_PF6
		P0_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x018, BIT_WRITE_ENABLE, 4, 8, 32) {
		P0_PF8
		P0_PF9
		P0_PF10
		P0_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x01c, BIT_WRITE_ENABLE, 4, 8, 16) {
		P0_PF12
		P0_PF13
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x020, BIT_WRITE_NONE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x024, BIT_WRITE_NONE, 2, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x028, BIT_WRITE_NONE, 2, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x02c, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x030, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x034, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 0, 0)
	  }
	},

	/* Define configuration register for Port 1*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x040, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x044, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x048, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x050, BIT_WRITE_ENABLE, 4, 8, 32) {
		P1_PF0
		P1_PF1
		P1_PF2
		P1_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x054, BIT_WRITE_ENABLE, 4, 8, 32) {
		P1_PF4
		P1_PF5
		P1_PF6
		P1_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x058, BIT_WRITE_ENABLE, 4, 8, 32) {
		P1_PF8
		P1_PF9
		P1_PF10
		P1_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x05c, BIT_WRITE_ENABLE, 4, 8, 32) {
		P1_PF12
		P1_PF13
		P1_PF14
		P1_PF15
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x060, BIT_WRITE_NONE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x064, BIT_WRITE_NONE, 2, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x068, BIT_WRITE_NONE, 2, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x06c, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x070, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x074, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 1, 0)
	  }
	},

	/* Define configuration register for Port 2*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x080, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x084, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x088, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x090, BIT_WRITE_ENABLE, 4, 8, 32) {
		P2_PF0
		P2_PF1
		P2_PF2
		P2_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x094, BIT_WRITE_ENABLE, 4, 8, 32) {
		P2_PF4
		P2_PF5
		P2_PF6
		P2_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x0a0, BIT_WRITE_NONE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x0a4, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x0a8, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x0ac, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x0b0, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x0b4, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 2, 0)
	  }
	},

	/* Define configuration register for Port 3*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x0c0, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x0c4, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x0c8, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x0d0, BIT_WRITE_ENABLE, 4, 8, 32) {
		P3_PF0
		P3_PF1
		P3_PF2
		P3_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x0d4, BIT_WRITE_ENABLE, 4, 8, 32) {
		P3_PF4
		P3_PF5
		P3_PF6
		P3_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x0d8, BIT_WRITE_ENABLE, 4, 8, 32) {
		P3_PF8
		P3_PF9
		P3_PF10
		P3_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x0dc, BIT_WRITE_ENABLE, 4, 8, 32) {
		P3_PF12
		P3_PF13
		P3_PF14
		P3_PF15
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x0e0, BIT_WRITE_NONE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x0e4, BIT_WRITE_NONE, 2, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x0e8, BIT_WRITE_NONE, 2, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x0ec, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x0f0, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x0f4, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 3, 0)
	  }
	},

	/* Define configuration register for Port 4*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x100, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x104, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x108, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x110, BIT_WRITE_ENABLE, 4, 8, 32) {
		P4_PF0
		P4_PF1
		P4_PF2
		P4_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x114, BIT_WRITE_ENABLE, 4, 8, 32) {
		P4_PF4
		P4_PF5
		P4_PF6
		P4_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x120, BIT_WRITE_NONE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x124, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x128, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x12c, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x130, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x134, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 4, 0)
	  }
	},

	/* Define configuration register for Port 5*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x140, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x144, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x148, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x150, BIT_WRITE_ENABLE, 4, 8, 32) {
		P5_PF0
		P5_PF1
		P5_PF2
		P5_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x160, BIT_WRITE_NONE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x164, BIT_WRITE_NONE, 2, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x168, BIT_WRITE_NONE, 2, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x16c, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x170, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x174, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 5, 0)
	  }
	},

	/* Define configuration register for Port 6*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x180, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x184, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x188, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x190, BIT_WRITE_ENABLE, 4, 8, 32) {
		P6_PF0
		P6_PF1
		P6_PF2
		P6_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x194, BIT_WRITE_ENABLE, 4, 8, 32) {
		P6_PF4
		P6_PF5
		P6_PF6
		P6_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x198, BIT_WRITE_ENABLE, 4, 8, 32) {
		P6_PF8
		P6_PF9
		P6_PF10
		P6_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x1a0, BIT_WRITE_NONE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x1a4, BIT_WRITE_NONE, 2, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x1a8, BIT_WRITE_NONE, 2, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x1ac, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x1b0, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x1b4, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 6, 0)
	  }
	},

	/* Define configuration register for Port 7*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x1c0, BIT_WRITE_ENABLE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x1c4, BIT_WRITE_ENABLE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x1c8, BIT_WRITE_ENABLE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x1d0, BIT_WRITE_ENABLE, 4, 8, 32) {
		P7_PF0
		P7_PF1
		P7_PF2
		P7_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x1d4, BIT_WRITE_ENABLE, 4, 8, 16) {
		P7_PF4
		P7_PF5
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x1e0, BIT_WRITE_NONE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x1e4, BIT_WRITE_NONE, 2, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x1e8, BIT_WRITE_NONE, 2, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x1ec, BIT_WRITE_ENABLE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x1f0, BIT_WRITE_ENABLE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x1f4, BIT_WRITE_ENABLE, 1, 1, 6) {
		PORT_PF5(RZV2M_PIN, 7, 0)
	  }
	},

	/* Define configuration register for Port 8*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x200, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x204, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x208, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x210, BIT_WRITE_ENABLE, 4, 8, 32) {
		P8_PF0
		P8_PF1
		P8_PF2
		P8_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x214, BIT_WRITE_ENABLE, 4, 8, 32) {
		P8_PF4
		P8_PF5
		P8_PF6
		P8_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x220, BIT_WRITE_NONE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x224, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x228, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x22c, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x230, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x234, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 8, 0)
	  }
	},

	/* Define configuration register for Port 9*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x240, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x244, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x248, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x250, BIT_WRITE_ENABLE, 4, 8, 32) {
		P9_PF0
		P9_PF1
		P9_PF2
		P9_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x254, BIT_WRITE_ENABLE, 4, 8, 32) {
		P9_PF4
		P9_PF5
		P9_PF6
		P9_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x260, BIT_WRITE_NONE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x264, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x268, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x26c, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x270, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x274, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 9, 0)
	  }
	},

	/* Define configuration register for Port 10*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x280, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x284, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x288, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x290, BIT_WRITE_ENABLE, 4, 8, 32) {
		P10_PF0
		P10_PF1
		P10_PF2
		P10_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x294, BIT_WRITE_ENABLE, 4, 8, 32) {
		P10_PF4
		P10_PF5
		P10_PF6
		P10_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x298, BIT_WRITE_ENABLE, 4, 8, 8) {
		P10_PF8
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x2a0, BIT_WRITE_NONE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x2a4, BIT_WRITE_NONE, 2, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x2a8, BIT_WRITE_NONE, 2, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x2ac, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x2b0, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x2b4, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 10, 0)
	  }
	},

	/* Define configuration register for Port 11*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x2c0, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x2c4, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x2c8, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x2d0, BIT_WRITE_ENABLE, 4, 8, 32) {
		P3_PF0
		P3_PF1
		P3_PF2
		P3_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x2d4, BIT_WRITE_ENABLE, 4, 8, 32) {
		P3_PF4
		P3_PF5
		P3_PF6
		P3_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x2d8, BIT_WRITE_ENABLE, 4, 8, 8) {
		P3_PF8
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x2e0, BIT_WRITE_NONE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x2e4, BIT_WRITE_NONE, 2, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x2e8, BIT_WRITE_NONE, 2, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x2ec, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x2f0, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x2f4, BIT_WRITE_ENABLE, 1, 1, 9) {
		PORT_PF8(RZV2M_PIN, 11, 0)
	  }
	},

	/* Define configuration register for Port 12*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x300, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x304, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x308, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x310, BIT_WRITE_ENABLE, 4, 8, 32) {
		P12_PF0
		P12_PF1
		P12_PF2
		P12_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x320, BIT_WRITE_NONE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x324, BIT_WRITE_NONE, 2, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x328, BIT_WRITE_NONE, 2, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x32c, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x330, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x334, BIT_WRITE_ENABLE, 1, 1, 4) {
		PORT_PF3(RZV2M_PIN, 12, 0)
	  }
	},

	/* Define configuration register for Port 13*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x340, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x344, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x348, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x350, BIT_WRITE_ENABLE, 4, 8, 32) {
		P13_PF0
		P13_PF1
		P13_PF2
		P13_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x354, BIT_WRITE_ENABLE, 4, 8, 32) {
		P13_PF4
		P13_PF5
		P13_PF6
		P13_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x358, BIT_WRITE_ENABLE, 4, 8, 32) {
		P13_PF8
		P13_PF9
		P13_PF10
		P13_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x360, BIT_WRITE_NONE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x364, BIT_WRITE_NONE, 2, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x368, BIT_WRITE_NONE, 2, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x36c, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x370, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x374, BIT_WRITE_ENABLE, 1, 1, 12) {
		PORT_PF11(RZV2M_PIN, 13, 0)
	  }
	},

	/* Define configuration register for Port 14*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x380, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x384, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x388, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x390, BIT_WRITE_ENABLE, 4, 8, 32) {
		P14_PF0
		P14_PF1
		P14_PF2
		P14_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x394, BIT_WRITE_ENABLE, 4, 8, 32) {
		P14_PF4
		P14_PF5
		P14_PF6
		P14_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x3a0, BIT_WRITE_NONE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x3a4, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x3a8, BIT_WRITE_NONE, 2, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x3ac, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x3b0, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x3b4, BIT_WRITE_ENABLE, 1, 1, 8) {
		PORT_PF7(RZV2M_PIN, 14, 0)
	  }
	},

	/* Define configuration register for Port 15*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x3c0, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x3c4, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x3c8, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x3d0, BIT_WRITE_ENABLE, 4, 8, 32) {
		P15_PF0
		P15_PF1
		P15_PF2
		P15_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x3d4, BIT_WRITE_ENABLE, 4, 8, 32) {
		P15_PF4
		P15_PF5
		P15_PF6
		P15_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x3d8, BIT_WRITE_ENABLE, 4, 8, 32) {
		P15_PF8
		P15_PF9
		P15_PF10
		P15_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x3dc, BIT_WRITE_ENABLE, 4, 8, 32) {
		P15_PF12
		P15_PF13
		P15_PF14
		P15_PF15
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x3e0, BIT_WRITE_NONE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x3e4, BIT_WRITE_NONE, 2, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x3e8, BIT_WRITE_NONE, 2, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x3ec, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x3f0, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x3f4, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF15(RZV2M_PIN, 15, 0)
	  }
	},

	/* Define configuration register for Port 16*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x400, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x404, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x408, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x410, BIT_WRITE_ENABLE, 4, 8, 32) {
		P16_PF0
		P16_PF1
		P16_PF2
		P16_PF3
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x414, BIT_WRITE_ENABLE, 4, 8, 32) {
		P16_PF4
		P16_PF5
		P16_PF6
		P16_PF7
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x418, BIT_WRITE_ENABLE, 4, 8, 32) {
		P16_PF8
		P16_PF9
		P16_PF10
		P16_PF11
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x41c, BIT_WRITE_ENABLE, 4, 8, 16) {
		P16_PF12
		P16_PF13
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x420, BIT_WRITE_NONE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x424, BIT_WRITE_NONE, 2, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x428, BIT_WRITE_NONE, 2, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x42c, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x430, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x434, BIT_WRITE_ENABLE, 1, 1, 14) {
		PORT_PF13(RZV2M_PIN, 16, 0)
	  }
	},

	/* Define configuration register for Port 17*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x440, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x444, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x448, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x450, BIT_WRITE_ENABLE, 4, 8, 8) {
		P17_PF0
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x460, BIT_WRITE_NONE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x464, BIT_WRITE_NONE, 2, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x468, BIT_WRITE_NONE, 2, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x46c, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x470, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x474, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 17, 0)
	  }
	},

	/* Define configuration register for Port 20*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x500, BIT_WRITE_ENABLE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x504, BIT_WRITE_ENABLE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x508, BIT_WRITE_ENABLE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x510, BIT_WRITE_ENABLE, 4, 8, 24) {
		P20_PF0
		P20_PF1
		P20_PF2
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x520, BIT_WRITE_NONE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x524, BIT_WRITE_NONE, 2, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x528, BIT_WRITE_NONE, 2, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x52c, BIT_WRITE_ENABLE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x530, BIT_WRITE_ENABLE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x534, BIT_WRITE_ENABLE, 1, 1, 3) {
		PORT_PF2(RZV2M_PIN, 20, 0)
	  }
	},

	/* Define configuration register for Port 21*/
	{ RZV2M_CONFIG_REG(GP_DO, 0x540, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF0(RZV2M_PIN, 21, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_OE, 0x544, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF0(RZV2M_PIN, 21, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(GP_IE, 0x548, BIT_WRITE_ENABLE, 1, 1, 16) {
		PORT_PF0(RZV2M_PIN, 21, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(SEL, 0x550, BIT_WRITE_ENABLE, 4, 8, 8) {
		P21_PF0
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MON, 0x560, BIT_WRITE_NONE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 21, 0),
	  }
	},
	{ RZV2M_CONFIG_REG(PUPD, 0x564, BIT_WRITE_NONE, 2, 1, 1) {
		PORT_PF0(RZV2M_PIN, 21, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DRV_STR, 0x568, BIT_WRITE_NONE, 2, 1, 1) {
		PORT_PF0(RZV2M_PIN, 21, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x56c, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 21, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(DI_MSK, 0x570, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 21, 0)
	  }
	},
	{ RZV2M_CONFIG_REG(EN_MSK, 0x574, BIT_WRITE_ENABLE, 1, 1, 1) {
		PORT_PF0(RZV2M_PIN, 21, 0)
	  }
	},

	/* Define configuration register for CSRXD_SEL*/
	{ RZV2M_CONFIG_REG(SEL, 0x580, BIT_WRITE_ENABLE, 1, 2, 12) {
		CSRXD0_03_01, CSRXD0_03_00,
		CSRXD1_03_05, CSRXD1_03_04,
		CSRXD2_03_09, CSRXD2_03_08,
		CSRXD3_03_13, CSRXD3_03_12,
		CSRXD4_04_01, CSRXD4_04_00,
		CSRXD5_04_05, CSRXD5_04_04,
	  }
	},

	/* Define configuration register for ROP_DI_SEL */
	{ RZV2M_CONFIG_REG(SEL, 0x584, BIT_WRITE_ENABLE, 2, 4, 32) {
		-1, -1, -1, -1,			/* bit [0:1] reserved*/
		-1, -1, -1, -1,			/* bit [2:3] reserved*/
		-1, -1, -1, -1,			/* bit [4:5] reserved*/
		-1, -1, -1, -1,			/* bit [6:7] reserved*/
		RESIG9_01, RESIG9_06, RESIG9_15, -1,	/* bit [8:9] (DI_SEL9)*/
		-1, -1, -1, -1,			/* bit [10:11] reserved*/
		-1, -1, -1, -1,			/* bit [12:13] reserved*/
		-1, -1, -1, -1,			/* bit [14:15] reserved*/
		-1, -1, -1, -1,			/* bit [16:17] reserved*/
		-1, -1, -1, -1,			/* bit [18:19] reserved*/
		-1, -1, -1, -1,			/* bit [20:21] reserved*/
		-1, -1, -1, -1,			/* bit [22:23] reserved*/
		-1, -1, -1, -1,			/* bit [24:25] reserved*/
		-1, -1, -1, -1,			/* bit [26:27] reserved*/
		-1, -1, -1, -1,			/* bit [28:29] reserved*/
		-1, -1, -1, -1,			/* bit [30:31] reserved*/
	  }
	},

	/* Define configuration register for PEX0*/
	{ RZV2M_CONFIG_REG(DRV_STR, 0x590, BIT_WRITE_NONE, 2, 1, 16) {
		RZV2M_PIN(PORT_NUMS, 0),
		RZV2M_PIN(PORT_NUMS, 1),
		RZV2M_PIN(PORT_NUMS, 2),
		-1, -1, //3, 4 reserved
		RZV2M_PIN(PORT_NUMS, 5),
		RZV2M_PIN(PORT_NUMS, 6),
		-1, -1, -1, -1, //7, 8, 9, 10 reserved
		RZV2M_PIN(PORT_NUMS, 11),
		RZV2M_PIN(PORT_NUMS, 12),
		-1, // 13 reserved
		RZV2M_PIN(PORT_NUMS, 14),
		-1, // 15 reserved
	  }
	},
	{ RZV2M_CONFIG_REG(SLEW, 0x594, BIT_WRITE_ENABLE, 1, 1, 16) {
		RZV2M_PIN(PORT_NUMS, 0),
		RZV2M_PIN(PORT_NUMS, 1),
		RZV2M_PIN(PORT_NUMS, 2),
		-1, -1, //3, 4 reserved
		RZV2M_PIN(PORT_NUMS, 5),
		RZV2M_PIN(PORT_NUMS, 6),
		-1, -1, -1, -1, //7, 8, 9, 10 reserved
		RZV2M_PIN(PORT_NUMS, 11),
		RZV2M_PIN(PORT_NUMS, 12),
		-1, // 13 reserved
		RZV2M_PIN(PORT_NUMS, 14),
		-1, // 15 reserved
	  }
	},
};

static const struct rzv2m_pfc_config_reg irq_regs[] = {
	/* Register for Interrupt invert*/
	{ RZV2M_CONFIG_REG(INT_INV, 0x5a0, BIT_WRITE_ENABLE, 1, 1, 16) {
		INEXINT0, INEXINT1, INEXINT2, INEXINT3, INEXINT4,
		INEXINT5, INEXINT6, INEXINT7, INEXINT8, INEXINT9,
		INEXINT10, INEXINT11, INEXINT12, INEXINT13, INEXINT14,
		INEXINT15,
	 }
	},
	{ RZV2M_CONFIG_REG(INT_INV, 0x5a4, BIT_WRITE_ENABLE, 1, 1, 16) {
		INEXINT16, INEXINT17, INEXINT18, INEXINT19, INEXINT20,
		INEXINT21, INEXINT22, INEXINT23, INEXINT24, INEXINT25,
		INEXINT26, INEXINT27, INEXINT28, INEXINT29, INEXINT30,
		INEXINT31,
	 }
	},
	{ RZV2M_CONFIG_REG(INT_INV, 0x5a8, BIT_WRITE_ENABLE, 1, 1, 7) {
		INEXINT32, INEXINT33, INEXINT34, INEXINT35, INEXINT36,
		INEXINT37, INEXINT38,
	 }
	},
	/* Register for Interrupt Mask*/
	{ RZV2M_CONFIG_REG(INT_MSK, 0x5b0, BIT_WRITE_ENABLE, 1, 1, 16) {
		INEXINT0, INEXINT1, INEXINT2, INEXINT3, INEXINT4,
		INEXINT5, INEXINT6, INEXINT7, INEXINT8, INEXINT9,
		INEXINT10, INEXINT11, INEXINT12, INEXINT13, INEXINT14,
		INEXINT15,
	 }
	},
	{ RZV2M_CONFIG_REG(INT_MSK, 0x5b4, BIT_WRITE_ENABLE, 1, 1, 16) {
		INEXINT16, INEXINT17, INEXINT18, INEXINT19, INEXINT20,
		INEXINT21, INEXINT22, INEXINT23, INEXINT24, INEXINT25,
		INEXINT26, INEXINT27, INEXINT28, INEXINT29, INEXINT30,
		INEXINT31,
	 }
	},
	{ RZV2M_CONFIG_REG(INT_MSK, 0x5b8, BIT_WRITE_ENABLE, 1, 1, 7) {
		INEXINT32, INEXINT33, INEXINT34, INEXINT35, INEXINT36,
		INEXINT37, INEXINT38,
	 }
	},
};

const struct rzv2m_pfc_info r8arzv2m_pinmux_info = {
	.pins = pinctrl_pins,
	.groups = groups_mux,
	.funcs = funcs_mux,
	.config_regs = config_regs,
	.ops = &rzv2m_pfc_ops_map,
	.irq_regs = irq_regs,
	.nr_pins = ARRAY_SIZE(pinctrl_pins),
	.nr_groups = ARRAY_SIZE(groups_mux),
	.nr_funcs = ARRAY_SIZE(funcs_mux),
	.nr_config_regs = ARRAY_SIZE(config_regs),
	.nr_irq_regs = ARRAY_SIZE(irq_regs),
};

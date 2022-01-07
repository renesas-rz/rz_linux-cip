/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 */
#ifndef __DT_BINDINGS_CLOCK_R9A07G043F_CPG_MSSR_H__
#define __DT_BINDINGS_CLOCK_R9A07G043F_CPG_MSSR_H__

#include <dt-bindings/clock/renesas-cpg-mssr.h>

/* R9A07G043F CPG Core Clocks */
#define R9A07G043F_CLK_I2               1
#define R9A07G043F_CLK_G                2
#define R9A07G043F_CLK_S0               3
#define R9A07G043F_CLK_S1               4
#define R9A07G043F_CLK_SPI0             5
#define R9A07G043F_CLK_SPI1             6
#define R9A07G043F_CLK_SD0              7
#define R9A07G043F_CLK_SD1              8
#define R9A07G043F_CLK_M0               9
#define R9A07G043F_CLK_M1               10
#define R9A07G043F_CLK_M2               11
#define R9A07G043F_CLK_M3               12
#define R9A07G043F_CLK_M4               13
#define R9A07G043F_CLK_HP               14
#define R9A07G043F_CLK_TSU              15
#define R9A07G043F_CLK_ZT               16
#define R9A07G043F_CLK_P0               17
#define R9A07G043F_CLK_P1               18
#define R9A07G043F_CLK_P2               19
#define R9A07G043F_CLK_AT               20
#define R9A07G043F_OSCCLK               21

/* RZFIVE Module Clocks */
#define R9A07G043F_CLK_NCEPLDM 0
#define R9A07G043F_CLK_NCEPLMT 1
#define R9A07G043F_CLK_NCEPLIC 2

/* RZFIVE AX45MPSS Module Clocks */
#define R9A07G043F_CLK_AX45MP 3

//#define R9A07G043F_CLK_NCEPLDM_DM_CLK 0
//#define R9A07G043F_CLK_AX45MP_CORE0_CL 
//#define R9A07G043F_CLK_AX45MP_ACLK 

/* R9A07G043F Module Clocks not verified */
#define R9A07G043F_CLK_IA55             4
#define R9A07G043F_CLK_SYC              5
#define R9A07G043F_CLK_DMAC             6
#define R9A07G043F_CLK_SYSC             7
#define R9A07G043F_CLK_MTU              8
#define R9A07G043F_CLK_GPT              9
#define R9A07G043F_CLK_ETH0             10
#define R9A07G043F_CLK_ETH1             11
#define R9A07G043F_CLK_I2C0             12
#define R9A07G043F_CLK_I2C1             13
#define R9A07G043F_CLK_I2C2             14
#define R9A07G043F_CLK_I2C3             15
#define R9A07G043F_CLK_SCIF0            16
#define R9A07G043F_CLK_SCIF1            17
#define R9A07G043F_CLK_SCIF2            18
#define R9A07G043F_CLK_SCIF3            19
#define R9A07G043F_CLK_SCIF4            20
#define R9A07G043F_CLK_SCI0             21
#define R9A07G043F_CLK_SCI1             22
#define R9A07G043F_CLK_GPIO             23
#define R9A07G043F_CLK_SDHI0            24
#define R9A07G043F_CLK_SDHI1            25
#define R9A07G043F_CLK_USB0             26
#define R9A07G043F_CLK_USB1             27
#define R9A07G043F_CLK_CANFD            28
#define R9A07G043F_CLK_SSI0             29
#define R9A07G043F_CLK_SSI1             30
#define R9A07G043F_CLK_SSI2             31
#define R9A07G043F_CLK_SSI3             32
#define R9A07G043F_CLK_MHU              33
#define R9A07G043F_CLK_OSTM0            34
#define R9A07G043F_CLK_OSTM1            35
#define R9A07G043F_CLK_OSTM2            36
#define R9A07G043F_CLK_WDT0             37
#define R9A07G043F_CLK_WDT2             38
#define R9A07G043F_CLK_WDT_PON          39
#define R9A07G043F_CLK_ISU              40
#define R9A07G043F_CLK_CRU              41
#define R9A07G043F_CLK_LCDC             42
#define R9A07G043F_CLK_SRC              43
#define R9A07G043F_CLK_RSPI0            44
#define R9A07G043F_CLK_RSPI1            45
#define R9A07G043F_CLK_RSPI2            46
#define R9A07G043F_CLK_ADC              47
#define R9A07G043F_CLK_TSU_PCLK         48
#define R9A07G043F_CLK_SPI              49
#define R9A07G043F_CLK_CSI2             50

#endif /* __DT_BINDINGS_CLOCK_R9A07G043F_CPG_H__ */

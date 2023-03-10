/*
 * Driver for the Renesas Clock ON/OFF Control Register(COCR)
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/r9a09g011gbg-cpg-cocr.h>
#include "renesas-cpg-cocr.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = 0,
	/* External Input Clocks */
//	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_MAIN_2,
	CLK_MAIN_24,
//	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL2_2,
	CLK_PLL2_8,
	CLK_PLL2_16,
	CLK_PLL6,
//	CLK_DIV_A,
	CLK_DIV_B,
	CLK_DIV_D,
	CLK_DIV_E,
	CLK_DIV_I,
	CLK_SEL_B,
	CLK_SEL_D,
	CLK_SEL_E,
	CLK_SEL_CSI0,
	CLK_SEL_CSI1,
	CLK_SEL_CSI2,
	CLK_SEL_CSI3,
	CLK_SEL_CSI4,
	CLK_SEL_CSI5,
	CLK_SEL_W0,
	CLK_SEL_W1,
	CLK_SEL_SDI0,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r9a09g055ma3gbg_core_clks[] __initconst = {

	/* Internal Core Clocks */
        DEF_RATE(".main",      CLK_MAIN,        CLK_EXTAL_48MHZ),
        DEF_RATE(".main2",     CLK_MAIN_2,      CLK_EXTAL_48MHZ/2),
        DEF_RATE(".main24",    CLK_MAIN_24,     CLK_EXTAL_48MHZ/24),
        DEF_RATE(".pll2",      CLK_PLL2,        CLK_PLL2_1600MHZ),
        DEF_RATE(".pll2_2",    CLK_PLL2_2,      CLK_PLL2_1600MHZ/2),
        DEF_RATE(".pll2_8",    CLK_PLL2_8,      CLK_PLL2_1600MHZ/8),
        DEF_RATE(".pll2_16",   CLK_PLL2_16,     CLK_PLL2_1600MHZ/16),
        DEF_RATE(".pll6",      CLK_PLL6,        CLK_PLL6_1260MHZ),

	DEF_DIV(".divb",     CLK_DIV_B,          CLK_PLL2,         4,
		CPG_SYS_DDIV, CPG_SYS_DDIV_WEN_DIVB|CPG_SYS_DDIV_WEN_DIVE|CPG_SYS_DDIV_WEN_DIVD,
		CPG_CLKSTATUS_DIVB|CPG_CLKSTATUS_DIVD|CPG_CLKSTATUS_DIVE, 0),
	DEF_DIV(".divd",     CLK_DIV_D,          CLK_PLL2,         8,
		CPG_SYS_DDIV, CPG_SYS_DDIV_WEN_DIVB|CPG_SYS_DDIV_WEN_DIVE|CPG_SYS_DDIV_WEN_DIVD,
		CPG_CLKSTATUS_DIVB|CPG_CLKSTATUS_DIVD|CPG_CLKSTATUS_DIVE, 0),
	DEF_DIV(".dive",     CLK_DIV_E,          CLK_PLL2,         16,
		CPG_SYS_DDIV, CPG_SYS_DDIV_WEN_DIVB|CPG_SYS_DDIV_WEN_DIVE|CPG_SYS_DDIV_WEN_DIVD,
		CPG_CLKSTATUS_DIVB|CPG_CLKSTATUS_DIVD|CPG_CLKSTATUS_DIVE, 0),
	DEF_DIV(".divi",     CLK_DIV_I,          CLK_PLL2,         4,
		CPG_ISP_DDIV2, CPG_ISP_DDIV2_WEN_DIVI,	CPG_CLKSTATUS_DIVI, 0),
	DEF_DIV(".selb",     CLK_SEL_B,          CLK_DIV_B,  1,
		CPG_CLK48_DSEL, CPG_CLK48_DSEL_WEN_SELB, 0,     CPG_CLK48_DSEL_SELB),
	DEF_DIV(".seld",     CLK_SEL_D,          CLK_DIV_D,  1,
		CPG_CLK48_DSEL, CPG_CLK48_DSEL_WEN_SELD, 0,     CPG_CLK48_DSEL_SELD),
	DEF_DIV(".sele",     CLK_SEL_E,          CLK_DIV_E,  1,
		CPG_CLK48_DSEL, CPG_CLK48_DSEL_WEN_SELE, 0,     CPG_CLK48_DSEL_SELE),
	
	DEF_STATIC(".selcsi0",     CLK_SEL_CSI0,          CLK_MAIN,       2,
		CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI0,       CPG_CSI_RCLK_SSEL_SELCSI0),
	DEF_STATIC(".selcsi1",     CLK_SEL_CSI1,          CLK_MAIN,       2,
		CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI1,       CPG_CSI_RCLK_SSEL_SELCSI1),
	DEF_STATIC(".selcsi2",     CLK_SEL_CSI2,          CLK_MAIN,       2,
		CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI2,       CPG_CSI_RCLK_SSEL_SELCSI2),
	DEF_STATIC(".selcsi3",     CLK_SEL_CSI3,          CLK_MAIN,       2,
		CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI3,       CPG_CSI_RCLK_SSEL_SELCSI3),
	DEF_STATIC(".selcsi4",     CLK_SEL_CSI4,          CLK_MAIN,       2,
		CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI4,       CPG_CSI_RCLK_SSEL_SELCSI4),
	DEF_STATIC(".selcsi5",     CLK_SEL_CSI5,          CLK_MAIN,       2,
		CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI5,       CPG_CSI_RCLK_SSEL_SELCSI5),
	DEF_STATIC(".selw",     CLK_SEL_W0,               CLK_MAIN,       1,
		CPG_URT_RCLK_SSEL, CPG_URT_RCLK_SSEL_WEN_SELW0,         CPG_URT_RCLK_SSEL_SELW0),
	DEF_STATIC(".selw1",    CLK_SEL_W1,               CLK_MAIN,       1,
		CPG_URT_RCLK_SSEL, CPG_URT_RCLK_SSEL_WEN_SELW1,         CPG_URT_RCLK_SSEL_SELW1),
	DEF_STATIC(".selsdi0",     CLK_SEL_SDI0,          CLK_PLL2,       8,
		CPG_SDIEMM_SSEL,   CPG_SDIEMM_SSEL_WEN_SELSDI,          CPG_SDIEMM_SSEL_SELSDI),
};

static const struct cocr_mod_clk r9a09g055ma3gbg_mod_clks[] __initconst = {
	DEF_MOD("dmaa_aclk",            111,    CLK_SEL_D   ),
	DEF_MOD("sdi0_aclk",            300,    CLK_SEL_D   ),
	DEF_MOD("sdi0_imclk",           301,    CLK_SEL_SDI0),
	DEF_MOD("sdi0_imclk2",          302,    CLK_SEL_SDI0),
	DEF_MOD("sdi0_clk_hs",          303,    CLK_PLL2_2  ),
	DEF_MOD("sdi1_aclk",            304,    CLK_SEL_D   ),
	DEF_MOD("sdi1_imclk",           305,    CLK_SEL_SDI0),
	DEF_MOD("sdi1_imclk2",          306,    CLK_SEL_SDI0),
	DEF_MOD("sdi1_clk_hs",          307,    CLK_PLL2_2  ),
	DEF_MOD("emm_aclk",             308,    CLK_SEL_D   ),
	DEF_MOD("emm_imclk",            309,    CLK_SEL_SDI0),
	DEF_MOD("emm_imclk2",           310,    CLK_SEL_SDI0),
	DEF_MOD("emm_clk_hs",           311,    CLK_PLL2_2  ),
	DEF_MOD("pci_aclk",             400,    CLK_SEL_D   ),
	DEF_MOD("pci_clk_pmu",          401,    CLK_SEL_D   ),
	DEF_MOD("pci_apb_clk",          402,    CLK_SEL_E   ),
	DEF_MOD("usb_aclk_h",           404,    CLK_SEL_D   ),
	DEF_MOD("usb_aclk_p",           405,    CLK_SEL_D   ),
	DEF_MOD("usb_pclk",             406,    CLK_SEL_E   ),
	DEF_MOD("eth0_clk_axi",         408,    CLK_PLL2_8, ),
//	DEF_MOD("eth0_clk_chi",         408,    CLK_PLL2_16,),
	DEF_MOD("eth0_clk_gptp_extern", 409,    CLK_PLL2_16 ),
	DEF_MOD("iic_pclk_0",           912,    CLK_SEL_E   ),
	DEF_MOD("tim_clk_0",            904,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_1",            905,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_2",            906,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_3",            907,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_4",            908,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_5",            909,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_6",            910,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_7",            911,    CLK_MAIN_24 ),
	DEF_MOD("tim_clk_8",            1004,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_9",            1005,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_10",           1006,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_11",           1007,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_12",           1008,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_13",           1009,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_14",           1010,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_15",           1011,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_16",           1104,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_17",           1105,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_18",           1106,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_19",           1107,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_20",           1108,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_21",           1109,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_22",           1110,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_23",           1111,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_24",           1204,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_25",           1205,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_26",           1206,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_27",           1207,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_28",           1208,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_29",           1209,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_30",           1210,   CLK_MAIN_24 ),
	DEF_MOD("tim_clk_31",           1211,   CLK_MAIN_24 ),
	DEF_MOD("iic_pclk_1",           1012,   CLK_SEL_E   ),
	DEF_MOD("pwm_clk_0",            1304,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_1",            1305,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_2",            1306,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_3",            1307,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_4",            1308,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_5",            1309,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_6",            1310,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_7",            1311,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_8",            1404,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_9",            1405,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_10",           1406,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_11",           1407,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_12",           1408,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_13",           1409,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_14",           1410,   CLK_MAIN    ),
	DEF_MOD("pwm_clk_15",           1411,   CLK_MAIN    ),
	DEF_MOD("urt_pclk",             1504,   CLK_SEL_E   ),
	DEF_MOD("urt_clk_0",            1505,   CLK_SEL_W0  ),
	DEF_MOD("urt_clk_1",            1506,   CLK_SEL_W1  ),
	DEF_MOD("csi_clk_0",            1508,   CLK_SEL_CSI0),
	DEF_MOD("csi_clk_1",            1509,   CLK_SEL_CSI1),
	DEF_MOD("csi_clk_2",            1510,   CLK_SEL_CSI2),
	DEF_MOD("csi_clk_3",            1511,   CLK_SEL_CSI3),
	DEF_MOD("csi_clk_4",            1512,   CLK_SEL_CSI4),
	DEF_MOD("csi_clk_5",            1513,   CLK_SEL_CSI5),
	DEF_MOD("drpa_aclk",            2000,   CLK_SEL_B   ),
	DEF_MOD("drpa_dclk",            2001,   CLK_PLL6    ),
	DEF_MOD("drpa_initclk",         2002,   CLK_MAIN    ),
	DEF_MOD("cperi_grpa_pclk",      900,    CLK_SEL_E   ),
	DEF_MOD("cperi_grpb_pclk",      1000,   CLK_SEL_E   ),
	DEF_MOD("cperi_grpc_pclk",      1100,   CLK_SEL_E   ),
	DEF_MOD("cperi_grpd_pclk",      1200,   CLK_SEL_E   ),
	DEF_MOD("cperi_grpe_pclk",      1300,   CLK_SEL_E   ),
	DEF_MOD("cperi_grpf_pclk",      1400,   CLK_SEL_E   ),
	DEF_MOD("cperi_grpg_pclk",      1500,   CLK_SEL_E   ),
	DEF_MOD("cperi_grph_pclk",      1501,   CLK_SEL_E   ),
	DEF_MOD("wdt_pclk_0",           1112,   CLK_SEL_E   ),
	DEF_MOD("wdt_clk_0",            1113,   CLK_MAIN    ),
	DEF_MOD("wdt_pclk_1",           1114,   CLK_SEL_E   ),
	DEF_MOD("wdt_clk_1",            1115,   CLK_MAIN    ),
	DEF_MOD("tsu0_plk",             114,    CLK_MAIN    ),
	DEF_MOD("tsu1_plk",             115,    CLK_MAIN    ),
	DEF_MOD("drpb_aclk",            2100,   CLK_SEL_B   ),
	DEF_MOD("drpb_dclk",            2101,   CLK_PLL6    ),
	DEF_MOD("drpb_initclk",         2102,   CLK_MAIN    ),
	DEF_MOD("vcd_aclk",             2605,   CLK_DIV_I   ),
};


static const unsigned int r9a09g011gbg_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(404),	/* usb_aclk_h */
	MOD_CLK_ID(405),	/* usb_aclk_p */
	MOD_CLK_ID(406),	/* usb_pclk */
	MOD_CLK_ID(408),	/* eth0_clk_axi */
	MOD_CLK_ID(409),	/* eth0_clk_gptp_extern */
};

static const struct rcr_reset r9a09g055ma3gbg_resets[] __initconst = {
	/*           Name                ID      Rst-type   msk(bit)  
	        clk-num     clk-id[clk-num]                 */
	"dmaa_aresetn",     107,    RST_TYPEB,  4,  
		1,  {111,    0,      0,      0,      0,      0,      0,      0,      0},
	"tsu0_resetn",      112,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {114,    0,      0,      0,      0,      0,      0,      0,      0},
	"tsu1_resetn",      113,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {115,    0,      0,      0,      0,      0,      0,      0,      0},
	"sdi0_ixrst",       300,    RST_TYPEB,  6,  
		1,  {302,    0,      0,      0,      0,      0,      0,      0,      0},
	"sdi1_ixrst",       301,    RST_TYPEB,  7,  
		1,  {306,    0,      0,      0,      0,      0,      0,      0,      0},
	"emm_ixrst",        302,    RST_TYPEB,  8,  
		1,  {310,    0,      0,      0,      0,      0,      0,      0,      0},
	"usb_preset_n",     307,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {406,    0,      0,      0,      0,      0,      0,      0,      0},
	"usb_drd_reset",    308,    RST_TYPEA,  RST_MON_UNUSED, 
		2,  {404,    405,    0,      0,      0,      0,      0,      0,      0},
	"usb_aresetn_p",    309,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {405,    0,      0,      0,      0,      0,      0,      0,      0},
	"usb_aresetn_h",    310,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {404,    0,      0,      0,      0,      0,      0,      0,      0},
	"eth0_rst_hw_n",    311,    RST_TYPEB,  11, 
		1,  {408,    0,      0,      0,      0,      0,      0,      0,      0},
	"pci_aresetn",      312,    RST_TYPEA,  RST_MON_UNUSED, 
		2,  {400,    401,    0,      0,      0,      0,      0,      0,      0},
	"tim_gpa_presetn",  600,    RST_TYPEA,  RST_MON_UNUSED, 
		9,  {900,    904,    905,    906,    907,    908,    909,    910,    911},
	"tim_gpb_presetn",  601,    RST_TYPEA,  RST_MON_UNUSED, 
		9,  {1000,   1004,   1005,   1006,   1007,   1008,   1009,   1010,   1011},
	"tim_gpc_presetn",  602,    RST_TYPEA,  RST_MON_UNUSED, 
		9,  {1100,   1104,   1105,   1106,   1107,   1108,   1109,   1110,   1111},
	"tim_gpd_presetn",  603,    RST_TYPEA,  RST_MON_UNUSED, 
		9,  {1200,   1204,   1205,   1206,   1207,   1208,   1209,   1210,   1211},
	"pwm_gpe_presetn",  604,    RST_TYPEB,  22, 
		9,  {1300,   1304,   1305,   1306,   1307,   1308,   1309,   1310,   1311},
	"pwm_gpf_presetn",  605,    RST_TYPEB,  23, 
		9,  {1400,   1404,   1405,   1406,   1407,   1408,   1409,   1410,   1411},
	"csi_gpg_presetn",  606,    RST_TYPEB,  24, 
		1,  {1500,   0,      0,      0,      0,      0,      0,      0,      0},
	"csi_gph_presetn",  607,    RST_TYPEB,  25, 
		1,  {1501,   0,      0,      0,      0,      0,      0,      0,      0},
	"iic_gpa_presetn",  608,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {912,    0,      0,      0,      0,      0,      0,      0,      0},
	"iic_gpb_presetn",  609,    RST_TYPEA,  RST_MON_UNUSED, 
		1,  {1012,   0,      0,      0,      0,      0,      0,      0,      0},
	"urt_presetn",      610,    RST_TYPEB,  26, 
		3,  {1505,   1506,   1504,   0,      0,      0,      0,      0,      0},
	"wdt_presetn[0]",   612,    RST_TYPEB,  19, 
		2,  {1113,   1112,   0,      0,      0,      0,      0,      0,      0},
	"wdt_presetn[1]",   613,    RST_TYPEB,  20, 
		2,  {1115,   1114,   0,      0,      0,      0,      0,      0,      0},
	"wdt_presetn[2]",   614,    RST_TYPEB,  21, 
		2,  {1213,   1212,   0,      0,      0,      0,      0,      0,      0},
	"drpa_aresetn",     900,    RST_TYPEB,  14, 
		1,  {2002,   0,      0,      0,      0,      0,      0,      0,      0},
	"drpb_aresetn",     1000,   RST_TYPEB,  15, 
		1,  {2102,   0,      0,      0,      0,      0,      0,      0,      0},
	"vcd_resetn",       1407,   RST_TYPEB,  18, 
		1,  {2605,   0,   0,      0,      0,      0,      0,      0,      0},
};

static int __init r9a09g011gbg_cpg_cocr_init(struct device *dev)
{
	return 0;
}

const struct cpg_cocr_info r9a09g055ma3gbg_cpg_cocr_info __initconst = {
	/* Core Clocks */
	.core_clks = r9a09g055ma3gbg_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a09g055ma3gbg_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r9a09g055ma3gbg_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a09g055ma3gbg_mod_clks),
	.num_hw_mod_clks = 27 * 32,/*20 Registers * 32bits */

	/* Critical Module Clocks */
	.crit_mod_clks = r9a09g011gbg_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r9a09g011gbg_crit_mod_clks),

	/*Resets*/
	.resets = r9a09g055ma3gbg_resets,
	.num_resets = ARRAY_SIZE(r9a09g055ma3gbg_resets),
	.num_hw_resets = 15 * 32, /*15 Registers * 32bits */ //[TODO]

       /* Callbacks */
	.init = r9a09g011gbg_cpg_cocr_init,
};

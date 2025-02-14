// SPDX-License-Identifier: GPL-2.0
/*
 * r9a09g077 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 *
 * Based on r8a7796-cpg-mssr.c
 *
 * Copyright (C) 2016 Glider bvba
 */

#include <linux/device.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/r9a09g077-cpg.h>

#include "rzt2-cpg.h"

/* Register desciption */

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	/* External Input Clocks */
	LAST_DT_CORE_CLK = R9A09G077_LCDC_CLKD,
	CLK_EXTAL,
	CLK_LOCO,

	/* Internal Core Clocks */
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL4,
	CLK_SEL_PLL0,
	CLK_SEL_CLK_PLL0,
	CLK_SEL_PLL1,
	CLK_SEL_CLK_PLL1,
	CLK_SEL_PLL2,
	CLK_SEL_CLK_PLL2,
	CLK_SEL_PLL4,
	CLK_SEL_CLK_PLL4,
	CLK_SEL_CLK_SRC,
	CLK_SEL_EXTAL,
	CLK_SEL_LOCO,
	CLK_PLL3_INPUT,
	/* Module Clocks */
	MOD_CLK_BASE,
};

static const struct clk_div_table dtable_1_2[] = {
	{0, 2},
	{0xF, 1},
	{0, 0},
};

static const struct clk_div_table dtable_24_32[] = {
	{0, 32},
	{1, 30},
	{2, 25},
	{3, 24},
	{0, 0},
};

static const struct clk_div_table dtable_2_32[] = {
	{0, 2},
	{1, 4},
	{2, 6},
	{3, 8},
	{4, 10},
	{5, 12},
	{6, 14},
	{7, 16},
	{8, 18},
	{9, 20},
	{10, 22},
	{11, 24},
	{12, 26},
	{13, 28},
	{14, 30},
	{15, 32},
	{0, 0},
};

/* Mux clock tables */
static const char * const sel_eth_phy[] = { ".pll1_eth_phy", ".osc_eth_phy" };
static const char * const eth_refclk[] = {".eth_clk_e", ".eth_clk_extal"};
static const char * const sel_clk_src[] = { ".sel_loco", ".sel_extal" };
static const char * const sel_clk_pll0[] = { ".sel_loco", ".sel_pll0" };
static const char * const sel_clk_pll1[] = { ".sel_loco", ".sel_pll1" };
static const char * const sel_clk_pll2[] = { ".sel_loco", ".sel_pll2" };
static const char * const sel_clk_pll4[] = { ".sel_loco", ".sel_pll4" };

static const struct {
	struct cpg_core_clk common[45];
} core_clks __initconst = {
	.common = {
		/* External Clock Inputs */
		DEF_INPUT("extal",     CLK_EXTAL),
		DEF_INPUT("loco",     CLK_LOCO),

		/* Internal Core Clocks */
		DEF_FIXED(".pll0", CLK_PLL0, CLK_EXTAL, 48, 1),
		DEF_FIXED(".pll1", CLK_PLL1, CLK_EXTAL, 40, 1),
		DEF_FIXED(".pll2", CLK_PLL2, CLK_EXTAL, 32, 1),
		DEF_FIXED(".pll4", CLK_PLL4, CLK_EXTAL, 96, 1),
		DEF_FIXED(".sel_pll0", CLK_SEL_PLL0, CLK_PLL0, 1, 1),
		DEF_MUX(".sel_clk_pll0", CLK_SEL_CLK_PLL0, SEL_PLL,
			sel_clk_pll0, ARRAY_SIZE(sel_clk_pll0), 0, CLK_MUX_READ_ONLY),
		DEF_FIXED(".sel_pll1", CLK_SEL_PLL1, CLK_PLL1, 1, 1),
		DEF_MUX(".sel_clk_pll1", CLK_SEL_CLK_PLL1, SEL_PLL,
			sel_clk_pll1, ARRAY_SIZE(sel_clk_pll1), 0, CLK_MUX_READ_ONLY),
		DEF_FIXED(".sel_pll2", CLK_SEL_PLL2, CLK_PLL2, 1, 1),
		DEF_MUX(".sel_clk_pll2", CLK_SEL_CLK_PLL2, SEL_PLL,
			sel_clk_pll2, ARRAY_SIZE(sel_clk_pll2), 0, CLK_MUX_READ_ONLY),
		DEF_FIXED(".sel_pll4", CLK_SEL_PLL4, CLK_PLL4, 1, 1),
		DEF_MUX(".sel_clk_pll4", CLK_SEL_CLK_PLL4, SEL_PLL,
			sel_clk_pll4, ARRAY_SIZE(sel_clk_pll4), 0, CLK_MUX_READ_ONLY),
		DEF_FIXED(".pll3_input", CLK_PLL3_INPUT, CLK_SEL_CLK_PLL4, 1, 50),
		DEF_SAMPLL(".pll3", CLK_PLL3, CLK_PLL3_INPUT, PLL3_CONF),

		/* Core output clk */
		DEF_DIV("CA55", R9A09G077_CA55, CLK_SEL_CLK_PLL0, DIVCA55,
					dtable_1_2, 0, 1),
		DEF_FIXED("SDHIHS", R9A09G077_SDHIHS, CLK_SEL_CLK_PLL2, 1, 1),
		DEF_FIXED(".osc_eth_phy", CLK_OSC_ETH_PHY, CLK_EXTAL, 1, 1),
		DEF_FIXED("PCLKAH", R9A09G077_PCLKAH, CLK_SEL_CLK_PLL4, 1, 6),
		DEF_FIXED("PCLKAM", R9A09G077_PCLKAM, CLK_SEL_CLK_PLL4, 1, 12),
		DEF_FIXED("PCLKAL", R9A09G077_PCLKAL, CLK_SEL_CLK_PLL4, 1, 24),
		DEF_FIXED("DFICLK", R9A09G077_DFI, CLK_PLL2, 1, 1),
		DEF_FIXED("PCLKH", R9A09G077_PCLKH, CLK_SEL_CLK_PLL1, 1, 4),
		DEF_FIXED("PCLKM", R9A09G077_PCLKM, CLK_SEL_CLK_PLL1, 1, 8),
		DEF_FIXED("PCLKL", R9A09G077_PCLKL, CLK_SEL_CLK_PLL1, 1, 16),
		DEF_FIXED("PCLKGPTL", R9A09G077_PCLKGPTL, CLK_SEL_CLK_PLL1, 1, 2),
		DEF_FIXED("PCLKSHOST", R9A09G077_PCLKSHOST, CLK_SEL_CLK_PLL4, 1, 6),
		DEF_FIXED("PCLKRTC", R9A09G077_PCLKRTC, CLK_EXTAL, 1, 128),
		DEF_FIXED("USB", R9A09G077_USB, CLK_SEL_CLK_PLL4, 1, 48),
		DEF_DIV("SPI0", R9A09G077_SPI0, CLK_SEL_CLK_PLL4, DIVSPI0,
				dtable_24_32, 0, 0),
		DEF_DIV("SPI1", R9A09G077_SPI1, CLK_SEL_CLK_PLL4, DIVSPI1,
				dtable_24_32, 0, 0),
		DEF_DIV("SPI2", R9A09G077_SPI2, CLK_SEL_CLK_PLL4, DIVSPI2,
				dtable_24_32, 0, 0),
		DEF_DIV("SPI3", R9A09G077_SPI3, CLK_SEL_CLK_PLL4, DIVSPI3,
				dtable_24_32, 0, 1),
		DEF_FIXED("ETCLKA", R9A09G077_ETCLKA, CLK_SEL_CLK_PLL1, 1, 5),
		DEF_FIXED("ETCLKB", R9A09G077_ETCLKB, CLK_SEL_CLK_PLL1, 1, 8),
		DEF_FIXED("ETCLKC", R9A09G077_ETCLKC, CLK_SEL_CLK_PLL1, 1, 10),
		DEF_FIXED("ETCLKD", R9A09G077_ETCLKD, CLK_SEL_CLK_PLL1, 1, 20),
		DEF_FIXED("ETCLKE", R9A09G077_ETCLKE, CLK_SEL_CLK_PLL1, 1, 40),
		DEF_FIXED(".eth_clk_e", R9A09G077_ETHCLKE, R9A09G077_ETCLKE, 1, 1),
		DEF_FIXED(".eth_clk_extal", R9A09G077_ETHCLK_EXTAL, CLK_EXTAL, 1, 1),
		DEF_MUX(".eth_refclk", R9A09G077_ETH_REFCLK, DIVETHPHY,
				eth_refclk, ARRAY_SIZE(eth_refclk), 0, 0),
		DEF_FIXED("LCDC_CLKA", R9A09G077_LCDC_CLKA, R9A09G077_PCLKAH,
			  1, 1),
		DEF_FIXED("LCDC_CLKP", R9A09G077_LCDC_CLKP, R9A09G077_PCLKAL,
			  1, 1),
		DEF_DIV("LCDC_CLKD", R9A09G077_LCDC_CLKD, CLK_PLL3, DIVLCDC,
			dtable_2_32, 0, 0),
	},
};

static const struct {
	struct rzt2_mod_clk common[61];
} mod_clks = {
	.common = {
		DEF_MOD("sci0",         R9A09G077_SCI0_CLK, R9A09G077_PCLKM,
					0x300, 8, 0),
		DEF_MOD("lcdc",		R9A09G077_LCDC_CLK, R9A09G077_LCDC_CLKD,
					0x330, 4, 0),
		DEF_MOD("pcie",		R9A09G077_PCIE_CLK, R9A09G077_PCLKAH,
					0X330, 8, 0),
		DEF_MOD("usb",		R9A09G077_USB_CLK, R9A09G077_USB,
					0x310, 8, 0),
		DEF_MOD("sdhi0",	R9A09G077_SDHI0_CLK, R9A09G077_PCLKAM,
					0x330, 12, 0),
		DEF_MOD("sdhi1",	R9A09G077_SDHI1_CLK, R9A09G077_PCLKAM,
					0x330, 13, 0),
		DEF_MOD("mtu3",		R9A09G077_MTU3_CLK, R9A09G077_PCLKH,
					0x308, 0, 0),
		DEF_MOD("gpt0",		R9A09G077_GPT0_CLK, R9A09G077_PCLKGPTL,
					0x308, 1, 0),
		DEF_MOD("gpt1",		R9A09G077_GPT1_CLK, R9A09G077_PCLKGPTL,
					0x308, 2, 0),
		DEF_MOD("gpt2",		R9A09G077_GPT2_CLK, R9A09G077_PCLKGPTL,
					0x308, 16, 0),
		DEF_MOD("gpt3",		R9A09G077_GPT3_CLK, R9A09G077_PCLKGPTL,
					0x308, 17, 0),
		DEF_MOD("gpt4",		R9A09G077_GPT4_CLK, R9A09G077_PCLKGPTL,
					0x308, 18, 0),
		DEF_MOD("gpt5",		R9A09G077_GPT5_CLK, R9A09G077_PCLKGPTL,
					0x308, 19, 0),
		DEF_MOD("gpt6",		R9A09G077_GPT6_CLK, R9A09G077_PCLKGPTL,
					0x308, 20, 0),
		DEF_MOD("gpt7",		R9A09G077_GPT7_CLK, R9A09G077_PCLKGPTL,
					0x308, 21, 0),
		DEF_MOD("gpt8",		R9A09G077_GPT8_CLK, R9A09G077_PCLKGPTL,
					0x308, 22, 0),
		DEF_MOD("gpt9",		R9A09G077_GPT9_CLK, R9A09G077_PCLKM,
					0x308, 23, 0),
		DEF_MOD("gpt10",	R9A09G077_GPT10_CLK, R9A09G077_PCLKM,
					0x318, 3, 1),
		DEF_MOD("adc0",		R9A09G077_ACD0_CLK, R9A09G077_PCLKL,
					0x308, 6, 0),
		DEF_MOD("adc1",		R9A09G077_ACD1_CLK, R9A09G077_PCLKL,
					0x308, 7, 0),
		DEF_MOD("adc2",		R9A09G077_ACD2_CLK, R9A09G077_PCLKL,
					0x308, 25, 0),
		DEF_MOD("gmac0",	R9A09G077_GMAC0_CLK, R9A09G077_PCLKH,
					0x310, 0, 0),
		DEF_MOD("gmac1",	R9A09G077_GMAC1_CLK, R9A09G077_PCLKAH,
					0x310, 16, 0),
		DEF_MOD("gmac2",	R9A09G077_GMAC2_CLK, R9A09G077_PCLKAH,
					0x310, 17, 0),
		DEF_MOD("ethss",	R9A09G077_ETHSS_CLK, R9A09G077_PCLKM,
					0x310, 3, 0),
		DEF_MOD("ethsw",	R9A09G077_ETHSW_CLK, R9A09G077_PCLKM,
					0x310, 1, 0),
		DEF_MOD("esc",		R9A09G077_ESC_CLK, R9A09G077_ETCLKC,
					0x310, 2, 0),
		DEF_MOD("shostif",	R9A09G077_SHOSTIF_CLK, R9A09G077_PCLKH,
					0x320, 1, 1),
		DEF_MOD("iic0",		R9A09G077_IIC0_CLK, R9A09G077_PCLKL,
					0x304, 0, 0),
		DEF_MOD("iic1",         R9A09G077_IIC1_CLK, R9A09G077_PCLKL,
					0x304, 1, 0),
		DEF_MOD("iic2",         R9A09G077_IIC2_CLK, R9A09G077_PCLKL,
					0x318, 1, 1),
		DEF_MOD("doc",		R9A09G077_DOC_CLK, R9A09G077_PCLKL,
					0x30C, 8, 0),
		DEF_MOD("cmt0",		R9A09G077_CMT0_CLK, R9A09G077_PCLKL,
					0x30C, 2, 0),
		DEF_MOD("cmt1",		R9A09G077_CMT1_CLK, R9A09G077_PCLKL,
					0x30C, 3, 0),
		DEF_MOD("cmt2",		R9A09G077_CMT2_CLK, R9A09G077_PCLKL,
					0x30C, 4, 0),
		DEF_MOD("cmtw0",	R9A09G077_CMTW0_CLK, R9A09G077_PCLKL,
					0x30C, 5, 0),
		DEF_MOD("cmtw1",	R9A09G077_CMTW1_CLK, R9A09G077_PCLKL,
					0x30C, 6, 0),
		DEF_MOD("tsu",		R9A09G077_TSU_CLK, R9A09G077_PCLKL,
					0x30C, 7, 0),
		DEF_MOD("spi0",		R9A09G077_SPI0_CLK, R9A09G077_SPI0,
					0x304, 4, 0),
		DEF_MOD("spi1",         R9A09G077_SPI1_CLK, R9A09G077_SPI1,
					0x304, 5, 0),
		DEF_MOD("spi2",         R9A09G077_SPI2_CLK, R9A09G077_SPI2,
					0x304, 6, 0),
		DEF_MOD("spi3",         R9A09G077_SPI3_CLK, R9A09G077_SPI3,
					0x318, 2, 1),
		DEF_MOD("sci1",		R9A09G077_SCI1_CLK, R9A09G077_PCLKM,
					0x300, 9, 0),
		DEF_MOD("sci2",		R9A09G077_SCI2_CLK, R9A09G077_PCLKM,
					0x300, 10, 0),
		DEF_MOD("sci3",		R9A09G077_SCI3_CLK, R9A09G077_PCLKM,
					0x300, 11, 0),
		DEF_MOD("sci4",		R9A09G077_SCI4_CLK, R9A09G077_PCLKM,
					0x300, 12, 0),
		DEF_MOD("sci5",		R9A09G077_SCI5_CLK, R9A09G077_PCLKM,
					0x318, 0, 1),
		DEF_MOD("scie0",	R9A09G077_SCIE0_CLK, R9A09G077_PCLKM,
					0x300, 16, 0),
		DEF_MOD("scie1",	R9A09G077_SCIE1_CLK, R9A09G077_PCLKM,
					0x300, 17, 0),
		DEF_MOD("scie2",	R9A09G077_SCIE2_CLK, R9A09G077_PCLKM,
					0x300, 18, 0),
		DEF_MOD("scie3",	R9A09G077_SCIE3_CLK, R9A09G077_PCLKM,
					0x300, 19, 0),
		DEF_MOD("scie4",	R9A09G077_SCIE4_CLK, R9A09G077_PCLKM,
					0x300, 20, 0),
		DEF_MOD("scie5",	R9A09G077_SCIE5_CLK, R9A09G077_PCLKM,
					0x300, 21, 0),
		DEF_MOD("scie6",	R9A09G077_SCIE6_CLK, R9A09G077_PCLKM,
					0x300, 22, 0),
		DEF_MOD("scie7",	R9A09G077_SCIE7_CLK, R9A09G077_PCLKM,
					0x300, 23, 0),
		DEF_MOD("scie8",	R9A09G077_SCIE8_CLK, R9A09G077_PCLKM,
					0x300, 24, 0),
		DEF_MOD("scie9",	R9A09G077_SCIE9_CLK, R9A09G077_PCLKM,
					0x300, 25, 0),
		DEF_MOD("scie10",	R9A09G077_SCIE10_CLK, R9A09G077_PCLKM,
					0x300, 26, 0),
		DEF_MOD("scie11",	R9A09G077_SCIE11_CLK, R9A09G077_PCLKM,
					0x300, 27, 0),
		DEF_MOD("rtc",		R9A09G077_RTC_CLK, R9A09G077_PCLKRTC,
					0x318, 5, 1),
		DEF_MOD("canfd",	R9A09G077_CANFD_CLK, R9A09G077_PCLKM,
					0x30C, 10, 0),
	},
};

static struct rzt2_reset r9a09g077_resets[] = {
	DEF_RST(R9A09G077_xSPI0_RST, 		MRCTLA, 4, 0),
	DEF_RST(R9A09G077_xSPI1_RST, 		MRCTLA, 5, 0),
	DEF_RST(R9A09G077_GMAC0_PCLKH_RST, 	MRCTLE, 0, 0),
	DEF_RST(R9A09G077_GMAC0_PCLKM_RST,	MRCTLE, 1, 0),
	DEF_RST(R9A09G077_ETHSW_RST,		MRCTLE, 2, 0),
	DEF_RST(R9A09G077_ESC_BUS_RST,		MRCTLE, 3, 0),
	DEF_RST(R9A09G077_ESC_IP_RST,		MRCTLE, 4, 0),
	DEF_RST(R9A09G077_ETH_SUBSYSTEM_RST,	MRCTLE, 5, 0),
	DEF_RST(R9A09G077_MII_CONVERT_RST,	MRCTLE, 6, 0),
	DEF_RST(R9A09G077_GMAC1_PCLKAH_RST,	MRCTLE, 16, 0),
	DEF_RST(R9A09G077_GMAC1_PCLKAM_RST,	MRCTLE, 17, 0),
	DEF_RST(R9A09G077_GMAC2_PCLKAH_RST,	MRCTLE, 18, 0),
	DEF_RST(R9A09G077_GMAC2_PCLKAM_RST,	MRCTLE, 19, 0),
	DEF_RST(R9A09G077_SHOSTIF_MASTER_RST,	MRCTLI, 1, 1),
	DEF_RST(R9A09G077_SHOSTIF_SLAVE_RST,	MRCTLI, 2, 1),
	DEF_RST(R9A09G077_SHOSTIF_IP_RST,	MRCTLI, 3, 1),
	DEF_RST(R9A09G077_PCIE_RST,		MRCTLM, 8, 0),
	DEF_RST(R9A09G077_DDRSS_RST_N_RST,	MRCTLM, 16, 0),
	DEF_RST(R9A09G077_DDRSS_PWROKIN_RST,	MRCTLM, 17, 0),
	DEF_RST(R9A09G077_DDRSS_RST_RST,	MRCTLM, 18, 0),
	DEF_RST(R9A09G077_DDRSS_AXI0_RST,	MRCTLM, 19, 0),
	DEF_RST(R9A09G077_DDRSS_AXI1_RST,	MRCTLM, 20, 0),
	DEF_RST(R9A09G077_DDRSS_AXI2_RST,	MRCTLM, 21, 0),
	DEF_RST(R9A09G077_DDRSS_AXI3_RST,	MRCTLM, 22, 0),
	DEF_RST(R9A09G077_DDRSS_AXI4_RST,	MRCTLM, 23, 0),
	DEF_RST(R9A09G077_DDRSS_MC_RST,		MRCTLM, 24, 0),
	DEF_RST(R9A09G077_DDRSS_PHY_RST,	MRCTLM, 25, 0),
};

static const unsigned int r9a09g077_crit_mod_clks[] __initconst = {

};

const struct rzt2_cpg_info r9a09g077_cpg_info = {
	/* Core Clocks */
	.core_clks = core_clks.common,
	.num_core_clks = ARRAY_SIZE(core_clks.common),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Critical Module Clocks */
	.crit_mod_clks = r9a09g077_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r9a09g077_crit_mod_clks),

	/* Module Clocks */
	.mod_clks = mod_clks.common,
	.num_mod_clks = ARRAY_SIZE(mod_clks.common),
	.num_hw_mod_clks = R9A09G077_LCDC_CLK + 1,

	/* Resets */
	.resets = r9a09g077_resets,
	.num_resets = R9A09G077_DDRSS_PHY_RST + 1, /* Last reset ID + 1 */
};

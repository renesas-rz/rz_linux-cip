// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3L CPG driver
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/r9a08g046-cpg.h>

#include "rzg2l-cpg.h"

/* RZ/G3L Specific registers. */
#define G3L_CPG_PL1_DDIV		(0x200)
#define G3L_CPG_PL2_DDIV		(0x204)
#define G3L_CPG_PL3_DDIV		(0x208)
#define G3L_CPG_SDHI_DDIV		(0x218)
#define G3L_CPG_PLL_DSEL		(0x240)
#define G3L_CPG_SDHI_DSEL		(0x244)
#define G3L_CLKDIVSTATUS		(0x280)
#define G3L_CLKSELSTATUS		(0x284)

/* RZ/G3L Specific division configuration.  */
#define G3L_DIVPL1A		DDIV_PACK(G3L_CPG_PL1_DDIV, 0, 3)
#define G3L_DIVPL2A		DDIV_PACK(G3L_CPG_PL2_DDIV, 0, 2)
#define G3L_DIVPL2B		DDIV_PACK(G3L_CPG_PL2_DDIV, 4, 2)
#define G3L_DIVPL3A		DDIV_PACK(G3L_CPG_PL3_DDIV, 0, 2)
#define G3L_DIVPL3B		DDIV_PACK(G3L_CPG_PL3_DDIV, 4, 2)
#define G3L_DIV_SDHI0		DDIV_PACK(G3L_CPG_SDHI_DDIV, 0, 2)

/* RZ/G3L Clock status configuration. */
#define G3L_DIVPL1_STS		DDIV_PACK(G3L_CLKDIVSTATUS, 0, 1)
#define G3L_DIVPL2A_STS		DDIV_PACK(G3L_CLKDIVSTATUS, 4, 1)
#define G3L_DIVPL2B_STS		DDIV_PACK(G3L_CLKDIVSTATUS, 5, 1)
#define G3L_DIVPL3A_STS		DDIV_PACK(G3L_CLKDIVSTATUS, 8, 1)
#define G3L_DIVPL3B_STS		DDIV_PACK(G3L_CLKDIVSTATUS, 9, 1)
#define G3L_DIV_SDHI0_STS	DDIV_PACK(G3L_CLKDIVSTATUS, 24, 1)

#define G3L_SEL_PLL4_STS	SEL_PLL_PACK(G3L_CLKSELSTATUS, 6, 1)
#define G3L_SEL_SDHI0_STS	SEL_PLL_PACK(G3L_CLKSELSTATUS, 16, 1)

/* RZ/G3L Specific clocks select. */
#define G3L_SEL_PLL4		SEL_PLL_PACK(G3L_CPG_PLL_DSEL, 6, 1)
#define G3L_SEL_SDHI0		SEL_PLL_PACK(G3L_CPG_SDHI_DSEL, 0, 2)

/* PLL 1/4/6/7 configuration registers macro. */
#define G3L_PLL1467_CONF(clk1, clk2, setting)	((clk1) << 22 | (clk2) << 12 | (setting))

#define DEF_G3L_MUX(_name, _id, _conf, _parent_names, _mux_flags, _clk_flags) \
	DEF_TYPE(_name, _id, CLK_TYPE_MUX, .conf = (_conf), \
		 .parent_names = (_parent_names), \
		 .num_parents = ARRAY_SIZE((_parent_names)), \
		 .mux_flags = CLK_MUX_HIWORD_MASK | (_mux_flags), \
		 .flag = (_clk_flags))

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A08G046_OSCCLK2,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_OSC_DIV1000,
	CLK_PLL1,
	CLK_PLL1_DIV2,
	CLK_PLL2,
	CLK_PLL2_DIV2,
	CLK_PLL2_DIV2_2,
	CLK_PLL2_DIV2_5,
	CLK_PLL2_DIV2_8,
	CLK_PLL2_DIV2_16,
	CLK_PLL2_DIV3,
	CLK_PLL2_DIV5,
	CLK_PLL2_DIV6,
	CLK_PLL2_DIV7,
	CLK_PLL3,
	CLK_PLL3_DIV2,
	CLK_PLL3_DIV2_2,
	CLK_PLL3_DIV2_8,
	CLK_PLL3_DIV2_16,
	CLK_PLL3_DIV3,
	CLK_PLL3_DIV5,
	CLK_PLL3_DIV6,
	CLK_PLL3_DIV6_2,
	CLK_PLL3_DIV7,
	CLK_PLL4,
	CLK_PLL6,
	CLK_PLL6_DIV10,
	CLK_PLL7,
	CLK_SEL_SDHI0,
	CLK_SEL_PLL4,
	CLK_SD0_DIV2,

	/* Module Clocks */
	MOD_CLK_BASE,
};

/* Divider tables */
static const struct clk_div_table dtable_1_4[] = {
	{ 0, 1 },
	{ 1, 2 },
	{ 2, 4 },
	{ 0, 0 },
};

static const struct clk_div_table dtable_1_32[] = {
	{ 0, 1 },
	{ 1, 2 },
	{ 2, 4 },
	{ 3, 8 },
	{ 4, 16 },
	{ 5, 32 },
	{ 0, 0 },
};

static const struct clk_div_table dtable_4_128[] = {
	{ 0, 4 },
	{ 1, 2 },
	{ 2, 16 },
	{ 3, 128 },
	{ 0, 0 },
};

static const struct clk_div_table dtable_8_256[] = {
	{ 0, 8 },
	{ 1, 16 },
	{ 2, 32 },
	{ 3, 256 },
	{ 0, 0 },
};

/* Mux clock names tables. */
static const char * const sel_sdhi[] = { ".pll2_div2", ".pll1_div2",  ".pll6", ".pll2_div6" };
static const char * const sel_pll4[] = { ".osc_div1000", ".pll4" };

/* Mux clock indices tables. */
static const u32 mtable_sd[] = { 0, 1, 2, 3 };
static const u32 mtable_pll4[] = { 0, 1 };

static const struct cpg_core_clk r9a08g046_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal", CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_FIXED(".osc_div1000", CLK_OSC_DIV1000, CLK_EXTAL, 1, 1000),
	DEF_G3S_PLL(".pll1", CLK_PLL1, CLK_EXTAL, G3L_PLL1467_CONF(0x4, 0x8, 0x100),
		    1200000000UL),
	DEF_FIXED(".pll2", CLK_PLL2, CLK_EXTAL, 200, 3),
	DEF_FIXED(".pll3", CLK_PLL3, CLK_EXTAL, 200, 3),
	DEF_G3S_PLL(".pll4", CLK_PLL4, CLK_EXTAL, G3L_PLL1467_CONF(0x34, 0x38, 0),
		    1067000000UL),
	DEF_G3S_PLL(".pll6", CLK_PLL6, CLK_EXTAL, G3L_PLL1467_CONF(0x54, 0x58, 0),
		    500000000UL),
	DEF_G3S_PLL(".pll7", CLK_PLL7, CLK_EXTAL, G3L_PLL1467_CONF(0x84, 0x88, 0),
		    3000000000UL),
	DEF_FIXED(".pll1_div2", CLK_PLL1_DIV2, CLK_PLL1, 1, 2),
	DEF_FIXED(".pll2_div2", CLK_PLL2_DIV2, CLK_PLL2, 1, 2),
	DEF_FIXED(".pll2_div2_8", CLK_PLL2_DIV2_8, CLK_PLL2_DIV2, 1, 8),
	DEF_FIXED(".pll2_div3", CLK_PLL2_DIV3, CLK_PLL2, 1, 3),
	DEF_FIXED(".pll2_div5", CLK_PLL2_DIV5, CLK_PLL2, 1, 5),
	DEF_FIXED(".pll2_div6", CLK_PLL2_DIV6, CLK_PLL2, 1, 6),
	DEF_FIXED(".pll2_div7", CLK_PLL2_DIV7, CLK_PLL2, 1, 7),
	DEF_FIXED(".pll3_div2", CLK_PLL3_DIV2, CLK_PLL3, 1, 2),
	DEF_FIXED(".pll3_div2_2", CLK_PLL3_DIV2_2, CLK_PLL3_DIV2, 1, 2),
	DEF_FIXED(".pll3_div2_8", CLK_PLL3_DIV2_8, CLK_PLL3_DIV2, 1, 8),
	DEF_FIXED(".pll3_div3", CLK_PLL3_DIV3, CLK_PLL3, 1, 3),
	DEF_FIXED(".pll3_div5", CLK_PLL3_DIV5, CLK_PLL3, 1, 5),
	DEF_FIXED(".pll3_div6", CLK_PLL3_DIV6, CLK_PLL3, 1, 6),
	DEF_FIXED(".pll3_div7", CLK_PLL3_DIV7, CLK_PLL3, 1, 7),
	DEF_FIXED(".pll6_div10", CLK_PLL6_DIV10, CLK_PLL6, 1, 10),
	DEF_SD_MUX(".sel_sd0", CLK_SEL_SDHI0, G3L_SEL_SDHI0, G3L_SEL_SDHI0_STS, sel_sdhi,
		   mtable_sd, 0, NULL),
	DEF_SD_MUX(".sel_pll4", CLK_SEL_PLL4, G3L_SEL_PLL4, G3L_SEL_PLL4_STS, sel_pll4,
		   mtable_pll4, CLK_SET_PARENT_GATE, NULL),

	/* Core output clk */
	DEF_G3S_DIV("I", R9A08G046_CLK_I, CLK_PLL1, G3L_DIVPL1A, G3L_DIVPL1_STS, dtable_1_32,
		    0, 0, 0, NULL),
	DEF_G3S_DIV("P0", R9A08G046_CLK_P0, CLK_PLL2_DIV2, G3L_DIVPL2B, G3L_DIVPL2B_STS,
		    dtable_8_256, 0, 0, 0, NULL),
	DEF_G3S_DIV("P1", R9A08G046_CLK_P1, CLK_PLL3_DIV2, G3L_DIVPL3A, G3L_DIVPL3A_STS,
		    dtable_4_128, 0, 0, 0, NULL),
	DEF_G3S_DIV("P2", R9A08G046_CLK_P2, CLK_PLL3_DIV2, G3L_DIVPL3B, G3L_DIVPL3B_STS,
		    dtable_8_256, 0, 0, 0, NULL),
	DEF_G3S_DIV("P3", R9A08G046_CLK_P3, CLK_PLL2_DIV2, G3L_DIVPL2A, G3L_DIVPL2A_STS,
		    dtable_4_128, 0, 0, 0, NULL),
	DEF_G3S_DIV("SD0", R9A08G046_CLK_SD0, CLK_SEL_SDHI0, G3L_DIV_SDHI0, G3L_DIV_SDHI0_STS,
		    dtable_1_4, 0, 0, CLK_SET_RATE_PARENT, NULL),
	DEF_FIXED(".sd0_div2", CLK_SD0_DIV2, R9A08G046_CLK_SD0, 1, 2),
	DEF_FIXED("M0", R9A08G046_CLK_M0, CLK_PLL3_DIV2_8, 1, 1),
	DEF_FIXED("S0", R9A08G046_CLK_S0, CLK_SEL_PLL4, 1, 2),
};

static const struct rzg2l_mod_clk r9a08g046_mod_clks[] = {
	DEF_MOD("gic_gicclk",		R9A08G046_GIC600_GICCLK, R9A08G046_CLK_P1, 0x514, 0),
	DEF_MOD("ia55_clk",		R9A08G046_IA55_CLK, R9A08G046_CLK_P1, 0x518, 1),
	DEF_MOD("dmac_aclk",		R9A08G046_DMAC_ACLK, R9A08G046_CLK_P3, 0x52c, 0),
	DEF_MOD("sdhi0_imclk",		R9A08G046_SDHI0_IMCLK, CLK_SD0_DIV2, 0x554, 0),
	DEF_MOD("sdhi0_imclk2",		R9A08G046_SDHI0_IMCLK2, CLK_SD0_DIV2, 0x554, 1),
	DEF_MOD("sdhi0_clk_hs",		R9A08G046_SDHI0_CLK_HS, R9A08G046_CLK_SD0, 0x554, 2),
	DEF_MOD("sdhi0_iaclks",		R9A08G046_SDHI0_IACLKS, R9A08G046_CLK_P1, 0x554, 3),
	DEF_MOD("sdhi0_iaclkm",		R9A08G046_SDHI0_IACLKM, R9A08G046_CLK_P1, 0x554, 12),
	DEF_MOD("scif0_clk_pck",	R9A08G046_SCIF0_CLK_PCK, R9A08G046_CLK_P0, 0x584, 0),
	DEF_MOD("gpio_hclk",		R9A08G046_GPIO_HCLK, CLK_EXTAL, 0x598, 0),
};

static const struct rzg2l_reset r9a08g046_resets[] = {
	DEF_RST(R9A08G046_GIC600_GICRESET_N, 0x814, 0),
	DEF_RST(R9A08G046_GIC600_DBG_GICRESET_N, 0x814, 1),
	DEF_RST(R9A08G046_SDHI0_IXRST, 0x854, 0),
	DEF_RST(R9A08G046_SDHI0_IXRSTAXIM, 0x854, 3),
	DEF_RST(R9A08G046_SDHI0_IXRSTAXIS, 0x854, 4),
	DEF_RST(R9A08G046_SCIF0_RST_SYSTEM_N, 0x884, 0),
	DEF_RST(R9A08G046_GPIO_RSTN, 0x898, 0),
	DEF_RST(R9A08G046_GPIO_PORT_RESETN, 0x898, 1),
	DEF_RST(R9A08G046_GPIO_SPARE_RESETN, 0x898, 2),
};

static const unsigned int r9a08g046_crit_mod_clks[] __initconst = {
	MOD_CLK_BASE + R9A08G046_GIC600_GICCLK,
	MOD_CLK_BASE + R9A08G046_IA55_CLK,
};

const struct rzg2l_cpg_info r9a08g046_cpg_info = {
	/* Core Clocks */
	.core_clks = r9a08g046_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a08g046_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Critical Module Clocks */
	.crit_mod_clks = r9a08g046_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r9a08g046_crit_mod_clks),

	/* Module Clocks */
	.mod_clks = r9a08g046_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a08g046_mod_clks),
	.num_hw_mod_clks = R9A08G046_VBAT_BCLK + 1,

	/* Resets */
	.resets = r9a08g046_resets,
	.num_resets = R9A08G046_VBAT_BRESETN + 1, /* Last reset ID + 1 */

	.has_clk_mon_regs = true,
};

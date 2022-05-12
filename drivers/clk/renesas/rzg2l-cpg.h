/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RZ/G2L Clock Pulse Generator
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#ifndef __RENESAS_RZG2L_CPG_H__
#define __RENESAS_RZG2L_CPG_H__

#define CPG_PL1_DDIV		(0x200)
#define CPG_PL2_DDIV		(0x204)
#define CPG_PL3A_DDIV		(0x208)
#define CPG_PL6_DDIV		(0x210)
#define CPG_PL2SDHI_DSEL	(0x218)
#define CPG_CLKSTATUS		(0x280)
#define CPG_PL3_SSEL		(0x408)
#define CPG_PL6_SSEL		(0x414)
#define CPG_PL6_ETH_SSEL	(0x418)
#define CPG_PL5_SDIV		(0x420)
#define CPG_OTHERFUNC1_REG	(0xBE8)

#define ACPU_MSTOP		(0xB60)
#define MCPU1_MSTOP		(0xB64)
#define MCPU2_MSTOP		(0xB68)
#define PERI_COM_MSTOP		(0xB6C)
#define PERI_CPU_MSTOP		(0xB70)
#define PERI_DDR_MSTOP		(0xB74)
#define PERI_VIDEO_MSTOP	(0xB78)
#define REG0_MSTOP		(0xB7C)
#define REG1_MSTOP		(0xB80)
#define TZCDDR_MSTOP		(0xB84)
#define MHU_MSTOP		(0xB88)
#define PERI_STP_MSTOP		(0xB8C)


#define CPG_CLKSTATUS_SELSDHI0_STS	BIT(28)
#define CPG_CLKSTATUS_SELSDHI1_STS	BIT(29)

#define CPG_SDHI_CLK_SWITCH_STATUS_TIMEOUT_US	20000

/* n = 0/1/2 for PLL1/4/6 */
#define CPG_SAMPLL_CLK1(n)	(0x04 + (16 * n))
#define CPG_SAMPLL_CLK2(n)	(0x08 + (16 * n))

#define PLL146_CONF(n)	(CPG_SAMPLL_CLK1(n) << 22 | CPG_SAMPLL_CLK2(n) << 12)

#define DDIV_PACK(offset, bitpos, size) \
		(((offset) << 20) | ((bitpos) << 12) | ((size) << 8))
#define DIVPL1A		DDIV_PACK(CPG_PL1_DDIV, 0, 2)
#define DIVPL2A		DDIV_PACK(CPG_PL2_DDIV, 0, 3)
#define DIVDSILPCLK	DDIV_PACK(CPG_PL2_DDIV, 12, 2)
#define DIVPL3A		DDIV_PACK(CPG_PL3A_DDIV, 0, 3)
#define DIVPL3B		DDIV_PACK(CPG_PL3A_DDIV, 4, 3)
#define DIVPL3C		DDIV_PACK(CPG_PL3A_DDIV, 8, 3)
#define DIVDSIA		DDIV_PACK(CPG_PL5_SDIV, 0, 2)
#define DIVDSIB		DDIV_PACK(CPG_PL5_SDIV, 8, 4)
#define DIVGPU		DDIV_PACK(CPG_PL6_DDIV, 0, 2)

#define SEL_PLL_PACK(offset, bitpos, size) \
		(((offset) << 20) | ((bitpos) << 12) | ((size) << 8))

#define SEL_PLL3_3	SEL_PLL_PACK(CPG_PL3_SSEL, 8, 1)
#define SEL_PLL5_4	SEL_PLL_PACK(CPG_OTHERFUNC1_REG, 0, 1)
#define SEL_PLL6_2	SEL_PLL_PACK(CPG_PL6_ETH_SSEL, 0, 1)
#define SEL_GPU2	SEL_PLL_PACK(CPG_PL6_SSEL, 12, 1)

#define SEL_SDHI0	DDIV_PACK(CPG_PL2SDHI_DSEL, 0, 2)
#define SEL_SDHI1	DDIV_PACK(CPG_PL2SDHI_DSEL, 4, 2)

#define MSTOP(off, bit)	((off & 0xffff) << 16 | bit)
#define MSTOP_OFF(val)	((val >> 16) & 0xffff)
#define MSTOP_BIT(val)	(val & 0xffff)

/**
 * Definitions of CPG Core Clocks
 *
 * These include:
 *   - Clock outputs exported to DT
 *   - External input clocks
 *   - Internal CPG clocks
 */
struct cpg_core_clk {
	const char *name;
	unsigned int id;
	unsigned int parent;
	unsigned int div;
	unsigned int mult;
	unsigned int type;
	unsigned int conf;
	unsigned int conf_a;
	unsigned int conf_b;
	const struct clk_div_table *dtable;
	const struct clk_div_table *dtable_a;
	const struct clk_div_table *dtable_b;
	const char * const *parent_names;
	int flag;
	int mux_flags;
	int num_parents;
};

enum clk_types {
	/* Generic */
	CLK_TYPE_IN,		/* External Clock Input */
	CLK_TYPE_FF,		/* Fixed Factor Clock */
	CLK_TYPE_SAM_PLL,

	/* Clock with divider */
	CLK_TYPE_DIV,
	CLK_TYPE_2DIV,

	/* Clock with clock source selector */
	CLK_TYPE_MUX,

	/* Clock with SD clock source selector */
	CLK_TYPE_SD_MUX,
};

#define DEF_TYPE(_name, _id, _type...) \
	{ .name = _name, .id = _id, .type = _type }
#define DEF_BASE(_name, _id, _type, _parent...) \
	DEF_TYPE(_name, _id, _type, .parent = _parent)
#define DEF_SAMPLL(_name, _id, _parent, _conf) \
	DEF_TYPE(_name, _id, CLK_TYPE_SAM_PLL, .parent = _parent, .conf = _conf)
#define DEF_INPUT(_name, _id) \
	DEF_TYPE(_name, _id, CLK_TYPE_IN)
#define DEF_FIXED(_name, _id, _parent, _mult, _div) \
	DEF_BASE(_name, _id, CLK_TYPE_FF, _parent, .div = _div, .mult = _mult)
#define DEF_DIV(_name, _id, _parent, _conf, _dtable, _flag) \
	DEF_TYPE(_name, _id, CLK_TYPE_DIV, .conf = _conf, \
		 .parent = _parent, .dtable = _dtable, .flag = _flag)
#define DEF_2DIV(_name, _id, _parent, _conf_a, _conf_b, _dtable_a, _dtable_b, _flag) \
	DEF_TYPE(_name, _id, CLK_TYPE_2DIV, .parent = _parent, \
		.conf_a = _conf_a, .conf_b = _conf_b, \
		.dtable_a = _dtable_a, .dtable_b = _dtable_b, .flag = _flag)
#define DEF_MUX(_name, _id, _conf, _parent_names, _num_parents, _flag, \
		_mux_flags) \
	DEF_TYPE(_name, _id, CLK_TYPE_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents, \
		 .flag = _flag, .mux_flags = _mux_flags)
#define DEF_SD_MUX(_name, _id, _conf, _parent_names, _num_parents) \
	DEF_TYPE(_name, _id, CLK_TYPE_SD_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents)

/**
 * struct rzg2l_mod_clk - Module Clocks definitions
 *
 * @name: handle between common and hardware-specific interfaces
 * @id: clock index in array containing all Core and Module Clocks
 * @parent: id of parent clock
 * @off: register offset
 * @bit: ON/MON bit
 * @is_coupled: flag to indicate coupled clock
 */
struct rzg2l_mod_clk {
	const char *name;
	unsigned int id;
	unsigned int parent;
	u16 off;
	u8 bit;
	u32 mstop;
	bool is_coupled;
};

#define DEF_MOD_BASE(_name, _id, _parent, _off, _bit, _mstop, _is_coupled)	\
	{ \
		.name = _name, \
		.id = MOD_CLK_BASE + (_id), \
		.parent = (_parent), \
		.off = (_off), \
		.bit = (_bit), \
		.mstop = (_mstop), \
		.is_coupled = (_is_coupled), \
	}

#define DEF_MOD(_name, _id, _parent, _off, _bit, _mstop)	\
	DEF_MOD_BASE(_name, _id, _parent, _off, _bit, _mstop, false)

#define DEF_COUPLED(_name, _id, _parent, _off, _bit, _mstop)	\
	DEF_MOD_BASE(_name, _id, _parent, _off, _bit, _mstop, true)

/**
 * struct rzg2l_reset - Reset definitions
 *
 * @off: register offset
 * @bit: reset bit
 */
struct rzg2l_reset {
	u16 off;
	u8 bit;
};

#define DEF_RST(_id, _off, _bit)	\
	[_id] = { \
		.off = (_off), \
		.bit = (_bit) \
	}

/**
 * struct rzg2l_cpg_info - SoC-specific CPG Description
 *
 * @core_clks: Array of Core Clock definitions
 * @num_core_clks: Number of entries in core_clks[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @num_total_core_clks: Total number of Core Clocks (exported + internal)
 *
 * @mod_clks: Array of Module Clock definitions
 * @num_mod_clks: Number of entries in mod_clks[]
 * @num_hw_mod_clks: Number of Module Clocks supported by the hardware
 *
 * @resets: Array of Module Reset definitions
 * @num_resets: Number of entries in resets[]
 *
 * @crit_mod_clks: Array with Module Clock IDs of critical clocks that
 *                 should not be disabled without a knowledgeable driver
 * @num_crit_mod_clks: Number of entries in crit_mod_clks[]
 */
struct rzg2l_cpg_info {
	/* Core Clocks */
	const struct cpg_core_clk *core_clks;
	unsigned int num_core_clks;
	unsigned int last_dt_core_clk;
	unsigned int num_total_core_clks;

	/* Module Clocks */
	const struct rzg2l_mod_clk *mod_clks;
	unsigned int num_mod_clks;
	unsigned int num_hw_mod_clks;

	/* Resets */
	const struct rzg2l_reset *resets;
	unsigned int num_resets;

	/* Critical Module Clocks that should not be disabled */
	const unsigned int *crit_mod_clks;
	unsigned int num_crit_mod_clks;
};

extern const struct rzg2l_cpg_info r9a07g044_cpg_info;
extern const struct rzg2l_cpg_info r9a07g043_cpg_info;
extern const struct rzg2l_cpg_info r9a07g054_cpg_info;

#endif

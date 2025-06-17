/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RZ/G2L Clock Pulse Generator
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#ifndef __RENESAS_RZT2_CPG_H__
#define __RENESAS_RZT2_CPG_H__

#define SCKCR				0x00
#define SCKCR2				0x04
#define SCKCR3				0x08
#define SCKCR4				0x0C
#define PMSEL				0x10
#define PMSEL_PLL0			BIT(0)
#define PMSEL_PLL2			BIT(2)
#define PMSEL_PLL3			BIT(3)
#define PLL0EN				BIT(0)
#define PLL2EN				BIT(0)
#define PLL3EN				BIT(0)
#define PLL0MON				0x20
#define PLL0EN_REG			0x30
#define PLL0_SSC_CTR			0x34
#define PLL1MON				0x40
#define LOCOCR				0x70
#define HIZCTRLEN			0x80
#define PLL2MON				0x90
#define PLL2EN_REG			0xA0
#define PLL2_SSC_CTR			0xAC
#define PLL3MON				0xB0
#define PLL3EN_REG			0xC0
#define PLL3_VCO_CTR0			0xC4
#define PLL3_VCO_CTR1			0xC8
#define PLL4MON				0xD0
#define PHYSEL				BIT(21)


#define MRCTLA			0x240
#define MRCTLE			0x250
#define MRCTLI			0x260
#define MRCTLM			0x270

#define DDIV_PACK(offset, bitpos, size) \
		(((offset) << 20) | ((bitpos) << 12) | ((size) << 8))

#define DIVCA55			DDIV_PACK(SCKCR2, 8, 4)
#define DIVCA55S		DDIV_PACK(SCKCR2, 12, 1)
#define DIVCR520		DDIV_PACK(SCKCR2, 2, 2)
#define DIVCR521		DDIV_PACK(SCKCR2, 0, 2)
#define DIVLCDC			DDIV_PACK(SCKCR3, 20, 3)
#define DIVCKIO			DDIV_PACK(SCKCR, 16, 3)
#define DIVETHPHY		DDIV_PACK(SCKCR, 21, 1)
#define DIVCANFD		DDIV_PACK(SCKCR, 20, 1)
#define DIVSPI0			DDIV_PACK(SCKCR3, 0, 2)
#define DIVSPI1			DDIV_PACK(SCKCR3, 2, 2)
#define DIVSPI2			DDIV_PACK(SCKCR3, 4, 2)
#define DIVSPI3			DDIV_PACK(SCKCR2, 16, 2)

#define SEL_PLL_PACK(offset, bitpos, size) \
	(((offset) << 20) | ((bitpos) << 12) | ((size) << 8))

#define	SEL_PLL		SEL_PLL_PACK(SCKCR, 22, 1)
#define DIV_RSMASK(v, s, m)	((v >> s) & m)
#define KDIV(val)		DIV_RSMASK(val, 16, 0xffff)
#define MDIV(val)		DIV_RSMASK(val, 0, 0x3ff)
#define PDIV(val)		DIV_RSMASK(val, 16, 0x3f)
#define SDIV(val)		DIV_RSMASK(val, 0, 0x7)
#define CPG_SAMPLL_CLK1		0xC4
#define CPG_SAMPLL_CLK2		0xC8
#define PLL3_CONF	(CPG_SAMPLL_CLK1 << 22 | CPG_SAMPLL_CLK2 << 12)

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
	const struct clk_div_table *dtable;
	const char * const *parent_names;
	int flag;
	int mux_flags;
	int num_parents;
	int sel_base;
};

enum clk_types {
	/* Generic */
	CLK_TYPE_IN,		/* External Clock Input */
	CLK_TYPE_MAIN,
	CLK_TYPE_FF,		/* Fixed Factor Clock */
	CLK_TYPE_PLL,
	CLK_TYPE_SAM_PLL,

	/* Clock with divider */
	CLK_TYPE_DIV,

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
#define DEF_DIV(_name, _id, _parent, _conf, _dtable, _flag, _sel_base) \
	DEF_TYPE(_name, _id, CLK_TYPE_DIV, .conf = _conf, .sel_base = _sel_base, \
		 .parent = _parent, .dtable = _dtable, .flag = _flag)
#define DEF_MUX(_name, _id, _conf, _parent_names, _num_parents, _flag, \
		_mux_flags) \
	DEF_TYPE(_name, _id, CLK_TYPE_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents, \
		 .flag = _flag, .mux_flags = _mux_flags)
#define DEF_SD_MUX(_name, _id, _conf, _parent_names, _num_parents) \
	DEF_TYPE(_name, _id, CLK_TYPE_SD_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents)

/**
 * struct rzt2_mod_clk - Module Clocks definitions
 *
 * @name: handle between common and hardware-specific interfaces
 * @id: clock index in array containing all Core and Module Clocks
 * @parent: id of parent clock
 * @off: MSTOP register offset
 * @bit: ON/OFF bit
 * @is_coupled: flag to indicate coupled clock
 */
struct rzt2_mod_clk {
	const char *name;
	unsigned int id;
	unsigned int parent;
	u32 addr;
	u8 bit;
	int sel_base;
	bool is_coupled;
};

#define DEF_MOD_BASE(_name, _id, _parent, _addr, _bit, _sel_base, _is_coupled)	\
	{ \
		.name = _name, \
		.id = MOD_CLK_BASE + (_id), \
		.parent = (_parent), \
		.addr = (_addr), \
		.bit = (_bit), \
		.sel_base = (_sel_base), \
		.is_coupled = (_is_coupled) \
	}

#define DEF_MOD(_name, _id, _parent, _addr, _bit, _sel_base)	\
	DEF_MOD_BASE(_name, _id, _parent, _addr, _bit, _sel_base, false)

#define DEF_COUPLED(_name, _id, _parent, _addr, _bit)	\
	DEF_MOD_BASE(_name, _id, _parent, _addr, _bit, true)

/**
 * struct rzt2_reset - Reset definitions
 *
 * @off: register offset
 * @bit: reset bit
 */
struct rzt2_reset {
	u32 off;
	u8 bit;
	int sel_base;
};

#define DEF_RST(_id, _off, _bit, _sel_base)	\
	[_id] = { \
		.off = (_off), \
		.bit = (_bit), \
		.sel_base = (_sel_base), \
	}

/**
 * struct rzt2_cpg_info - SoC-specific CPG Description
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
struct rzt2_cpg_info {
	/* Core Clocks */
	const struct cpg_core_clk *core_clks;
	unsigned int num_core_clks;
	unsigned int last_dt_core_clk;
	unsigned int num_total_core_clks;

	/* Module Clocks */
	const struct rzt2_mod_clk *mod_clks;
	unsigned int num_mod_clks;
	unsigned int num_hw_mod_clks;

	/* Resets */
	const struct rzt2_reset *resets;
	unsigned int num_resets;

	/* Critical Module Clocks that should not be disabled */
	const unsigned int *crit_mod_clks;
	unsigned int num_crit_mod_clks;
};

extern const struct rzt2_cpg_info r9a09g077_cpg_info;

#endif

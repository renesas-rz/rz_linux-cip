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

#ifndef __CLK_RENESAS_CPG_COCR_H__
#define __CLK_RENESAS_CPG_COCR_H__

/*
* Definitions of CPG Core Clocks
*
* These include:
*   - Clock outputs exported to DT
*   - External input clocks
*   - Internal CPG clocks
*/

struct cpg_core_clk {
	/* Common */
	const char *name;
	unsigned int id;
	unsigned int parent;
	unsigned int offset;
	unsigned int div;
	unsigned int msk;
	unsigned long val;
	unsigned int status;
	unsigned int type;
};

enum clk_types {
	/* Generic */
	CLK_TYPE_IN,            /* External Clock Input */
	CLK_TYPE_FF,            /* Fixed Factor Clock */
	CLK_TYPE_FR,            /* Fixed Rate Clock */

	/* Custom definitions start here */
	CLK_TYP_STATIC,
	CLK_TYPE_DIV,           /* Fixed Rate Clock */
	CLK_TYPE_CUSTOM,
};

enum rst_types {
	RST_NON,
	RST_TYPEA,
	RST_TYPEB,
};

#define DEF_TYPE(_name, _id, _type...) \
		{ .name = _name, .id = _id, .type = _type }
#define DEF_BASE(_name, _id, _type, _parent...)        \
		DEF_TYPE(_name, _id, _type, .parent = _parent)
#define DEF_INPUT(_name, _id) \
		DEF_TYPE(_name, _id, CLK_TYPE_IN)
#define DEF_FIXED(_name, _id, _parent, _div, _mult)    \
		DEF_BASE(_name, _id, CLK_TYPE_FF, _parent, .div = _div, .val = _mult)
#define DEF_STATIC(_name, _id, _parent, _div, _offset, _msk, _mult)    \
		DEF_BASE(_name, _id, CLK_TYP_STATIC, _parent,.div = _div, .offset=_offset, .msk=_msk, .val = _mult)
#define DEF_DIV(_name, _id, _parent, _div, _offset, _msk, _status, _mult)      \
		DEF_BASE(_name, _id, CLK_TYPE_DIV, _parent, .div = _div, .offset=_offset, .msk=_msk, .status=_status, .val = _mult)
#define DEF_DIV6P1(_name, _id, _parent, _offset)       \
		DEF_BASE(_name, _id, CLK_TYPE_DIV6P1, _parent, .offset = _offset)
#define DEF_DIV6_RO(_name, _id, _parent, _offset, _div)        \
		DEF_BASE(_name, _id, CLK_TYPE_DIV6_RO, _parent, .offset = _offset, .div = _div, .mult = 1)
#define DEF_RATE(_name, _id, _rate)    \
		DEF_TYPE(_name, _id, CLK_TYPE_FR, .val=_rate)

/*
* Definitions of Module Clocks
*/
struct cocr_mod_clk {
       const char *name;
       unsigned int id;
       unsigned int parent;    /* Add MOD_CLK_BASE for Module Clocks */
};

struct rcr_reset {
       const char *name;
       unsigned int id;
       unsigned int type;
       unsigned int reset_msk;
       unsigned int clk_num;
       unsigned int clk_id[9];
};

/* Convert from sparse base-100 to packed index space */
#define MOD_CLK_PACK(x)		((x) - ((x) / 100) * (100 - 32))
#define MOD_CLK_ID(x)  (MOD_CLK_BASE + MOD_CLK_PACK(x))

#define DEF_MOD(_name, _mod, _parent...)     \
	{ .name = _name, .id = MOD_CLK_ID(_mod), .parent = _parent}
	
#define DEF_RESET(_name, _id,  _type, _reset_msk, _clk_num, clk0, clk1, clk2, clk3, clk4, clk5, clk6, clk7, clk8)     \
	{ .name = _name, .id = _id, .type=_type, .reset_msk = _reset_msk,  .clk_num= _clk_num, \
          .clk_id = { clk0, clk1, clk2 , clk3, clk4, clk5, clk6, clk7, clk8 } \
	}

#define RST_MON_UNUSED		0xFFFFFFFF
#define RST_MON_TIMEOUT		100
#define RST_MON_DEASSERT	0
#define RST_MON_ASSERT		1

#define CPG_MIN_CLKID		100
#define CPG_REG_WEN_SHIFT	(16)
#define CPG_SET_DATA_MASK	(0xFFFF)

#define CPG_CLK_ON_STATUS	1
#define CPG_CLK_OFF_STATUS	0

struct device_node;

/**
* SoC-specific CPG Description
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
* @crit_mod_clks: Array with Module Clock IDs of critical clocks that
*                 should not be disabled without a knowledgeable driver
* @num_crit_mod_clks: Number of entries in crit_mod_clks[]
*
* @init: Optional callback to perform SoC-specific initialization
* @cpg_clk_register: Optional callback to handle special Core Clock types
*/
struct cpg_cocr_info {
	/* Core Clocks */
	const struct cpg_core_clk *core_clks;
	unsigned int num_core_clks;
	unsigned int last_dt_core_clk;
	unsigned int num_total_core_clks;

	/* Module Clocks */
	const struct cocr_mod_clk *mod_clks;
	unsigned int num_mod_clks;
	unsigned int num_hw_mod_clks;

	/* Critical Module Clocks that should not be disabled */
	const unsigned int *crit_mod_clks;
	unsigned int num_crit_mod_clks;

	/*Resets*/
	const struct rcr_reset *resets;
	unsigned int num_resets;
	unsigned int num_hw_resets;

	/* Callbacks */
	int (*init)(struct device *dev);
	struct clk *(*cpg_clk_register)(struct device *dev,
				const struct cpg_core_clk *core,
				const struct cpg_cocr_info *info,
				struct clk **clks, void __iomem *base,
				struct raw_notifier_head *notifiers);
};

extern const struct cpg_cocr_info r9a09g011gbg_cpg_cocr_info;
extern const struct cpg_cocr_info r9a09g055ma3gbg_cpg_cocr_info;


/** CPG Registor Offset */
#define CPG_PLL1_STBY           (0x0000)
#define CPG_PLL1_CLK1           (0x0004)
#define CPG_PLL1_CLK2           (0x0008)
#define CPG_PLL1_MON            (0x000C)
#define CPG_PLL2_STBY           (0x0010)
#define CPG_PLL2_CLK1           (0x0014)
#define CPG_PLL2_CLK2           (0x0018)
#define CPG_PLL2_MON            (0x001C)
#define CPG_PLL3_STBY           (0x0020)
#define CPG_PLL3_CLK1           (0x0024)
#define CPG_PLL3_CLK2           (0x0028)
#define CPG_PLL3_MON            (0x002C)
#define CPG_PLL6_STBY           (0x0030)
#define CPG_PLL6_CLK1           (0x0034)
#define CPG_PLL6_CLK2           (0x0038)
#define CPG_PLL6_MON            (0x003C)
#define CPG_PLL7_STBY           (0x0040)
#define CPG_PLL7_CLK1           (0x0044)
#define CPG_PLL7_CLK2           (0x0048)
#define CPG_PLL7_MON            (0x004C)

#define CPG_PLL4_STBY           (0x0100)
#define CPG_PLL4_CLK1           (0x0104)
#define CPG_PLL4_CLK2           (0x0108)
#define CPG_PLL4_MON            (0x010C)

#define CPG_PLL1_CCTRL_RST      (0x0180)
#define CPG_PLL2_CCTRL_RST      (0x0184)
#define CPG_PLL3_CCTRL_RST      (0x0188)
#define CPG_PLL4_CCTRL_RST      (0x018C)
#define CPG_PLL7_CCTRL_RST      (0x0198)

#define CPG_CA53_DDIV           (0x0200)
#define CPG_SYS_DDIV            (0x0204)
#define CPG_MMCDDI_DDIV         (0x0210)
#define CPG_CLK48_DSEL          (0x0214)
#define CPG_CLKSTATUS           (0x0224)

#define CPG_SDIEMM_SSEL         (0x0300)
#define CPG_GMCLK_SDIV          (0x031C)
#define CPG_GMCLK_SSEL          (0x0320)
#define CPG_URT_RCLK_SDIV       (0x0328)
#define CPG_URT_RCLK_SSEL       (0x032C)
#define CPG_CSI_RCLK_SSEL       (0x0330)

#define CPG_CLK_ON1             (0x0400)
#define CPG_CLK_ON2             (0x0404)
#define CPG_CLK_ON3             (0x0408)
#define CPG_CLK_ON4             (0x040C)
#define CPG_CLK_ON5             (0x0410)
#define CPG_CLK_ON6             (0x0414)
#define CPG_CLK_ON7             (0x0418)
#define CPG_CLK_ON8             (0x041C)
#define CPG_CLK_ON9             (0x0420)
#define CPG_CLK_ON10            (0x0424)
#define CPG_CLK_ON11            (0x0428)
#define CPG_CLK_ON12            (0x042C)
#define CPG_CLK_ON13            (0x0430)
#define CPG_CLK_ON14            (0x0434)
#define CPG_CLK_ON15            (0x0438)
#define CPG_CLK_ON16            (0x043C)
#define CPG_CLK_ON17            (0x0440)
#define CPG_CLK_ON18            (0x0444)
#define CPG_CLK_ON19            (0x0448)
#define CPG_CLK_ON20            (0x044C)
#define CPG_CLK_ON21            (0x0450)
#define CPG_CLK_ON22            (0x0454)
#define CPG_CLK_ON23            (0x0458)
#define CPG_CLK_ON24            (0x045C)
#define CPG_CLK_ON25            (0x0460)
#define CPG_CLK_ON26            (0x0464)
#define CPG_CLK_ON27            (0x0468)

#define CPG_RST_MSK             (0x0504)

#define CPG_RST1                (0x0600)
#define CPG_RST2                (0x0604)
#define CPG_RST3                (0x0608)
#define CPG_RST4                (0x060C)
#define CPG_RST5                (0x0610)
#define CPG_RST6                (0x0614)
#define CPG_RST7                (0x0618)
#define CPG_RST8                (0x061C)
#define CPG_RST9                (0x0620)
#define CPG_RST10               (0x0624)
#define CPG_RST11               (0x0628)
#define CPG_RST12               (0x062C)
#define CPG_RST13               (0x0630)
#define CPG_RST14               (0x0634)
#define CPG_RST15               (0x0638)

#define CPG_RST_MON             (0x0680)
#define CPG_PD_RST              (0x0800)

/** Bit assign */
#define CPG_PLL_STBY_RESETB                 (0x00000001)
#define CPG_PLL_STBY_WEN_RESETB             (0x00010000)
#define CPG_PLL_STBY_WEN_SSE_EN             (0x00040000)
#define CPG_PLL_STBY_WEN_SSC_MODE           (0x00100000)

#define CPG_PLL_MON_RESETB                  (0x00000001)
#define CPG_PLL_MON_PLL_LOCK                (0x00000010)

#define CPG_PLL1_CCTRL_RST_P1_0_RST         (0x00000001)
#define CPG_PLL1_CCTRL_WEN_RST_P1_0_RST     (0x00010000)

#define CPG_CA53_DDIV_DIVA_SET_MIN          (0)
#define CPG_CA53_DDIV_WEN_DIVA              (0x00010000)

#define CPG_SYS_DDIV_DIVD_SET_SHIFT         (4)
#define CPG_SYS_DDIV_DIVD_SET_MIN           (0)
#define CPG_SYS_DDIV_DIVE_SET_SHIFT         (8)
#define CPG_SYS_DDIV_DIVE_SET_MIN           (0)
#define CPG_SYS_DDIV_WEN_DIVB               (0x00010000)
#define CPG_SYS_DDIV_WEN_DIVD               (0x00100000)
#define CPG_SYS_DDIV_WEN_DIVE               (0x01000000)

#define CPG_MMCDDI_DDIV_DIVX_SET_MSK        (0x00000003)
#define CPG_MMCDDI_DDIV_DIVX_SET_SHIFT      (0)
#define CPG_MMCDDI_DDIV_DIVX_SET_MAX        (2)
#define CPG_MMCDDI_DDIV_DIVX_SET_MIN        (0)
#define CPG_MMCDDI_DDIV_WEN_DIVX            (0x00010000)

#define CPG_CLK48_DSEL_SELB                 (0x00000001)
#define CPG_CLK48_DSEL_SELD                 (0x00000002)
#define CPG_CLK48_DSEL_SELE                 (0x00000004)
#define CPG_CLK48_DSEL_WEN_SELB             (0x00010000)
#define CPG_CLK48_DSEL_WEN_SELD             (0x00020000)
#define CPG_CLK48_DSEL_WEN_SELE             (0x00040000)

#define CPG_SDIEMM_SSEL_SELSDI              (0x00000001)
#define CPG_SDIEMM_SSEL_WEN_SELSDI          (0x00010000)

#define CPG_URT_RCLK_SSEL_WEN_SELW0         (0x00010000)

#define CPG_CLKSTATUS_DIVA                  (0x00000001)
#define CPG_CLKSTATUS_DIVB                  (0x00000002)
#define CPG_CLKSTATUS_DIVD                  (0x00000004)
#define CPG_CLKSTATUS_DIVE                  (0x00000008)
#define CPG_CLKSTATUS_DIVF                  (0x00000010)
#define CPG_CLKSTATUS_DIVG                  (0x00000020)
#define CPG_CLKSTATUS_DIVNFI                (0x00000040)
#define CPG_CLKSTATUS_DIVX                  (0x00000080)
#define CPG_CLKSTATUS_DIVH                  (0x00000100)
#define CPG_CLKSTATUS_DIVI                  (0x00000200)
#define CPG_CLKSTATUS_DIVJ                  (0x00000400)
#define CPG_CLKSTATUS_DIVM                  (0x00000800)
#define CPG_CLKSTATUS_DIVH2                 (0x00001000)

#define CPG_PD_RST_MEM_RSTB                 (0x00000001)
#define CPG_PD_RST_WEN_MEM_RSTB             (0x00010000)

#define CPG_RST_MON_EMM                     (0x00000100)
#define CPG_RST_MON_URT                     (0x04000000)
#define CPG_PD_RST_RFX_RSTB                 (0x00000010)
#define CPG_PD_RST_WEN_RFX_RSTB             (0x00100000)

#define CPG_PLL_MIN                         (1)
#define CPG_PLL_MAX                         (7)

#define CPG_CLK_ON_REG_MIN                  (1)
#define CPG_CLK_ON_REG_MAX                  (27)

#define CPG_RST_REG_MIN                     (1)
#define CPG_RST_REG_MAX                     (15)


#endif

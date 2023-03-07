
// SPDX-License-Identifier: GPL-2.0
/*
 * r8arzv2m Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 *
 * Based on r8a77990-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r9a09g011gbg-cpg-mssr.h>
#include "r9a09g011-cpg-mssr.h"

#include "renesas-cpg-clkon.h"
#include "rcar-gen3-cpg.h"

#define CPG_REG_WEN_SHIFT           (16)
#define CPG_SET_DATA_MASK           (0x0000FFFFUL)

enum clk_ids {
       /* Core Clock Outputs exported to DT */
       //LAST_DT_CORE_CLK = R8A774C0_CLK_CANFD,
	LAST_DT_CORE_CLK = 0,

       /* External Input Clocks */
       CLK_EXTAL,

       /* Internal Core Clocks */
       CLK_MAIN,
       CLK_MAIN_2,
       CLK_MAIN_24,
//     CLK_PLL1,
       CLK_PLL2,
       CLK_PLL2_2,
       CLK_PLL2_8,
       CLK_PLL2_16,
//     CLK_DIV_A,
       CLK_DIV_B,
       CLK_DIV_D,
       CLK_DIV_E,
       CLK_SEL_B,
       CLK_SEL_D,
       CLK_SEL_E,
#if 0//static defined is disabled
       CLK_SEL_CSI0,
       CLK_SEL_CSI2,
       CLK_SEL_W0,
       CLK_SEL_SDI0,
#endif

       /* Module Clocks */
       MOD_CLK_BASE
};

static const struct cpg_core_clk r8arzv2m_core_clks[] __initconst = {

       /* Internal Core Clocks */
       DEF_RATE(".main",      CLK_MAIN,        48*1000*1000),
       DEF_RATE(".main2",     CLK_MAIN_2,      24*1000*1000),
       DEF_RATE(".main24",    CLK_MAIN_24,     2*1000*1000),
       DEF_RATE(".pll2",      CLK_PLL2,        1600*1000*1000),
       DEF_RATE(".pll2_2",    CLK_PLL2_2,      800*1000*1000),
       DEF_RATE(".pll2_8",    CLK_PLL2_8,      200*1000*1000),
       DEF_RATE(".pll2_16",   CLK_PLL2_16,     100*1000*1000),

       DEF_DIV(".divb",     CLK_DIV_B,          CLK_PLL2,         4,
               CPG_SYS_DDIV, CPG_SYS_DDIV_WEN_DIVB|CPG_SYS_DDIV_WEN_DIVE|CPG_SYS_DDIV_WEN_DIVD,
               CPG_CLKSTATUS_DIVB|CPG_CLKSTATUS_DIVD|CPG_CLKSTATUS_DIVE, 0),
       DEF_DIV(".divd",     CLK_DIV_D,          CLK_PLL2,         8,
               CPG_SYS_DDIV, CPG_SYS_DDIV_WEN_DIVB|CPG_SYS_DDIV_WEN_DIVE|CPG_SYS_DDIV_WEN_DIVD,
               CPG_CLKSTATUS_DIVB|CPG_CLKSTATUS_DIVD|CPG_CLKSTATUS_DIVE, 0),
       DEF_DIV(".dive",     CLK_DIV_E,          CLK_PLL2,         16,
               CPG_SYS_DDIV, CPG_SYS_DDIV_WEN_DIVB|CPG_SYS_DDIV_WEN_DIVE|CPG_SYS_DDIV_WEN_DIVD,
               CPG_CLKSTATUS_DIVB|CPG_CLKSTATUS_DIVD|CPG_CLKSTATUS_DIVE, 0),
       DEF_DIV(".selb",     CLK_SEL_B,          CLK_DIV_B,  1,
               CPG_CLK48_DSEL, CPG_CLK48_DSEL_WEN_SELB, 0,     CPG_CLK48_DSEL_SELB),
       DEF_DIV(".seld",     CLK_SEL_D,          CLK_DIV_D,  1,
               CPG_CLK48_DSEL, CPG_CLK48_DSEL_WEN_SELD, 0,     CPG_CLK48_DSEL_SELD),
       DEF_DIV(".sele",     CLK_SEL_E,          CLK_DIV_D,  1,
               CPG_CLK48_DSEL, CPG_CLK48_DSEL_WEN_SELE, 0,     CPG_CLK48_DSEL_SELE),
#if 0 //static defined is disabled
       DEF_STATIC(".selcsi0",     CLK_SEL_CSI0,          CLK_MAIN,       1,
               CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI0,       CPG_CSI_RCLK_SSEL_SELCSI0),
       DEF_STATIC(".selcsi2",     CLK_SEL_CSI2,          CLK_MAIN,       1,
               CPG_CSI_RCLK_SSEL, CPG_CSI_RCLK_SSEL_WEN_SELCSI2,       CPG_CSI_RCLK_SSEL_SELCSI2),
       DEF_STATIC(".selw",     CLK_SEL_W0,               CLK_MAIN,       1,
               CPG_URT_RCLK_SSEL, CPG_URT_RCLK_SSEL_WEN_SELW0,         0),
       DEF_STATIC(".selsdi0",     CLK_SEL_SDI0,          CLK_PLL2,       2,
               CPG_SDIEMM_SSEL,   CPG_SDIEMM_SSEL_WEN_SELSDI,          CPG_SDIEMM_SSEL_SELSDI),
#endif
};

static const struct mssr_mod_clk r8arzv2m_mod_clks[] __initconst = {

       DEF_MOD("dmaa_aclk",            111,    CLK_SEL_D,              RST_TYPEB,      1,      7,      4),
//#if 0
       DEF_MOD("sdi0_aclk",            300,    CLK_SEL_D,              RST_NON,        0,      0,      0),
       DEF_MOD("sdi0_imclk",           301,    CLK_SEL_SDI0,   RST_NON,        0,      0,      0),
       DEF_MOD("sdi0_imclk2",          302,    CLK_SEL_SDI0,   RST_TYPEB,      3,      0,      6),
       DEF_MOD("sdi0_clk_hs",          303,    CLK_PLL2_2,             RST_NON,        0,      0,      0),
       DEF_MOD("emm_aclk",                     308,    CLK_SEL_D,              RST_NON,        0,      0,      0),
       DEF_MOD("emm_imclk",            309,    CLK_SEL_SDI0,   RST_NON,        0,      0,      0),
       DEF_MOD("emm_imclk2",           310,    CLK_SEL_SDI0,   RST_TYPEB,      3,      2,      7),
       DEF_MOD("emm_clk_hs",           311,    CLK_PLL2_2,             RST_NON,        0,      0,      0),
//#endif
       DEF_MOD("pci_aclk",                     400,    CLK_SEL_D,              RST_TYPEA,      3,      8,      0),
       DEF_MOD("pci_clk_pmu",          401,    CLK_SEL_D,              RST_TYPEA,      3,      8,      0),
       DEF_MOD("pci_apb_clk",          402,    CLK_SEL_E,              RST_NON,        0,      0,      0),
       DEF_MOD("usb_aclk_h",           404,    CLK_SEL_D,              RST_TYPEA,      3,      10,     0),
       DEF_MOD("usb_aclk_p",           405,    CLK_SEL_D,              RST_TYPEA,      3,      9,      0),
       DEF_MOD("usb_pclk",                     406,    CLK_SEL_E,              RST_TYPEA,      3,      7,      0),
       DEF_MOD("eth0_clk_axi",         408,    CLK_PLL2_8,             RST_TYPEB,      3,      11,     11),
//     DEF_MOD("eth0_clk_chi",         408,    CLK_PLL2_16,    RST_TYPEB,      3,      11,     11),
       DEF_MOD("eth0_clk_gptp_extern",409,             CLK_PLL2_16,RST_NON,    0,      0,      0,),
       DEF_MOD("iic_pclk0",            912,    CLK_SEL_E,              RST_TYPEA,      6,      8,      0),
       DEF_MOD("iic_pclk1",            1012,   CLK_SEL_E,              RST_TYPEA,      6,      9,      0),
       DEF_MOD("tim_clk16",            1104,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk17",            1105,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk18",            1106,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk19",            1107,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk20",            1108,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk21",            1109,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk22",            1110,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk23",            1111,   CLK_MAIN_24,    RST_TYPEA,      6,      2,      0,),
       DEF_MOD("tim_clk24",            1204,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk25",            1205,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk26",            1206,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk27",            1207,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk28",            1208,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk29",            1209,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk30",            1210,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("tim_clk31",            1211,   CLK_MAIN_24,    RST_TYPEA,      6,      3,      0,),
       DEF_MOD("pwm_clk8",                     1404,   CLK_MAIN,               RST_TYPEB,      6,      5,      0,),
       DEF_MOD("pwm_clk9",                     1405,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
       DEF_MOD("pwm_clk10",            1406,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
       DEF_MOD("pwm_clk11",            1407,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
       DEF_MOD("pwm_clk12",            1408,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
       DEF_MOD("pwm_clk13",            1409,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
       DEF_MOD("pwm_clk14",            1410,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
       DEF_MOD("pwm_clk15",            1411,   CLK_MAIN,               RST_TYPEB,      6,      5,      23),
#if 0 //static defined is disabled
       DEF_MOD("urt_pclk",                     1504,   CLK_SEL_E,              RST_TYPEB,      6,      10,     26),
       DEF_MOD("urt_clk0",                     1505,   CLK_SEL_W0,             RST_NON,        0,      0,      0),
       DEF_MOD("csi_clk0",                     1508,   CLK_SEL_CSI0,   RST_NON,        0,      0,      0),
       DEF_MOD("csi_clk2",                     1510,   CLK_SEL_CSI2,   RST_NON,        0,      0,      0),
#endif
#if 0
       DEF_MOD("drpa_aclk",            2000,   CLK_SEL_B,              RST_NON,        0,      0,      14),
       DEF_MOD("drpa_dclk",            2001,   CLK_PLL6,               RST_NON,        0,      0,      0),
       DEF_MOD("drpa_initclk",         2002,   CLK_MAIN,               RST_TYPEB,      9,      0,      0),
#endif
};

static const unsigned int r8arzv2m_crit_mod_clks[] __initconst = {
       //MOD_CLK_ID(408),        /* INTC-AP (GIC) */
       000
};

/*
 * CPG Clock Data
 */

/*
 * MD19                EXTAL (MHz)     PLL0            PLL1            PLL3
 *--------------------------------------------------------------------
 * 0           48 x 1          x100/1          x100/3          x100/3
 * 1           48 x 1          x100/1          x100/3           x58/3
 */
#define CPG_PLL_CONFIG_INDEX(md)       (((md) & BIT(19)) >> 19)

static const struct rcar_gen3_cpg_pll_config cpg_pll_configs[2] __initconst = {
       /* EXTAL div    PLL1 mult/div   PLL3 mult/div */
       { 1,            100,    3,      100,    3,      },
       { 1,            100,    3,       58,    3,      },
};

int32_t r8arzv2m_cpg_setClockCtrl(void __iomem *base, uint8_t reg_num, uint16_t target, uint16_t set_value)
{
    void __iomem *offset = base + CPG_CLK_ON1;
    uint32_t value;

       if (reg_num < CPG_CLK_ON_REG_MIN || CPG_CLK_ON_REG_MAX < reg_num)
       {
               return -EINVAL;
       }
       offset += ((reg_num - 1) * sizeof(uint32_t));
        value = ((uint32_t)target << CPG_REG_WEN_SHIFT)
              | (set_value & CPG_SET_DATA_MASK);

       writel(value,offset);

       return 0;
}

int32_t r8arzv2m_cpg_getClockCtrl(void __iomem *base, uint8_t reg_num, uint16_t target)
{
    void __iomem *offset = base + CPG_CLK_ON1;
    uint32_t value;
    if (reg_num < CPG_RST_REG_MIN || CPG_RST_REG_MAX < reg_num)
    {
        return 0xFFFFFFFF;
    }
    offset += ((reg_num - 1) * sizeof(uint32_t));

       value = readl(offset);

       value = value & target;
    return value;
}

int32_t CPG_SetResetCtrl(void __iomem *base, uint8_t reg_num, uint16_t target, uint16_t set_value)
{
    void __iomem *offset = base + CPG_RST1;
    uint32_t value;

    if (reg_num < CPG_RST_REG_MIN || CPG_RST_REG_MAX < reg_num)
    {
        return -EINVAL;
    }

    offset += ((reg_num - 1) * sizeof(uint32_t));

    value = ((uint32_t)target << CPG_REG_WEN_SHIFT)
		| (set_value & CPG_SET_DATA_MASK);

    writel(value,offset);
    
    return 0;
}

int32_t CPG_WaitResetMon(void __iomem *base, uint32_t timeout_c, uint32_t msk, uint32_t val)
{
    int32_t rslt = 0;
    uint32_t count;

       if (0 == msk)
       {
               rslt = 0;
               return rslt;
       }

       count = timeout_c;
       while (true)
       {
               if (val == (readl(base + CPG_RST_MON) & msk))
               {
                       rslt = 0;
                       break;
               }
               if ((0 == timeout_c) || (0 < count))
               {
                       udelay(1);
                       count--;
               }
               else
               {
                       rslt = -EBUSY;
                       break;
               }
       }

    return rslt;
}

static int __init r8arzv2m_cpg_mssr_init(struct device *dev)
{
       return 0;
}

const struct cpg_mssr_info r8arzv2m_cpg_mssr_info __initconst = {
       /* Core Clocks */
       .core_clks = r8arzv2m_core_clks,
       .num_core_clks = ARRAY_SIZE(r8arzv2m_core_clks),
       .last_dt_core_clk = LAST_DT_CORE_CLK,
       .num_total_core_clks = MOD_CLK_BASE,

       /* Module Clocks */
       .mod_clks = r8arzv2m_mod_clks,
       .num_mod_clks = ARRAY_SIZE(r8arzv2m_mod_clks),
       .num_hw_mod_clks = ARRAY_SIZE(r8arzv2m_mod_clks),

       /* Critical Module Clocks */
       .crit_mod_clks = r8arzv2m_crit_mod_clks,
       .num_crit_mod_clks = ARRAY_SIZE(r8arzv2m_crit_mod_clks),

       /* Callbacks */
       .init = r8arzv2m_cpg_mssr_init,
       //.cpg_clk_register = rcar_gen3_cpg_clk_register,
};

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Renesas Electronics Corp.
 */
#ifndef __R8ARZV2M_CPG_MSSR_H__
#define __R8ARZV2M_CPG_MSSR_H__


/*******************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only
 * intended for use with Renesas products. No other uses are authorized. This
 * software is owned by Renesas Electronics Corporation and is protected under
 * all applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
 * LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
 * TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
 * ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
 * ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
 * BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software
 * and to discontinue the availability of this software. By using this software,
 * you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 * Copyright (C) 2019 Renesas Electronics Corporation. All rights reserved.
 ******************************************************************************/
/*******************************************************************************
 * File Name    : rdk_cmn_cpg.h
 * Version      : 0.9
 * Description  : This module solves all the world's problems
 ******************************************************************************/

#ifndef RDK_CMN_CPG_H
#define RDK_CMN_CPG_H

/* CPG */
#define CPG_BASE_ADDRESS        (0x0A3500000ULL)

/** Registor Offset */
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
#define CPG_CR8REF_DDIV         (0x0208)
#define CPG_NFI_DDIV            (0x020C)
#define CPG_MMCDDI_DDIV         (0x0210)
#define CPG_CLK48_DSEL          (0x0214)
#define CPG_ISP_DDIV1           (0x0218)
#define CPG_ISP_DDIV2           (0x021C)
#define CPG_ISP_DSEL            (0x0220)
#define CPG_CLKSTATUS           (0x0224)


#define CPG_SDIEMM_SSEL         (0x0300)
#define CPG_STG_SDIV            (0x0304)
#define CPG_STGCIF_SSEL         (0x0308)
#define CPG_DISP_SDIV1          (0x030C)
#define CPG_DISP_SDIV2          (0x0310)
#define CPG_DISP_SSEL1          (0x0314)
#define CPG_DISP_SSEL2          (0x0318)
#define CPG_GMCLK_SDIV          (0x031C)
#define CPG_GMCLK_SSEL          (0x0320)
#define CPG_MTR_SSEL            (0x0324)
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


#define CPG_WDT_RST             (0x0500)
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
#define CPG_PLL_STBY_SSC_EN                 (0x00000004)
#define CPG_PLL_STBY_SSC_MODE_MSK           (0x00000030)
#define CPG_PLL_STBY_SSC_MODE_DOWN_SPREAD   (0x00000000)
#define CPG_PLL_STBY_SSC_MODE_UP_SPREAD     (0x00000010)
#define CPG_PLL_STBY_SSC_MODE_CENTER_SPREAD (0x00000020)
#define CPG_PLL_STBY_WEN_RESETB             (0x00010000)
#define CPG_PLL_STBY_WEN_SSE_EN             (0x00040000)
#define CPG_PLL_STBY_WEN_SSC_MODE           (0x00100000)

#define CPG_PLL_CLK1_DIV_P_MSK              (0x0000001F)
#define CPG_PLL_CLK1_DIV_M_MSK              (0x000003FF)
#define CPG_PLL_CLK1_DIV_K_MSK              (0x0000FFFF)

#define CPG_PLL_CLK1_DIV_P_SHIFT            (0)
#define CPG_PLL_CLK1_DIV_M_SHIFT            (5)
#define CPG_PLL_CLK1_DIV_K_SHIFT            (16)

#define CPG_PLL_CLK1_DIV_P_MAX              (+32767)
#define CPG_PLL_CLK1_DIV_P_MIN              (-32768)

#define CPG_PLL_CLK1_DIV_M_MAX              (533)
#define CPG_PLL_CLK1_DIV_M_MIN              (64)

#define CPG_PLL_CLK1_DIV_K_MAX              (8)
#define CPG_PLL_CLK1_DIV_K_MIN              (2)

#define CPG_PLL_CLK2_DIV_S_MSK              (0x00000007)
#define CPG_PLL_CLK2_MRR_MSK                (0x0000003F)
#define CPG_PLL_CLK2_MFR_MSK                (0x000000FF)

#define CPG_PLL_CLK2_DIV_S_SHIFT            (0)
#define CPG_PLL_CLK2_MRR_SHIFT              (8)
#define CPG_PLL_CLK2_MFR_SHIFT              (6)
#define CPG_PLL_CLK2_DIV_S_MIN              (0)

#define CPG_PLL_CLK2_MRR_MAX                (63)
#define CPG_PLL_CLK2_MRR_MIN                (1)

#define CPG_PLL_CLK2_MFR_MAX                (255)
#define CPG_PLL_CLK2_MFR_MIN                (0)

#define CPG_PLL_MON_RESETB                  (0x00000001)
#define CPG_PLL_MON_PLL_LOCK                (0x00000010)

#define CPG_PLL1_CCTRL_RST_P1_0_RST         (0x00000001)        /** DIV A */
#define CPG_PLL1_CCTRL_WEN_RST_P1_0_RST     (0x00010000)

#define CPG_PLL2_CCTRL_RST_P2_0_RSTB        (0x00000001)        /** PL2_DIV */
#define CPG_PLL2_CCTRL_RST_P2_1_RSTB        (0x00000002)        /** DIV B, D, E, DIVSEL G */
#define CPG_PLL2_CCTRL_RST_P2_2_RSTB        (0x00000004)        /** SEL B */
#define CPG_PLL2_CCTRL_RST_P2_3_RSTB        (0x00000008)        /** PL2_DIV DIV2 (800MHz*1/4) */
#define CPG_PLL2_CCTRL_RST_P2_4_RSTB        (0x00000010)        /** DIV_NFI */
#define CPG_PLL2_CCTRL_RST_P2_5_RSTB        (0x00000020)        /** PL2_DIV DIV8 (200MHz*1/2) */
#define CPG_PLL2_CCTRL_RST_P2_6_RSTB        (0x00000040)        /** DIVSEL F */
#define CPG_PLL2_CCTRL_RST_P2_7_RSTB        (0x00000080)        /** DIV H */
#define CPG_PLL2_CCTRL_RST_P2_9_RSTB        (0x00000200)        /** DIV I, J, M */
#define CPG_PLL2_CCTRL_RST_P2_10_RSTB       (0x00000400)        /** DIV M(1/2), JPEG0_CLK DIV_I, JPG1_CLK DIV_I, VCD_PCLK DIV_I */
#define CPG_PLL2_CCTRL_RST_P2_11_RSTB       (0x00000800)        /** DIV T */
#define CPG_PLL2_CCTRL_RST_P2_12_RSTB       (0x00001000)        /** DIV U */
#define CPG_PLL2_CCTRL_RST_P2_13_RSTB       (0x00002000)        /** DIV H2 */
#define CPG_PLL2_CCTRL_RST_WEN_P2_0_RSTB    (0x00010000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_1_RSTB    (0x00020000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_2_RSTB    (0x00040000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_3_RSTB    (0x00080000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_4_RSTB    (0x00100000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_5_RSTB    (0x00200000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_6_RSTB    (0x00400000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_7_RSTB    (0x00800000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_9_RSTB    (0x02000000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_10_RSTB   (0x04000000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_11_RSTB   (0x08000000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_12_RSTB   (0x10000000)
#define CPG_PLL2_CCTRL_RST_WEN_P2_13_RSTB   (0x20000000)

#define CPG_PLL3_CCTRL_RST_P3_0_RSTB        (0x00000001)        /** DIV X */
#define CPG_PLL3_CCTRL_RST_P3_1_RSTB        (0x00000002)        /** DIV X 1/2 */
#define CPG_PLL3_CCTRL_RST_WEN_P3_0_RSTB    (0x00010000)
#define CPG_PLL3_CCTRL_RST_WEN_P3_1_RSTB    (0x00020000)

#define CPG_PLL4_CCTRL_RST_P4_0_RSTB        (0x00000001)        /** DIV STG */
#define CPG_PLL4_CCTRL_RST_P4_1_RSTB        (0x00000002)        /** DIV_STG 1/2 */
#define CPG_PLL4_CCTRL_RST_P4_2_RSTB        (0x00000004)        /** DIV STG 1/2 -> 1/2 */
#define CPG_PLL4_CCTRL_RST_P4_3_RSTB        (0x00000008)        /** PLL4 1/3 */
#define CPG_PLL4_CCTRL_RST_P4_4_RSTB        (0x00000010)        /** PLL4 1/3 -> 1/2 */
#define CPG_PLL4_CCTRL_RST_P4_5_RSTB        (0x00000020)        /** DIV W */
#define CPG_PLL4_CCTRL_RST_WEN_P4_0_RSTB    (0x00010000)
#define CPG_PLL4_CCTRL_RST_WEN_P4_1_RSTB    (0x00020000)
#define CPG_PLL4_CCTRL_RST_WEN_P4_2_RSTB    (0x00040000)
#define CPG_PLL4_CCTRL_RST_WEN_P4_3_RSTB    (0x00080000)
#define CPG_PLL4_CCTRL_RST_WEN_P4_4_RSTB    (0x00100000)
#define CPG_PLL4_CCTRL_RST_WEN_P4_5_RSTB    (0x00200000)

#define CPG_PLL7_CCTRL_RST_P7_0_RSTB        (0x00000001)        /** DIV R */
#define CPG_PLL7_CCTRL_RST_P7_1_RSTB        (0x00000002)        /** DIV R 1/2 */
#define CPG_PLL7_CCTRL_RST_P7_2_RSTB        (0x00000004)        /** DIV LCILP */
#define CPG_PLL7_CCTRL_RST_WEN_P7_0_RSTB    (0x00010000)
#define CPG_PLL7_CCTRL_RST_WEN_P7_1_RSTB    (0x00020000)
#define CPG_PLL7_CCTRL_RST_WEN_P7_2_RSTB    (0x00040000)

#define CPG_CA53_DDIV_DIVA_SET_MSK          (0x00000007)
#define CPG_CA53_DDIV_DIVA_SET_SHIFT        (0)
#define CPG_CA53_DDIV_DIVA_SET_MAX          (6)
#define CPG_CA53_DDIV_DIVA_SET_MIN          (0)
#define CPG_CA53_DDIV_WEN_DIVA              (0x00010000)

#define CPG_SYS_DDIV_DIVB_SET_MSK           (0x00000003)
#define CPG_SYS_DDIV_DIVB_SET_SHIFT         (0)
#define CPG_SYS_DDIV_DIVB_SET_MAX           (3)
#define CPG_SYS_DDIV_DIVB_SET_MIN           (0)
#define CPG_SYS_DDIV_DIVD_SET_MSK           (0x00000003)
#define CPG_SYS_DDIV_DIVD_SET_SHIFT         (4)
#define CPG_SYS_DDIV_DIVD_SET_MAX           (2)
#define CPG_SYS_DDIV_DIVD_SET_MIN           (0)
#define CPG_SYS_DDIV_DIVE_SET_MSK           (0x00000001)
#define CPG_SYS_DDIV_DIVE_SET_SHIFT         (8)
#define CPG_SYS_DDIV_DIVE_SET_MAX           (1)
#define CPG_SYS_DDIV_DIVE_SET_MIN           (0)
#define CPG_SYS_DDIV_WEN_DIVB               (0x00010000)
#define CPG_SYS_DDIV_WEN_DIVD               (0x00100000)
#define CPG_SYS_DDIV_WEN_DIVE               (0x01000000)

#define CPG_CR8REF_DDIV_DIVF_SET_MSK        (0x00000007)
#define CPG_CR8REF_DDIV_DIVF_SET_SHIFT      (0)
#define CPG_CR8REF_DDIV_DIVF_SET_MAX        (4)
#define CPG_CR8REF_DDIV_DIVF_SET_MIN        (0)
#define CPG_CR8REF_DDIV_DIVG_SET_MSK        (0x00000003)
#define CPG_CR8REF_DDIV_DIVG_SET_SHIFT      (4)
#define CPG_CR8REF_DDIV_DIVG_SET_MAX        (3)
#define CPG_CR8REF_DDIV_DIVG_SET_MIN        (0)
#define CPG_CR8REF_DDIV_WEN_DIVF            (0x00010000)
#define CPG_CR8REF_DDIV_WEN_DIVG            (0x00100000)

#define CPG_NFI_DDIV_DIVNFI_SET_MSK         (0x00000003)
#define CPG_NFI_DDIV_DIVNFI_SET_SHIFT       (0)
#define CPG_NFI_DDIV_DIVNFI_SET_MAX         (3)
#define CPG_NFI_DDIV_DIVNFI_SET_MIN         (0)
#define CPG_NFI_DDIV_WEN_DIVNFI             (0x00010000)

#define CPG_MMCDDI_DDIV_DIVX_SET_MSK        (0x00000003)
#define CPG_MMCDDI_DDIV_DIVX_SET_SHIFT      (0)
#define CPG_MMCDDI_DDIV_DIVX_SET_MAX        (2)
#define CPG_MMCDDI_DDIV_DIVX_SET_MIN        (0)
#define CPG_MMCDDI_DDIV_WEN_DIVX            (0x00010000)

#define CPG_CLK48_DSEL_SELB                 (0x00000001)        /** ICB - DIV B */
#define CPG_CLK48_DSEL_SELD                 (0x00000002)        /** PCLK_200 - DIV D */
#define CPG_CLK48_DSEL_SELE                 (0x00000004)        /** PCLK_100 - DIV E */
#define CPG_CLK48_DSEL_SELF                 (0x00000008)        /** CR8 core - DIVSEL F */
#define CPG_CLK48_DSEL_SELG                 (0x00000010)        /** Reflex - DIVSEL G */
#define CPG_CLK48_DSEL_SELNFI               (0x00000020)        /** NFI - DIV NFI */
#define CPG_CLK48_DSEL_WEN_SELB             (0x00010000)
#define CPG_CLK48_DSEL_WEN_SELD             (0x00020000)
#define CPG_CLK48_DSEL_WEN_SELE             (0x00040000)
#define CPG_CLK48_DSEL_WEN_SELF             (0x00080000)
#define CPG_CLK48_DSEL_WEN_SELG             (0x00100000)
#define CPG_CLK48_DSEL_WEN_SELNFI           (0x00200000)

#define CPG_ISP_DDIV1_DIVH_SET_MSK          (0x00000003)
#define CPG_ISP_DDIV1_DIVH_SET_SHIFT        (0)
#define CPG_ISP_DDIV1_DIVH_SET_MAX          (3)
#define CPG_ISP_DDIV1_DIVH_SET_MIN          (0)
#define CPG_ISP_DDIV1_DIVH2_SET_MSK         (0x00000003)
#define CPG_ISP_DDIV1_DIVH2_SET_SHIFT       (4)
#define CPG_ISP_DDIV1_DIVH2_SET_MAX         (3)
#define CPG_ISP_DDIV1_DIVH2_SET_MIN         (0)
#define CPG_ISP_DDIV1_WEN_DIVH              (0x00010000)
#define CPG_ISP_DDIV1_WEN_DIVH2             (0x00100000)

#define CPG_ISP_DDIV2_DIVI_SET_MSK          (0x00000003)
#define CPG_ISP_DDIV2_DIVI_SET_SHIFT        (0)
#define CPG_ISP_DDIV2_DIVI_SET_MAX          (3)
#define CPG_ISP_DDIV2_DIVI_SET_MIN          (0)
#define CPG_ISP_DDIV2_DIVJ_SET_MSK          (0x00000003)
#define CPG_ISP_DDIV2_DIVJ_SET_SHIFT        (4)
#define CPG_ISP_DDIV2_DIVJ_SET_MAX          (3)
#define CPG_ISP_DDIV2_DIVJ_SET_MIN          (0)
#define CPG_ISP_DDIV2_DIVM_SET_MSK          (0x00000003)
#define CPG_ISP_DDIV2_DIVM_SET_SHIFT        (8)
#define CPG_ISP_DDIV2_DIVM_SET_MAX          (3)
#define CPG_ISP_DDIV2_DIVM_SET_MIN          (0)
#define CPG_ISP_DDIV2_WEN_DIVI              (0x00010000)
#define CPG_ISP_DDIV2_WEN_DIVJ              (0x00100000)
#define CPG_ISP_DDIV2_WEN_DIVM              (0x01000000)

#define CPG_ISP_DSEL_SELH                   (0x00000001)        /** RBIA, CMIA core - 533MHz */
#define CPG_ISP_DSEL_SELH2                  (0x00000010)        /** CIF core - 533MHz */
#define CPG_ISP_DSEL_SELI                   (0x00000100)        /** BIMA_CLK, ICB_BIMA_CLK - 533MHz */
#define CPG_ISP_DSEL_WEN_SELH               (0x00010000)
#define CPG_ISP_DSEL_WEN_SELH2              (0x00100000)
#define CPG_ISP_DSEL_WEN_SELI               (0x01000000)


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
#define CPG_CLKSTATUS_SELB                  (0x00010000)
#define CPG_CLKSTATUS_SELD                  (0x00020000)
#define CPG_CLKSTATUS_SELE                  (0x00040000)
#define CPG_CLKSTATUS_SELF                  (0x00080000)
#define CPG_CLKSTATUS_SELG                  (0x00100000)
#define CPG_CLKSTATUS_SELNFI                (0x00200000)
#define CPG_CLKSTATUS_SELH                  (0x01000000)
#define CPG_CLKSTATUS_SELH2                 (0x02000000)
#define CPG_CLKSTATUS_SELI                  (0x04000000)

#define CPG_SDIEMM_SSEL_SELSDI              (0x00000001)
#define CPG_SDIEMM_SSEL_WEN_SELSDI          (0x00010000)

#define CPG_STG_SDIV_DIVSTG_SET_MSK         (0x00000003)
#define CPG_STG_SDIV_DIVSTG_SET_SHIFT       (0)
#define CPG_STG_SDIV_DIVSTG_SET_MAX         (3)
#define CPG_STG_SDIV_DIVSTG_SET_MIN         (0)
#define CPG_STG_SDIV_WEN_DIVSTG             (0x00010000)

#define CPG_STGCIF_SSEL_SELSTG0             (0x00000001)
#define CPG_STGCIF_SSEL_SELCIF_SET_MSK      (0x00000003)
#define CPG_STGCIF_SSEL_SELCIF_SET_SHIFT    (8)
#define CPG_STGCIF_SSEL_SELCIF_SET_MAX      (2)
#define CPG_STGCIF_SSEL_SELCIF_SET_MIN      (0)
#define CPG_STGCIF_SSEL_WEN_SESTG0          (0x00010000)
#define CPG_STGCIF_SSEL_WEN_SELCIF          (0x01000000)

#define CPG_DISP_SDIV1_DIVR_SET_MSK         (0x0000001F)
#define CPG_DISP_SDIV1_DIVR_SET_SHIFT       (8)
#define CPG_DISP_SDIV1_DIVR_SET_MAX         (0x1F)
#define CPG_DISP_SDIV1_DIVR_SET_MIN         (0)
#define CPG_DISP_SDIVI_WEN_DIVR             (0x01000000)

#define CPG_DISP_SDIV2_DIVLCILP_SET_MSK     (0x0000007F)
#define CPG_DISP_SDIV2_DIVLCILP_SET_SHIFT   (0)
#define CPG_DISP_SDIV2_DIVLCILP_SET_MAX     (0x7F)
#define CPG_DISP_SDIV2_DIVLCILP_SET_MIN     (0)
#define CPG_DISP_SDIV2_WEN_DIVLCILP         (0x00010000)

#define CPG_DISP_SSEL1_SELR                 (0x00000100)        /** HDMI - DIV R/2 */
#define CPG_DISP_SSEL1_SELS1                (0x00001000)        /** LCI - PLL7 */
#define CPG_DISP_SSEL1_WEN_SELR             (0x01000000)
#define CPG_DISP_SSEL1_WEN_SELS1            (0x10000000)

#define CPG_DISP_SSEL2_SELS2_SET_MSK        (0x00000003)
#define CPG_DISP_SSEL2_SELS2_SET_SHIFT      (0)
#define CPG_DISP_SSEL2_SELS2_SET_MAX        (2)
#define CPG_DISP_SSEL2_SELS2_SET_MIN        (0)
#define CPG_DISP_SSEL2_WEN_SELS2            (0x00010000)

#define CPG_GMCLK_SDIV_DIVT_SET_MSK         (0x0000000F)
#define CPG_GMCLK_SDIV_DIVT_SET_SHIFT       (0)
#define CPG_GMCLK_SDIV_DIVT_SET_MAX         (12)
#define CPG_GMCLK_SDIV_DIVT_SET_MIN         (0)
#define CPG_GMCLK_SDIV_DIVU_SET_MSK         (0x0000000F)
#define CPG_GMCLK_SDIV_DIVU_SET_SHIFT       (8)
#define CPG_GMCLK_SDIV_DIVU_SET_MAX         (12)
#define CPG_GMCLK_SDIV_DIVU_SET_MIN         (0)
#define CPG_GMCLK_SDIV_WEN_DIVT             (0x00010000)
#define CPG_GMCLK_SDIV_WEN_DIVU             (0x01000000)

#define CPG_GMCLK_SSEL_SELT_SET_MSK         (0x00000003)
#define CPG_GMCLK_SSEL_SELT_SET_SHIFT       (0)
#define CPG_GMCLK_SSEL_SELT_SET_MAX         (3)
#define CPG_GMCLK_SSEL_SELT_SET_MIN         (0)
#define CPG_GMCLK_SSEL_SELU_SET_MSK         (0x00000003)
#define CPG_GMCLK_SSEL_SETU_SET_SHIFT       (8)
#define CPG_GMCLK_SSEL_SETU_SET_MAX         (3)
#define CPG_GMCLK_SSEL_SETU_SET_MIN         (0)
#define CPG_GMCLK_SSEL_WEN_SELT             (0x00010000)
#define CPG_GMCLK_SSEL_WEN_SELU             (0x01000000)

#define CPG_MTR_SSEL_SELMTR_SET_MSK         (0x00000003)
#define CPG_MTR_SSEL_SELMTR_SET_SHIFT       (0)
#define CPG_MTR_SSEL_SELMTR_SET_MAX         (2)
#define CPG_MTR_SSEL_SELMTR_SET_MIN         (0)
#define CPG_MTR_SSEL_WEN_SELMTR             (0x00010000)

#define CPG_URT_RCLK_SDIV_DIVW_SET_MSK      (0x00000007)
#define CPG_URT_RCLK_SDIV_DIVW_SET_SHIFT    (0)
#define CPG_URT_RCLK_SDIV_DIVW_SET_MAX      (6)
#define CPG_URT_RCLK_SDIV_DIVW_SET_MIN      (0)
#define CPG_URT_RCLK_SDIV_WEN_DIVW          (0x00010000)

#define CPG_URT_RCLK_SSEL_SELW0             (0x00000001)        /** UART0 - DIV W */
#define CPG_URT_RCLK_SSEL_SELW1             (0x00000010)        /** UART1 - DIV W */
#define CPG_URT_RCLK_SSEL_WEN_SELW0         (0x00010000)
#define CPG_URT_RCLK_SSEL_WEN_SELW1         (0x00100000)

#define CPG_CSI_RCLK_SSEL_SELCSI0           (0x00000001)        /** CSI0 - 48MHz */
#define CPG_CSI_RCLK_SSEL_SELCSI1           (0x00000002)        /** CSI1 - 48MHz */
#define CPG_CSI_RCLK_SSEL_SELCSI2           (0x00000004)        /** CSI2 - 48MHz */
#define CPG_CSI_RCLK_SSEL_SELCSI3           (0x00000008)        /** CSI3 - 48MHz */
#define CPG_CSI_RCLK_SSEL_SELCSI4           (0x00000010)        /** CSI4 - 48MHz */
#define CPG_CSI_RCLK_SSEL_SELCSI5           (0x00000020)        /** CSI5 - 48MHz */
#define CPG_CSI_RCLK_SSEL_WEN_SELCSI0       (0x00010000)
#define CPG_CSI_RCLK_SSEL_WEN_SELCSI1       (0x00020000)
#define CPG_CSI_RCLK_SSEL_WEN_SELCSI2       (0x00040000)
#define CPG_CSI_RCLK_SSEL_WEN_SELCSI3       (0x00080000)
#define CPG_CSI_RCLK_SSEL_WEN_SELCSI4       (0x00100000)
#define CPG_CSI_RCLK_SSEL_WEN_SELCSI5       (0x00200000)

                                                           /**         1,           2,           3,                4,           5,         6,          7,           8,               9,              10,              11,              12,              13,              14,              15,                        16,                         17,              18,                              19,              20,           21,                          22,             23,          24,          25,                26,                     27, */
#define CPG_CLK_ON_CLK0_ON                  (0x00000001)        /**         -,   CST_TRACE,   SDI0_ACLK,         PCI_ACLK,     SDT_CLK,  HMI_PCLK,    AUI_CLK,    ATGA_CLK, CPERI_GRPA_PCLK, CPERI_GRPB_PCLK, CPERI_GRPC_PCLK, CPERI_GRPD_PCLK, CPERI_GRPE_PCLK, CPERI_GRPF_PCLK, CPERI_GRPG_PCLK,     ICB_ACLK1 ICB_GIC_CLK,              ICB_DRPA_ACLK,      ICB_MPCLK3,                        CA53_CLK, DRPA_ACLK(MCLK),    DRPB_ACLK,       CR8_CLK CR8_PERIPHCLK, RAMB_ACLK[3:0], CIMA_CLKAPB, BIMA_CLKAPB,                 -, MMC_CORE_DDRC_CORE_CLK, */
#define CPG_CLK_ON_CLK1_ON                  (0x00000002)        /**   SYS_CLK,  CST_SB_CLK,  SDI0_IMCLK,      PCI_CLK_PMU,  SDT_CLKAPB,         -, AUI_CLKAXI, ATGA_CLKAPB,               -,               -,               -,               -,               -,               -, CPERI_GRPH_PCLK,                         -,                          -,    ICB_CIMA_CLK,                       CA53_ACLK,       DRPA_DCLK,    DRPB_DCLK, CR8_ACLK_MSTR CR8_ACLK_TCMS,      DMAB_ACLK,    CIMA_CLK,    BIMA_CLK,       TRFA_CLK533,               MMC_ACLK, */
#define CPG_CLK_ON_CLK2_ON                  (0x00000004)        /**  PFC_PCLK, CST_AHB_CLK, SDI0_IMCLK2,      PCI_APB_CLK,   SDT_CLK48,         -, AUI_CLKAPB,    ATGB_CLK,               -,               -,               -,               -,               -,               -,               -,                ICB_MPCLK1,                          -,    ICB_CIMB_CLK, CA53_APCLK_DBG CST_APB_CA53_CLK,    DRPA_INITCLK, DRPB_INITCLK,      CR8_PCLK CR8_ACLK_LLPP,              -,    CIMB_CLK,           -,       TRFA_CLK400,               MMC_PCLK, */
#define CPG_CLK_ON_CLK3_ON                  (0x00000008)        /**  MHU_PCLK, CST_APB_CR8, SDI0_CLK_HS,                -,     GRP_CLK,         -,     AUMCLK, ATGB_CLKAPB,               -,               -,               -,               -,               -,               -,               -,                ICB_SPCLK1,                          -,               -, CA53_ATCLK CST_CS_CLK CR8_ATCLK,               -,            -,                           -,              -,    FAFA_CLK,    FAFB_CLK,          TRFB_CLK,                      -, */
#define CPG_CLK_ON_CLK4_ON                  (0x00000010)        /**  PMC_CORE,  CST_ATB_SB,   SDI1_ACLK,       USB_ACLK_H,  CIF_P0_CLK,         -,     GMCLK0,    JOG0_CLK,      TIM_CLK[0],      TIM_CLK[8],     TIM_CLK[16],     TIM_CLK[24],      PWM_CLK[0],      PWM_CLK[8],        URT_PCLK,                 ICB_CLK48, ICB_RFX_ACLK ICB_RFX_PCLK5,               -, CA53_TSCLK CST_TS_CLK CR8_TSCLK,               -,            -,                           -,       RBIA_CLK,  STG_CLKAXI,     FCD_CLK,           RIM_CLK,             DDI_APBCLK, */
#define CPG_CLK_ON_CLK5_ON                  (0x00000020)        /**   GIC_CLK,           -,  SDI1_IMCLK,       USB_ACLK_P,  CIF_P1_CLK,         -,     GMCLK1, JOG0_CLKAPB,      TIM_CLK[1],      TIM_CLK[9],     TIM_CLK[17],     TIM_CLK[25],      PWM_CLK[1],      PWM_CLK[9],      URT_CLK[0],               ICB_CLK48_2,                          -,               -,                  CA53_APCLK_REG,               -,            -,                           -,    RBIA_CLKAPB,    STG_CLK0,  FCD_CLKAXI, VCD_ACLK VCD_PCLK,                      -, */
#define CPG_CLK_ON_CLK6_ON                  (0x00000040)        /** RAMA_ACLK,           -, SDI1_IMCLK2,         USB_PCLK, CIF_APB_CLK,         -,          -,    JOG1_CLK,      TIM_CLK[2],     TIM_CLK[10],     TIM_CLK[18],     TIM_CLK[26],      PWM_CLK[2],     PWM_CLK[10],      URT_CLK[1],               ICB_CLK48_3,             ICB_RBIA_ACLK5,               -,                               -,               -,            -,                           -,              -,           -,           -,                 -,                      -, */
#define CPG_CLK_ON_CLK7_ON                  (0x00000080)        /**  ROM_ACLK,           -, SDI1_CLK_HS,                -,  CIF_REFCLK,         -,          -, JOG1_CLKAPB,      TIM_CLK[3],     TIM_CLK[11],     TIM_CLK[19],     TIM_CLK[27],      PWM_CLK[3],     PWM_CLK[11],               -, ICB_CLK48_4L ICB_CLK48_4R,                          -,               -,                               -,               -,            -,                           -,              -,           -,           -,                 -,                      -, */
#define CPG_CLK_ON_CLK8_ON                  (0x00000100)        /**  SEC_ACLK,           -,    EMM_ACLK, ETH0_CLK_AXI CHI,  DCI_CLKAXI,  LCI_PCLK,   MTR_CLK0,   SEQ_CLK48,      TIM_CLK[4],     TIM_CLK[12],     TIM_CLK[20],     TIM_CLK[28],      PWM_CLK[4],     PWM_CLK[12],      CSI_CLK[0],                         -,               ICB_MMC_ACLK,    ICB_BIMA_CLK,                               -,               -,            -,                           -,       RBIB_CLK,           -,           -,          JPG0_CLK,                      -, */
#define CPG_CLK_ON_CLK9_ON                  (0x00000200)        /**  SEC_PCLK,           -,   EMM_IMCLK,    ETH0_CLK_GPTP,  DCI_CLKAPB,  LCI_ACLK,   MTR_CLK1,  SEQ_CLKAXI,      TIM_CLK[5],     TIM_CLK[13],     TIM_CLK[21],     TIM_CLK[29],      PWM_CLK[5],     PWM_CLK[13],      CSI_CLK[1],               ICB_CLK48_5,                          -,  ICB_FCD_CLKAXI,                               -,               -,            -,                           -,    RBIB_CLKAPB,           -,           -,         JPG0_ACLK,                      -, */
#define CPG_CLK_ON_CLK10_ON                 (0x00000400)        /**  SEC_TCLK,           -,  EMM_IMCLK2,                -,           -,  LCI_VCLK, MTR_CLKAPB,  SEQ_CLKAPB,      TIM_CLK[6],     TIM_CLK[14],     TIM_CLK[22],     TIM_CLK[30],      PWM_CLK[6],     PWM_CLK[14],      CSI_CLK[2],        ICB_CST_ATB_SB_CLK,                          -, ICB_TRFA_CLK533,                               -,               -,            -,                           -,  RBIB_CLKRSP48,           -,           -,          JPG1_CLK,                      -, */
#define CPG_CLK_ON_CLK11_ON                 (0x00000800)        /** DMAA_ACLK,           -,  EMM_CLK_HS,                -,           -, LCI_LPCLK,          -,           -,      TIM_CLK[7],     TIM_CLK[15],     TIM_CLK[23],     TIM_CLK[31],      PWM_CLK[7],     PWM_CLK[15],      CSI_CLK[3],            ICB_CST_CS_CLK,                          -,    ICB_VD_ACLK4,                               -,               -,            -,                           -,              -,           -,           -,         JPG1_ACLK,                      -, */
#define CPG_CLK_ON_CLK12_ON                 (0x00001000)        /**  OTP_PCLK,           -,    NFI_ACLK,                -, DCI_CLKDCI2,         -,    GFT_CLK, SYC_CNT_CLK,     IIC_PCLK[0],     IIC_PCLK[1],     WDT_PCLK[0],     WDT_PCLK[2],               -,               -,      CSI_CLK[4],              ICB_CLK100_1,                          -,      ICB_MPCLK4,                               -,               -,            -,                           -,              -,           -,           -,                 -,                      -, */
#define CPG_CLK_ON_CLK13_ON                 (0x00002000)        /**  OTP_SCLK,           -,  NFI_NF_CLK,                -,           -,         -, GFT_CLKAPB,    PSC_PCLK,               -,               -,      WDT_CLK[0],      WDT_CLK[2],               -,               -,      CSI_CLK[5],          ICB_ETH0_CLK_AXI,                          -,   ICB_VCD_PCLK4,                               -,               -,            -,                           -,              -,           -,           -,                 -,                      -, */
#define CPG_CLK_ON_CLK14_ON                 (0x00004000)        /** TSU0_PCLK,           -,           -,                -,           -,         -,   GFT_MCLK,           -,               -,               -,     WDT_PCLK[1],               -,               -,               -,               -,            ICB_DCI_CLKAXI,                          -,               -,                               -,               -,            -,                           -,              -,           -,           -,                 -,                      -, */
#define CPG_CLK_ON_CLK15_ON                 (0x00008000)        /** TSU1_PCLK,           -,           -,                -,           -,         -,          -,           -,               -,               -,      WDT_CLK[1],               -,               -,               -,               -,           ICB_SYC_CNT_CLK,                          -,               -,                               -,               -,            -,                           -,              -,           -,           -,                 -,                      -, */
#define CPG_CLK_ON_WEN_CLK0_ON              (0x00010000)
#define CPG_CLK_ON_WEN_CLK1_ON              (0x00020000)      
#define CPG_CLK_ON_WEN_CLK2_ON              (0x00040000)                                    
#define CPG_CLK_ON_WEN_CLK3_ON              (0x00080000)      
#define CPG_CLK_ON_WEN_CLK4_ON              (0x00100000)
#define CPG_CLK_ON_WEN_CLK5_ON              (0x00200000)
#define CPG_CLK_ON_WEN_CLK6_ON              (0x00400000)
#define CPG_CLK_ON_WEN_CLK7_ON              (0x00800000)
#define CPG_CLK_ON_WEN_CLK8_ON              (0x01000000)
#define CPG_CLK_ON_WEN_CLK9_ON              (0x02000000)
#define CPG_CLK_ON_WEN_CLK10_ON             (0x04000000)
#define CPG_CLK_ON_WEN_CLK11_ON             (0x08000000)
#define CPG_CLK_ON_WEN_CLK12_ON             (0x10000000)
#define CPG_CLK_ON_WEN_CLK13_ON             (0x20000000)
#define CPG_CLK_ON_WEN_CLK14_ON             (0x40000000)
#define CPG_CLK_ON_WEN_CLK15_ON             (0x80000000)

#define CPG_WDT_RST_WDT_RST0                (0x00000001)
#define CPG_WDT_RST_WDT_RST1                (0x00000002)
#define CPG_WDT_RST_WDT_RST2                (0x00000004)
#define CPG_WDT_RST_WEN_WDT_RST0            (0x00010000)
#define CPG_WDT_RST_WEN_WDT_RST1            (0x00020000)
#define CPG_WDT_RST_WEN_WDT_RST2            (0x00040000)

#define CPG_RST_MSK_WARM0_MSK               (0x00000001)
#define CPG_RST_MSK_WARM1_MSK               (0x00000002)
#define CPG_RST_MSK_DBG0_MSK                (0x00000004)
#define CPG_RST_MSK_DBG1_MSK                (0x00000008)
#define CPG_RST_MSK_WEN_WARM0_MSK           (0x00010000)
#define CPG_RST_MSK_WEN_WARM1_MSK           (0x00020000)
#define CPG_RST_MSK_WEN_DBG0_MSK            (0x00040000)
#define CPG_RST_MSK_WEN_DBG1_MSK            (0x00080000)


#define CPG_RST_UNIT0_RSTB                  (0x00000001)
#define CPG_RST_UNIT1_RSTB                  (0x00000002)
#define CPG_RST_UNIT2_RSTB                  (0x00000004)
#define CPG_RST_UNIT3_RSTB                  (0x00000008)
#define CPG_RST_UNIT4_RSTB                  (0x00000010)
#define CPG_RST_UNIT5_RSTB                  (0x00000020)
#define CPG_RST_UNIT6_RSTB                  (0x00000040)
#define CPG_RST_UNIT7_RSTB                  (0x00000080)
#define CPG_RST_UNIT8_RSTB                  (0x00000100)
#define CPG_RST_UNIT9_RSTB                  (0x00000200)
#define CPG_RST_UNIT10_RSTB                 (0x00000400)
#define CPG_RST_UNIT11_RSTB                 (0x00000800)
#define CPG_RST_UNIT12_RSTB                 (0x00001000)
#define CPG_RST_UNIT13_RSTB                 (0x00002000)
#define CPG_RST_UNIT14_RSTB                 (0x00004000)
#define CPG_RST_UNIT15_RSTB                 (0x00008000)
#define CPG_RST_WEN_UNIT0_RSTB              (0x00010000)
#define CPG_RST_WEN_UNIT1_RSTB              (0x00020000)
#define CPG_RST_WEN_UNIT2_RSTB              (0x00040000)
#define CPG_RST_WEN_UNIT3_RSTB              (0x00080000)
#define CPG_RST_WEN_UNIT4_RSTB              (0x00100000)
#define CPG_RST_WEN_UNIT5_RSTB              (0x00200000)
#define CPG_RST_WEN_UNIT6_RSTB              (0x00400000)
#define CPG_RST_WEN_UNIT7_RSTB              (0x00800000)
#define CPG_RST_WEN_UNIT8_RSTB              (0x01000000)
#define CPG_RST_WEN_UNIT9_RSTB              (0x02000000)
#define CPG_RST_WEN_UNIT10_RSTB             (0x04000000)
#define CPG_RST_WEN_UNIT11_RSTB             (0x08000000)
#define CPG_RST_WEN_UNIT12_RSTB             (0x10000000)
#define CPG_RST_WEN_UNIT13_RSTB             (0x20000000)
#define CPG_RST_WEN_UNIT14_RSTB             (0x40000000)
#define CPG_RST_WEN_UNIT15_RSTB             (0x80000000)


#define CPG_RST_MON_MHU                     (0x00000001)
#define CPG_RST_MON_GIC                     (0x00000002)
#define CPG_RST_MON_RAMA                    (0x00000004)
#define CPG_RST_MON_ROM                     (0x00000008)
#define CPG_RST_MON_DMAA                    (0x00000010)
#define CPG_RST_MON_SEC                     (0x00000020)
#define CPG_RST_MON_SDI0                    (0x00000040)
#define CPG_RST_MON_SDI1                    (0x00000080)
#define CPG_RST_MON_EMM                     (0x00000100)
#define CPG_RST_MON_NFI_1                   (0x00000200)
#define CPG_RST_MON_NFI_2                   (0x00000400)
#define CPG_RST_MON_ETH0                    (0x00000800)
#define CPG_RST_MON_SYC                     (0x00002000)
#define CPG_RST_MON_DRPA                    (0x00004000)
#define CPG_RST_MON_DRPB                    (0x00008000)
#define CPG_RST_MON_DMAB                    (0x00010000)
#define CPG_RST_MON_RAMB                    (0x00020000)
#define CPG_RST_MON_VCD                     (0x00040000)
#define CPG_RST_MON_WDT0                    (0x00080000)
#define CPG_RST_MON_WDT1                    (0x00100000)
#define CPG_RST_MON_WDT2                    (0x00200000)
#define CPG_RST_MON_PWM_0                   (0x00400000)
#define CPG_RST_MON_PWM_1                   (0x00800000)
#define CPG_RST_MON_CSI_1                   (0x01000000)
#define CPG_RST_MON_CSI_2                   (0x02000000)
#define CPG_RST_MON_URT                     (0x04000000)
#define CPG_RST_MON_JPG0                    (0x10000000)
#define CPG_RST_MON_JPG1                    (0x20000000)

#define CPG_RST_MON_SYSTEM_ON   \
    (CPG_RST_MON_CSI_2 | CPG_RST_MON_CSI_1 | CPG_RST_MON_PWM_1 | \
     CPG_RST_MON_PWM_0 | CPG_RST_MON_SYC | CPG_RST_MON_NFI_2 | \
     CPG_RST_MON_NFI_1 | CPG_RST_MON_SDI1 | CPG_RST_MON_SDI0 )

#define CPG_PD_RST_MEM_RSTB                 (0x00000001)
#define CPG_PD_RST_VD0_RSTB                 (0x00000002)
#define CPG_PD_RST_VD1A_RSTB                (0x00000004)
#define CPG_PD_RST_VD1B_RSTB                (0x00000008)
#define CPG_PD_RST_RFX_RSTB                 (0x00000010)
#define CPG_PD_RST_WEN_MEM_RSTB             (0x00010000)
#define CPG_PD_RST_WEN_VD0_RSTB             (0x00020000)
#define CPG_PD_RST_WEN_VD1A_RSTB            (0x00040000)
#define CPG_PD_RST_WEN_VD1B_RSTB            (0x00080000)
#define CPG_PD_RST_WEN_RFX_RSTB             (0x00100000)

#define CPG_PLL_MIN                         (1)
#define CPG_PLL_MAX                         (7)

#define CPG_CLK_ON_REG_MIN                  (1)
#define CPG_CLK_ON_REG_MAX                  (27)

#define CPG_RST_REG_MIN                     (1)
#define CPG_RST_REG_MAX                     (15)

typedef struct
{
    union
    {
        uint32_t        word;
        struct
        {
            uint32_t    :2;
            uint32_t    enable:1;
            uint32_t    :1;
            uint32_t    mode:2;
            uint32_t    :26;
        }bit;
    }ssc;
    union
    {
        uint32_t        word[2];
        struct
        {
            uint32_t    p:6;
            uint32_t    m:10;
            uint32_t    k:16;
            uint32_t    s:3;
            uint32_t    :5;
            uint32_t    mrr:6;
            uint32_t    :2;
            uint32_t    mfr:8;
            uint32_t    :8;
        } bit;
    } clk;
} st_cpg_pll_param_t;
typedef enum
{
    CPG_ERROR_ARGUMENT               = -201,
    CPG_ERROR_NO_REGISTER            = -202,
    CPG_ERROR_NULL_POINTER           = -203,
    CPG_ERROR_PLL_TURN_MODE_TIMEOUT  = -204,
    CPG_ERROR_PLL_ACTIVE             = -205,
    CPG_ERROR_PLL_STANDBY            = -206,
    CPG_ERROR_TURN_RESET_TIMEOUT     = -207,
    CPG_ERROR_PLL_NOT_ACTIVE         = -280,
    CPG_ERROR_PLL_NOT_STANDBY        = -290
} e_cpg_error_code_t;

typedef enum
{
    CPG_PLL_1 = 1,
    CPG_PLL_2 = 2,
    CPG_PLL_3 = 3,
    CPG_PLL_4 = 4,
    CPG_PLL_6 = 6,
    CPG_PLL_7 = 7
} e_cpg_pll_num_t;

typedef enum
{
    CPG_DDIV_CA53 = 0,
    CPG_DDIV_SYS,
    CPG_DDIV_CR8REF,
    CPG_DDIV_NFI,
    CPG_DDIV_MMCDDI,
    CPG_DSEL_CLK48,
    CPG_DDIV_ISP1,
    CPG_DDIV_ISP2,
    CPG_DSEL_ISP,
    CPG_SSEL_SDIEMM = 64,
    CPG_SDIV_STG,
    CPG_SSEL_STGCIF,
    CPG_SDIV_DISP1,
    CPG_SDIV_DISP2,
    CPG_SSEL_DISP1,
    CPG_SSEL_DISP2,
    CPG_SDIV_GMCLK,
    CPG_SSEL_GMCLK,
    CPG_SSEL_MTR,
    CPG_SDIV_URT_RCLK,
    CPG_SSEL_URT_RCLK,
    CPG_SSEL_CSI_RCLK
} e_cpg_divsel_t;

/** prototype defined **/

int32_t r8arzv2m_cpg_setClockCtrl(void __iomem *base, uint8_t reg_num, uint16_t target, uint16_t set_value);
int32_t r8arzv2m_cpg_getClockCtrl(void __iomem *base, uint8_t reg_num, uint16_t target);
int32_t CPG_SetResetCtrl(void __iomem *base, uint8_t reg_num, uint16_t target, uint16_t set_value);
int32_t CPG_WaitResetMon(void __iomem *base, uint32_t timeout_c, uint32_t msk, uint32_t val);



#endif /* RDK_CMN_CPG_H */

#endif /* __DT_BINDINGS_CLOCK_R8ARZV2M_CPG_MSSR_H__ */

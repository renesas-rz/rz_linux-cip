/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rzg2l-cru-regs.h--RZ/G2L (and alike SoCs) CRU Registers Definitions
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __RZG2L_CRU_REGS_H__
#define __RZG2L_CRU_REGS_H__

/* HW CRU Registers Definition */

/* CRU Control Register */
#define CRUnCTRL_VINSEL(x)		((x) << 0)

/* CRU Interrupt Enable Register */
#define CRUnIE_EFE			BIT(17)

/* CRU Interrupt Status Register */
#define CRUnINTS_SFS			BIT(16)

/* CRU Reset Register */
#define CRUnRST_VRESETN			BIT(0)

/* Memory Bank Base Address (Lower) Register for CRU Image Data */
#define AMnMBxADDRL(base, x)		((base) + (x) * 2)

/* Memory Bank Base Address (Higher) Register for CRU Image Data */
#define AMnMBxADDRH(base, x)		((base) + (x) * 2)

/* Memory Bank Enable Register for CRU Image Data */
#define AMnMBVALID_MBVALID(x)		GENMASK(x, 0)

/* Memory Bank Status Register for CRU Image Data */
#define AMnMBS_MBSTS			0x7

/* AXI-VD Bus Master Transfer Setting Register */
#define AMnAXIATTR_AXILEN_MASK		GENMASK(3, 0)
#define AMnAXIATTR_AXILEN		(0xf)

/* AXI Master FIFO Pointer Register for CRU Image Data */
#define AMnFIFOPNTR_FIFOWPNTR		GENMASK(7, 0)
#define AMnFIFOPNTR_FIFORPNTR_Y		GENMASK(23, 16)

/* AXI Master Transfer Stop Register for CRU Image Data */
#define AMnAXISTP_AXI_STOP		BIT(0)

/* AXI Master Transfer Stop Status Register for CRU Image Data */
#define AMnAXISTPACK_AXI_STOP_ACK	BIT(0)

/* CRU Image Processing Enable Register */
#define ICnEN_ICEN			BIT(0)

/* CRU Image Processing Main Control Register */
#define ICnMC_DEMTHR			BIT(3)
#define ICnMC_CSCTHR			BIT(5)
#define ICnMC_VCSEL(x)			((x) << 22)
#define ICnMC_INF_MASK			GENMASK(21, 16)
#define ICnMC_INF_YUV8_422		(0x1E << 16)
#define ICnMC_INF_YUV10_422		(0x1F << 16)
#define ICnMC_INF_RGB444		(0x20 << 16)
#define ICnMC_INF_RGB565		(0x22 << 16)
#define ICnMC_INF_RGB666		(0x23 << 16)
#define ICnMC_INF_RGB888		(0x24 << 16)
#define ICnMC_INF_RAW8			(0x2A << 16)
#define ICnMC_INF_RAW10			(0x2B << 16)
#define ICnMC_INF_RAW12			(0x2C << 16)
#define ICnMC_INF_RAW14			(0x2D << 16)
#define ICnMC_INF_RAW16			(0x2E << 16)
#define ICnMC_INF_USER			(0x30 << 16)
#define ICnMC_RAWSTTYP_RGRG		0
#define ICnMC_RAWSTTYP_GRGR		BIT(24)
#define ICnMC_RAWSTTYP_GBGB		BIT(25)
#define ICnMC_RAWSTTYP_BGBG		(BIT(25) | BIT(24))
#define ICnMC_RAWSTTYP_MASK		(BIT(25) | BIT(24))

/* CRU Module Status Register */
#define ICnMS_IA			BIT(2)

/* CRU Data Output Mode Register */
#define ICnDMR_RGBMODE_RGB24		(0 << 0)
#define ICnDMR_RGBMODE_XRGB32		(1 << 0)
#define ICnDMR_RGBMODE_ABGR32		(2 << 0)
#define ICnDMR_RGBMODE_ARGB32		(3 << 0)
#define ICnDMR_YCMODE_YUYV		(0 << 4)
#define ICnDMR_YCMODE_UYVY		(1 << 4)
#define ICnDMR_YCMODE_NV16		(2 << 4)
#define ICnDMR_YCMODE_GREY		(3 << 4)

#endif /* __RZG2L_CRU_REGS_H__ */

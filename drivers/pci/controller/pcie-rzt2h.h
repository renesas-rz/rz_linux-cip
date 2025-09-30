/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCIe driver for Renesas RZ/T2H Series SoCs
 *  Copyright (C) 2022 Renesas Electronics Ltd
 *
 */

#ifndef _PCIE_RZT2H_H
#define _PCIE_RZT2H_H

#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/io.h>
#include "../../../drivers/dma/virt-dma.h"
#include "../../../drivers/dma/dmaengine.h"

#define PCIE_MAX_CHANNEL	2

/* PCI Express to AXI Access */
#define AXI_WINDOW_BASEL_REG(x)					(0x1000 + ((x) * 0x20))
	#define AXI_WINDOW_ENABLE					(0x00000001)
#define AXI_WINDOW_BASEU_REG(x)					(0x1004 + ((x) * 0x20))
#define AXI_WINDOW_MASKL_REG(x)					(0x1008 + ((x) * 0x20))
#define AXI_WINDOW_MASKU_REG(x)					(0x100C + ((x) * 0x20))
#define AXI_DESTINATIONL_REG(x)					(0x1010 + ((x) * 0x20))
#define AXI_DESTINATIONU_REG(x)					(0x1014 + ((x) * 0x20))

/* Supplemental setting */
#define PCIE_INTX			0x0
#define PCIE_MS1			0x4
#define PCIE_MS2			0x8
#define PCIE_MS3			0xC
#define PCIE_MS4			0x10
#define PCIE_MS5			0x14
#define PCIE_PME			0x18
#define PCIE_ACK			0x1C
#define PCIE_MISC			0x20
	#define ALLOW_ENTER_L1		BIT(0)
#define PCIE_MODE			0x24
	#define MODE_PORT_RC		BIT(0)
	#define MODE_PORT_EP		0x0

#define PCI_EP_PCMSET1			0x400
	#define MODE_PORT		(~BIT(1))

#define PCI_EP_RESET_REG		0x310
	#define RESET_CONFIG_DEASSERT	0x0000001C

/* AXI to PCI Express Access */
#define PCIE_WINDOW_BASEL_REG(x)					(0x1100 + ((x) * 0x20))
	#define PCIE_WINDOW_ENABLE					(0x00000001)
#define PCIE_WINDOW_BASEU_REG(x)					(0x1104 + ((x) * 0x20))
#define PCIE_WINDOW_MASKL_REG(x)					(0x1108 + ((x) * 0x20))
#define PCIE_WINDOW_MASKU_REG(x)					(0x110C + ((x) * 0x20))
#define PCIE_DESTINATION_LO_REG(x)				(0x1110 + ((x) * 0x20))
#define PCIE_DESTINATION_HI_REG(x)				(0x1114 + ((x) * 0x20))

/* Request Issuing */
#define REQUEST_DATA_REG(x)						(0x0080 + ((x) * 0x04))
#define REQUEST_RCV_DATA_REG					0x008C
#define REQUEST_ADDR_1_REG						0x0090
#define REQUEST_ADDR_2_REG						0x0094
#define REQUEST_BYTE_ENABLE_REG					0x0098
#define REQUEST_ISSUE_REG						0x009C
	#define MOR_STATUS							0x00070000
	#define TR_TYPE_CFREAD_TP0					0x00000400
	#define TR_TYPE_CFWRITE_TP0					0x00000500
	#define TR_TYPE_CFREAD_TP1					0x00000600
	#define TR_TYPE_CFWRITE_TP1					0x00000700
	#define REQ_ISSUE							0x00000001

/* Interruption */
#define MSI_RCV_WINDOW_ADDRL_REG				0x0100
		#define MSI_RCV_WINDOW_ENABLE			0x00000001
#define MSI_RCV_WINDOW_ADDRU_REG				0x0104
#define MSI_RCV_WINDOW_MASK_REG					0x0108
	#define MSI_RCV_NUM							32
	#define MSI_RCV_WINDOW_SIZE					(MSI_RCV_NUM * sizeof(unsigned int))
	#define MSI_RCV_WINDOW_MASK_MIN				((unsigned long)MSI_RCV_WINDOW_SIZE - 1)
	#define MSI_RCV_WINDOW_INVALID				0xFFFFFFFF
#define PCI_INTX_RCV_INTERRUPT_ENABLE_REG		0x0110
	#define MSI_RECEIVE_INTERRUPT_ENABLE		0x00000010
	#define INTX_RECEIVE_INTERRUPT_ENABLE		0x0000000F
#define PCI_INTX_RCV_INTERRUPT_STATUS_REG		0x0114
	#define MSI_RECEIVE_INTERRUPT_STATUS		0x00000010
	#define INTX_RECEIVE_INTERRUPT_STATUS		0x0000000F
	#define ALL_RECEIVE_INTERRUPT_STATUS		0x00000001F
#define PCI_INTX_OUT_STATUS_REG					0x0118

/* Message */
#define MSG_RCV_INTERRUPT_ENABLE_REG			0x0120
	#define	MESSAGE_ENABLE						0x01000000
#define MSG_RCV_INTERRUPT_STATUS_REG			0x0124
#define MSG_CODE_REG							0x0130
#define MSG_DATA_REG							0x0134
#define MSG_HEADER_3RDDW_REG					0x0138
#define MSG_HEADER_4THDW_REG					0x013C

/* Interrupt Table */
#define INTERRUPT_TABLE_REG						0x0140
	#define	INTERRUPT_TABLE_DMA_INT				0xFF000000

/* Error Event */
#define PCIE_EVENT_INTERRUPT_EANBLE_0_REG		0x0200
	#define	DL_UPDOWN_ENABLE					0x00000200
#define PCIE_EVENT_INTERRUPT_STATUS_0_REG		0x0204
	#define	DL_UPDOWN_STATUS					0x00000200
#define AXI_MASTER_ERR_INTERRUPT_EANBLE_REG		0x0210
#define AXI_MASTER_ERR_INTERRUPT_STATUS_REG		0x0214
#define AXI_SLAVE_ERR_INTERRUPT_EANBLE_1_REG	0x0210
#define AXI_SLAVE_ERR_INTERRUPT_STATUS_1_REG	0x0214

#define PCI_RC_LEQCTL	0x61BC
	#define PCI_RC_1_LANE		0x7F7F7878
	#define PCI_RC_2_LANE		0x78787878

#define	PCI_RC_PCCTRL1		0x0404
	#define MODE_EQ_AUTONOMOUS	BIT(10)

/* Macro Control */
#define PERMISSION_REG							0x0300
	#define CFG_HWINIT_EN						0x4
	#define CFG_PHYINIT_EN						0x2
#define PCI_RC_RESET_REG						0x0310
	#define RST_B							BIT(0)
	#define RST_GP_B						BIT(1)
	#define RST_RSM_B						BIT(2)
	#define RST_CFG_B						BIT(3)
	#define RST_LOAD_B						BIT(4)
	#define RST_PS_B						BIT(5)
	#define RST_OUT_B						BIT(6)
	#define RST_PREG_B						BIT(7)
#define MODE_SET_0_REG							0x0314
#define MODE_SET_1_REG							0x0318
#define GENERAL_PURPOSE_OUTPUT_REG(x)			(0x0380 + ((x) * 0x04))
#define GENERAL_PURPOSE_INPUT_REG(x)			(0x0390 + ((x) * 0x04))
#define PCIE_CORE_MODE_SET_1_REG				0x0400
#define PCIE_CORE_CONTROL_1_REG					0x0404
#define PCIE_CORE_STATUS_1_REG					0x0408
	#define LTSSM_ST_ALL_SHIFT					(8)
	#define LTSSM_ST_ALL_MASK					(0x7F << LTSSM_ST_ALL_SHIFT)
	#define LTSSM_ST_DETECT						(0x0C << LTSSM_ST_ALL_SHIFT)
	#define DL_DOWN_STATUS						0x00000001
#define PCIE_LOOPBACK_TEST_REG					0x040C
#define PCIE_CORE_CONTROL_2_REG					0x0410
#define PCIE_LINK_SPEED_CHANGE_MASK				(3 << 8)
#define PCIE_LINK_SPEED_CHANGE_5_0GTS				(1 << 8)
#define PCIE_LINK_SPEED_CHANGE_8_0GTS				(2 << 8)
#define PCIE_LINK_CHANGE_REASON_INTENTIONAL			BIT(4)
#define PCIE_LINK_SPEED_CHANGE_REQ				BIT(0)
#define PCIE_CORE_STATUS_2_REG					0x0414
	#define STATE_RECEIVER_DETECTED				(0xff << 8)
#define PCIE_LINK_DATA_RATE					GENMASK(3, 1)
#define PCIE_LINK_DATA_RATE_2_5GTS				(1 << 1)
#define PCIE_LINK_DATA_RATE_5_0GTS				(3 << 1)
#define PCIE_LINK_DATA_RATE_8_0GTS				(7 << 1)

#define PCI_PHY_XCFGD(x)		(2000 + (0x10 * x))
#define PCI_PHY_XCFGA_CMN(x)		(2400 + (0x10 * x))
#define PCI_PHY_XCFGA_L0(x)		(2500 + (0x10 * x))
#define PCI_PHY_XCFGA_L1(x)		(2560 + (0x10 * x))
/* MSI receive register group */
#define PCI_RC_MSIRCVE(x)			(0x600 + 0x10 * (x))
#define PCI_RC_MSIRCVE_EN			BIT(0)
#define PCI_RC_MSIRCVMSK(x)			(0x608 + 0x10 * (x))
#define PCI_RC_MSIRCVMSK_MSI_MASK		0xFFFFFFFF
#define PCI_RC_MSIRCVSTAT(x)			(0x60C + 0x10 * (x))

/* PCIe EP control */
#define PCI_EP_PEIS0_REG			0x204
#define PCI_EP_PEIE0_REG			0x200
#define PCI_EP_PEIS1_REG			0x20C
#define PCI_EP_PEIE1_REG			0x208
#define PCI_EP_AMEIS_REG			0x214
#define PCI_EP_AMEIE_REG			0x210
#define PCI_EP_ASEIS1_REG			0x224
#define PCI_EP_ASEIE1_REG			0x220
#define PCI_EP_MSGRCVIS_REG			0x124
#define PCI_EP_MSGRCVIE_REG			0x120
	#define PCI_EP_MSGRCVIE			0x010A0000
#define PCI_EP_REQISS				0x009C
	#define PCI_EP_REQISS_RI		BIT(0)
	#define PCI_EP_REQISS_TR_TYPE		(0x4 << 8)

/* PCIe RC Control */
#define PCI_RC_BASE_ADD							0x85030000
#define PCI_RC_MSGRCVIE_REG						0x0120
	#define  INT_MR_SET    						0x01050000
#define PCI_RC_MSGRCVIS_REG						0x0124
	#define  INT_MR_CLR							0x010F0000
#define PCI_RC_PEIE0_REG						0x0200
	#define INT_EN0_SET							0x60001200
#define PCI_RC_PEIS0_REG						0x0204
	#define INT_ST0_CLR							0x00001200
	#define INT_SPEED_CHANGE_DONE					BIT(29)
#define PCI_RC_PEIE1_REG						0x0208
	#define INT_EN1_SET							0x00030303
#define PCI_RC_PEIS1_REG						0x020c
	#define INT_ST1_CLR							0x00030303
#define PCI_RC_AMEIE_REG						0x0210
	#define INT_EN_AXIM_SET						0x00000F0F
#define PCI_RC_AMEIS_REG						0x0214
	#define INT_ST_AXIM_CLR						0x00000F0F
#define PCI_RC_ASEIE1_REG						0x0220
	#define INT_EN_AXIS_SET						0x00000F03
#define PCI_RC_ASEIS1_REG						0x0224
	#define INT_ST_AXIS_CLR						0x00000F03
#define PCI_RC_PERM_REG							0x0300
#define PCI_RC_VID_REG							0x6000
#define PCI_RC_RID_CC_REG						0x6008
	#define  REVID_CLSCODE_INIT					0xFFFFFFDF
#define PCI_RC_LINKCS						0x6070
	#define PCI_RC_LINKCS_CUR_LINK_SPEED_MASK		(0xF << 16)
	#define PCI_RC_LINKCS_CUR_LINK_SPEED_2_5GTS		(1 << 16)
	#define PCI_RC_LINKCS_CUR_LINK_SPEED_5_0GTS		(2 << 16)
	#define PCI_RC_LINKCS_CUR_LINK_SPEED_8_0GTS		(3 << 16)
#define PCI_RC_LINKCS2						0x6090
	#define PCI_RC_LINKCS2_TARGET_LINK_SPEED_MASK		0xF
	#define PCI_RC_LINKCS2_TARGET_LINK_SPEED_2_5GTS		1
	#define PCI_RC_LINKCS2_TARGET_LINK_SPEED_5_0GTS		2
	#define PCI_RC_LINKCS2_TARGET_LINK_SPEED_8_0GTS		3
#define PCI_RC_BARMSK00L_REG					0x60A0
	#define  BASEADR_MKL_ALLM					0xFFFFFFFF
#define PCI_RC_BARMSK00U_REG	 				0x60A4
	#define  BASEADR_MKU_ALLM					0xFFFFFFFF
#define PCI_RC_BSIZE00_01_REG	 				0x60C8
	#define  BASESZ_INIT						0x00000000

/* DMAC Common Control */
#define DMA_CONTROL_REG							0x0800
#define DMA_INTERRUPT_ENABLE_REG				0x0808
	#define DMA_D_PMRS_256B						0x00000001
	#define	DMA_INTERRUPT_ENABLE_INT			0xF
	#define DMA_END_EN							BIT(0)
	#define DMA_STOP_EN							BIT(1)
	#define DMA_QUE_EM_EN						BIT(2)
	#define DMA_ERR_EN							BIT(3)
#define DMA_INTERRUPT_STATUS_REG				0x080C
	#define RZT2H_PCIE_DMA_END					BIT(0)
	#define RZT2H_PCIE_DMA_STOP					BIT(1)
	#define RZT2H_PCIE_DMA_QUE_EMP				BIT(2)
	#define RZT2H_PCIE_DMA_ERR					BIT(3)
	#define RZT2H_PCIE_DMA_STATUS_ALL			(RZT2H_PCIE_DMA_END | \
								RZT2H_PCIE_DMA_STOP | \
								RZT2H_PCIE_DMA_QUE_EMP | \
								RZT2H_PCIE_DMA_ERR)

/* DMAC Channel Control */
#define DMA_CHANNEL_CONTROL_REG(x)				(0x0900 + 0x80 * (x))
	#define DMA_RDMA_EN							BIT(0)
	#define DMA_QUE_EN							BIT(1)
	#define DMA_QUE_CLR							BIT(8)
#define DMA_DESCRIPTORL_ADDR_LOWER_REG(x)		(0x0908 + 0x80 * (x))
#define DMA_DESCRIPTORL_ADDR_UPPER_REG(x)		(0x090C + 0x80 * (x))
#define QUE_ENTRY_REG(x)						(0x0910 + 0x80 * (x))
	#define QUE_ENTRY_LS						BIT(24)
	#define QUE_ENTRY_EI						BIT(25)
	#define QUE_REGISTRACTION					(BIT(31) | BIT(30) | \
							BIT(29) | BIT(28) | BIT(27) | BIT(26))
	#define QUE_INTERRUPT_OFF					QUE_REGISTRACTION
	#define QUE_INTERRUPT_ON					(QUE_REGISTRACTION | \
							QUE_ENTRY_EI | QUE_ENTRY_LS)

/* DMAC DMA Setting */
#define DMA_DESCRIPTOR_CONTROL_REG(x)			(0x0920 + 0x80 * (x))
#define DMA_TRANSACTION_CONTROL_REG(x)			(0x0924 + 0x80 * (x))
#define DMA_SIZE_REG(x)							(0x0928 + 0x80 * (x))
#define DMA_SOURCE_ADDR_LOWER_REG(x)			(0x0930 + 0x80 * (x))
#define DMA_SOURCE_ADDR_UPPER_REG(x)			(0x0934 + 0x80 * (x))
#define DMA_DESTINATION_ADDR_LOWER_REG(x)		(0x0938 + 0x80 * (x))
#define DMA_DESTINATION_ADDR_UPPER_REG(x)		(0x093C + 0x80 * (x))
#define DMA_DESCRIPTOR_LINK_POINTER_LOWER_REG(x)	(0x0940 + 0x80 * (x))
#define DMA_DESCRIPTOR_LINK_POINTER_UPPER_REG(x)	(0x0944 + 0x80 * (x))

/* DMAC DMA Status */
#define DMA_REST_SIZE_REG(x)					(0x0950 + 0x80 * (x))
#define AXI_REQUEST_ADDR_LOWER_REG(x)			(0x0960 + 0x80 * (x))
#define AXI_REQUEST_ADDR_UPPER_REG(x)			(0x0964 + 0x80 * (x))
#define PCIE_REQUEST_ADDR_LOWER_REG(x)			(0x0968 + 0x80 * (x))
#define PCIE_REQUEST_ADDR_UPPER_REG(x)			(0x096C + 0x80 * (x))
#define QUE_STATUS_REG(x)						(0x0970 + 0x80 * (x))
#define DMAC_ERROR_STATUS_REG(x)				(0x0978 + 0x80 * (x))

/* PCIe Configuration Register */
#define PCIE_CONFIGURATION_REG					0x6000
	#define PCI_RC_VID_ADR						0x00
	#define PCI_RC_RID_CC_ADR					0x08
	#define PCI_PM_CAPABILITIES					0x40
	#define PCI_RC_BARMSK00L_ADR				0xA0
	#define PCI_RC_BARMSK00U_ADR				0xA4
	#define PCI_RC_BSIZE00_01_ADR				0xC8

/* PCIe EP Configuration Register */
#define PCIE_CONFIGURATION_REG_EP(f)				(0x7000 - (0x1000 * (f)))
#define PCI_EP_VID_F						0x00
#define PCI_EP_COM_STA_F					0x04
	#define PCI_EP_COM_STA_F_MSE				BIT(1)
	#define PCI_EP_COM_STA_F_BME				BIT(2)
#define PCI_EP_RID_CC_F						0x08
#define PCI_EP_SUBSID_F						0x2C
#define PCI_EP_INTERRUPT_F					0x3C
#define PCI_EP_BARMSK00L_F					0xA0
	#define PCIE_CFG_BAR_MASK0_L_EP_F0			(0x0000007F)
#define PCI_EP_BARMSK00U_F					0xA4
	#define	PCIE_CFG_BAR_MASK0_H_EP_F0			(0x00000000)
#define	PCI_EP_BARMSK01L_F					0xA8
	#define PCIE_CFG_BAR_MASK1_L_EP_F0			(0x0000007F)
#define PCI_EP_BARMSK01U_F					0xAC
	#define PCIE_CFG_BAR_MASK1_H_EP_F0			(0x00000000)
#define PCI_EP_BARMSK02L_F					0xB0
	#define PCIE_CFG_BAR_MASK2_L_EP_F0			(0x000000FF)
#define PCI_EP_BARMSK02U_F					0xB4
	#define PCIE_CFG_BAR_MASK2_H_EP_F0			(0x00000000)
#define PCI_EP_BSIZE00_01_F					0xC8
	#define PCIE_CFG_BASE_SIZE_0001_EP_F0			(0x00000000)
#define PCI_EP_BSIZE02_03_F					0xCC
	#define PCIE_CFG_BASE_SIZE_0203_EP_F0			(0x00000000)
#define PCI_EP_BSIZE04_05_F					0xD0
	#define PCIE_CFG_BASE_SIZE_0405_EP_F0			(0x00000000)
#define PCI_EP_BSIZE06_F					0xD4
	#define PCIE_CFG_BASE_SIZE_06_EP_F0			(0x00000000)
#define PCI_EP_MSICAP(x)					(0xE0 + ((x) * 0x4))
	#define  MSICAP0_MSIE					BIT(16)
	#define  MSICAP0_MMESCAP_OFFSET				17
	#define MSICAP0_MMESE_OFFSET				20
	#define  MSICAP0_MMESE_MASK				GENMASK(22, 20)

/* PCIe Configuration Special Register offset */
#define PCIE_CONF_OFFSET_BAR0_MASK_LO			0x00A0
#define PCIE_CONF_OFFSET_BAR0_MASK_UP			0x00A4

/* PCI/AXI Window alignment */
#define RZT2H_WINDOW_SIZE_MIN					0x00001000
#define RZT2H_WINDOW_SIZE_MAX					0xFFFFFFFF
#define RZT2H_PCI_WINDOW_SIZE_MAX				0x40000000

#define INT_PCI_MSI_NR	32
#define INT_PCI_INTX_NR	1

#define RZT2H_PCI_MAX_RESOURCES		2
#define MAX_NR_INBOUND_MAPS		8

#define PCIE_CONF_BUS(b)	(((b) & 0xff) << 24)
#define PCIE_CONF_DEV(d)	(((d) & 0x1f) << 19)
#define PCIE_CONF_FUNC(f)	(((f) & 0x7) << 16)

#define STS_CHECK_LOOP	500

/* ----------------------------------------------------
  PCIe Configuration setting value
-------------------------------------------------------*/
#define PCIE_CONF_VENDOR_ID					0x1912
#define PCIE_CONF_DEVICE_ID					0x1135

#define PCIE_CONF_REVISION_ID					0x00

#define PCIE_CONF_BASE_CLASS					0xFF
#define PCIE_CONF_SUB_CLASS					0x04
#define PCIE_CONF_PROGRAMING_IF					0x00

#define PM_CAPABILITIES_INIT					0x4803E001

#define PCIE_CONF_PRIMARY_BUS					0x00
#define PCIE_CONF_SECOUNDARY_BUS				0x01
#define PCIE_CONF_SUBORDINATE_BUS				0x01

#define PCIE_CONF_MEMORY_BASE					0xFFFF
#define PCIE_CONF_MEMORY_LIMIT					0xFFFF

#define PCIE_CONF_BAR0_MASK_LO					0xFFFFFFFF
#define PCIE_CONF_BAR0_MASK_UP					0xFFFFFFFF

#define LINK_WIDTH_CHANGE_ENABLE 				0x01000000
#define LINK_WIDTH_CHANGE_REQ_ON				0x00010000
#define LINK_WIDTH_CHANGE_DONE					0x20000000
#define LINK_WIDTH_CHANGE_REQ_OFF				0x00000000

#define  LAM_PREFETCH		BIT(3)
#define  LAM_64BIT		BIT(2)
#define  LAR_ENABLE		BIT(1)
#define  PAR_ENABLE		BIT(31)
#define  IO_SPACE		BIT(8)

#ifdef CONFIG_RZT2H_PCIE_DMA
#define PCIE_DMA_MAX_CHANNEL			8
#define PCIE_DMA_MAX_DESCRIPTOR_LIST	8
#define PCIE_DMA_MIN_PACKET_SIZE		0x1000
#define PCIE_DMA_MAX_PACKET_SIZE		0xFFFFFFFF

#define PCIE_DMA_DSCFM						(1 << 28)
#define PCIE_DMA_WBD						(1 << 26)
#define PCIE_DMA_LE						(1 << 25)
#define PCIE_DMA_LV						(1 << 24)
#define PCIE_DMA_D						(1 << 23)

#define PCIE_DMA_CCH_L(x)					((x & 0xf) << 20)
	#define PCIE_DMA_CCH_L_DEFAULT					0
#define PCIE_DMA_CCH_D(x)					((x & 0xf) << 16)
	#define PCIE_DMA_CCH_D_PCIE_TO_AXI				1
	#define PCIE_DMA_CCH_D_AXI_TO_PCIE				0
#define PCIE_DMA_TC(x)						((x & 0x7) << 12)
	#define PCIE_DMA_TC_DEFAULT					0
#define PCIE_DMA_ATB(x)						((x & 0x3) << 8)
	#define PCIE_DMA_ATB_DEFAULT					0
#define PCIE_DMA_FUNC(x)					((x & 0x4) << 4)
	#define PCIE_DMA_FUNC_0						0
#define PCIE_DMA_DIR(x)						(x & BIT(0))
	#define PCIE_DMA_DIR_AXI_TO_PCIE				1
	#define PCIE_DMA_DIR_PCIE_TO_AXI				0
#endif

struct rzt2h_axi_window_set {
	u32	base[RZT2H_PCI_MAX_RESOURCES];
	u32	base_u[RZT2H_PCI_MAX_RESOURCES];
	u32	mask[RZT2H_PCI_MAX_RESOURCES];
	u32	dest[RZT2H_PCI_MAX_RESOURCES];
	u32	dest_u[RZT2H_PCI_MAX_RESOURCES];
};

struct rzt2h_pci_window_set {
	u32	base[RZT2H_PCI_MAX_RESOURCES];
	u32	mask[RZT2H_PCI_MAX_RESOURCES];
	u32	dest_u[RZT2H_PCI_MAX_RESOURCES];
	u32	dest_l[RZT2H_PCI_MAX_RESOURCES];
};

struct rzt2h_interrupt_set {
	u32	msi_win_addrl;
	u32	msi_win_addru;
	u32	msi_win_mask;
	u32	intx_ena;
	u32	msi_ena;
};

struct rzt2h_save_reg {
	struct rzt2h_axi_window_set		axi_window;
	struct rzt2h_pci_window_set		pci_window;
	struct rzt2h_interrupt_set		interrupt;
};

#ifdef CONFIG_RZT2H_PCIE_DMA
struct rzt2h_pcie_dma_hw_node {
	u32 param0;
	u32 param1;
	u32 size;
	u32 padding;
	u32 saddr_L;
	u32 saddr_U;
	u32 daddr_L;
	u32 daddr_U;
	u32 next_L;
	u32 next_U;
} __aligned(64);

struct rzt2h_pcie_dma_sw_node {
	dma_addr_t pdesc;
	struct rzt2h_pcie_dma_hw_node *desc;
};

struct rzt2h_pcie_dma_desc {
	struct virt_dma_desc vd;
	struct rzt2h_pcie_dma_chan *chan;
	dma_addr_t dev_addr;
	enum dma_transfer_direction direction;
	unsigned int n_nodes;
	struct rzt2h_pcie_dma_sw_node node[];
};

struct rzt2h_pcie_dma_chan {
	struct virt_dma_chan vc;
	struct rzt2h_pcie_dmac *dmac;
	struct dma_pool *pool;
	struct dma_slave_config scfg;
	int index;
	struct rzt2h_pcie_dma_desc *desc;
	enum dma_status	status;
};

struct rzt2h_pcie_dmac {
	struct dma_device engine;
	struct rzt2h_pcie *pcie;
	struct device *dev;
	int dma_irq;
	unsigned int n_channels;
	struct rzt2h_pcie_dma_chan *channels;
	/* Reserved for support DMA transfer requested from userspace */
	int dma_mode[PCIE_DMA_MAX_CHANNEL];
	int dma_status[PCIE_DMA_MAX_CHANNEL];
};
#endif

struct rzt2h_pcie {
	struct device			*dev;
	void __iomem			*base;
	struct rzt2h_save_reg	save_reg;
#ifdef CONFIG_RZT2H_PCIE_DMA
	struct rzt2h_pcie_dmac	dmac;
#endif
};

void rzt2h_pci_write_reg(struct rzt2h_pcie *pcie, u32 val, unsigned long reg);
u32 rzt2h_pci_read_reg(struct rzt2h_pcie *pcie, unsigned long reg);
void rzt2h_rmw(struct rzt2h_pcie *pcie, int where, u32 mask, u32 data);
u32 rzt2h_read_conf(struct rzt2h_pcie *pcie, int where);
void rzt2h_write_conf(struct rzt2h_pcie *pcie, u32 data, int where);
void rzt2h_phy_write(struct rzt2h_pcie *pcie, u32 data, int where);
void rzt2h_pcie_set_outbound(struct rzt2h_pcie *pcie, int win,
			    struct resource_entry *window);
void rzt2h_pcie_set_inbound(struct rzt2h_pcie *pcie, u64 cpu_addr,
			   u64 pci_addr, u64 flags, int idx, bool host);

u32 rzt2h_read_conf_ep(struct rzt2h_pcie *pcie, int where, u8 fn);
void rzt2h_write_conf_ep(struct rzt2h_pcie *pcie, u32 data, int where, u8 fn);
void rzt2h_pcie_ep_set_outbound(struct rzt2h_pcie *pcie, int win, struct resource_entry *window,
			       phys_addr_t cpu_addr, u64 pci_addr, size_t size);
void rzt2h_pcie_ep_set_inbound(struct rzt2h_pcie *pcie, u64 cpu_addr, u64 pci_addr,
			      u64 flags, int idx, bool host);

#ifdef CONFIG_RZT2H_PCIE_DMA
int rzt2h_pcie_dma_init(struct rzt2h_pcie *pcie);
void rzt2h_pcie_dma_remove(struct rzt2h_pcie *pcie);
#endif

#endif

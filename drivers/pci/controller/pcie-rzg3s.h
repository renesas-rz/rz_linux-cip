/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCIe driver for Renesas RZ/G3S SoCs
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 *
 */

#ifndef _PCIE_RZG3S_H
#define _PCIE_RZG3S_H

/* AXI registers */
#define RZG3S_PCI_REQDATA(id)			(0x80 + (id) * 0x4)
#define RZG3S_PCI_REQRCVDAT			0x8c

#define RZG3S_PCI_REQADR1			0x90
#define RZG3S_PCI_REQADR1_BUS			GENMASK(31, 24)
#define RZG3S_PCI_REQADR1_DEV			GENMASK(23, 19)
#define RZG3S_PCI_REQADR1_FUNC			GENMASK(18, 16)
#define RZG3S_PCI_REQADR1_REG			GENMASK(11, 0)

#define RZG3S_PCI_REQBE				0x98
#define RZG3S_PCI_REQBE_BYTE_EN			GENMASK(3, 0)

#define RZG3S_PCI_REQISS			0x9c
#define RZG3S_PCI_REQISS_MOR_STATUS		GENMASK(18, 16)
#define RZG3S_PCI_REQISS_TR_TYPE		GENMASK(11, 8)
#define RZG3S_PCI_REQISS_TR_TP0_RD		FIELD_PREP(RZG3S_PCI_REQISS_TR_TYPE, 0x4)
#define RZG3S_PCI_REQISS_TR_TP0_WR		FIELD_PREP(RZG3S_PCI_REQISS_TR_TYPE, 0x5)
#define RZG3S_PCI_REQISS_TR_TP1_RD		FIELD_PREP(RZG3S_PCI_REQISS_TR_TYPE, 0x6)
#define RZG3S_PCI_REQISS_TR_TP1_WR		FIELD_PREP(RZG3S_PCI_REQISS_TR_TYPE, 0x7)
#define RZG3S_PCI_REQISS_REQ_ISSUE		BIT(0)

#define RZG3S_PCI_MSIRCVWADRL			0x100
#define RZG3S_PCI_MSIRCVWADRL_MASK		GENMASK(31, 3)
#define RZG3S_PCI_MSIRCVWADRL_MSG_DATA_ENA	BIT(1)
#define RZG3S_PCI_MSIRCVWADRL_ENA		BIT(0)

#define RZG3S_PCI_MSIRCVWADRU			0x104

#define RZG3S_PCI_MSIRCVWMSKL			0x108
#define RZG3S_PCI_MSIRCVWMSKL_MASK		GENMASK(31, 2)

#define RZG3S_PCI_PINTRCVIE			0x110
#define RZG3S_PCI_PINTRCVIE_INTX(i)		BIT(i)
#define RZG3S_PCI_PINTRCVIE_MSI			BIT(4)

/* Register is R/W1C, it doesn't require locking. */
#define RZG3S_PCI_PINTRCVIS			0x114
#define RZG3S_PCI_PINTRCVIS_INTX(i)		BIT(i)
#define RZG3S_PCI_PINTRCVIS_MSI			BIT(4)

#define RZG3S_PCI_MSGRCVIE			0x120
#define RZG3S_PCI_MSGRCVIE_PME_TURN_OFF_RCV	BIT(17)
#define RZG3S_PCI_MSGRCVIE_PM_STATE_NAK_RCV	BIT(19)
#define RZG3S_PCI_MSGRCVIE_MSG_RCV		BIT(24)

#define RZG3S_PCI_MSGRCVIS			0x124
#define RZG3S_PCI_MSGRCVIS_MRI			BIT(24)

#define RZG3S_PCI_PEIE0				0x200
#define RZG3S_PCI_PEIE0_DL_UPDOWN		BIT(9)

#define RZG3S_PCI_PEIS0				0x204
#define RZG3S_PCI_PEIS0_RX_DLLP_PM_ENTER	BIT(12)
#define RZG3S_PCI_PEIS0_DL_UPDOWN		BIT(9)

#define RZG3S_PCI_PEIE1				0x208
#define RZG3S_PCI_PEIS1				0x20c
#define RZG3S_PCI_AMEIE				0x210
#define RZG3S_PCI_AMEIS				0x214
#define RZG3S_PCI_ASEIE1			0x220
#define RZG3S_PCI_ASEIS1			0x224

#define RZG3S_PCI_PCMSET1			0x400
#define RZG3S_PCI_PCMSET1_MODE_PORT		BIT(1)

#define RZG3S_PCI_PCSTAT1			0x408
#define RZG3S_PCI_PCSTAT1_LTSSM_STATE		GENMASK(14, 10)
#define RZG3S_PCI_PCSTAT1_DL_DOWN_STS		BIT(0)

#define RZG3S_PCI_PCCTRL1			0x404
#define RZG3S_PCI_PCCTRL1_MODE_EQ_AUTO		BIT(10)

#define RZG3S_PCI_PCCTRL2			0x410
#define RZG3S_PCI_PCCTRL2_LS_CHG		GENMASK(9, 8)
#define RZG3S_PCI_PCCTRL2_LS_CHG_REQ		BIT(0)

#define RZG3S_PCI_PCSTAT2			0x414
#define RZG3S_PCI_PCSTAT2_LS_CHG_DONE		BIT(28)
#define RZG3S_PCI_PCSTAT2_SDRIRE		GENMASK(7, 1)
#define RZG3S_PCI_PCSTAT2_STATE_RX_DETECT	GENMASK(15, 8)

#define RZG3S_PCI_PERM				0x300
#define RZG3S_PCI_PERM_CFG_HWINIT_EN		BIT(2)
#define RZG3S_PCI_PERM_PIPE_PHY_REG_EN		BIT(1)

#define RZG3S_PCI_RESET				0x310
#define RZG3S_PCI_RESET_RST_OUT_B		BIT(6)
#define RZG3S_PCI_RESET_RST_PS_B		BIT(5)
#define RZG3S_PCI_RESET_RST_LOAD_B		BIT(4)
#define RZG3S_PCI_RESET_RST_CFG_B		BIT(3)
#define RZG3S_PCI_RESET_RST_RSM_B		BIT(2)
#define RZG3S_PCI_RESET_RST_GP_B		BIT(1)
#define RZG3S_PCI_RESET_RST_B			BIT(0)

#define RZG3S_PCI_MSIRE(id)			(0x600 + (id) * 0x10)
#define RZG3S_PCI_MSIRE_ENA			BIT(0)

#define RZG3S_PCI_MSIRM(id)			(0x608 + (id) * 0x10)

/* Register is R/W1C, it doesn't require locking. */
#define RZG3S_PCI_MSIRS(id)			(0x60c + (id) * 0x10)

#define RZG3S_PCI_AWBASEL(id)			(0x1000 + (id) * 0x20)
#define RZG3S_PCI_AWBASEL_WIN_ENA		BIT(0)

#define RZG3S_PCI_AWBASEU(id)			(0x1004 + (id) * 0x20)
#define RZG3S_PCI_AWMASKL(id)			(0x1008 + (id) * 0x20)
#define RZG3S_PCI_AWMASKU(id)			(0x100c + (id) * 0x20)
#define RZG3S_PCI_ADESTL(id)			(0x1010 + (id) * 0x20)
#define RZG3S_PCI_ADESTU(id)			(0x1014 + (id) * 0x20)

#define RZG3S_PCI_PWBASEL(id)			(0x1100 + (id) * 0x20)
#define RZG3S_PCI_PWBASEL_ENA			BIT(0)

#define RZG3S_PCI_PWBASEU(id)			(0x1104 + (id) * 0x20)
#define RZG3S_PCI_PDESTL(id)			(0x1110 + (id) * 0x20)
#define RZG3S_PCI_PDESTU(id)			(0x1114 + (id) * 0x20)
#define RZG3S_PCI_PWMASKL(id)			(0x1108 + (id) * 0x20)
#define RZG3S_PCI_PWMASKU(id)			(0x110c + (id) * 0x20)

/* PHY control registers */
#define RZG3S_PCI_PHY_XCFGD(id)			(0x2000 + (id) * 0x10)
#define RZG3S_PCI_PHY_XCFGD_NUM			39

#define RZG3S_PCI_PHY_XCFGA_CMN(id)		(0x2400 + (id) * 0x10)
#define RZG3S_PCI_PHY_XCFGA_CMN_NUM		16

#define RZG3S_PCI_PHY_XCFGA_RX(id)		(0x2500 + (id) * 0x10)
#define RZG3S_PCI_PHY_XCFGA_RX_NUM		13

#define RZG3S_PCI_PHY_XCFGA_TX			0x25d0

#define RZT2H_PCI_PHY_XCFGA_L0_L1_NUM		12

#define RZG3S_PCI_PHY_XCFG_CTRL			0x2a20
#define RZG3S_PCI_PHY_XCFG_CTRL_PHYREG_SEL	BIT(0)

/* PCIe registers */
#define RZG3S_PCI_CFG_BASE			0x6000
#define RZG3S_PCI_CFG_VID			0x00
#define RZG3S_PCI_CFG_DEVCS			0x68
#define RZG3S_PCI_CFG_BARMSK00L			0xa0
#define RZG3S_PCI_CFG_BARMSK00U			0xa4
#define RZG3S_PCI_CFG_BSIZE00_01		0xc8

/* Only in endpoint mode */
#define RZ_PCI_EP_AXI_CFG(reg, fn)		(reg  + (fn) * 0x200)
#define RZ_PCI_EP_TYPE0_CFG(reg, fn)		(reg  + (fn) * 0x1000)

#define RZ_PCI_EP_INTX(fn)			BIT(fn)

#define RZ_PCI_EP_CFG_BARMSK(id)		(RZG3S_PCI_CFG_BARMSK00L + (id) * 0x4)
#define RZ_PCI_EP_CFG_INT			0x3c
#define RZ_PCI_EP_CFG_BSIZE02_03		0xcc
#define RZ_PCI_EP_CFG_BSIZE04_05		0xd0
#define RZ_PCI_EP_CFG_BSIZE06			0xd4

#define RZ_PCI_EP_CFG_MSICAP			0xe0
#define RZ_PCI_EP_CFG_MSICAP_MSIE		BIT(16)
#define RZ_PCI_EP_CFG_MSICAP_MMESE		GENMASK(22, 20)
#define RZ_PCI_EP_CFG_MSICAP_MMESCAP		GENMASK(19, 17)

#define PCI_EP_LEQCTL_F1			0x1C0
#define PCI_EP_CFG_OFFSET_MAX			PCI_EP_LEQCTL_F1
#define PCI_EP_CFG_REG_NUM			((PCI_EP_CFG_OFFSET_MAX / 4) + 1)

#define RZ_PCI_EP_MAX_RESOURCES			2

#define RZG3S_PCI_CFG_PCIEC			0x60

/* Maximum number of windows */
#define RZG3S_MAX_WINDOWS			8

/* Number of MSI interrupts per register */
#define RZG3S_PCI_MSI_INT_PER_REG		32
/* The number of MSI interrupts */
#define RZG3S_PCI_MSI_INT_NR			RZG3S_PCI_MSI_INT_PER_REG

/* Timeouts experimentally determined */
#define RZG3S_REQ_ISSUE_TIMEOUT_US		2500

#define PCIE_LINK_WAIT_DL_MS			5
#define PCIE_LINK_WAIT_DL_MAX_RETRIES		50

/**
 * struct rzg3s_sysc_function - System Controller function descriptor
 * @offset: Register offset from the System Controller base address
 * @stride: Distance in bytes between consecutive registers for this function
 * @mask: Bit mask for the function within the register
 */
struct rzg3s_sysc_function {
	u32 offset;
	u32 stride;
	u32 mask;
};

/**
 * enum rzg3s_sysc_func_id - System controller function IDs
 * @RZG3S_SYSC_FUNC_ID_RST_RSM_B: RST_RSM_B SYSC function ID
 * @RZG3S_SYSC_FUNC_ID_L1_ALLOW: L1 allow SYSC function ID
 * @RZG3S_SYSC_FUNC_ID_MODE: Mode SYSC function ID
 * @RZG3S_SYSC_FUNC_LANES_MODE: Link selection SYSC function ID
 * @RZG3S_SYSC_FUNC_ID_MAX: Max SYSC function ID
 */
enum rzg3s_sysc_func_id {
	RZG3S_SYSC_FUNC_ID_RST_RSM_B,
	RZG3S_SYSC_FUNC_ID_INTX,
	RZG3S_SYSC_FUNC_ID_MSI1,
	RZG3S_SYSC_FUNC_ID_MSI2,
	RZG3S_SYSC_FUNC_ID_MSI4,
	RZG3S_SYSC_FUNC_ID_L1_ALLOW,
	RZG3S_SYSC_FUNC_ID_MODE,
	RZG3S_SYSC_FUNC_LANES_MODE,
	RZG3S_SYSC_FUNC_ID_MAX,
};

/**
 * struct rzg3s_sysc_info - RZ/G3S System Controller info
 * @functions: SYSC function descriptors array
 */
struct rzg3s_sysc_info {
	const struct rzg3s_sysc_function functions[RZG3S_SYSC_FUNC_ID_MAX];
};

/**
 * struct rzg3s_sysc - RZ/G3S System Controller descriptor
 * @regmap: System controller regmap
 * @info: System controller info
 */
struct rzg3s_sysc {
	struct regmap *regmap;
	const struct rzg3s_sysc_info *info;
};

static inline u32 rzg3s_sysc_func_offset(
		const struct rzg3s_sysc_function *func, u32 channel)
{
	return func->offset + (channel ? func->stride : 0);
}
static int rzg3s_sysc_config_func(struct rzg3s_sysc *sysc,
				  enum rzg3s_sysc_func_id fid, u32 val,
				  u32 channel)
{
	const struct rzg3s_sysc_info *info = sysc->info;
	const struct rzg3s_sysc_function *functions = info->functions;
	u32 offset;

	if (fid >= RZG3S_SYSC_FUNC_ID_MAX)
		return -EINVAL;

	if (!functions[fid].mask)
		return 0;

	offset = rzg3s_sysc_func_offset(&functions[fid], channel);

	return regmap_update_bits(sysc->regmap, offset, functions[fid].mask,
				  field_prep(functions[fid].mask, val));
}

static void rzg3s_pcie_update_bits(void __iomem *base, u32 offset, u32 mask,
				   u32 val)
{
	u32 tmp;

	tmp = readl_relaxed(base + offset);
	tmp &= ~mask;
	tmp |= val & mask;
	writel_relaxed(tmp, base + offset);
}
#endif /* _PCIE_RZG3S_H */

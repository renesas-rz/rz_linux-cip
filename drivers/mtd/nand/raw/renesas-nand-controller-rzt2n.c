// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/T2N NAND controller driver
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include "internals.h"

#define MAX_ADDRESS_CYC		6
#define MAX_ERASE_ADDRESS_CYC	3
#define MAX_DATA_SIZE		0xFFFC
#define DMA_DATA_SIZE_ALIGN	8

/* Command and Status Registers */
#define CMDREG0			0x0000	/* Command Register */
#define   CMDREG0_CMD_TYPE	GENMASK(15, 0)
#define     CMD_TYPE_RD		0x2200
#define     CMD_TYPE_PG		0x2100
#define     CMD_TYPE_CPB	0x1200
#define     CMD_TYPE_RST	0x1100
#define     CMD_TYPE_ERS	0x1000
#define     CMD_TYPE_FEAT       0x0100
#define   CMDREG0_INT		BIT(20)
#define   CMDREG0_DMA_SEL	BIT(21)
#define   CMDREG0_TRD_NUM	GENMASK(26, 24)
#define   CMDREG0_CT		GENMASK(31, 30)
#define	    CMDREG0_CT_CDMA	0uL
#define     CMDREG0_CT_PIO	1uL
#define CMDREG1			0x0004
#define CMDREG2			0x0008
#define CMDREG3			0x000C
#define CMDREG4			0x0020
#define CMDREG5			0x0024
#define CMDREG6			0x0028
#define CMDSTATPTR		0x0010	/* Command Status Pointer Register */
#define CMDSTAT			0x0014	/* Command Status Register */
#define   CMDSTAT_CMDERR	BIT(0)
#define   CMDSTAT_FAIL		BIT(14)
#define   CMDSTAT_COMPLETE	BIT(15)
#define CMDSTATEXT		0x0018	/* Extended Command Status Register */
#define INTSTAT			0x0110	/* Interrupt Status Register */
#define   INTSTAT_CDMAIDLE	BIT(16)
#define   INTSTAT_CDMATERR	BIT(17)
#define   INTSTAT_DDMATERR	BIT(18)
#define   INTSTAT_CMDIGNORED	BIT(20)
#define   INTSTAT_PROTERR	BIT(25)
#define INTENABLE		0x0114	/* Interrupt Enable Register */
#define   INTENABLE_INTEN	BIT(31)
#define CTRLSTAT		0x0118	/* Control Status Register */
#define   CTRLSTAT_MDMABUSY	BIT(1)
#define   CTRLSTAT_CMDENGBUSY	BIT(2)
#define   CTRLSTAT_MCBUSY	BIT(3)
#define   CTRLSTAT_CTRLBUSY	BIT(8)
#define   CTRLSTAT_INITCOMP	BIT(9)
#define TRDSTAT			0x0120	/* Command Engine Thread Status Register */
#define TRDERRINTSTAT		0x0128	/* Thread Error Interrupt Status Register */
#define TRDERRINTEN		0x0130	/* Thread Error Interrupt Enable Register */
#define TRDCOMPINTSTAT		0x0138	/* Thread Completion Interrupt Status Register */
#define DMATRGTERRL		0x0140	/* DMA Target Error Low Register */
#define DMATRGTERRH		0x0144	/* DMA Target Error High Register */
#define TRDTMOINTSTAT		0x014C	/* Thread Timeout Interrupt Status Register */
#define TRDTMOINTEN		0x0154	/* Thread Timeout Interrupt Enable Register */

/* Configuration Registers */
#define TRFCFG0			0x0400	/* Transfer Config Register 0 */
#define   TRFCFG0_SECTORCNT	GENMASK(7, 0)
#define   TRFCFG0_OFFSET	GENMASK(31, 16)
#define TRFCFG1			0x0404	/* Transfer Config Register 1 */
#define   TRFCFG1_SECTORSZ	GENMASK(15, 0)
#define   TRFCFG1_LASTSECTORSZ	GENMASK(31, 16)
#define LONGPOLL		0x0408	/* Long Polling Register */
#define SHORTPOLL		0x040C	/* Short Polling Register */
#define RDSTCTRL0		0x0410	/* Device Ready Status Control Register 0 */
#define   RDSTCTRL0_RBEN	BIT(0)
#define   RDSTCTRL0_RDYVAL	GENMASK(23, 16)
#define   RDSTCTRL0_RDYMSK	GENMASK(31, 24)
#define RDSTCTRL1		0x0414	/* Device Ready Status Control Register 1 */
#define LUNSTATCMD		0x0418	/* LUN Status Command Register */
#define LUNILVCMD		0x041C	/* LUN Interleaved Command Register */
#define LUNADDROFFSET		0x0420	/* LUN Address Offset Register */
#define NFDEVLAYOUT		0x0424	/* NAND Flash Device Layout Register */
#define ECCCFG0			0x0428	/* ECC Config Register 0 */
#define   ECCCFG0_ECCEN		BIT(0)
#define   ECCCFG0_ERSDETEN	BIT(1)
#define   ECCCFG0_CORRSTR	GENMASK(10, 8)
#define ECCCFG1			0x042C	/* ECC Config Register 1 */
#define DEVCTRL			0x0430	/* Device Control Register */
#define   DEVCTRL_CHRCWDTH	BIT(5)
#define MPLCFG			0x0434	/* Multiplane Config Register */
#define CACHECFG		0x0438	/* Cache Config Register */
#define DMASET			0x043C	/* DMA Setting Register */
#define TIMEOUT			0x0448	/* Timeout Register */
#define FIFOTRGLVL		0x0454	/* FIFO Trigger Level Register */
#define REMAPCTRL		0x0480	/* Remap Control Register */
#define REMAPMASK		0x0484	/* Remap Mask Register */
#define REMAPACCESS		0x0488	/* Remap Access Register */
#define REMAPLOGADDR		0x048C	/* Remap Logical Address Register */
#define REMAPPHYSADDR		0x0490	/* Remap Physical Address Register */
#define CTRLDATACTRL		0x0494	/* Control-Data Control Register */
#define   CTRLDATACTRL_SIZE	GENMASK(15, 0)

/* Data Integrity (DI) Registers */
#define DICTRL			0x0700	/* Data Integrity Control Register */
#define DIINJECT0		0x0704	/* Data Integrity Error Injection Register 0 */
#define DIINJECT1		0x0708	/* Data Integrity Error Injection Register 1 */
#define DIERRADDR		0x070C	/* Data Integrity Error Address Register */
#define DIINJECT2		0x0710	/* Data Integrity Error Injection Register 2 */

/* Control and Device Parameters */
#define CTRLVERSION		0x0800	/* Controller Version Register */
#define   CTRLVERSION_REV	GENMASK(15, 0)
#define   CTRLVERSION_ID	GENMASK(31, 16)
#define CTRFEAT			0x0804	/* Controller Feature Register */
#define   CTRFEAT_CTRLDATA	BIT(10)
#define   CTRFEAT_DMADWIDTH	BIT(21)
#define   CTRFEAT_BANK		GENMASK(25, 24)
#define MFGID			0x0808	/* Manufacturer ID Register */
#define   MFGID_MID		GENMASK(7, 0)
#define   MFGID_DID		GENMASK(23, 16)
#define NFDEVAREA		0x080C	/* Controller Version Register */
#define DEVPARAM0		0x0810	/* Device Parameter Register 0 */
#define DEVPARAM1		0x0814	/* Device Parameter Register 1 */
#define DEVFEAT			0x0818	/* Device Feature Register */
#define DEVBLKPERLUN		0x081C	/* Device Blocks per LUN Register */
#define DEVREV			0x0820	/* Device Revision Register */
#define ONFITMD0		0x0824	/* ONFI Timing Mode Register 0 */
#define ONFIILOPATTR		0x082C	/* ONFI Interleaved Operation Attribute Register */
#define BCHCFG0			0x0838	/* BCH Config Register 0 */
#define   BCHCFG0_BCHCORR0	GENMASK(7, 0)
#define   BCHCFG0_BCHCORR1	GENMASK(15, 8)
#define   BCHCFG0_BCHCORR2	GENMASK(23, 16)
#define   BCHCFG0_BCHCORR3	GENMASK(31, 24)
#define BCHCFG1			0x083C	/* BCH Config Register 1 */
#define   BCHCFG1_BCHCORR4	GENMASK(7, 0)
#define   BCHCFG1_BCHCORR5	GENMASK(15, 8)
#define   BCHCFG1_BCHCORR6	GENMASK(23, 16)
#define   BCHCFG1_BCHCORR7	GENMASK(31, 24)
#define BCHCFG2			0x0840	/* BCH Config Register 2 */
#define   BCHCFG2_BCHSECT0	GENMASK(15, 0)
#define   BCHCFG2_BCHSECT1	GENMASK(31, 16)
#define BCHCFG3			0x0844	/* BCH Config Register 3 */
#define   BCHCFG3_BCHMETADSZ	GENMASK(23, 16)

/* Protect Mechanism Registers */
#define PROTCTRL0	0x0900	/* Protect Control Register 0 */
#define PROTDOWN0	0x0904	/* Protect Down Register 0 */
#define PROTUP0		0x0908	/* Protect Up Register 0 */
#define PROTCTRL1	0x0910	/* Protect Control Register 1 */
#define PROTDOWN1	0x0914	/* Protect Down Register 1 */
#define PROTUP1		0x0918	/* Protect Up Register 1 */

/* Minicontroller Registers */
#define WPSET				0x1000	/* Write Setting Register */
#define RBNSET				0x1004	/* RBN Setting Register */
#define SKIPBYTECFG			0x100C	/* Skip Bytes Config Register */
#define	  SKIPBYTECFG_SKIPBYTE		GENMASK(7, 0)
#define	  SKIPBYTECFG_MARKER		GENMASK(31, 16)
#define SKIPBYTEOFFSET			0x1010	/* Skip Bytes Offset Register */
#define	  SKIPBYTEOFFSET_VALUE		GENMASK(23, 0)
#define ASYNCTOGGLETIMING		0x101C	/* SDR Timing Configuration Register */
#define   ASYNCTOGGLETIMING_TWP		GENMASK(4, 0)
#define   ASYNCTOGGLETIMING_TWH		GENMASK(12, 8)
#define   ASYNCTOGGLETIMING_TRP		GENMASK(20, 16)
#define   ASYNCTOGGLETIMING_TRH		GENMASK(28, 24)
#define TIMING0				0x1024	/* Timing Register 0 */
#define   TIMING0_TRHW			GENMASK(7, 0)
#define   TIMING0_TWHR			GENMASK(15, 8)
#define   TIMING0_TADL			GENMASK(31, 24)
#define TIMING1				0x1028	/* Timing Register 1 */
#define   TIMING1_TWB			GENMASK(23, 16)
#define   TIMING1_TRHZ			GENMASK(31, 24)
#define TIMING2				0x102C	/* Timing Register 2 */
#define   TIMING2_CSSETUP		GENMASK(5, 0)
#define   TIMING2_CSHOLD		GENMASK(13, 8)
#define   TIMING2_TFEAT			GENMASK(25, 16)
#define DLLPHYCTRL			0x1034	/* DLL PHY Control Register */
#define   DLLPHYCTRL_EXTENDRMD		BIT(16)
#define   DLLPHYCTRL_EXTENDWMD		BIT(17)
#define PHYCTRLREG			0x2080	/* PHY Control Register */
#define   PHYCTRLREG_PHONYDQSTIMING	GENMASK(8, 4)

/* Supplementary Registers */
#define DDCTRL0			0x000	/* NANDC Device Discovery Control Register 0 */
#define DDCTRL1			0x004	/* NANDC Device Discovery Control Register 1 */
#define DDCTRL2			0x008	/* NANDC Device Discovery Control Register 2 */
#define DDREQ			0x00C	/* NANDC Device Discovery Request Register */
#define DDACK			0x010	/* Device Discovery Acknowledge Register */
#define DDIDLOW			0x014	/* NANDC Device Discovery Read ID Register 0 */
#define DDIDHIGH		0x018	/* NANDC Device Discovery Read ID Register 1 */
#define DDPAGECTRL		0x01C	/* NANDC Device Discovery Page Control Register */
#define PROTCTRL		0x020	/* NANDC Register Protect Control Register */

#define BCH_MAX_NUM_CORR_CAPS		8
#define BCH_MAX_NUM_SECTOR_SIZES	2

/* Flash pointer memory shift. */
#define CDMA_CFPTR_MEM_SHIFT	24
/* Flash pointer memory mask. */
#define CDMA_CFPTR_MEM		GENMASK(26, 24)

/* Command DMA descriptor flags */
#define CDMA_CF_INT		BIT(8)
#define CDMA_CF_CONT		BIT(9)
/* DMA master flag of command DMA descriptor. */
#define CDMA_CF_DMA_MASTER	BIT(10)

/* Operation complete status of command descriptor. */
#define CDMA_CS_COMP		BIT(15)
/* Operation complete status of command descriptor. */
/* Command descriptor status - operation fail. */
#define CDMA_CS_FAIL		BIT(14)
/* Command descriptor status - page erased. */
#define CDMA_CS_ERP		BIT(11)
/* Command descriptor status - timeout occurred. */
#define CDMA_CS_TOUT		BIT(10)
/*
 * Maximum amount of correction applied to one ECC sector.
 * It is part of command descriptor status.
 */
#define CDMA_CS_MAXERR		GENMASK(9, 2)
/* Command descriptor status - uncorrectable ECC error. */
#define CDMA_CS_UNCE		BIT(1)
/* Command descriptor status - descriptor error. */
#define CDMA_CS_ERR		BIT(0)

/* Status of operation - OK. */
#define STAT_OK			0
/* Status of operation - FAIL. */
#define STAT_FAIL		2
/* Status of operation - uncorrectable ECC error. */
#define STAT_ECC_UNCORR		3
/* Status of operation - page erased. */
#define STAT_ERASED		5
/* Status of operation - correctable ECC error. */
#define STAT_ECC_CORR		6
/* Status of operation - unsuspected state. */
#define STAT_UNKNOWN		7
/* Status of operation - operation is not completed yet. */
#define STAT_BUSY		0xFF

struct rzt2n_nand_timings {
	u32 async_toggle_timings;
	u32 timings0;
	u32 timings1;
	u32 timings2;
	u32 dll_phy_ctrl;
	u32 phy_ctrl;
};

/* Command DMA descriptor. */
struct rzt2n_nand_cdma_desc {
	u64 next_pointer;
	u32 flash_pointer;
	u16 bank;
	u16 rsvd0;
	u16 command_type;
	u16 rsvd1;
	u16 command_flags;
	u16 rsvd2;
	u64 memory_pointer;
	u32 status;
	u32 rsvd3;
	u64 sync_flag_pointer;
	u32 sync_arguments;
	u32 rsvd4;
	u64 ctrl_data_ptr;
};

/* Interrupt status. */
struct rzt2n_nand_irq_status {
	/* Thread operation complete status. */
	u32 trd_status;
	/* Thread operation error. */
	u32 trd_error;
	/* Controller status. */
	u32 status;
};

/* NAND flash controller capabilities get from driver data. */
struct rzt2n_nand_dt_devdata {
	/* Skew value of the output signals of the NAND Flash interface. */
	u32 if_skew;
	/* It informs if slave DMA interface is connected to DMA engine. */
	unsigned int has_dma:1;
};

/* NAND flash controller capabilities read from registers. */
struct cdns_nand_caps {
	/* Maximum number of banks supported by hardware. */
	u8 max_banks;
	/* Slave and Master DMA data width in bytes (4 or 8). */
	u8 data_dma_width;
	/* Control Data feature supported. */
	bool data_control_supp;
};

struct cdns_nand_ctrl {
	struct device *dev;
	struct nand_controller controller;
	struct rzt2n_nand_cdma_desc *cdma_desc;
	const struct rzt2n_nand_dt_devdata *caps1;
	struct cdns_nand_caps caps2;
	u16 ctrl_rev;
	u16 ctrl_id;
	dma_addr_t dma_cdma_desc;
	u8 *buf;
	u32 buf_size;
	u8 curr_corr_str_idx;

	void __iomem *reg;

	struct {
		void __iomem *virt;
		dma_addr_t dma;
	} io;

	int irq;
	/* Interrupts that have happened. */
	struct rzt2n_nand_irq_status irq_status;
	/* Interrupts we are waiting for. */
	struct rzt2n_nand_irq_status irq_mask;
	struct completion complete;
	/* Protect irq_mask and irq_status. */
	spinlock_t irq_lock;

	int ecc_strengths[BCH_MAX_NUM_CORR_CAPS];
	struct nand_ecc_step_info ecc_stepinfos[BCH_MAX_NUM_SECTOR_SIZES];
	struct nand_ecc_caps ecc_caps;

	int curr_trans_type;

	struct dma_chan *dmac;

	u32 nf_clk_rate;
	u32 board_delay;

	struct nand_chip *selected_chip;

	unsigned long assigned_cs;
	struct list_head chips;
	u8 bch_metadata_size;

	struct reset_control *rst_sys;
	struct reset_control *rst_slave;
	struct reset_control *rst_prot;
};

struct cdns_nand_chip {
	struct rzt2n_nand_timings timings;
	struct nand_chip chip;
	u8 nsels;
	struct list_head node;

	/*
	 * part of oob area of NAND flash memory page.
	 * This part is available for user to read or write.
	 */
	u32 avail_oob_size;

	/* Sector size. There are few sectors per mtd->writesize */
	u32 sector_size;
	u32 sector_count;

	/* Offset of BBM. */
	u8 bbm_offs;
	/* Number of bytes reserved for BBM. */
	u8 bbm_len;
	/* ECC strength index. */
	u8 corr_str_idx;

	/* Timing mode requested from DT */
	u8 req_mode;

	u8 cs[] __counted_by(nsels);
};

/* Offset (ps) described in RZ/T2N HW manual */
struct rzt2n_timing_ctx {
	bool slow_clk;
	u32 clk_ps;
	u32 if_skew_ps;
	u32 board_delay_ps;
	u32 board_delay_skew_max_ps;

	u32 off_pulse_ps;
	u32 off_cycle_ps;
	u32 off_wc_ps;
	u32 off_rea_non_edo_ps;
	u32 off_rea_edo_ps;
	u32 off_als_ps;
	u32 off_cls_ps;
	u32 off_ds_ps;
	u32 off_alh_ps;
	u32 off_ch_ps;
	u32 off_clh_ps;
	u32 off_dh_ps;
};

static inline struct
cdns_nand_chip *to_cdns_nand_chip(struct nand_chip *chip)
{
	return container_of(chip, struct cdns_nand_chip, chip);
}

static inline struct
cdns_nand_ctrl *to_cdns_nand_ctrl(struct nand_controller *controller)
{
	return container_of(controller, struct cdns_nand_ctrl, controller);
}

static bool
rzt2n_nand_dma_buf_ok(struct cdns_nand_ctrl *cdns_ctrl, const void *buf,
		      u32 buf_len)
{
	u8 data_dma_width = cdns_ctrl->caps2.data_dma_width;

	return buf && virt_addr_valid(buf) &&
		likely(IS_ALIGNED((uintptr_t)buf, data_dma_width)) &&
		likely(IS_ALIGNED(buf_len, DMA_DATA_SIZE_ALIGN));
}

static int rzt2n_nand_wait_for_value(struct cdns_nand_ctrl *cdns_ctrl,
				     u32 reg_offset, u32 timeout_us,
				     u32 mask, bool is_clear)
{
	u32 val;
	int ret;

	ret = readl_relaxed_poll_timeout(cdns_ctrl->reg + reg_offset,
					 val, !(val & mask) == is_clear,
					 10, timeout_us);

	if (ret < 0) {
		dev_err(cdns_ctrl->dev,
			"Timeout while waiting for reg %x with mask %x is clear %d\n",
			reg_offset, mask, is_clear);
	}

	return ret;
}

static int rzt2n_nand_set_ecc_enable(struct cdns_nand_ctrl *cdns_ctrl,
				     bool enable)
{
	u32 reg;

	if (rzt2n_nand_wait_for_value(cdns_ctrl, CTRLSTAT,
				      1000000,
				      CTRLSTAT_CTRLBUSY, true))
		return -ETIMEDOUT;

	reg = readl_relaxed(cdns_ctrl->reg + ECCCFG0);

	if (enable)
		reg |= ECCCFG0_ECCEN;
	else
		reg &= ~ECCCFG0_ECCEN;

	writel_relaxed(reg, cdns_ctrl->reg + ECCCFG0);

	return 0;
}

static void rzt2n_nand_set_ecc_strength(struct cdns_nand_ctrl *cdns_ctrl,
					u8 corr_str_idx)
{
	u32 reg;

	if (cdns_ctrl->curr_corr_str_idx == corr_str_idx)
		return;

	reg = readl_relaxed(cdns_ctrl->reg + ECCCFG0);
	reg &= ~ECCCFG0_CORRSTR;
	reg |= FIELD_PREP(ECCCFG0_CORRSTR, corr_str_idx);
	writel_relaxed(reg, cdns_ctrl->reg + ECCCFG0);

	cdns_ctrl->curr_corr_str_idx = corr_str_idx;
}

static int rzt2n_nand_get_ecc_strength_idx(struct cdns_nand_ctrl *cdns_ctrl,
					   u8 strength)
{
	int i, corr_str_idx = -1;

	for (i = 0; i < BCH_MAX_NUM_CORR_CAPS; i++) {
		if (cdns_ctrl->ecc_strengths[i] == strength) {
			corr_str_idx = i;
			break;
		}
	}

	return corr_str_idx;
}

static int rzt2n_nand_set_skip_marker_val(struct cdns_nand_ctrl *cdns_ctrl,
					  u16 marker_value)
{
	u32 reg;

	if (rzt2n_nand_wait_for_value(cdns_ctrl, CTRLSTAT,
				      1000000,
				      CTRLSTAT_CTRLBUSY, true))
		return -ETIMEDOUT;

	reg = readl_relaxed(cdns_ctrl->reg + SKIPBYTECFG);
	reg &= ~SKIPBYTECFG_MARKER;
	reg |= FIELD_PREP(SKIPBYTECFG_MARKER, marker_value);

	writel_relaxed(reg, cdns_ctrl->reg + SKIPBYTECFG);

	return 0;
}

static int rzt2n_nand_set_skip_bytes_conf(struct cdns_nand_ctrl *cdns_ctrl,
					  u8 num_of_bytes,
					  u32 offset_value,
					  int enable)
{
	u32 reg, skip_bytes_offset;

	if (rzt2n_nand_wait_for_value(cdns_ctrl, CTRLSTAT,
				      1000000,
				      CTRLSTAT_CTRLBUSY, true))
		return -ETIMEDOUT;

	if (!enable) {
		num_of_bytes = 0;
		offset_value = 0;
	}

	reg = readl_relaxed(cdns_ctrl->reg + SKIPBYTECFG);
	reg &= ~SKIPBYTECFG_SKIPBYTE;
	reg |= FIELD_PREP(SKIPBYTECFG_SKIPBYTE, num_of_bytes);
	skip_bytes_offset = FIELD_PREP(SKIPBYTEOFFSET_VALUE,
				       offset_value);

	writel_relaxed(reg, cdns_ctrl->reg + SKIPBYTECFG);
	writel_relaxed(skip_bytes_offset, cdns_ctrl->reg + SKIPBYTEOFFSET);

	return 0;
}

static void rzt2n_nand_set_erase_detection(struct cdns_nand_ctrl *cdns_ctrl,
					   bool enable,
					   u8 bitflips_threshold)
{
	u32 reg;

	reg = readl_relaxed(cdns_ctrl->reg + ECCCFG0);

	if (enable)
		reg |= ECCCFG0_ERSDETEN;
	else
		reg &= ~ECCCFG0_ERSDETEN;

	writel_relaxed(reg, cdns_ctrl->reg + ECCCFG0);
	writel_relaxed(bitflips_threshold, cdns_ctrl->reg + ECCCFG1);
}

static void rzt2n_nand_clear_interrupt(struct cdns_nand_ctrl *cdns_ctrl,
				       struct rzt2n_nand_irq_status *irq_status)
{
	writel_relaxed(irq_status->status, cdns_ctrl->reg + INTSTAT);
	writel_relaxed(irq_status->trd_status, cdns_ctrl->reg + TRDCOMPINTSTAT);
	writel_relaxed(irq_status->trd_error, cdns_ctrl->reg + TRDERRINTSTAT);
}

static void rzt2n_nand_read_int_status(struct cdns_nand_ctrl *cdns_ctrl,
				       struct rzt2n_nand_irq_status *irq_status)
{
	irq_status->status = readl_relaxed(cdns_ctrl->reg + INTSTAT);
	irq_status->trd_status = readl_relaxed(cdns_ctrl->reg + TRDCOMPINTSTAT);
	irq_status->trd_error = readl_relaxed(cdns_ctrl->reg + TRDERRINTSTAT);
}

static u32 irq_detected(struct cdns_nand_ctrl *cdns_ctrl,
			struct rzt2n_nand_irq_status *irq_status)
{
	rzt2n_nand_read_int_status(cdns_ctrl, irq_status);

	return irq_status->status || irq_status->trd_status ||
		irq_status->trd_error;
}

static void rzt2n_nand_reset_irq(struct cdns_nand_ctrl *cdns_ctrl)
{
	unsigned long flags;

	spin_lock_irqsave(&cdns_ctrl->irq_lock, flags);
	memset(&cdns_ctrl->irq_status, 0, sizeof(cdns_ctrl->irq_status));
	memset(&cdns_ctrl->irq_mask, 0, sizeof(cdns_ctrl->irq_mask));
	spin_unlock_irqrestore(&cdns_ctrl->irq_lock, flags);
}

static irqreturn_t rzt2n_nand_isr(int irq, void *dev_id)
{
	struct cdns_nand_ctrl *cdns_ctrl = dev_id;
	struct rzt2n_nand_irq_status irq_status;
	irqreturn_t result = IRQ_NONE;

	spin_lock(&cdns_ctrl->irq_lock);

	if (irq_detected(cdns_ctrl, &irq_status)) {
		/* Handle interrupt. */
		/* First acknowledge it. */
		rzt2n_nand_clear_interrupt(cdns_ctrl, &irq_status);
		/* Status in the device context for someone to read. */
		cdns_ctrl->irq_status.status |= irq_status.status;
		cdns_ctrl->irq_status.trd_status |= irq_status.trd_status;
		cdns_ctrl->irq_status.trd_error |= irq_status.trd_error;
		/* Notify anyone who cares that it happened. */
		complete(&cdns_ctrl->complete);
		/* Tell the OS that we've handled this. */
		result = IRQ_HANDLED;
	}
	spin_unlock(&cdns_ctrl->irq_lock);

	return result;
}

static void rzt2n_nand_set_irq_mask(struct cdns_nand_ctrl *cdns_ctrl,
				    struct rzt2n_nand_irq_status *irq_mask)
{
	writel_relaxed(INTENABLE_INTEN | irq_mask->status,
		       cdns_ctrl->reg + INTENABLE);

	writel_relaxed(irq_mask->trd_error,
		       cdns_ctrl->reg + TRDERRINTEN);
}

static void
rzt2n_nand_wait_for_irq(struct cdns_nand_ctrl *cdns_ctrl,
			struct rzt2n_nand_irq_status *irq_mask,
			struct rzt2n_nand_irq_status *irq_status)
{
	unsigned long timeout = msecs_to_jiffies(10000);
	unsigned long time_left;

	time_left = wait_for_completion_timeout(&cdns_ctrl->complete,
						timeout);

	*irq_status = cdns_ctrl->irq_status;
	if (time_left == 0) {
		/* Timeout error. */
		dev_err(cdns_ctrl->dev, "timeout occurred:\n");
		dev_err(cdns_ctrl->dev, "\tstatus = 0x%x, mask = 0x%x\n",
			irq_status->status, irq_mask->status);
		dev_err(cdns_ctrl->dev,
			"\ttrd_status = 0x%x, trd_status mask = 0x%x\n",
			irq_status->trd_status, irq_mask->trd_status);
		dev_err(cdns_ctrl->dev,
			"\t trd_error = 0x%x, trd_error mask = 0x%x\n",
			irq_status->trd_error, irq_mask->trd_error);
	}
}

static void rzt2n_nand_get_caps(struct cdns_nand_ctrl *cdns_ctrl)
{
	u32  reg;

	reg = readl_relaxed(cdns_ctrl->reg + CTRFEAT);

	cdns_ctrl->caps2.max_banks = 1 << FIELD_GET(CTRFEAT_BANK, reg);

	if (FIELD_GET(CTRFEAT_DMADWIDTH, reg))
		cdns_ctrl->caps2.data_dma_width = 8;
	else
		cdns_ctrl->caps2.data_dma_width = 4;

	if (reg & CTRFEAT_CTRLDATA)
		cdns_ctrl->caps2.data_control_supp = true;
}

static void
rzt2n_nand_cdma_desc_prepare(struct cdns_nand_ctrl *cdns_ctrl,
			     char nf_mem, u32 flash_ptr, dma_addr_t mem_ptr,
			     dma_addr_t ctrl_data_ptr, u16 ctype)
{
	struct rzt2n_nand_cdma_desc *cdma_desc = cdns_ctrl->cdma_desc;

	memset(cdma_desc, 0, sizeof(struct rzt2n_nand_cdma_desc));

	/* Set fields for one descriptor. */
	cdma_desc->flash_pointer = flash_ptr;
	if (cdns_ctrl->ctrl_rev >= 13)
		cdma_desc->bank = nf_mem;
	else
		cdma_desc->flash_pointer |= (nf_mem << CDMA_CFPTR_MEM_SHIFT);

	cdma_desc->command_flags |= CDMA_CF_DMA_MASTER;
	cdma_desc->command_flags |= CDMA_CF_INT;

	cdma_desc->memory_pointer = mem_ptr;
	cdma_desc->status = 0;
	cdma_desc->sync_flag_pointer = 0;
	cdma_desc->sync_arguments = 0;

	cdma_desc->command_type = ctype;
	cdma_desc->ctrl_data_ptr = ctrl_data_ptr;
}

static u8 rzt2n_nand_check_desc_error(struct cdns_nand_ctrl *cdns_ctrl,
					u32 desc_status)
{
	if (desc_status & CDMA_CS_ERP)
		return STAT_ERASED;

	if (desc_status & CDMA_CS_UNCE)
		return STAT_ECC_UNCORR;

	if (desc_status & CDMA_CS_ERR) {
		dev_err(cdns_ctrl->dev, ":CDMA desc error flag detected.\n");
		return STAT_FAIL;
	}

	if (FIELD_GET(CDMA_CS_MAXERR, desc_status))
		return STAT_ECC_CORR;

	return STAT_FAIL;
}

static int rzt2n_nand_cdma_finish(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct rzt2n_nand_cdma_desc *desc_ptr = cdns_ctrl->cdma_desc;
	u8 status = STAT_BUSY;

	if (desc_ptr->status & CDMA_CS_FAIL) {
		status = rzt2n_nand_check_desc_error(cdns_ctrl,
						     desc_ptr->status);
		dev_err(cdns_ctrl->dev, "CDMA error %x\n", desc_ptr->status);
	} else if (desc_ptr->status & CDMA_CS_COMP) {
		/* Descriptor finished with no errors. */
		if (desc_ptr->command_flags & CDMA_CF_CONT) {
			dev_info(cdns_ctrl->dev, "DMA unsupported flag is set");
			status = STAT_UNKNOWN;
		} else {
			/* Last descriptor.  */
			status = STAT_OK;
		}
	}

	return status;
}

static int rzt2n_nand_cdma_send(struct cdns_nand_ctrl *cdns_ctrl,
				u8 thread)
{
	u32 reg;
	int status;

	/* Wait for thread ready. */
	status = rzt2n_nand_wait_for_value(cdns_ctrl, TRDSTAT,
					   1000000,
					   BIT(thread), true);
	if (status)
		return status;

	rzt2n_nand_reset_irq(cdns_ctrl);
	reinit_completion(&cdns_ctrl->complete);

	writel_relaxed((u32)cdns_ctrl->dma_cdma_desc,
		       cdns_ctrl->reg + CMDREG2);
	writel_relaxed(0, cdns_ctrl->reg + CMDREG3);

	/* Select CDMA mode. */
	reg = FIELD_PREP(CMDREG0_CT, CMDREG0_CT_CDMA);
	/* Thread number. */
	reg |= FIELD_PREP(CMDREG0_TRD_NUM, thread);
	/* Issue command. */
	writel_relaxed(reg, cdns_ctrl->reg + CMDREG0);

	return 0;
}

/* Send SDMA command and wait for finish. */
static u32
rzt2n_nand_cdma_send_and_wait(struct cdns_nand_ctrl *cdns_ctrl,
			      u8 thread)
{
	struct rzt2n_nand_irq_status irq_mask, irq_status = {0};
	int status;

	irq_mask.trd_status = BIT(thread);
	irq_mask.trd_error = BIT(thread);
	irq_mask.status = INTSTAT_CDMATERR;

	rzt2n_nand_set_irq_mask(cdns_ctrl, &irq_mask);

	status = rzt2n_nand_cdma_send(cdns_ctrl, thread);
	if (status)
		return status;

	rzt2n_nand_wait_for_irq(cdns_ctrl, &irq_mask, &irq_status);

	if (irq_status.status == 0 && irq_status.trd_status == 0 &&
	    irq_status.trd_error == 0) {
		dev_err(cdns_ctrl->dev, "CDMA command timeout\n");
		return -ETIMEDOUT;
	}
	if (irq_status.status & irq_mask.status) {
		dev_err(cdns_ctrl->dev, "CDMA command failed\n");
		return -EIO;
	}

	return 0;
}

/*
 * ECC size depends on configured ECC strength and on maximum supported
 * ECC step size.
 */
static int rzt2n_nand_calc_ecc_bytes(int max_step_size, int strength)
{
	int nbytes = DIV_ROUND_UP(fls(8 * max_step_size) * strength, 8);

	return ALIGN(nbytes, 2);
}

#define RZT2N_NAND_CALC_ECC_BYTES(max_step_size) \
	static int \
	rzt2n_nand_calc_ecc_bytes_##max_step_size(int step_size, \
						    int strength)\
	{\
		return rzt2n_nand_calc_ecc_bytes(max_step_size, strength);\
	}

RZT2N_NAND_CALC_ECC_BYTES(256)
RZT2N_NAND_CALC_ECC_BYTES(512)
RZT2N_NAND_CALC_ECC_BYTES(1024)
RZT2N_NAND_CALC_ECC_BYTES(2048)
RZT2N_NAND_CALC_ECC_BYTES(4096)

/* Function reads BCH capabilities. */
static int rzt2n_nand_read_bch_caps(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct nand_ecc_caps *ecc_caps = &cdns_ctrl->ecc_caps;
	int max_step_size = 0, nstrengths, i;
	u32 reg;

	reg = readl_relaxed(cdns_ctrl->reg + BCHCFG3);
	cdns_ctrl->bch_metadata_size = FIELD_GET(BCHCFG3_BCHMETADSZ, reg);
	if (cdns_ctrl->bch_metadata_size < 4) {
		dev_err(cdns_ctrl->dev,
			"Driver needs at least 4 bytes of BCH meta data\n");
		return -EIO;
	}

	reg = readl_relaxed(cdns_ctrl->reg + BCHCFG0);
	cdns_ctrl->ecc_strengths[0] = FIELD_GET(BCHCFG0_BCHCORR0, reg);
	cdns_ctrl->ecc_strengths[1] = FIELD_GET(BCHCFG0_BCHCORR1, reg);
	cdns_ctrl->ecc_strengths[2] = FIELD_GET(BCHCFG0_BCHCORR2, reg);
	cdns_ctrl->ecc_strengths[3] = FIELD_GET(BCHCFG0_BCHCORR3, reg);

	reg = readl_relaxed(cdns_ctrl->reg + BCHCFG1);
	cdns_ctrl->ecc_strengths[4] = FIELD_GET(BCHCFG1_BCHCORR4, reg);
	cdns_ctrl->ecc_strengths[5] = FIELD_GET(BCHCFG1_BCHCORR5, reg);
	cdns_ctrl->ecc_strengths[6] = FIELD_GET(BCHCFG1_BCHCORR6, reg);
	cdns_ctrl->ecc_strengths[7] = FIELD_GET(BCHCFG1_BCHCORR7, reg);

	reg = readl_relaxed(cdns_ctrl->reg + BCHCFG2);
	cdns_ctrl->ecc_stepinfos[0].stepsize =
		FIELD_GET(BCHCFG2_BCHSECT0, reg);

	cdns_ctrl->ecc_stepinfos[1].stepsize =
		FIELD_GET(BCHCFG2_BCHSECT1, reg);

	nstrengths = 0;
	for (i = 0; i < BCH_MAX_NUM_CORR_CAPS; i++) {
		if (cdns_ctrl->ecc_strengths[i] != 0)
			nstrengths++;
	}

	ecc_caps->nstepinfos = 0;
	for (i = 0; i < BCH_MAX_NUM_SECTOR_SIZES; i++) {
		/* ECC strengths are common for all step infos. */
		cdns_ctrl->ecc_stepinfos[i].nstrengths = nstrengths;
		cdns_ctrl->ecc_stepinfos[i].strengths =
			cdns_ctrl->ecc_strengths;

		if (cdns_ctrl->ecc_stepinfos[i].stepsize != 0)
			ecc_caps->nstepinfos++;

		if (cdns_ctrl->ecc_stepinfos[i].stepsize > max_step_size)
			max_step_size = cdns_ctrl->ecc_stepinfos[i].stepsize;
	}
	ecc_caps->stepinfos = &cdns_ctrl->ecc_stepinfos[0];

	switch (max_step_size) {
	case 256:
		ecc_caps->calc_ecc_bytes = &rzt2n_nand_calc_ecc_bytes_256;
		break;
	case 512:
		ecc_caps->calc_ecc_bytes = &rzt2n_nand_calc_ecc_bytes_512;
		break;
	case 1024:
		ecc_caps->calc_ecc_bytes = &rzt2n_nand_calc_ecc_bytes_1024;
		break;
	case 2048:
		ecc_caps->calc_ecc_bytes = &rzt2n_nand_calc_ecc_bytes_2048;
		break;
	case 4096:
		ecc_caps->calc_ecc_bytes = &rzt2n_nand_calc_ecc_bytes_4096;
		break;
	default:
		dev_err(cdns_ctrl->dev,
			"Unsupported sector size(ecc step size) %d\n",
			max_step_size);
		return -EIO;
	}

	return 0;
}

static int rzt2n_nand_hw_init(struct cdns_nand_ctrl *cdns_ctrl)
{
	int status;
	u32 reg;

	status = rzt2n_nand_wait_for_value(cdns_ctrl, CTRLSTAT,
					   1000000,
					   CTRLSTAT_INITCOMP, false);
	if (status)
		return status;

	reg = readl_relaxed(cdns_ctrl->reg + CTRLVERSION);
	cdns_ctrl->ctrl_rev = FIELD_GET(CTRLVERSION_REV, reg);
	cdns_ctrl->ctrl_id = FIELD_GET(CTRLVERSION_ID, reg);

	dev_info(cdns_ctrl->dev,
		 "%s: Renesas RZ/T2N NAND Controller version reg %x\n",
		 __func__, reg);

	/* Disable cache and multiplane. */
	writel_relaxed(0, cdns_ctrl->reg + MPLCFG);
	writel_relaxed(0, cdns_ctrl->reg + CACHECFG);

	/* Clear all interrupts. */
	writel_relaxed(0xFFFFFFFF, cdns_ctrl->reg + INTSTAT);

	rzt2n_nand_get_caps(cdns_ctrl);
	if (rzt2n_nand_read_bch_caps(cdns_ctrl))
		return -EIO;

	return 0;
}

#define TT_MAIN_OOB_AREAS	2
#define TT_RAW_PAGE		3
#define TT_BBM			4
#define TT_MAIN_OOB_AREA_EXT	5

/* Prepare size of data to transfer. */
static void rzt2n_nand_prepare_data_size(struct nand_chip *chip,
					 int transfer_type)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 sec_size = 0, offset = 0, sec_cnt = 1;
	u32 last_sec_size = cdns_chip->sector_size;
	u32 data_ctrl_size = 0;
	u32 reg = 0;

	if (cdns_ctrl->curr_trans_type == transfer_type)
		return;

	switch (transfer_type) {
	case TT_MAIN_OOB_AREA_EXT:
		sec_cnt = cdns_chip->sector_count;
		sec_size = cdns_chip->sector_size;
		data_ctrl_size = cdns_chip->avail_oob_size;
		break;
	case TT_MAIN_OOB_AREAS:
		sec_cnt = cdns_chip->sector_count;
		last_sec_size = cdns_chip->sector_size
			+ cdns_chip->avail_oob_size;
		sec_size = cdns_chip->sector_size;
		break;
	case TT_RAW_PAGE:
		last_sec_size = mtd->writesize + mtd->oobsize;
		break;
	case TT_BBM:
		offset = mtd->writesize + cdns_chip->bbm_offs;
		last_sec_size = 8;
		break;
	}

	reg = 0;
	reg |= FIELD_PREP(TRFCFG0_OFFSET, offset);
	reg |= FIELD_PREP(TRFCFG0_SECTORCNT, sec_cnt);
	writel_relaxed(reg, cdns_ctrl->reg + TRFCFG0);

	reg = 0;
	reg |= FIELD_PREP(TRFCFG1_LASTSECTORSZ, last_sec_size);
	reg |= FIELD_PREP(TRFCFG1_SECTORSZ, sec_size);
	writel_relaxed(reg, cdns_ctrl->reg + TRFCFG1);

	if (cdns_ctrl->caps2.data_control_supp) {
		reg = readl_relaxed(cdns_ctrl->reg + CTRLDATACTRL);
		reg &= ~CTRLDATACTRL_SIZE;
		reg |= FIELD_PREP(CTRLDATACTRL_SIZE, data_ctrl_size);
		writel_relaxed(reg, cdns_ctrl->reg + CTRLDATACTRL);
	}

	cdns_ctrl->curr_trans_type = transfer_type;
}

static int
rzt2n_nand_cdma_transfer(struct cdns_nand_ctrl *cdns_ctrl, u8 chip_nr,
			 int page, void *buf, void *ctrl_dat, u32 buf_size,
			 u32 ctrl_dat_size, enum dma_data_direction dir,
			 bool with_ecc)
{
	dma_addr_t dma_buf, dma_ctrl_dat = 0;
	u8 thread_nr = chip_nr;
	int status;
	u16 ctype;

	if (dir == DMA_FROM_DEVICE)
		ctype = CMD_TYPE_RD;
	else
		ctype = CMD_TYPE_PG;

	rzt2n_nand_set_ecc_enable(cdns_ctrl, with_ecc);

	dma_buf = dma_map_single(cdns_ctrl->dev, buf, buf_size, dir);
	if (dma_mapping_error(cdns_ctrl->dev, dma_buf)) {
		dev_err(cdns_ctrl->dev, "Failed to map DMA buffer\n");
		return -EIO;
	}

	if (ctrl_dat && ctrl_dat_size) {
		dma_ctrl_dat = dma_map_single(cdns_ctrl->dev, ctrl_dat,
					      ctrl_dat_size, dir);
		if (dma_mapping_error(cdns_ctrl->dev, dma_ctrl_dat)) {
			dma_unmap_single(cdns_ctrl->dev, dma_buf,
					 buf_size, dir);
			dev_err(cdns_ctrl->dev, "Failed to map DMA buffer\n");
			return -EIO;
		}
	}

	rzt2n_nand_cdma_desc_prepare(cdns_ctrl, chip_nr, page,
				     dma_buf, dma_ctrl_dat, ctype);

	status = rzt2n_nand_cdma_send_and_wait(cdns_ctrl, thread_nr);

	dma_unmap_single(cdns_ctrl->dev, dma_buf,
			 buf_size, dir);

	if (ctrl_dat && ctrl_dat_size)
		dma_unmap_single(cdns_ctrl->dev, dma_ctrl_dat,
				 ctrl_dat_size, dir);
	if (status)
		return status;

	return rzt2n_nand_cdma_finish(cdns_ctrl);
}

static void rzt2n_nand_set_timings(struct cdns_nand_ctrl *cdns_ctrl,
				   struct rzt2n_nand_timings *t)
{
	writel_relaxed(t->async_toggle_timings,
		       cdns_ctrl->reg + ASYNCTOGGLETIMING);
	writel_relaxed(t->timings0, cdns_ctrl->reg + TIMING0);
	writel_relaxed(t->timings1, cdns_ctrl->reg + TIMING1);
	writel_relaxed(t->timings2, cdns_ctrl->reg + TIMING2);
	writel_relaxed(t->phy_ctrl, cdns_ctrl->reg + PHYCTRLREG);
}

static int rzt2n_nand_select_target(struct nand_chip *chip)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);

	if (chip == cdns_ctrl->selected_chip)
		return 0;

	if (rzt2n_nand_wait_for_value(cdns_ctrl, CTRLSTAT,
				      1000000,
				      CTRLSTAT_CTRLBUSY, true))
		return -ETIMEDOUT;

	writel_relaxed(RDSTCTRL0_RBEN, cdns_ctrl->reg + RDSTCTRL0);

	rzt2n_nand_set_timings(cdns_ctrl, &cdns_chip->timings);

	rzt2n_nand_set_ecc_strength(cdns_ctrl,
				    cdns_chip->corr_str_idx);

	rzt2n_nand_set_erase_detection(cdns_ctrl, true,
					 chip->ecc.strength);

	cdns_ctrl->curr_trans_type = -1;
	cdns_ctrl->selected_chip = chip;

	return 0;
}

static int rzt2n_nand_pio_send_and_wait(struct cdns_nand_ctrl *cdns_ctrl,
					u32 row_addr, u16 ctype, u8 cs)
{
	u32 reg;
	int status;

	/* Wait for thread ready */
	status = rzt2n_nand_wait_for_value(cdns_ctrl, TRDSTAT,
					   1000000,
					   BIT(cs), true);
	if (status)
		return status;

	/* Set ROW_ADDRESS */
	writel_relaxed(row_addr, cdns_ctrl->reg + CMDREG1);

	/* Set MEM_ADDR_PTR */
	writel_relaxed((u32)cdns_ctrl->io.dma, cdns_ctrl->reg + CMDREG2);

	/* Set BANK */
	writel_relaxed(cs, cdns_ctrl->reg + CMDREG4);

	/* Set CMDREG0 */
	reg = FIELD_PREP(CMDREG0_CT, CMDREG0_CT_PIO);
	reg |= FIELD_PREP(CMDREG0_TRD_NUM, cs);
	reg |= FIELD_PREP(CMDREG0_INT, 1);
	reg |= FIELD_PREP(CMDREG0_DMA_SEL, 1);
	reg |= FIELD_PREP(CMDREG0_CMD_TYPE, ctype | 0x0);

	/* Issue command */
	writel_relaxed(reg, cdns_ctrl->reg + CMDREG0);

	/* Wait Thread Completion */
	status = rzt2n_nand_wait_for_value(cdns_ctrl, TRDCOMPINTSTAT,
					   1000000,
					   BIT(cs), false);
	if (status)
		return status;

	writel_relaxed(BIT(cs), cdns_ctrl->reg + TRDCOMPINTSTAT);

	return 0;
}

static int rzt2n_nand_cmd_set_feature(struct nand_chip *chip,
				      const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	const struct nand_op_instr *instr, *data_instr = NULL;
	u32 reg, feat_val;
	u8 cs = chip->cur_cs, feat_addr;
	int i, status, op_id;
	size_t len = 0;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		instr = &subop->instrs[op_id];
		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			break;

		case NAND_OP_ADDR_INSTR:
			feat_addr = instr->ctx.addr.addrs[0];
			break;

		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_DATA_OUT_INSTR:
			const u8 *p = NULL;

			data_instr = instr;
			len = instr->ctx.data.len;
			p = (const u8 *)instr->ctx.data.buf.out;
			feat_val |= (u32)p[i] << (8 * i);
			break;

		case NAND_OP_WAITRDY_INSTR:
			break;
		}
	}

	/* Wait for thread ready */
	status = rzt2n_nand_wait_for_value(cdns_ctrl, TRDSTAT,
					   1000000,
					   BIT(cs), true);
	if (status)
		return status;

	/* Set Feature operation */
	writel_relaxed(feat_addr, cdns_ctrl->reg + CMDREG1);
	writel_relaxed(feat_val, cdns_ctrl->reg + CMDREG2);

	/* Set CMDREG0 */
	reg = FIELD_PREP(CMDREG0_CT, CMDREG0_CT_PIO);
	reg |= FIELD_PREP(CMDREG0_TRD_NUM, cs);
	reg |= FIELD_PREP(CMDREG0_INT, 1);
	reg |= FIELD_PREP(CMDREG0_CMD_TYPE, CMD_TYPE_FEAT);

	/* Issue command */
	writel_relaxed(reg, cdns_ctrl->reg + CMDREG0);

	/* Wait Thread Completion */
	status = rzt2n_nand_wait_for_value(cdns_ctrl, TRDCOMPINTSTAT,
					   1000000,
					   BIT(cs), false);
	if (status)
		return status;

	writel_relaxed(BIT(cs), cdns_ctrl->reg + TRDCOMPINTSTAT);

	return 0;
}

static int rzt2n_nand_erase(struct nand_chip *chip, u32 page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	int status;
	u8 thread_nr = cdns_chip->cs[chip->cur_cs];

	rzt2n_nand_cdma_desc_prepare(cdns_ctrl,
				     cdns_chip->cs[chip->cur_cs],
				     page, 0, 0,
				     CMD_TYPE_ERS);
	status = rzt2n_nand_cdma_send_and_wait(cdns_ctrl, thread_nr);
	if (status) {
		dev_err(cdns_ctrl->dev, "erase operation failed\n");
		return -EIO;
	}

	status = rzt2n_nand_cdma_finish(cdns_ctrl);
	if (status)
		return status;

	return 0;
}

static int rzt2n_nand_read_bbm(struct nand_chip *chip, int page, u8 *buf)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int status;

	rzt2n_nand_prepare_data_size(chip, TT_BBM);

	rzt2n_nand_set_skip_bytes_conf(cdns_ctrl, 0, 0, 0);

	status = rzt2n_nand_cdma_transfer(cdns_ctrl,
					  cdns_chip->cs[chip->cur_cs],
					  page, cdns_ctrl->buf, NULL,
					  mtd->oobsize,
					  0, DMA_FROM_DEVICE, false);
	if (status) {
		dev_err(cdns_ctrl->dev, "read BBM failed\n");
		return -EIO;
	}

	memcpy(buf + cdns_chip->bbm_offs, cdns_ctrl->buf, cdns_chip->bbm_len);

	return 0;
}

static int rzt2n_nand_write_page(struct nand_chip *chip,
				 const u8 *buf, int oob_required,
				 int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int status;
	u16 marker_val = 0xFFFF;

	status = rzt2n_nand_select_target(chip);
	if (status)
		return status;

	rzt2n_nand_set_skip_bytes_conf(cdns_ctrl, cdns_chip->bbm_len,
				       mtd->writesize
				       + cdns_chip->bbm_offs,
				       1);

	if (oob_required) {
		marker_val = *(u16 *)(chip->oob_poi
				      + cdns_chip->bbm_offs);
	} else {
		/* Set oob data to 0xFF. */
		memset(cdns_ctrl->buf + mtd->writesize, 0xFF,
		       cdns_chip->avail_oob_size);
	}

	rzt2n_nand_set_skip_marker_val(cdns_ctrl, marker_val);

	rzt2n_nand_prepare_data_size(chip, TT_MAIN_OOB_AREA_EXT);

	if (rzt2n_nand_dma_buf_ok(cdns_ctrl, buf, mtd->writesize) &&
	    cdns_ctrl->caps2.data_control_supp) {
		u8 *oob;

		if (oob_required)
			oob = chip->oob_poi;
		else
			oob = cdns_ctrl->buf + mtd->writesize;

		status = rzt2n_nand_cdma_transfer(cdns_ctrl,
						  cdns_chip->cs[chip->cur_cs],
						  page, (void *)buf, oob,
						  mtd->writesize,
						  cdns_chip->avail_oob_size,
						  DMA_TO_DEVICE, true);
		if (status) {
			dev_err(cdns_ctrl->dev, "write page failed\n");
			return -EIO;
		}

		return 0;
	}

	if (oob_required) {
		/* Transfer the data to the oob area. */
		memcpy(cdns_ctrl->buf + mtd->writesize, chip->oob_poi,
		       cdns_chip->avail_oob_size);
	}

	memcpy(cdns_ctrl->buf, buf, mtd->writesize);

	rzt2n_nand_prepare_data_size(chip, TT_MAIN_OOB_AREAS);

	return rzt2n_nand_cdma_transfer(cdns_ctrl,
					cdns_chip->cs[chip->cur_cs],
					page, cdns_ctrl->buf, NULL,
					mtd->writesize
					+ cdns_chip->avail_oob_size,
					0, DMA_TO_DEVICE, true);
}

static int rzt2n_nand_write_oob(struct nand_chip *chip, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);

	memset(cdns_ctrl->buf, 0xFF, mtd->writesize);

	return rzt2n_nand_write_page(chip, cdns_ctrl->buf, 1, page);
}

static int rzt2n_nand_write_page_raw(struct nand_chip *chip,
				     const u8 *buf, int oob_required,
				     int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int writesize = mtd->writesize;
	int oobsize = mtd->oobsize;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	void *tmp_buf = cdns_ctrl->buf;
	int oob_skip = cdns_chip->bbm_len;
	size_t size = writesize + oobsize;
	int i, pos, len;
	int status = 0;

	status = rzt2n_nand_select_target(chip);
	if (status)
		return status;

	/*
	 * Fill the buffer with 0xff first except the full page transfer.
	 * This simplifies the logic.
	 */
	if (!buf || !oob_required)
		memset(tmp_buf, 0xff, size);

	rzt2n_nand_set_skip_bytes_conf(cdns_ctrl, 0, 0, 0);

	/* Arrange the buffer for syndrome payload/ecc layout. */
	if (buf) {
		for (i = 0; i < ecc_steps; i++) {
			pos = i * (ecc_size + ecc_bytes);
			len = ecc_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(tmp_buf + pos, buf, len);
			buf += len;
			if (len < ecc_size) {
				len = ecc_size - len;
				memcpy(tmp_buf + writesize + oob_skip, buf,
				       len);
				buf += len;
			}
		}
	}

	if (oob_required) {
		const u8 *oob = chip->oob_poi;
		u32 oob_data_offset = (cdns_chip->sector_count - 1) *
			(cdns_chip->sector_size + chip->ecc.bytes)
			+ cdns_chip->sector_size + oob_skip;

		/* BBM at the beginning of the OOB area. */
		memcpy(tmp_buf + writesize, oob, oob_skip);

		/* OOB free. */
		memcpy(tmp_buf + oob_data_offset, oob,
		       cdns_chip->avail_oob_size);
		oob += cdns_chip->avail_oob_size;

		/* OOB ECC. */
		for (i = 0; i < ecc_steps; i++) {
			pos = ecc_size + i * (ecc_size + ecc_bytes);
			if (i == (ecc_steps - 1))
				pos += cdns_chip->avail_oob_size;

			len = ecc_bytes;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(tmp_buf + pos, oob, len);
			oob += len;
			if (len < ecc_bytes) {
				len = ecc_bytes - len;
				memcpy(tmp_buf + writesize + oob_skip, oob,
				       len);
				oob += len;
			}
		}
	}

	rzt2n_nand_prepare_data_size(chip, TT_RAW_PAGE);

	return rzt2n_nand_cdma_transfer(cdns_ctrl,
					cdns_chip->cs[chip->cur_cs],
					page, cdns_ctrl->buf, NULL,
					mtd->writesize +
					mtd->oobsize,
					0, DMA_TO_DEVICE, false);
}

static int rzt2n_nand_write_oob_raw(struct nand_chip *chip,
				    int page)
{
	return rzt2n_nand_write_page_raw(chip, NULL, true, page);
}

static int rzt2n_nand_read_page(struct nand_chip *chip,
				u8 *buf, int oob_required, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int status = 0;
	int ecc_err_count = 0;

	status = rzt2n_nand_select_target(chip);
	if (status)
		return status;

	rzt2n_nand_set_skip_bytes_conf(cdns_ctrl, cdns_chip->bbm_len,
				       mtd->writesize
				       + cdns_chip->bbm_offs, 1);

	/*
	 * If data buffer can be accessed by DMA and data_control feature
	 * is supported then transfer data and oob directly.
	 */
	if (rzt2n_nand_dma_buf_ok(cdns_ctrl, buf, mtd->writesize) &&
	    cdns_ctrl->caps2.data_control_supp) {
		u8 *oob;

		if (oob_required)
			oob = chip->oob_poi;
		else
			oob = cdns_ctrl->buf + mtd->writesize;

		rzt2n_nand_prepare_data_size(chip, TT_MAIN_OOB_AREA_EXT);
		status = rzt2n_nand_cdma_transfer(cdns_ctrl,
						  cdns_chip->cs[chip->cur_cs],
						  page, buf, oob,
						  mtd->writesize,
						  cdns_chip->avail_oob_size,
						  DMA_FROM_DEVICE, true);
	/* Otherwise use bounce buffer. */
	} else {
		rzt2n_nand_prepare_data_size(chip, TT_MAIN_OOB_AREAS);
		status = rzt2n_nand_cdma_transfer(cdns_ctrl,
						  cdns_chip->cs[chip->cur_cs],
						  page, cdns_ctrl->buf,
						  NULL, mtd->writesize
						  + cdns_chip->avail_oob_size,
						  0, DMA_FROM_DEVICE, true);

		memcpy(buf, cdns_ctrl->buf, mtd->writesize);
		if (oob_required)
			memcpy(chip->oob_poi,
			       cdns_ctrl->buf + mtd->writesize,
			       mtd->oobsize);
	}

	switch (status) {
	case STAT_ECC_UNCORR:
		mtd->ecc_stats.failed++;
		ecc_err_count++;
		break;
	case STAT_ECC_CORR:
		ecc_err_count = FIELD_GET(CDMA_CS_MAXERR,
					  cdns_ctrl->cdma_desc->status);
		mtd->ecc_stats.corrected += ecc_err_count;
		break;
	case STAT_ERASED:
	case STAT_OK:
		break;
	default:
		dev_err(cdns_ctrl->dev, "read page failed\n");
		return -EIO;
	}

	if (oob_required)
		if (rzt2n_nand_read_bbm(chip, page, chip->oob_poi))
			return -EIO;

	return ecc_err_count;
}

static int rzt2n_nand_read_oob(struct nand_chip *chip, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);

	return rzt2n_nand_read_page(chip, cdns_ctrl->buf, 1, page);
}

static int rzt2n_nand_read_page_raw(struct nand_chip *chip,
				    u8 *buf, int oob_required, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int oob_skip = cdns_chip->bbm_len;
	int writesize = mtd->writesize;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	void *tmp_buf = cdns_ctrl->buf;
	int i, pos, len;
	int status = 0;

	status = rzt2n_nand_select_target(chip);
	if (status)
		return status;

	rzt2n_nand_set_skip_bytes_conf(cdns_ctrl, 0, 0, 0);

	rzt2n_nand_prepare_data_size(chip, TT_RAW_PAGE);
	status = rzt2n_nand_cdma_transfer(cdns_ctrl,
					  cdns_chip->cs[chip->cur_cs],
					  page, cdns_ctrl->buf, NULL,
					  mtd->writesize
					  + mtd->oobsize,
					  0, DMA_FROM_DEVICE, false);

	switch (status) {
	case STAT_ERASED:
	case STAT_OK:
		break;
	default:
		dev_err(cdns_ctrl->dev, "read raw page failed\n");
		return -EIO;
	}

	/* Arrange the buffer for syndrome payload/ecc layout. */
	if (buf) {
		for (i = 0; i < ecc_steps; i++) {
			pos = i * (ecc_size + ecc_bytes);
			len = ecc_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(buf, tmp_buf + pos, len);
			buf += len;
			if (len < ecc_size) {
				len = ecc_size - len;
				memcpy(buf, tmp_buf + writesize + oob_skip,
				       len);
				buf += len;
			}
		}
	}

	if (oob_required) {
		u8 *oob = chip->oob_poi;
		u32 oob_data_offset = (cdns_chip->sector_count - 1) *
			(cdns_chip->sector_size + chip->ecc.bytes)
			+ cdns_chip->sector_size + oob_skip;

		/* OOB free. */
		memcpy(oob, tmp_buf + oob_data_offset,
		       cdns_chip->avail_oob_size);

		/* BBM at the beginning of the OOB area. */
		memcpy(oob, tmp_buf + writesize, oob_skip);

		oob += cdns_chip->avail_oob_size;

		/* OOB ECC */
		for (i = 0; i < ecc_steps; i++) {
			pos = ecc_size + i * (ecc_size + ecc_bytes);
			len = ecc_bytes;

			if (i == (ecc_steps - 1))
				pos += cdns_chip->avail_oob_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(oob, tmp_buf + pos, len);
			oob += len;
			if (len < ecc_bytes) {
				len = ecc_bytes - len;
				memcpy(oob, tmp_buf + writesize + oob_skip,
				       len);
				oob += len;
			}
		}
	}

	return 0;
}

static int rzt2n_nand_read_oob_raw(struct nand_chip *chip,
				     int page)
{
	return rzt2n_nand_read_page_raw(chip, NULL, true, page);
}

static int rzt2n_nand_cmd_erase(struct nand_chip *chip,
				const struct nand_subop *subop)
{
	int i;
	const struct nand_op_instr *instr = NULL;
	unsigned int offset, naddrs;
	const u8 *addrs;
	u32 page = 0;

	instr = &subop->instrs[1];
	offset = nand_subop_get_addr_start_off(subop, 1);
	naddrs = nand_subop_get_num_addr_cyc(subop, 1);
	addrs = &instr->ctx.addr.addrs[offset];

	for (i = 0; i < naddrs; i++)
		page |= (u32)addrs[i] << (8 * i);

	return rzt2n_nand_erase(chip, page);
}

static int rzt2n_nand_cmd_reset(struct nand_chip *chip,
				const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	const struct nand_op_instr *instr;
	u32 reg;
	u8 cs = chip->cur_cs;
	int op_id, ret;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		instr = &subop->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (instr->ctx.cmd.opcode != NAND_CMD_RESET)
				return 0;

		default:
			break;
		}
	}

	/* Wait for thread ready */
	ret = rzt2n_nand_wait_for_value(cdns_ctrl, TRDSTAT,
					1000000, BIT(cs), true);
	if (ret)
		return ret;

	writel_relaxed(0, cdns_ctrl->reg + CMDREG1);

	reg = FIELD_PREP(CMDREG0_CT, CMDREG0_CT_PIO);
	reg |= FIELD_PREP(CMDREG0_TRD_NUM, cs);
	reg |= FIELD_PREP(CMDREG0_CMD_TYPE, CMD_TYPE_RST);

	writel_relaxed(reg, cdns_ctrl->reg + CMDREG0);

	return 0;
}

static int rzt2n_nand_cmd_page_read(struct nand_chip *chip,
				    const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	const struct nand_op_instr *instr, *data_instr = NULL;
	const u8 *addrs = NULL;
	unsigned int col_cycles = (mtd->writesize > 512) ? 2 : 1;
	int op_id, i, offset, naddrs;
	size_t len = 0;
	u32 reg, row_addr = 0, ctype;
	u8 cs = chip->cur_cs;
	bool readid = false;
	u8 rd_mid = 0, rd_did = 0;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		instr = &subop->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (instr->ctx.cmd.opcode == NAND_CMD_READID) {
				reg = readl_relaxed(cdns_ctrl->reg + MFGID);
				rd_mid = FIELD_GET(MFGID_MID, reg);
				rd_did = FIELD_GET(MFGID_DID, reg);
				readid = true;
			} else
				ctype = CMD_TYPE_RD;

			break;

		case NAND_OP_ADDR_INSTR:
			offset = nand_subop_get_addr_start_off(subop, op_id);
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];

			if (naddrs > col_cycles) {
				int row_cycles = naddrs - col_cycles;

				row_addr = 0;

				for (i = 0; i < row_cycles; i++)
					row_addr |= (u32)addrs[i + col_cycles] << (i * 8);

			}
			break;

		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_DATA_OUT_INSTR:
			data_instr = instr;
			len = nand_subop_get_data_len(subop, op_id);
			break;

		case NAND_OP_WAITRDY_INSTR:
			break;
		}
	}

	if (readid) {
		if (data_instr && data_instr->ctx.data.buf.in && len) {
			u8 *id_data = data_instr->ctx.data.buf.in;

			if (len >= 1)
				id_data[0] = rd_mid;
			if (len >= 2)
				id_data[1] = rd_did;
		}
		return 0;
	}

	rzt2n_nand_pio_send_and_wait(cdns_ctrl, row_addr, ctype, cs);

	if (len)
		memcpy_fromio(data_instr->ctx.data.buf.in, cdns_ctrl->io.virt, len);

	return 0;
}

static int rzt2n_nand_cmd_write(struct nand_chip *chip,
				const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	const struct nand_op_instr *instr, *data_instr = NULL;
	const u8 *addrs = NULL;
	unsigned int col_cycles = (mtd->writesize > 512) ? 2 : 1;
	int op_id, i, offset, naddrs;
	size_t len = 0;
	u32 row_addr = 0, ctype;
	u8 cs = chip->cur_cs;
	const void *buf = NULL;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		instr = &subop->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			ctype = CMD_TYPE_PG;
			break;

		case NAND_OP_ADDR_INSTR:
			offset = nand_subop_get_addr_start_off(subop, op_id);
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];

			if (naddrs > col_cycles) {
				int row_cycles = naddrs - col_cycles;

				row_addr = 0;

				for (i = 0; i < row_cycles; i++)
					row_addr |= (u32)addrs[i + col_cycles] << (i * 8);
			}
			break;

		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_DATA_OUT_INSTR:
			offset = nand_subop_get_data_start_off(subop, op_id);
			buf = instr->ctx.data.buf.out + offset;
			len = nand_subop_get_data_len(subop, op_id);
			data_instr = instr;
			break;

		case NAND_OP_WAITRDY_INSTR:
			break;
		}
	}

	if (len)
		memcpy_fromio(cdns_ctrl->io.virt, buf, len);

	return rzt2n_nand_pio_send_and_wait(cdns_ctrl, row_addr, ctype, cs);
}

static int rzt2n_nand_cmd_status(struct nand_chip *chip,
				 const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	const struct nand_op_instr *instr;
	u8 *status_out = NULL;
	int op_id;
	u32 reg;

	if (subop->instrs[0].ctx.cmd.opcode != NAND_CMD_STATUS)
		return -EOPNOTSUPP;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		instr = &subop->instrs[op_id];
		if (instr->type == NAND_OP_DATA_IN_INSTR) {
			status_out = instr->ctx.data.buf.in;
			break;
		}
	}

	if (!status_out)
		return -EINVAL;

	reg = readl_relaxed(cdns_ctrl->reg + CMDSTAT);

	if (reg & CMDSTAT_COMPLETE) {
		if (reg & CMDSTAT_FAIL)
			*status_out = NAND_STATUS_FAIL;
		else
			*status_out = NAND_STATUS_READY;
	} else {
		*status_out = NAND_STATUS_FAIL;
	}

	return 0;
}

static const struct nand_op_parser rzt2n_nand_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(
		rzt2n_nand_cmd_erase,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ERASE_ADDRESS_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)
	),
	NAND_OP_PARSER_PATTERN(
		rzt2n_nand_cmd_status,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 1)
	),
	NAND_OP_PARSER_PATTERN(
		rzt2n_nand_cmd_page_read,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, MAX_DATA_SIZE)
	),
	NAND_OP_PARSER_PATTERN(
		rzt2n_nand_cmd_set_feature,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, 1),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, 4),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)
	),
	NAND_OP_PARSER_PATTERN(
		rzt2n_nand_cmd_write,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, MAX_DATA_SIZE),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)
	),
	NAND_OP_PARSER_PATTERN(
		rzt2n_nand_cmd_reset,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)
	),
);

static int rzt2n_nand_exec_op(struct nand_chip *chip,
			      const struct nand_operation *op,
			      bool check_only)
{
	if (!check_only) {
		int status = rzt2n_nand_select_target(chip);

		if (status)
			return status;
	}

	return nand_op_parser_exec_op(chip, &rzt2n_nand_op_parser, op,
				      check_only);
}

static int rzt2n_nand_ooblayout_free(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);

	if (section)
		return -ERANGE;

	oobregion->offset = cdns_chip->bbm_len;
	oobregion->length = cdns_chip->avail_oob_size - cdns_chip->bbm_len;

	return 0;
}

static int rzt2n_nand_ooblayout_ecc(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);

	if (section)
		return -ERANGE;

	oobregion->offset = cdns_chip->avail_oob_size;
	oobregion->length = chip->ecc.total;

	return 0;
}

static const struct mtd_ooblayout_ops rzt2n_nand_ooblayout_ops = {
	.free = rzt2n_nand_ooblayout_free,
	.ecc = rzt2n_nand_ooblayout_ecc,
};

static u32 rzt2n_cnt_clamp(u32 t_ps, u32 clk_ps, u32 add,
			   u32 min_field, u32 max_field)
{
	u64 cyc;
	u32 f;

	if (!clk_ps)
		return min_field;

	/* If timing is unspecified, pick the minimum of the chosen range. */
	if (!t_ps)
		return min_field;

	cyc = DIV_ROUND_UP_ULL(t_ps, clk_ps);

	/* field >= ceil(t/tclk) - add */
	if (cyc <= add)
		f = 0;
	else
		f = (u32)(cyc - add);

	return clamp_val(f, min_field, max_field);
}

/*
 * ASYNCTOGGLETIMING waveform fields:
 * (field + 1) cycles are generated by HW.
 * For the formulas: t(min) = cycles * tclk - off, cycles=(field+1).
 */
static u32 rzt2n_wave_cnt_m1(u32 t_ps, u32 clk_ps, u32 off_ps)
{
	u64 cyc;

	if (!clk_ps)
		return 0;
	if (!t_ps)
		return 0;

	cyc = DIV_ROUND_UP_ULL((u64)t_ps + off_ps, clk_ps);
	return cyc ? (u32)(cyc - 1) : 0;
}

static void rzt2n_init_timing_ctx(struct rzt2n_timing_ctx *ctx,
				  struct cdns_nand_ctrl *cdns_ctrl)
{
	ctx->clk_ps = DIV_ROUND_DOWN_ULL(1000000000000ULL, cdns_ctrl->nf_clk_rate);
	ctx->if_skew_ps = cdns_ctrl->caps1->if_skew;
	ctx->board_delay_ps = cdns_ctrl->board_delay;
	ctx->board_delay_skew_max_ps = ctx->board_delay_ps + ctx->if_skew_ps;
	ctx->slow_clk = (cdns_ctrl->nf_clk_rate <= 20ULL * 1000 * 1000);

	/* Offsets depend on nf_clk <=20MHz or >20MHz */
	ctx->off_pulse_ps = ctx->slow_clk ? 3000 : 2000;
	ctx->off_cycle_ps = ctx->slow_clk ? 5000 : 3000;
	ctx->off_wc_ps = ctx->slow_clk ? 5000 : 3000;
	ctx->off_rea_non_edo_ps = ctx->slow_clk ? 12000 : 11000; /* non-EDO */
	ctx->off_rea_edo_ps  = ctx->slow_clk ? 14000 : 10500; /* EDO */

	/* Setup/hold offsets */
	ctx->off_als_ps = ctx->slow_clk ? 4000 : 3000;
	ctx->off_cls_ps = ctx->slow_clk ? 4000 : 3000;
	ctx->off_ds_ps  = 3000;
	ctx->off_alh_ps = 3000;
	ctx->off_ch_ps  = ctx->slow_clk ? 3000 : 2000;
	ctx->off_clh_ps = ctx->slow_clk ? 3000 : 2000;
	ctx->off_dh_ps  = ctx->slow_clk ? 6000 : 5000;
}

/* Calculate TRP/TRH/TWP/TWH for ASYNCTOGGLETIMING register */
static void rzt2n_calc_async_toggle(const struct nand_sdr_timings *sdr,
				    const struct rzt2n_timing_ctx *ctx,
				    struct rzt2n_nand_timings *t)
{
	u32 trp_cnt, trh_cnt, twp_cnt, twh_cnt;

	/* Base TRP/TRH from tRP_min / tREH_min */
	trp_cnt = rzt2n_wave_cnt_m1(sdr->tRP_min, ctx->clk_ps, ctx->off_pulse_ps);
	trh_cnt = rzt2n_wave_cnt_m1(sdr->tREH_min, ctx->clk_ps, ctx->off_pulse_ps);

	/* Enforce tRC_min */
	{
		u32 need_sum = (u32)DIV_ROUND_UP_ULL((u64)sdr->tRC_min + ctx->off_cycle_ps,
				    ctx->clk_ps);
		u32 have_sum = (trp_cnt + 1) + (trh_cnt + 1);

		if (have_sum < need_sum) {
			u32 need_trh_cycles = need_sum - (trp_cnt + 1);

			need_trh_cycles = max(need_trh_cycles, 1U);
			trh_cnt = max(trh_cnt, need_trh_cycles - 1);
		}
	}

	/*
	 * ONFI specification dictates that the host shall use EDO data
	 * output cycle timings when running with a tRC value less than 30ns.
	 */
	if (sdr->tRC_min >= 30000U) {
		/* non-EDO */
		trp_cnt = max(trp_cnt,
			      rzt2n_wave_cnt_m1(sdr->tREA_max + ctx->board_delay_skew_max_ps,
						ctx->clk_ps, ctx->off_rea_non_edo_ps));
	} else {
		/* EDO */
		/* Enforce tREA_max capability (EDO) by increasing TRP */
		u32 need_sum, have_sum;
		u32 need_trp_cycles;

		need_sum = (u32)DIV_ROUND_UP_ULL((u64)sdr->tREA_max +
						 ctx->board_delay_skew_max_ps +
						 ctx->off_rea_edo_ps,
						 ctx->clk_ps);
		have_sum = (trp_cnt + 1) + (trh_cnt + 1);
		if (have_sum < need_sum) {
			need_trp_cycles = need_sum - (trh_cnt + 1);
			need_trp_cycles = max(need_trp_cycles, 1U);
			/*
			 * need_trp_cycles is in cycles; register stores (cycles - 1).
			 * Use 'need_trp_cycles' (instead of 'need_trp_cycles - 1') to add
			 * one extra cycle margin in EDO mode.
			 */
			trp_cnt = max(trp_cnt, need_trp_cycles);
		}
	}

	/* Base TWP */
	twp_cnt = 0;
	twp_cnt = max(twp_cnt, rzt2n_wave_cnt_m1(sdr->tALS_min + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_als_ps));
	twp_cnt = max(twp_cnt, rzt2n_wave_cnt_m1(sdr->tCLS_min + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_cls_ps));
	twp_cnt = max(twp_cnt, rzt2n_wave_cnt_m1(sdr->tWP_min  + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_pulse_ps));
	twp_cnt = max(twp_cnt, rzt2n_wave_cnt_m1(sdr->tDS_min  + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_ds_ps));

	/* Base TWH */
	twh_cnt = 0;
	twh_cnt = max(twh_cnt, rzt2n_wave_cnt_m1(sdr->tALH_min + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_alh_ps));
	twh_cnt = max(twh_cnt, rzt2n_wave_cnt_m1(sdr->tCH_min  + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_ch_ps));
	twh_cnt = max(twh_cnt, rzt2n_wave_cnt_m1(sdr->tCLH_min + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_clh_ps));
	twh_cnt = max(twh_cnt, rzt2n_wave_cnt_m1(sdr->tWH_min  + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_pulse_ps));
	twh_cnt = max(twh_cnt, rzt2n_wave_cnt_m1(sdr->tDH_min  + ctx->if_skew_ps,
						 ctx->clk_ps, ctx->off_dh_ps));

	/* Enforce tWC_min */
	{
		u32 need_sum = (u32)DIV_ROUND_UP_ULL((u64)(sdr->tWC_min + ctx->if_skew_ps) +
						     ctx->off_wc_ps, ctx->clk_ps);
		u32 have_sum = (twp_cnt + 1) + (twh_cnt + 1);

		if (have_sum < need_sum) {
			u32 need_twh_cycles = need_sum - (twp_cnt + 1);

			need_twh_cycles = max(need_twh_cycles, 1U);
			twh_cnt = max(twh_cnt, need_twh_cycles - 1);
		}
	}

	/* Clamp to 5-bit waveform fields */
	trp_cnt = clamp_val(trp_cnt, 0U, 0x1FU);
	trh_cnt = clamp_val(trh_cnt, 0U, 0x1FU);
	twp_cnt = clamp_val(twp_cnt, 0U, 0x1FU);
	twh_cnt = clamp_val(twh_cnt, 0U, 0x1FU);

	t->async_toggle_timings =
		FIELD_PREP(ASYNCTOGGLETIMING_TRH, trh_cnt) |
		FIELD_PREP(ASYNCTOGGLETIMING_TRP, trp_cnt) |
		FIELD_PREP(ASYNCTOGGLETIMING_TWH, twh_cnt) |
		FIELD_PREP(ASYNCTOGGLETIMING_TWP, twp_cnt);
}

/* Calculate for TIMING0/1/2 registers */
static void rzt2n_calc_seq_regs(const struct nand_sdr_timings *sdr,
				const struct rzt2n_timing_ctx *ctx,
				struct rzt2n_nand_timings *t)
{
	u32 tadl_cnt, twhr_cnt, trhw_cnt;
	u32 trhz_cnt, twb_cnt;
	u32 tfeat_cnt, tceh_cnt, tcs_cnt;
	u32 reg;

	/* TIMING0 */
	tadl_cnt = rzt2n_cnt_clamp(sdr->tADL_min + ctx->if_skew_ps, ctx->clk_ps,
				   57, 0x00, 0xFF);
	twhr_cnt = rzt2n_cnt_clamp(sdr->tWHR_min + ctx->if_skew_ps, ctx->clk_ps,
				   2, 0x0A, 0xFF);
	trhw_cnt = rzt2n_cnt_clamp(sdr->tRHW_min + ctx->if_skew_ps, ctx->clk_ps,
				   1, 0x9C, 0xFF);

	reg  = FIELD_PREP(TIMING0_TADL, tadl_cnt);
	reg |= FIELD_PREP(TIMING0_TWHR, twhr_cnt);
	reg |= FIELD_PREP(TIMING0_TRHW, trhw_cnt);
	reg |= GENMASK(23, 16);
	t->timings0 = reg;

	/* TIMING1 */
	trhz_cnt = rzt2n_cnt_clamp(sdr->tRHZ_max, ctx->clk_ps,
				   2, 0x1A, 0xFF);
	twb_cnt = rzt2n_cnt_clamp(sdr->tWB_max + ctx->board_delay_ps, ctx->clk_ps,
				  11, 0x29, 0xFF);
	/* HW requirement: +2 cycles for synchronizers */
	twb_cnt = clamp_val(twb_cnt + 2, 0x29U, 0xFFU);

	reg  = FIELD_PREP(TIMING1_TRHZ, trhz_cnt);
	reg |= FIELD_PREP(TIMING1_TWB, twb_cnt);
	reg |= GENMASK(15, 0);
	t->timings1 = reg;

	/* TIMING2 */
	tfeat_cnt = rzt2n_cnt_clamp(sdr->tFEAT_max, ctx->clk_ps,
				    318, 0x000, 0x3FF);
	tceh_cnt = rzt2n_cnt_clamp(sdr->tCEH_min, ctx->clk_ps,
				   1, 0x00, 0x3F);
	tcs_cnt = rzt2n_cnt_clamp(sdr->tCS_min + ctx->if_skew_ps, ctx->clk_ps,
				  11, 0x00, 0x3F);

	reg  = FIELD_PREP(TIMING2_TFEAT, tfeat_cnt);
	reg |= FIELD_PREP(TIMING2_CSHOLD, tceh_cnt);
	reg |= FIELD_PREP(TIMING2_CSSETUP, tcs_cnt);
	t->timings2 = reg;
}

static int rzt2n_nand_setup_interface(struct nand_chip *chip, int chipnr,
				      const struct nand_interface_config *conf)
{
	const struct nand_sdr_timings *sdr;
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct rzt2n_nand_timings *t = &cdns_chip->timings;
	struct rzt2n_timing_ctx ctx;
	u32 reg, trp_cnt;

	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	memset(t, 0, sizeof(*t));

	rzt2n_init_timing_ctx(&ctx, cdns_ctrl);
	rzt2n_calc_async_toggle(sdr, &ctx, t);
	rzt2n_calc_seq_regs(sdr, &ctx, t);

	/*
	 * PHYCTRLREG: Extended read mode is always enabled.
	 * As HW, the PHONYDQSTIMING value should match the TRP bits.
	 */
	trp_cnt = FIELD_GET(ASYNCTOGGLETIMING_TRP, t->async_toggle_timings);
	reg = readl_relaxed(cdns_ctrl->reg + PHYCTRLREG);
	reg &= ~PHYCTRLREG_PHONYDQSTIMING;
	reg |= FIELD_PREP(PHYCTRLREG_PHONYDQSTIMING, trp_cnt);
	t->phy_ctrl = reg;

	/* Optional debug */
	dev_dbg(cdns_ctrl->dev, "nf_clk=%u clk_ps=%u slow_clk=%d\n",
		cdns_ctrl->nf_clk_rate, ctx.clk_ps, ctx.slow_clk);
	dev_dbg(cdns_ctrl->dev, "ASYNCTOGGLETIMING=0x%08x\n",
		t->async_toggle_timings);
	dev_dbg(cdns_ctrl->dev,
		"TIMING0=0x%08x TIMING1=0x%08x TIMING2=0x%08x PHYCTRL=0x%08x\n",
		t->timings0, t->timings1, t->timings2, t->phy_ctrl);

	/*
	 * Reset cdns->selected_chip so the next command will cause the timing
	 * registers to be updated in rzt2n_nand_select_target().
	 */
	cdns_ctrl->selected_chip = NULL;

	return 0;
}

/* Generic flash bbt descriptors */
static uint8_t bbt_pattern[] = {'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = {'1', 't', 'b', 'B' };

static struct nand_bbt_descr rzt2n_bbt_main_no_oob_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		 | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP
		 | NAND_BBT_NO_OOB,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 8,
	.pattern = bbt_pattern
};

static struct nand_bbt_descr rzt2n_bbt_mirror_no_oob_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		 | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP
		 | NAND_BBT_NO_OOB,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 8,
	.pattern = mirror_pattern
};

static int rzt2n_nand_attach_chip(struct nand_chip *chip)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	u32 ecc_size;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	chip->bbt_options |= NAND_BBT_USE_FLASH;
	chip->bbt_options |= NAND_BBT_NO_OOB;
	chip->bbt_td = &rzt2n_bbt_main_no_oob_descr;
	chip->bbt_md = &rzt2n_bbt_mirror_no_oob_descr;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	chip->options |= NAND_NO_SUBPAGE_WRITE;

	cdns_chip->bbm_offs = chip->badblockpos;
	cdns_chip->bbm_offs &= ~0x01;
	cdns_ctrl->controller.controller_wp = 1;

	/* this value should be even number */
	cdns_chip->bbm_len = 2;

	ret = nand_ecc_choose_conf(chip,
				   &cdns_ctrl->ecc_caps,
				   mtd->oobsize - cdns_chip->bbm_len);
	if (ret) {
		dev_err(cdns_ctrl->dev, "ECC configuration failed\n");
		return ret;
	}

	dev_dbg(cdns_ctrl->dev,
		"chosen ECC settings: step=%d, strength=%d, bytes=%d\n",
		chip->ecc.size, chip->ecc.strength, chip->ecc.bytes);

	/* Error correction configuration. */
	cdns_chip->sector_size = chip->ecc.size;
	cdns_chip->sector_count = mtd->writesize / cdns_chip->sector_size;
	ecc_size = cdns_chip->sector_count * chip->ecc.bytes;

	cdns_chip->avail_oob_size = mtd->oobsize - ecc_size;

	if (cdns_chip->avail_oob_size > cdns_ctrl->bch_metadata_size)
		cdns_chip->avail_oob_size = cdns_ctrl->bch_metadata_size;

	if ((cdns_chip->avail_oob_size + cdns_chip->bbm_len + ecc_size)
	    > mtd->oobsize)
		cdns_chip->avail_oob_size -= 4;

	ret = rzt2n_nand_get_ecc_strength_idx(cdns_ctrl, chip->ecc.strength);
	if (ret < 0)
		return -EINVAL;

	cdns_chip->corr_str_idx = (u8)ret;

	if (rzt2n_nand_wait_for_value(cdns_ctrl, CTRLSTAT,
					1000000,
					CTRLSTAT_CTRLBUSY, true))
		return -ETIMEDOUT;

	rzt2n_nand_set_ecc_strength(cdns_ctrl,
				    cdns_chip->corr_str_idx);

	rzt2n_nand_set_erase_detection(cdns_ctrl, true,
					 chip->ecc.strength);

	/* Override the default read operations. */
	chip->ecc.read_page = rzt2n_nand_read_page;
	chip->ecc.read_page_raw = rzt2n_nand_read_page_raw;
	chip->ecc.write_page = rzt2n_nand_write_page;
	chip->ecc.write_page_raw = rzt2n_nand_write_page_raw;
	chip->ecc.read_oob = rzt2n_nand_read_oob;
	chip->ecc.write_oob = rzt2n_nand_write_oob;
	chip->ecc.read_oob_raw = rzt2n_nand_read_oob_raw;
	chip->ecc.write_oob_raw = rzt2n_nand_write_oob_raw;

	if ((mtd->writesize + mtd->oobsize) > cdns_ctrl->buf_size)
		cdns_ctrl->buf_size = mtd->writesize + mtd->oobsize;

	/* Is 32-bit DMA supported? */
	ret = dma_set_mask(cdns_ctrl->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(cdns_ctrl->dev, "no usable DMA configuration\n");
		return ret;
	}

	mtd_set_ooblayout(mtd, &rzt2n_nand_ooblayout_ops);

	return 0;
}

static const struct nand_controller_ops rzt2n_nand_controller_ops = {
	.attach_chip = rzt2n_nand_attach_chip,
	.exec_op = rzt2n_nand_exec_op,
	.setup_interface = rzt2n_nand_setup_interface,
};

static int rzt2n_nand_choose_interface_config(struct nand_chip *chip,
					      struct nand_interface_config *iface)
{
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct nand_interface_config ifc;

	onfi_fill_interface_config(chip, &ifc, NAND_SDR_IFACE, cdns_chip->req_mode);

	return nand_choose_best_sdr_timings(chip, iface, &ifc.timings.sdr);
}

static int rzt2n_nand_chip_init(struct cdns_nand_ctrl *cdns_ctrl,
				struct device_node *np)
{
	struct cdns_nand_chip *cdns_chip;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int nsels, ret, i;
	u32 dt_mode;
	u32 cs;

	nsels = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (nsels <= 0) {
		dev_err(cdns_ctrl->dev, "missing/invalid reg property\n");
		return -EINVAL;
	}

	/* Allocate the nand chip structure. */
	cdns_chip = devm_kzalloc(cdns_ctrl->dev, sizeof(*cdns_chip) +
				 (nsels * sizeof(u8)),
				 GFP_KERNEL);
	if (!cdns_chip) {
		dev_err(cdns_ctrl->dev, "could not allocate chip structure\n");
		return -ENOMEM;
	}

	cdns_chip->nsels = nsels;

	for (i = 0; i < nsels; i++) {
		/* Retrieve CS id. */
		ret = of_property_read_u32_index(np, "reg", i, &cs);
		if (ret) {
			dev_err(cdns_ctrl->dev,
				"could not retrieve reg property: %d\n",
				ret);
			return ret;
		}

		if (cs >= cdns_ctrl->caps2.max_banks) {
			dev_err(cdns_ctrl->dev,
				"invalid reg value: %u (max CS = %d)\n",
				cs, cdns_ctrl->caps2.max_banks);
			return -EINVAL;
		}

		if (test_and_set_bit(cs, &cdns_ctrl->assigned_cs)) {
			dev_err(cdns_ctrl->dev,
				"CS %d already assigned\n", cs);
			return -EINVAL;
		}

		cdns_chip->cs[i] = cs;
	}

	chip = &cdns_chip->chip;
	chip->controller = &cdns_ctrl->controller;

	nand_set_flash_node(chip, np);

	mtd = nand_to_mtd(chip);
	mtd->dev.parent = cdns_ctrl->dev;

	/*
	 * Get timing mode if present in DTS.
	 * If not set or invalid, use the default timing mode.
	 */
	if (!of_property_read_u32(np, "timing-mode", &dt_mode)) {
		if (dt_mode > 5) {
			dev_warn(cdns_ctrl->dev, "invalid DT timing-mode %u, using default\n",
				 dt_mode);
		} else {
			cdns_chip->req_mode = dt_mode;

			dev_info(cdns_ctrl->dev, "DT requested timing-mode: %u\n",
				 cdns_chip->req_mode);

			chip->ops.choose_interface_config = rzt2n_nand_choose_interface_config;
		}
	}

	chip->parameters.supports_set_get_features = 1;
	set_bit(ONFI_FEATURE_ADDR_TIMING_MODE, chip->parameters.set_feature_list);

	/*
	 * Default to HW ECC engine mode. If the nand-ecc-mode property is given
	 * in the DT node, this entry will be overwritten in nand_scan_ident().
	 */
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	ret = nand_scan(chip, cdns_chip->nsels);
	if (ret) {
		dev_err(cdns_ctrl->dev, "could not scan the nand chip\n");
		return ret;
	}

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(cdns_ctrl->dev,
			"failed to register mtd device: %d\n", ret);
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&cdns_chip->node, &cdns_ctrl->chips);

	return 0;
}

static void rzt2n_nand_chips_cleanup(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct cdns_nand_chip *entry, *temp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(entry, temp, &cdns_ctrl->chips, node) {
		chip = &entry->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&entry->node);
	}
}

static int rzt2n_nand_chips_init(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct device_node *np = cdns_ctrl->dev->of_node;
	int max_cs = cdns_ctrl->caps2.max_banks;
	int nchips, ret;

	nchips = of_get_child_count(np);

	if (nchips > max_cs) {
		dev_err(cdns_ctrl->dev,
			"too many NAND chips: %d (max = %d CS)\n",
			nchips, max_cs);
		return -EINVAL;
	}

	for_each_child_of_node_scoped(np, nand_np) {
		ret = rzt2n_nand_chip_init(cdns_ctrl, nand_np);
		if (ret) {
			rzt2n_nand_chips_cleanup(cdns_ctrl);
			return ret;
		}
	}

	return 0;
}

static void rzt2n_nand_irq_cleanup(int irqnum, struct cdns_nand_ctrl *cdns_ctrl)
{
	/* Disable interrupts. */
	writel_relaxed(INTENABLE_INTEN, cdns_ctrl->reg + INTENABLE);
}

static int rzt2n_nand_init(struct cdns_nand_ctrl *cdns_ctrl)
{
	int ret;

	cdns_ctrl->cdma_desc = dma_alloc_coherent(cdns_ctrl->dev,
						  sizeof(*cdns_ctrl->cdma_desc),
						  &cdns_ctrl->dma_cdma_desc,
						  GFP_KERNEL);
	if (!cdns_ctrl->dma_cdma_desc)
		return -ENOMEM;

	cdns_ctrl->buf_size = SZ_16K;
	cdns_ctrl->buf = kmalloc(cdns_ctrl->buf_size, GFP_KERNEL);
	if (!cdns_ctrl->buf) {
		ret = -ENOMEM;
		goto free_buf_desc;
	}

	if (devm_request_irq(cdns_ctrl->dev, cdns_ctrl->irq, rzt2n_nand_isr,
			     IRQF_SHARED, "rzt2n-nand-controller",
			     cdns_ctrl)) {
		dev_err(cdns_ctrl->dev, "Unable to allocate IRQ\n");
		ret = -ENODEV;
		goto free_buf;
	}

	spin_lock_init(&cdns_ctrl->irq_lock);
	init_completion(&cdns_ctrl->complete);

	ret = rzt2n_nand_hw_init(cdns_ctrl);
	if (ret)
		goto disable_irq;

	nand_controller_init(&cdns_ctrl->controller);
	INIT_LIST_HEAD(&cdns_ctrl->chips);

	cdns_ctrl->controller.ops = &rzt2n_nand_controller_ops;
	cdns_ctrl->curr_corr_str_idx = 0xFF;

	ret = rzt2n_nand_chips_init(cdns_ctrl);
	if (ret) {
		dev_err(cdns_ctrl->dev, "Failed to register MTD: %d\n",
			ret);
		goto disable_irq;
	}

	kfree(cdns_ctrl->buf);
	cdns_ctrl->buf = kzalloc(cdns_ctrl->buf_size, GFP_KERNEL);
	if (!cdns_ctrl->buf) {
		ret = -ENOMEM;
		goto disable_irq;
	}

	return 0;

disable_irq:
	rzt2n_nand_irq_cleanup(cdns_ctrl->irq, cdns_ctrl);

free_buf:
	kfree(cdns_ctrl->buf);

free_buf_desc:
	dma_free_coherent(cdns_ctrl->dev, sizeof(struct rzt2n_nand_cdma_desc),
			  cdns_ctrl->cdma_desc, cdns_ctrl->dma_cdma_desc);

	return ret;
}

static void rzt2n_nand_remove(struct cdns_nand_ctrl *cdns_ctrl)
{
	rzt2n_nand_chips_cleanup(cdns_ctrl);
	rzt2n_nand_irq_cleanup(cdns_ctrl->irq, cdns_ctrl);
	kfree(cdns_ctrl->buf);
	dma_free_coherent(cdns_ctrl->dev, sizeof(struct rzt2n_nand_cdma_desc),
			  cdns_ctrl->cdma_desc, cdns_ctrl->dma_cdma_desc);
}

struct rzt2n_nand_dt {
	struct cdns_nand_ctrl cdns_ctrl;
	struct clk *clk;
	struct clk *clk_sys;
	struct clk *clk_bus;
};

static const struct rzt2n_nand_dt_devdata rzt2n_nand_default = {
	.if_skew = 0,
	.has_dma = 1,
};

static const struct of_device_id rzt2n_nand_dt_ids[] = {
	{
		.compatible = "renesas,r9a07g076-nandc",
		.data = &rzt2n_nand_default
	}, {}
};

MODULE_DEVICE_TABLE(of, rzt2n_nand_dt_ids);

static int rzt2n_nand_dt_probe(struct platform_device *ofdev)
{
	struct resource *res;
	struct rzt2n_nand_dt *dt;
	struct cdns_nand_ctrl *cdns_ctrl;
	int ret;
	const struct rzt2n_nand_dt_devdata *devdata;

	devdata = device_get_match_data(&ofdev->dev);
	if (!devdata) {
		pr_err("Failed to find the right device id.\n");
		return -ENOMEM;
	}

	dt = devm_kzalloc(&ofdev->dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;

	cdns_ctrl = &dt->cdns_ctrl;
	cdns_ctrl->caps1 = devdata;

	cdns_ctrl->dev = &ofdev->dev;
	cdns_ctrl->irq = platform_get_irq(ofdev, 0);
	if (cdns_ctrl->irq < 0)
		return cdns_ctrl->irq;

	dev_info(cdns_ctrl->dev, "IRQ: nr %d\n", cdns_ctrl->irq);

	cdns_ctrl->reg = devm_platform_ioremap_resource(ofdev, 0);
	if (IS_ERR(cdns_ctrl->reg))
		return PTR_ERR(cdns_ctrl->reg);

	cdns_ctrl->io.virt = devm_platform_get_and_ioremap_resource(ofdev, 1, &res);
	if (IS_ERR(cdns_ctrl->io.virt))
		return PTR_ERR(cdns_ctrl->io.virt);
	cdns_ctrl->io.dma = res->start;

	/* Get clocks */
	dt->clk_sys = devm_clk_get(cdns_ctrl->dev, "sysclk");
	if (IS_ERR(dt->clk_sys))
		return dev_err_probe(cdns_ctrl->dev, PTR_ERR(dt->clk_sys),
				     "failed to get sysclk\n");

	dt->clk_bus = devm_clk_get(cdns_ctrl->dev, "busclk");
	if (IS_ERR(dt->clk_bus))
		return dev_err_probe(cdns_ctrl->dev, PTR_ERR(dt->clk_bus),
				     "failed to get busclk\n");

	dt->clk = devm_clk_get(cdns_ctrl->dev, "nf_clk");
	if (IS_ERR(dt->clk))
		return PTR_ERR(dt->clk);

	cdns_ctrl->nf_clk_rate = clk_get_rate(dt->clk);

	clk_prepare_enable(dt->clk);
	clk_prepare_enable(dt->clk_sys);
	clk_prepare_enable(dt->clk_bus);

	/* Get resets */
	cdns_ctrl->rst_sys = devm_reset_control_get(&ofdev->dev, "rst_sys");
	if (IS_ERR(cdns_ctrl->rst_sys))
		return PTR_ERR(cdns_ctrl->rst_sys);
	ret = reset_control_deassert(cdns_ctrl->rst_sys);
	if (ret)
		return ret;

	cdns_ctrl->rst_slave = devm_reset_control_get(&ofdev->dev, "rst_slave");
	if (IS_ERR(cdns_ctrl->rst_slave))
		return PTR_ERR(cdns_ctrl->rst_slave);
	ret = reset_control_deassert(cdns_ctrl->rst_slave);
	if (ret)
		return ret;

	cdns_ctrl->rst_prot = devm_reset_control_get(&ofdev->dev, "rst_prot");
	if (IS_ERR(cdns_ctrl->rst_prot))
		return PTR_ERR(cdns_ctrl->rst_prot);
	ret = reset_control_deassert(cdns_ctrl->rst_prot);
	if (ret)
		return ret;

	ret = rzt2n_nand_init(cdns_ctrl);
	if (ret)
		return ret;

	platform_set_drvdata(ofdev, dt);

	return 0;
}

static void rzt2n_nand_dt_remove(struct platform_device *ofdev)
{
	struct rzt2n_nand_dt *dt = platform_get_drvdata(ofdev);

	rzt2n_nand_remove(&dt->cdns_ctrl);
}

static struct platform_driver rzt2n_nand_dt_driver = {
	.probe		= rzt2n_nand_dt_probe,
	.remove_new	= rzt2n_nand_dt_remove,
	.driver		= {
		.name	= "rzt2n-nand-controller",
		.of_match_table = rzt2n_nand_dt_ids,
	},
};

module_platform_driver(rzt2n_nand_dt_driver);

MODULE_DESCRIPTION("Driver for RZ/T2N NAND flash controller");
MODULE_AUTHOR("Anh Ly");
MODULE_LICENSE("GPL v2");

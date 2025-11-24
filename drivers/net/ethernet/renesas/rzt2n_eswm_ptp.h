/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas gPTP device driver using for ESWM
 * Based on gPTP driver for Ethernet Switch2
 * support for Rcar Gen4 from Linux kernel v6.11-rc2
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __ESWM_PTP_H__
#define __ESWM_PTP_H__

#include <linux/ptp_clock_kernel.h>

#define PTPTIVC_INIT			0x28000000	/* 200MHz */
#define ESWM_PTP_CLOCK			PTPTIVC_INIT
#define ESWM_GPTP_OFFSET		0x00020000

/* for eswm_ptp_init */
enum eswm_ptp_reg_layout {
	ESWM_PTP_REG_LAYOUT
};

/* driver's definitions */
#define ESWM_RXTSTAMP_ENABLED		BIT(0)
#define ESWM_RXTSTAMP_TYPE_V2_L2_EVENT	BIT(1)
#define ESWM_RXTSTAMP_TYPE_ALL		(ESWM_RXTSTAMP_TYPE_V2_L2_EVENT | BIT(2))
#define ESWM_RXTSTAMP_TYPE		ESWM_RXTSTAMP_TYPE_ALL

#define ESWM_TXTSTAMP_ENABLED		BIT(0)

#define PTPRO				0

enum eswm_ptp_reg {
	PTPTMEC		= PTPRO + 0x0010,
	PTPTMDC		= PTPRO + 0x0014,
	PTPTIVC0	= PTPRO + 0x0020,
	PTPTOVC00	= PTPRO + 0x0030,
	PTPTOVC10	= PTPRO + 0x0034,
	PTPTOVC20	= PTPRO + 0x0038,
	PTPGPTPTM00	= PTPRO + 0x0050,
	PTPGPTPTM10	= PTPRO + 0x0054,
	PTPGPTPTM20	= PTPRO + 0x0058,
};

struct eswm_ptp_reg_offset {
	u16 enable;
	u16 disable;
	u16 increment;
	u16 config_t0;
	u16 config_t1;
	u16 config_t2;
	u16 monitor_t0;
	u16 monitor_t1;
	u16 monitor_t2;
};

struct eswm_ptp_private {
	void __iomem *addr;
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	const struct eswm_ptp_reg_offset *offs;
	spinlock_t lock;	/* For multiple registers access */
	u32 tstamp_tx_ctrl;
	u32 tstamp_rx_ctrl;
	s64 default_addend;
	bool initialized;
	bool parallel_mode;
};

int eswm_ptp_register(struct eswm_ptp_private *ptp_priv,
			   enum eswm_ptp_reg_layout layout, u32 clock);
int eswm_ptp_unregister(struct eswm_ptp_private *ptp_priv);
struct eswm_ptp_private *eswm_ptp_alloc(struct platform_device *pdev);
int eswm_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts);

#endif	/* #ifndef __ESWM_PTP_H__ */

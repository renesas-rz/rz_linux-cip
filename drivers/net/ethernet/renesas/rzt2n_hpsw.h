/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) Renesas Electronics Corp.
 *
 * Nhat Nguyen <nhat.nguyen.xb@renesas.com>
 */

#ifndef _RZT2N_HPSW_H_
#define _RZT2N_HPSW_H_

#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/if.h>

/* HSRPRPCONTROL: RED HsrPrp Control Register */
#define HPSW_REG_CTRL			0x30000
#define HPSW_CTRL_EN			BIT(0)

/* HSRPRPSTATUS: RED HsrPrp Status Register */
#define HPSW_REG_STATUS			0x30004
#define HPSW_STATUS_TOA			BIT(0)
#define HPSW_STATUS_TOB			BIT(1)
#define HPSW_STATUS_LINKA		BIT(8)
#define HPSW_STATUS_LINKB		BIT(9)
#define HPSW_STATUS_LINKC		BIT(10)

/* HSRPRPVERSION: RED HsrPrp Version Register */
#define HPSW_REG_VERSION		0x3000C

/* HSRPRPFRAMECOUNTCONTROL: RED HsrPrp Frame Count Control Register */
#define HPSW_REG_CNTCTL			0x30010

/* Per-port counters: n=0:A, n=1:B, n=2:C */
#define HPSW_REG_RXCNT(n)		(0x30020 + (n) * 0x20)
#define HPSW_REG_RXERR(n)		(0x30024 + (n) * 0x20)
#define HPSW_REG_TXCNT(n)		(0x30030 + (n) * 0x20)
#define HPSW_REG_TXERR(n)		(0x30034 + (n) * 0x20)

/* HSRPRPCONFIGCONTROL: RED HsrPrp Config Control Register */
#define HPSW_REG_CFGCTL			0x30080
#define HPSW_CFGCTL_MODEVAL		BIT(0)
#define HPSW_CFGCTL_VLANVAL		BIT(1)

/* HSRPRPCONFIGMODE: RED HsrPrp Config Mode Register */
#define HPSW_REG_CFGMODE		0x30084
#define HPSW_CFGMODE_MODE_MASK		GENMASK(2, 0)
#define HPSW_CFGMODE_REDBOXID_SHIFT	15
#define HPSW_CFGMODE_NOFORWARD		BIT(17)
#define HPSW_CFGMODE_PRP_UNTAGGING	BIT(19)
#define HPSW_CFGMODE_CUTTHROUGH		BIT(20)

/* HSRPRPCONFIGVLAN: RED HsrPrp Config VLAN Register */
#define HPSW_REG_CFGVLAN		0x30088
#define HPSW_CFGVLAN_VLAN_MASK		GENMASK(15, 0)
#define HPSW_CFGVLAN_VLANEN		BIT(16)

/* HSRPRPMACCONTROL: RED HsrPrp MAC Control Register */
#define HPSW_REG_MACCTL			0x30100
#define HPSW_MACCTL_MACVAL		BIT(0)

/* HSRPRPMAC1/2: RED HsrPrp MAC 1/2 Register */
#define HPSW_REG_MAC1			0x30104		/* MAC[0..3] */
#define HPSW_REG_MAC2			0x30108		/* MAC[4..5] */

#define HPSW_PORTS_NUM			3
#define HPSW_MAX_USER_PORTS		2

struct rzt2n_hpsw {
	void __iomem *base; /* MMIO base of HPSW */
	struct clk *clk; /* core clock */
	struct device *dev;
	struct reset_control *rst;
	struct phylink_pcs *pcs[HPSW_PORTS_NUM - 1];
	struct phylink *phylink;
	struct phylink_config phylink_config;
	phy_interface_t phy_interface;
	struct ethss *ethss;
	spinlock_t reg_lock;	/* protect simple register sequences */

	int port;
	u32 index;

	/* Config state */
	u8 mode;	/* 1 = PRP, 2 = HSR */
	u8 redbox_id;	/* used when in RedBox profile */
	u8 mac_addr[ETH_ALEN];
	u16 vlan_id;
	bool cut_through;
	bool no_forward;
	bool vlan_en;

	/* Supervision worker state */
	u32 sv_last_status;
};

static inline u32 rzt2n_hpsw_read(struct rzt2n_hpsw *hpsw, u32 offset)
{
	return readl(hpsw->base + offset);
}

static inline void rzt2n_hpsw_write(struct rzt2n_hpsw *hpsw, u32 offset, u32 val)
{
	writel(val, hpsw->base + offset);
}

/* RED HsrPrp Status  */
static inline u32 rzt2n_hpsw_status_read(struct rzt2n_hpsw *hpsw)
{
	return rzt2n_hpsw_read(hpsw, HPSW_REG_STATUS);
}

/* Supervision: TOA/TOB handling */
static inline void rzt2n_hpsw_status_clear_timeout(struct rzt2n_hpsw *hpsw,
						   bool clr_toa, bool clr_tob)
{
	u32 val = rzt2n_hpsw_status_read(hpsw);

	if (clr_toa)
		val &= ~HPSW_STATUS_TOA; /* Only 0 clears the flag */
	if (clr_tob)
		val &= ~HPSW_STATUS_TOB;

	rzt2n_hpsw_write(hpsw, HPSW_REG_STATUS, val);
}
#endif /* _RZT2N_HPSW_H_ */

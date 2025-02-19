/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Schneider Electric
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/net/renesas/rzt2h-ethss.h>
#include <net/dsa.h>

#define ETHSW_REVISION			0x0
#define ETHSW_PORT_OFFSET(port)		(0x400 * (port))

#define ETHSW_PORT_ENA			0x8
#define ETHSW_PORT_ENA_TX(port)		BIT(port)
#define ETHSW_PORT_ENA_RX_SHIFT		16
#define ETHSW_PORT_ENA_TX_RX(port)	(BIT(port) << ETHSW_PORT_ENA_RX_SHIFT) | \
					 BIT(port)
#define ETHSW_UCAST_DEF_MASK		0xC

#define ETHSW_VLAN_VERIFY		0x10
#define ETHSW_VLAN_VERI_SHIFT		0
#define ETHSW_VLAN_DISC_SHIFT		16

#define ETHSW_BCAST_DEF_MASK		0x14
#define ETHSW_MCAST_DEF_MASK		0x18

#define ETHSW_INPUT_LEARN		0x1C
#define ETHSW_INPUT_LEARN_DIS(p)	BIT((p) + 16)
#define ETHSW_INPUT_LEARN_BLOCK(p)	BIT(p)

#define ETHSW_MGMT_CFG			0x20
#define ETHSW_MGMT_CFG_ENABLE		BIT(6)

#define ETHSW_MODE_CFG			0x24
#define ETHSW_MODE_STATS_RESET		BIT(31)

#define ETHSW_VLAN_IN_MODE		0x28
#define ETHSW_VLAN_IN_MODE_PORT_SHIFT(port)	((port) * 2)
#define ETHSW_VLAN_IN_MODE_PORT(port)		(GENMASK(1, 0) << \
					ETHSW_VLAN_IN_MODE_PORT_SHIFT(port))
#define ETHSW_VLAN_IN_MODE_SINGLE_PASSTHROUGH	0x0
#define ETHSW_VLAN_IN_MODE_SINGLE_REPLACE	0x1
#define ETHSW_VLAN_IN_MODE_TAG_ALWAYS		0x2

#define ETHSW_VLAN_OUT_MODE		0x2C
#define ETHSW_VLAN_OUT_MODE_PORT_SHIFT(port)	((port) * 2)
#define ETHSW_VLAN_OUT_MODE_PORT(port)	(GENMASK(1, 0) << \
					ETHSW_VLAN_OUT_MODE_PORT_SHIFT(port))
#define ETHSW_VLAN_OUT_MODE_DIS		0x0
#define ETHSW_VLAN_OUT_MODE_STRIP	0x1
#define ETHSW_VLAN_OUT_MODE_TAG_THROUGH	0x2
#define ETHSW_VLAN_OUT_MODE_TRANSPARENT	0x3

#define ETHSW_VLAN_IN_MODE_ENA		0x30
#define ETHSW_VLAN_TAG_ID		0x34

#define ETHSW_SYSTEM_TAGINFO(port)	(0x200 + 4 * (port))

#define ETHSW_AUTH_PORT(port)		(0x240 + 4 * (port))
#define ETHSW_AUTH_PORT_AUTHORIZED	BIT(0)

#define ETHSW_VLAN_RES(entry)		(0x280 + 4 * (entry))
#define ETHSW_VLAN_RES_WR_PORTMASK	BIT(30)
#define ETHSW_VLAN_RES_WR_TAGMASK	BIT(29)
#define ETHSW_VLAN_RES_RD_TAGMASK	BIT(28)
#define ETHSW_VLAN_RES_VLANID		GENMASK(15, 4)
#define ETHSW_VLAN_RES_PORTMASK		GENMASK(3, 0)

#define ETHSW_RXMATCH_CONFIG(port)	(0x3F00 + 4 * (port))
#define ETHSW_RXMATCH_CONFIG_PATTERN(p)	BIT(p)

#define ETHSW_PATTERN_CTRL(p)		(0x3F40 + 4  * (p))
#define ETHSW_PATTERN_CTRL_MGMTFWD	BIT(1)

#define ETHSW_LK_CTRL			0x400
#define ETHSW_LK_ADDR_CTRL_ENABLE	BIT(0)
#define ETHSW_LK_ADDR_CTRL_LEARNING	BIT(1)
#define ETHSW_LK_ADDR_CTRL_AGEING	BIT(2)
#define ETHSW_LK_ADDR_CTRL_ALLOW_MIGR	BIT(3)
#define ETHSW_LK_ADDR_CTRL_CLEAR_TABLE	BIT(6)

#define ETHSW_LK_ADDR_CTRL		0x408
#define ETHSW_LK_ADDR_CTRL_BUSY		BIT(31)
#define ETHSW_LK_ADDR_CTRL_DELETE_PORT	BIT(30)
#define ETHSW_LK_ADDR_CTRL_CLEAR	BIT(29)
#define ETHSW_LK_ADDR_CTRL_LOOKUP	BIT(28)
#define ETHSW_LK_ADDR_CTRL_WAIT		BIT(27)
#define ETHSW_LK_ADDR_CTRL_READ		BIT(26)
#define ETHSW_LK_ADDR_CTRL_WRITE	BIT(25)
#define ETHSW_LK_ADDR_CTRL_ADDRESS	GENMASK(11, 0)

#define ETHSW_LK_DATA_LO		0x40C
#define ETHSW_LK_DATA_HI		0x410
#define ETHSW_LK_DATA_HI_VALID		BIT(16)
#define ETHSW_LK_DATA_HI_PORT		BIT(16)

#define ETHSW_LK_LEARNCOUNT		0x418
#define ETHSW_LK_LEARNCOUNT_COUNT	GENMASK(12, 0)
#define ETHSW_LK_LEARNCOUNT_MODE	GENMASK(31, 30)
#define ETHSW_LK_LEARNCOUNT_MODE_SET	0x0
#define ETHSW_LK_LEARNCOUNT_MODE_INC	0x1
#define ETHSW_LK_LEARNCOUNT_MODE_DEC	0x2

#define ETHSW_MGMT_TAG_CFG		0x480
#define ETHSW_MGMT_TAG_CFG_TAGFIELD	GENMASK(31, 16)
#define ETHSW_MGMT_TAG_CFG_ALL_FRAMES	BIT(1)
#define ETHSW_MGMT_TAG_CFG_ENABLE	BIT(0)

#define ETHSW_LK_AGETIME		0x41C
#define ETHSW_LK_AGETIME_MASK		GENMASK(23, 0)

#define ETHSW_MDIO_CFG_STATUS		0x700
#define ETHSW_MDIO_CFG_STATUS_CLKDIV	GENMASK(15, 7)
#define ETHSW_MDIO_CFG_STATUS_READERR	BIT(1)
#define ETHSW_MDIO_CFG_STATUS_BUSY	BIT(0)

#define ETHSW_MDIO_COMMAND		0x704
/* Register is named TRAININIT in datasheet and should be set when reading */
#define ETHSW_MDIO_COMMAND_READ		BIT(15)
#define ETHSW_MDIO_COMMAND_PHY_ADDR	GENMASK(9, 5)
#define ETHSW_MDIO_COMMAND_REG_ADDR	GENMASK(4, 0)

#define ETHSW_MDIO_DATA			0x708
#define ETHSW_MDIO_DATA_MASK		GENMASK(15, 0)

#define ETHSW_CMD_CFG(port)		(0x808 + ETHSW_PORT_OFFSET(port))
#define ETHSW_CMD_CFG_CNTL_FRM_ENA	BIT(23)
#define ETHSW_CMD_CFG_SW_RESET		BIT(13)
#define ETHSW_CMD_CFG_TX_CRC_APPEND	BIT(11)
#define ETHSW_CMD_CFG_HD_ENA		BIT(10)
#define ETHSW_CMD_CFG_PAUSE_IGNORE	BIT(8)
#define ETHSW_CMD_CFG_CRC_FWD		BIT(6)
#define ETHSW_CMD_CFG_ETH_SPEED		BIT(3)
#define ETHSW_CMD_CFG_RX_ENA		BIT(1)
#define ETHSW_CMD_CFG_TX_ENA		BIT(0)

#define ETHSW_FRM_LENGTH(port)		(0x814 + ETHSW_PORT_OFFSET(port))
#define ETHSW_FRM_LENGTH_MASK		GENMASK(13, 0)

#define ETHSW_STATUS(port)		(0x840 + ETHSW_PORT_OFFSET(port))

#define ETHSW_STATS_HIWORD		0x900

/* Stats */
#define ETHSW_FRAMES_TRANSMITTED_OK		0x868
#define ETHSW_FRAMES_RECEIVED_OK		0x86C
#define ETHSW_FRAME_CHECK_SEQUENCE_ERRORS	0x870
#define ETHSW_ALIGNMENT_ERRORS			0x874
#define ETHSW_OCTETS_TRANSMITTED_OK		0x878
#define ETHSW_OCTETS_RECEIVED_OK		0x87C
#define ETHSW_TX_PAUSE_MAC_CTRL_FRAMES		0x880
#define ETHSW_RX_PAUSE_MAC_CTRL_FRAMES		0x884
/* If */
#define ETHSW_IF_IN_ERRORS			0x888
#define ETHSW_IF_OUT_ERRORS			0x88C
#define ETHSW_IF_IN_UCAST_PKTS			0x890
#define ETHSW_IF_IN_MULTICAST_PKTS		0x894
#define ETHSW_IF_IN_BROADCAST_PKTS		0x898
#define ETHSW_IF_OUT_DISCARDS			0x89C
#define ETHSW_IF_OUT_UCAST_PKTS			0x8A0
#define ETHSW_IF_OUT_MULTICAST_PKTS		0x8A4
#define ETHSW_IF_OUT_BROADCAST_PKTS		0x8A8
/* Ether */
#define ETHSW_ETHER_STATS_DROP_EVENTS			0x8AC
#define ETHSW_ETHER_STATS_OCTETS			0x8B0
#define ETHSW_ETHER_STATS_PKTS				0x8B4
#define ETHSW_ETHER_STATS_UNDERSIZE_PKTS		0x8B8
#define ETHSW_ETHER_STATS_OVERSIZE_PKTS			0x8BC
#define ETHSW_ETHER_STATS_PKTS_64_OCTETS		0x8C0
#define ETHSW_ETHER_STATS_PKTS_65_TO_127_OCTETS		0x8C4
#define ETHSW_ETHER_STATS_PKTS_128_TO_255_OCTETS	0x8C8
#define ETHSW_ETHER_STATS_PKTS_256_TO_511_OCTETS	0x8CC
#define ETHSW_ETHER_STATS_PKTS_512_TO_1023_OCTETS	0x8D0
#define ETHSW_ETHER_STATS_PKTS_1024_TO_1518_OCTETS	0x8D4
#define ETHSW_ETHER_STATS_PKTS_1519_TO_X_OCTETS		0x8D8
#define ETHSW_ETHER_STATS_JABBERS			0x8DC
#define ETHSW_ETHER_STATS_FRAGMENTS			0x8E0

#define ETHSW_VLAN_RECEIVED			0x8E8
#define ETHSW_VLAN_TRANSMITTED			0x8EC

#define ETHSW_DEFERRED				0x910
#define ETHSW_MULTIPLE_COLLISIONS		0x914
#define ETHSW_SINGLE_COLLISIONS			0x918
#define ETHSW_LATE_COLLISIONS			0x91C
#define ETHSW_EXCESSIVE_COLLISIONS		0x920
#define ETHSW_CARRIER_SENSE_ERRORS		0x924

#define ETHSW_VLAN_TAG(prio, id)	(((prio) << 12) | (id))
#define ETHSW_PORTS_NUM			4
#define ETHSW_CPU_PORT			(ETHSW_PORTS_NUM - 1)
#define ETHSW_MDIO_DEF_FREQ		2500000
#define ETHSW_MDIO_TIMEOUT		100
#define ETHSW_JUMBO_LEN			(10 * SZ_1K)
#define ETHSW_MDIO_CLK_DIV_MIN		5
#define ETHSW_TAG_LEN			8
#define ETHSW_VLAN_COUNT		32

/* Ensure enough space for 2 VLAN tags */
#define ETHSW_EXTRA_MTU_LEN		(ETHSW_TAG_LEN + 8)
#define ETHSW_MAX_MTU			(ETHSW_JUMBO_LEN - ETHSW_EXTRA_MTU_LEN)

#define ETHSW_EXTRA_FRM_LENGTH		40

#define ETHSW_PATTERN_MGMTFWD		0

#define ETHSW_LK_BUSY_USEC_POLL		10
#define ETHSW_CTRL_TIMEOUT		1000
#define ETHSW_TABLE_ENTRIES		8192

struct fdb_entry {
	u8 mac[ETH_ALEN];
	u16 valid:1;
	u16 is_static:1;
	u16 prio:3;
	u16 port_mask:4;
	u16 reserved:7;
} __packed;

union lk_data {
	struct {
		u32 lo;
		u32 hi;
	};
	struct fdb_entry entry;
};

/**
 * struct ethsw - switch struct
 * @base: Base address of the switch
 * @clk: clk_switch clock
 * @dev: Device associated to the switch
 * @mii_bus: MDIO bus struct
 * @pcs: Array of PCS connected to the switch ports (not for the CPU)
 * @ds: DSA switch struct
 * @lk_lock: Lock for the lookup table
 * @vlan_lock: Lock for the vlan operation
 * @reg_lock: Lock for register read-modify-write operation
 * @bridged_ports: Mask of ports that are bridged and should be flooded
 * @br_dev: Bridge net device
 * @reset: Reset gpio signal for PHY
 * @rst: Reset control
 */
struct ethsw {
	void __iomem *base;
	struct clk *clk;
	struct device *dev;
	struct mii_bus	*mii_bus;
	struct ethss_port *pcs[ETHSW_PORTS_NUM - 1];
	struct ethss *ethss;
	struct dsa_switch ds;
	struct mutex lk_lock; /* Lock for the lookup table */
	struct mutex vlan_lock; /* Lock for the vlan operation */
	spinlock_t reg_lock; /* Lock for register read-modify-write operation */
	u32 bridged_ports;
	struct net_device *br_dev;
	struct gpio_desc *reset;
	struct reset_control *rst;
};

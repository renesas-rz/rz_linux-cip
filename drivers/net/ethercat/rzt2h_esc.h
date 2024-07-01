/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) Renesas Electronics Corp.
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/net/renesas/rzt2h-ethss.h>

#if IS_BUILTIN(CONFIG_ESC_APPLICATION)
#define _ESC_APPLICATION_ 1
#endif

#include "esc_device.h"

#include "soes/esc_eep.h"
#include "soes/options.h"
#include "soes/ecat_slv.h"

#ifndef _RZT2H_ESC_H_
#define _RZT2H_ESC_H_

#define PHY_ADR				0x0512
#define PHY_REG_ADR			0x0513
#define PHY_DATA			0x0514
#define ESC_DL_STATUS			0x0110
#define ESC_DL_STATUS_PDIOPE		BIT(0)

/* ESC EEPROM status */
#define EEP_CONT_STAT		0x0502
#define ESC_EEPROM_MASK_STATE	(0x2800)
#define ESC_EEPROM_ERROR_BLANK	(0x0800)
#define ESC_EEPROM_ERROR_I2CBUS	(0x2800)

/* MDIO */

#define MII_CONT_STAT			0x510
#define MII_CONT_STAT_BUSY		BIT(15)
#define MII_CONT_STAT_COMMAND_READ	(1 << 8)
#define MII_CONT_STAT_COMMAND_WRITE	(2 << 8)
#define MII_CONT_STAT_WRITE_ENABLE	BIT(0)
#define MII_CONT_STAT_CMDERR		BIT(14)
#define MII_CONT_STAT_READERR		BIT(13)

#define MII_ECAT_ACS_STAT		0x0516
#define MII_ECAT_ACS_STAT_ACSMII	BIT(0)

#define MII_PDI_ACS_STAT		0x0517
#define MII_PDI_ACS_STAT_ACSMII		BIT(0)

#define ESC_MAX_PORTS_NUM	3

/**
 * struct esc - ethercat slave struct
 * @base: Base address of the ethercat
 * @clk: ethercat clock
 * @dev: Device associated to the ethercat
 * @mii_bus: MDIO bus struct
 * @pcs: Array of PCS connected to the ethercat ports
 * @lock: Lock for interrupt operation
 * @rst_bus: Reset control bus
 * @rst_ip: Reset control ip
 * @mutex: Lock for workqueue operation
 */

struct esc {
	void __iomem *base;
	struct clk *clk;
	struct device *dev;
	struct esc_device ed;
	struct mii_bus  *mii_bus;
	struct ethss *ethss;
	struct ethss_port *pcs[ESC_MAX_PORTS_NUM];
	spinlock_t lock; /* Lock for interrupt operation */
	struct reset_control *rst_bus;
	struct reset_control *rst_ip;
	int eeprom_size;
	int phy_address_offset;
	int txc_port_delay;
	int sync0_irq;
	int sync1_irq;
	int cat_irq;
	struct hrtimer esc_hr_timer;
	struct work_struct workqueue;
	struct mutex mutex; /* Lock for workqueue operation */
	bool application_run;
};

void application_loop(void);
int application_init(struct esc *esc);
void ESC_interrupt_enable(u32 mask);
void ESC_interrupt_disable(u32 mask);

#endif

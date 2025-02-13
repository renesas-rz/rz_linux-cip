// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) Renesas Electronics Corp.
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/clk.h>
#include <linux/if_ether.h>
#include <linux/of_net.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/gpio/consumer.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/net/renesas/rzt2h-ethss.h>

#include "rzt2h_esc.h"

static struct esc esc;

char port_delay[4][4] = {
	"0",
	"10",
	"20",
	"30"
};

#ifndef _ESC_APPLICATION_
const struct _objectlist SDOobjects[] = {
};

void cb_get_inputs(void)
{
}

void cb_set_outputs(void)
{
}
#endif

void ESC_init(const struct esc_cfg_t *cfg)
{
}

void ESC_read(u16 address, void *buf, u16 len)
{
	memcpy_fromio(buf, esc.base + address, len);
}

void ESC_write(u16 address, void *buf, u16 len)
{
	memcpy_toio(esc.base + address, buf, len);
}

s8 EEP_read(u32 addr, u8 *data, u16 size)
{
	return 0;
}

s8 EEP_write(u32 addr, u8 *data, u16 size)
{
	return 0;
}

static int esc_rzt2h_setup(struct esc_device *ed)
{
	int ret = 0;
#ifdef _ESC_APPLICATION_
	ret = application_init(&esc);
#endif
	return ret;
}

static void esc_rzt2h_phylink_mac_link_up(struct esc_device *ed, int port,
					  unsigned int mode,
					  phy_interface_t interface,
					  struct phy_device *phydev,
					  int speed, int duplex,
					  bool tx_pause, bool rx_pause)
{
	ethss_link_up(esc.pcs[port], interface, speed, duplex);
}

static const struct esc_device_ops rzesc_device_ops = {
	.setup = esc_rzt2h_setup,
	.phylink_mac_link_up = esc_rzt2h_phylink_mac_link_up,
};

/** ESC interrupt enable function by the Slave stack in IRQ mode.
 *
 * @param[in]   mask     = of interrupts to enable
 */
void ESC_interrupt_enable(u32 mask)
{
	u16 readmask;

	ESC_read(ESCREG_ALEVENTMASK, &readmask, sizeof(readmask));
	readmask |= mask;
	ESC_write(ESCREG_ALEVENTMASK, &readmask, sizeof(readmask));
}

/** ESC interrupt disable function by the Slave stack in IRQ mode.
 *
 * @param[in]   mask     = interrupts to disable
 */
void ESC_interrupt_disable(u32 mask)
{
	u16 readmask;

	ESC_read(ESCREG_ALEVENTMASK, &readmask, sizeof(readmask));
	readmask &= ~mask;
	ESC_write(ESCREG_ALEVENTMASK, &readmask, sizeof(readmask));
}

static int esc_mdio_wait_busy(void)
{
	u32 status;
	int err;

	err = readw_poll_timeout(esc.base + MII_CONT_STAT, status,
				 !(status & MII_CONT_STAT_BUSY), 10,
				 1000 * USEC_PER_MSEC);
	if (err)
		dev_err(esc.dev, "MDIO command timeout\n");

	return err;
}

static int esc_wait_pdi_access(void)
{
	u32 status;
	int err;

	err = readb_poll_timeout(esc.base + MII_ECAT_ACS_STAT, status,
				 !(status & MII_ECAT_ACS_STAT_ACSMII), 10,
				 1000 * USEC_PER_MSEC);
	if (err)
		dev_err(esc.dev, "PDI can not access MII timeout\n");

	return err;
}

static int esc_mdio_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	u8 tmp;
	u16 ret, cmd, status;

	ret = esc_mdio_wait_busy();
	if (ret)
		return ret;

	/* Get PDI access right */
	ESC_read(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));
	tmp &= ~MII_PDI_ACS_STAT_ACSMII;
	tmp |= MII_PDI_ACS_STAT_ACSMII;
	ESC_write(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));

	ret = esc_wait_pdi_access();
	if (ret)
		return ret;

	/* Read PHY register */
	ESC_write(PHY_ADR, &phy_id, sizeof(phy_id));
	ESC_write(PHY_REG_ADR, &phy_reg, sizeof(phy_reg));

	cmd = MII_CONT_STAT_COMMAND_READ | MII_CONT_STAT_WRITE_ENABLE;
	ESC_write(MII_CONT_STAT, &cmd, sizeof(cmd));

	ret = esc_mdio_wait_busy();
	if (ret)
		return ret;

	/* Check the error bit */
	ESC_read(MII_CONT_STAT, &status, sizeof(status));
	if ((status & MII_CONT_STAT_CMDERR) || (status & MII_CONT_STAT_READERR))
		return -EIO;

	/* Get read data */
	ESC_read(PHY_DATA, &ret, sizeof(ret));

	/* Give the access right to ECAT */
	ESC_read(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));
	tmp &= ~MII_PDI_ACS_STAT_ACSMII;
	tmp |= 0;
	ESC_write(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));

	return ret;
}

static int esc_mdio_write(struct mii_bus *bus, int phy_id, int phy_reg,
			  u16 phy_data)
{
	u8 tmp;
	u16 ret, cmd, status;

	ret = esc_mdio_wait_busy();
	if (ret)
		return ret;

	/* Get PDI access right */
	ESC_read(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));
	tmp &= ~MII_PDI_ACS_STAT_ACSMII;
	tmp |= MII_PDI_ACS_STAT_ACSMII;
	ESC_write(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));

	ret = esc_wait_pdi_access();
	if (ret)
		return ret;

	/* Write PHY register */
	ESC_write(PHY_ADR, &phy_id, sizeof(phy_id));
	ESC_write(PHY_REG_ADR, &phy_reg, sizeof(phy_reg));
	ESC_write(PHY_DATA, &phy_data, sizeof(phy_data));

	cmd = MII_CONT_STAT_COMMAND_WRITE | MII_CONT_STAT_WRITE_ENABLE;
	ESC_write(MII_CONT_STAT, &cmd, sizeof(cmd));

	ret = esc_mdio_wait_busy();
	if (ret)
		return ret;

	/* Check the error bit */
	ESC_read(MII_CONT_STAT, &status, sizeof(status));
	if ((status & MII_CONT_STAT_CMDERR) || (status & MII_CONT_STAT_READERR))
		return -EIO;

	/* Give the access right to ECAT */
	ESC_read(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));
	tmp &= ~MII_PDI_ACS_STAT_ACSMII;
	tmp |= 0;
	ESC_write(MII_PDI_ACS_STAT, &tmp, sizeof(tmp));

	return 0;
}

static int esc_probe_mdio(struct device_node *node)
{
	struct device *dev = esc.dev;
	struct mii_bus *bus;

	bus = mdiobus_alloc();
	if (!bus)
		return -ENOMEM;

	bus->name = "esc_mdio";
	bus->read = esc_mdio_read;
	bus->write = esc_mdio_write;
	bus->parent = dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	esc.mii_bus = bus;

	return of_mdiobus_register(bus, node);
}

static void esc_pcs_free(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(esc.pcs); i++) {
		if (esc.pcs[i])
			ethss_destroy(esc.pcs[i]);
	}
}

static int esc_pcs_get(void)
{
	struct device_node *ports, *port, *pcs_node;
	struct ethss_port *pcs;
	phy_interface_t interface;
	int ret;
	u32 reg;

	ports = of_get_child_by_name(esc.dev->of_node, "ethernet-ports");
	if (!ports)
		return -EINVAL;

	for_each_available_child_of_node(ports, port) {
		pcs_node = of_parse_phandle(port, "pcs-handle", 0);
		if (!pcs_node)
			continue;

		if (of_property_read_u32(port, "reg", &reg)) {
			ret = -EINVAL;
			goto free_pcs;
		}

		if (reg > ARRAY_SIZE(esc.pcs)) {
			ret = -ENODEV;
			goto free_pcs;
		}

		pcs = ethss_create(esc.dev, pcs_node);
		if (IS_ERR(pcs)) {
			dev_err(esc.dev, "Failed to create PCS for port %d\n",
				reg);
			ret = PTR_ERR(pcs);
			goto free_pcs;
		}

		ret = of_get_phy_mode(port, &interface);
		if (ret < 0) {
			dev_err(esc.dev, "Failed to get phy mode\n");
			goto free_pcs;
		}

		ret = ethss_config(pcs, interface);
		if (ret < 0) {
			dev_err(esc.dev, "Failed to config ethss\n");
			goto free_pcs;
		}

		esc.pcs[reg] = pcs;
		esc.ethss = pcs->ethss;

		dev_info(esc.dev, "Config ETHSS port = %d, mode = %s for ESC OK\n",
			 esc.pcs[reg]->port,
			 phy_modes(esc.pcs[reg]->interface));

		of_node_put(pcs_node);
	}
	of_node_put(ports);

	return 0;

free_pcs:
	of_node_put(pcs_node);
	of_node_put(port);
	of_node_put(ports);
	esc_pcs_free();

	return ret;
}

static int esc_loading_eeprom(void)
{
	u32 timeout = 1000000000;
	u16 tmp16;

	while (1) {
		ESC_read(ESC_DL_STATUS, &tmp16, sizeof(tmp16));
		/* Loading successful, PDI operations */
		if (tmp16 & ESC_DL_STATUS_PDIOPE)
			return 0;

		ESC_read(EEP_CONT_STAT, &tmp16, sizeof(tmp16));
		tmp16 &= ESC_EEPROM_MASK_STATE;
		/* ACMDERR,CKSUMERR = 01 */
		if (tmp16 == ESC_EEPROM_ERROR_BLANK) {
			dev_info(esc.dev, "EEPROM is loaded, but it is blank\n");
			return -ENOENT;
		}

		/* ACMDERR,CKSUMERR = 11 */
		if (tmp16 == ESC_EEPROM_ERROR_I2CBUS) {
			dev_err(esc.dev, "I2C but error\n");
			return -EIO;
		}

		if (timeout == 0) { /* Timeout */
			dev_err(esc.dev, "EEPROM loaded timeout\n");
			return -ETIMEDOUT;
		}

		timeout--;
	}

	return 0;
}

#ifndef _ESC_APPLICATION_
/* SYNC0 ISR handler */
void Sync0_Isr(void)
{
}

/* SYNC1 ISR handler */
void Sync1_Isr(void)
{
}

/* PDI ISR handler */
void PDI_Isr(void)
{
}
#endif

static irqreturn_t esc_sync0irq_interrupt(int irq, void *data)
{
	int ret = IRQ_NONE;
	unsigned long flags;

	spin_lock_irqsave(&esc.lock, flags);
	Sync0_Isr();
	ret = IRQ_HANDLED;
	spin_unlock_irqrestore(&esc.lock, flags);

	return ret;
}

static irqreturn_t esc_sync1irq_interrupt(int irq, void *data)
{
	int ret = IRQ_NONE;
	unsigned long flags;

	spin_lock_irqsave(&esc.lock, flags);
	Sync1_Isr();
	ret = IRQ_HANDLED;
	spin_unlock_irqrestore(&esc.lock, flags);

	return ret;
}

static irqreturn_t esc_catirq_interrupt(int irq, void *data)
{
	int ret = IRQ_NONE;
	unsigned long flags;

	spin_lock_irqsave(&esc.lock, flags);
	PDI_Isr();
	ret = IRQ_HANDLED;
	spin_unlock_irqrestore(&esc.lock, flags);

	return ret;
}

static void workqueue_fn(struct work_struct *work)
{
#ifdef _ESC_APPLICATION_
	/* Execute the stack */
	while (esc.application_run)
		application_loop();
#endif
}

static ssize_t esc_start_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	mutex_lock(&esc.mutex);

	if (val == 1) {
		/* Set stack run flag */
		esc.application_run = true;
		schedule_work(&esc.workqueue);
	} else {
		esc.application_run = false;
		flush_work(&esc.workqueue);
	}

	mutex_unlock(&esc.mutex);

	return count;
}

static ssize_t esc_start_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%u\r\n", esc.application_run);
}

static DEVICE_ATTR_RW(esc_start);

static struct attribute *buffer_attrs[] = {
	&dev_attr_esc_start.attr,
	NULL,
};

static const struct attribute_group buffer_attr_group = {
	.attrs = buffer_attrs,
};

static int esc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *mdio;
	struct esc_device *ed;
	int ret;

	esc.dev = dev;
	esc.base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(esc.base))
		return PTR_ERR(esc.base);

	of_property_read_u32(node, "eeprom-size", &esc.eeprom_size);
	of_property_read_u32(node, "phy-address-offset",
			     &esc.phy_address_offset);
	of_property_read_u32(node, "txc-port-delay", &esc.txc_port_delay);

	ret = esc_pcs_get();
	if (ret)
		return ret;

	ret = ethss_esc_config(esc.ethss, esc.eeprom_size,
			       esc.phy_address_offset, esc.txc_port_delay);
	if (ret < 0) {
		dev_err(esc.dev, "Failed to config ethss for esc\n");
		return ret;
	}

	dev_info(esc.dev, "Config ESC eeprom %s, phy-address-offset = %d, txc-port-delay = %s ns OK\n",
		 esc.eeprom_size ? "more 32Kbits" : "less 16Kbits",
		 esc.phy_address_offset, port_delay[esc.txc_port_delay]);

	esc.clk = devm_clk_get(dev, NULL);
	if (IS_ERR(esc.clk)) {
		dev_err(dev, "failed get clk ethercat clock\n");
		return PTR_ERR(esc.clk);
	}

	ret = clk_prepare_enable(esc.clk);
	if (ret)
		return ret;

	esc.rst_bus = devm_reset_control_get(&pdev->dev, "reset_bus");
	if (IS_ERR(esc.rst_bus)) {
		dev_err(dev, "failed get ethercat reset_bus\n");
		return PTR_ERR(esc.rst_bus);
	}

	esc.rst_ip = devm_reset_control_get(&pdev->dev, "reset_ip");
	if (IS_ERR(esc.rst_ip)) {
		dev_err(dev, "failed get ethercat reset_ip\n");
		return PTR_ERR(esc.rst_ip);
	}

	/* Reset ESC */
	ret = reset_control_assert(esc.rst_bus);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Assert reset_bus control OK\n");

	ret = reset_control_assert(esc.rst_ip);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Assert reset_ip control OK\n");

	ret = ethss_esc_reset_out(esc.ethss, 1);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Force ESC_RESETOUT OK\n");

	usleep_range(1000, 2000);

	/* Release reset ESC */
	ret = reset_control_deassert(esc.rst_bus);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Deassert reset_bus control OK\n");

	ret = reset_control_deassert(esc.rst_ip);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Deassert reset_ip control OK\n");

	ret = ethss_esc_reset_out(esc.ethss, 0);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Release ESC_RESETOUT OK\n");

	ret = esc_loading_eeprom();
	if (ret < 0) {
		dev_info(&pdev->dev, "Must load valid SII binary from a supported ESI file to EEPROM for ESC working\n");
		return ret;
	}

	dev_dbg(&pdev->dev, "EEPROM loaded OK\n");

	usleep_range(1000, 2000);

	spin_lock_init(&esc.lock);
	/* Register interrupt */
	esc.sync0_irq = platform_get_irq_byname(pdev, "sync0irq");
	if (esc.sync0_irq < 0) {
		dev_err(&pdev->dev, "Failed to obtain sync0irq IRQ\n");
		return esc.sync0_irq;
	}

	ret = devm_request_irq(&pdev->dev, esc.sync0_irq,
			       esc_sync0irq_interrupt, 0,
			       dev_name(&pdev->dev), &esc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request sync0irq IRQ\n");
		return ret;
	}

	esc.sync1_irq = platform_get_irq_byname(pdev, "sync1irq");
	if (esc.sync1_irq < 0) {
		dev_err(&pdev->dev, "Failed to obtain sync1irq IRQ\n");
		return esc.sync1_irq;
	}

	ret = devm_request_irq(&pdev->dev, esc.sync1_irq,
			       esc_sync1irq_interrupt, 0,
			       dev_name(&pdev->dev), &esc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request sync1irq IRQ\n");
		return ret;
	}

	esc.cat_irq = platform_get_irq_byname(pdev, "catirq");
	if (esc.cat_irq < 0) {
		dev_err(&pdev->dev, "Failed to obtain catirq IRQ\n");
		return esc.cat_irq;
	}

	ret = devm_request_irq(&pdev->dev, esc.cat_irq,
			       esc_catirq_interrupt, 0,
			       dev_name(&pdev->dev), &esc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request catirq IRQ\n");
		return ret;
	}

	/*Creating work*/
	INIT_WORK(&esc.workqueue, workqueue_fn);
	mutex_init(&esc.mutex);

	ret = sysfs_create_group(&pdev->dev.kobj, &buffer_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs: %d\n", ret);
		return ret;
	}

	mdio = of_get_child_by_name(dev->of_node, "mdio");
	if (of_device_is_available(mdio)) {
		ret = esc_probe_mdio(mdio);
		if (ret) {
			of_node_put(mdio);
			dev_err(dev, "Failed to register MDIO: %d\n", ret);
			return ret;
		}
	}

	of_node_put(mdio);

	ed = &esc.ed;
	ed->dev = dev;
	ed->max_num_ports = ESC_MAX_PORTS_NUM;
	ed->ops = &rzesc_device_ops;
	ed->priv = &esc;

	ret = esc_register(ed);
	if (ret) {
		dev_err(dev, "Failed to register ESC: %d\n", ret);
		return ret;
	}

	dev_info(dev, "ESC probed OK\n");

	return 0;
}

static int esc_remove(struct platform_device *pdev)
{
	esc_pcs_free();
	clk_disable_unprepare(esc.clk);
	reset_control_assert(esc.rst_bus);
	reset_control_assert(esc.rst_ip);
	ethss_esc_reset_out(esc.ethss, 1);
	cancel_work_sync(&esc.workqueue);
	sysfs_remove_group(&pdev->dev.kobj, &buffer_attr_group);

	return 0;
}

static const struct of_device_id esc_of_mtable[] = {
	{ .compatible = "renesas,rzt2h-esc", },
	{ .compatible = "renesas,rzn2h-esc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, esc_of_mtable);

static struct platform_driver esc_driver = {
	.driver = {
		.name	 = "rzt2h_esc",
		.of_match_table = of_match_ptr(esc_of_mtable),
	},
	.probe = esc_probe,
	.remove = esc_remove,
};
module_platform_driver(esc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas RZ/T2H EtherCat driver");
MODULE_AUTHOR("LongLuu <long.luu.ur@renesas.com>");

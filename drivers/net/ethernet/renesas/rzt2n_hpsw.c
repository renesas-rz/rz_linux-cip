// SPDX-License-Identifier: GPL-2.0
/* Renesas RZ/T2N HSR/PRP Switch (HPSW)
 *
 * Copyright (C) Renesas Electronics Corp.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/netdevice.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/of_net.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/net/renesas/rzt2-ethss.h>

#include "rzt2n_hpsw.h"

static int rzt2n_hpsw_release_bus_stop(struct device *dev)
{
	void __iomem *base;
	u32 value;
	int ret;

	/* Map SSTPCR7 : Slave Stop Control Register 7 */
	base = ioremap(0x8129020C, 0x4);
	if (!base) {
		dev_err(dev, "Failed to ioremap SSTPCR7\n");
		return -ENOMEM;
	}

	/* Clear bit to release HPSW from Bus Stop Request State */
	value = readl(base);
	writel(value & ~BIT(16), base);

	/* Polling ACK bit until it changes to 0 */
	ret = readl_poll_timeout(base, value, !(value & BIT(17)), 100, 250000);
	if (ret) {
		dev_err(dev, "Timeout waiting for HPSW ACK\n");
		return ret;
	}

	return 0;
}

static const char *rzt2n_hpsw_mode_str(u8 mode)
{
	switch (mode) {
	case 2:
		return "HSR";
	case 1:
		return "PRP";
	case 0:
		return "No profile";
	default:
		return "Unknown";
	}
}

static void rzt2n_hpsw_setup_mac(struct rzt2n_hpsw *hpsw)
{
	u32 mac1 = (hpsw->mac_addr[3] << 24) |
		   (hpsw->mac_addr[2] << 16) |
		   (hpsw->mac_addr[1] << 8)  |
		   (hpsw->mac_addr[0]);
	u32 mac2 = (hpsw->mac_addr[5] << 8)  |
		   (hpsw->mac_addr[4]);

	rzt2n_hpsw_write(hpsw, HPSW_REG_MAC1, mac1);
	rzt2n_hpsw_write(hpsw, HPSW_REG_MAC2, mac2);
	rzt2n_hpsw_write(hpsw, HPSW_REG_MACCTL, HPSW_MACCTL_MACVAL);
}

static int rzt2n_hpsw_get_mac_fallback(struct rzt2n_hpsw *hpsw)
{
	eth_random_addr(hpsw->mac_addr);

	return 0;
}

static void rzt2n_hpsw_setup_mode(struct rzt2n_hpsw *hpsw)
{
	u32 cfg = 0;

	cfg |= (hpsw->mode & HPSW_CFGMODE_MODE_MASK);
	cfg |= ((u32)hpsw->redbox_id & 0xF) << HPSW_CFGMODE_REDBOXID_SHIFT;

	if (hpsw->cut_through)
		cfg |= HPSW_CFGMODE_CUTTHROUGH;

	rzt2n_hpsw_write(hpsw, HPSW_REG_CFGMODE, cfg);
}

static void rzt2n_hpsw_clear_counters(struct rzt2n_hpsw *hpsw)
{
	rzt2n_hpsw_write(hpsw, HPSW_REG_CNTCTL, BIT(0)); /* CLR */
}

static void rzt2n_hpsw_pcs_free(struct rzt2n_hpsw *hpsw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hpsw->pcs); i++) {
		if (hpsw->pcs[i])
			ethss_destroy(hpsw->pcs[i]);
	}
}

static int rzt2n_hpsw_pcs_get(struct rzt2n_hpsw *hpsw)
{
	struct device_node *ports, *port, *pcs_node;
	struct phylink_pcs *pcs;
	struct ethss_port *ethss_port;
	phy_interface_t interface;
	unsigned long advertising = 0;
	int ret;
	u32 reg;

	ports = of_get_child_by_name(hpsw->dev->of_node, "ethernet-ports");
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

		if (reg >= ARRAY_SIZE(hpsw->pcs)) {
			ret = -ENODEV;
			goto free_pcs;
		}

		pcs = ethss_create(hpsw->dev, pcs_node);
		if (IS_ERR(pcs)) {
			dev_err(hpsw->dev,
				"Failed to create PCS for port %d\n", reg);
			ret = PTR_ERR(pcs);
			goto free_pcs;
		}

		hpsw->pcs[reg] = pcs;
		ethss_port = phylink_pcs_to_ethss_port(pcs);
		hpsw->ethss = ethss_port->ethss;
		of_node_put(pcs_node);

		ret = of_get_phy_mode(port, &interface);
		if (ret)
			interface = PHY_INTERFACE_MODE_RGMII;

		if (pcs->ops && pcs->ops->pcs_config) {
			ret = pcs->ops->pcs_config(pcs, MLO_AN_PHY, interface,
						   &advertising, true);
			if (ret)
				dev_warn(hpsw->dev,
					 "pcs_config(port%u) ret=%d\n",
					 reg, ret);
		}

		if (pcs->ops && pcs->ops->pcs_link_up)
			pcs->ops->pcs_link_up(pcs, 1000, interface, false, false);
	}
	of_node_put(ports);

	return 0;

free_pcs:
	of_node_put(pcs_node);
	of_node_put(port);
	of_node_put(ports);
	rzt2n_hpsw_pcs_free(hpsw);

	return ret;
}

static void rzt2n_hpsw_check_sv_timeout(struct rzt2n_hpsw *hpsw, u32 status)
{
	u32 prev = hpsw->sv_last_status;

	if ((status & HPSW_STATUS_TOA) && !(prev & HPSW_STATUS_TOA))
		dev_warn(hpsw->dev, "SV-A Timeout\n");

	if ((status & HPSW_STATUS_TOB) && !(prev & HPSW_STATUS_TOB))
		dev_warn(hpsw->dev, "SV-B Timeout\n");

	hpsw->sv_last_status = status;
}

static ssize_t REDHsrPrpStatusRaw_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_status_read(hpsw);

	return sysfs_emit(buf, "0x%08x\n", val);
}
static DEVICE_ATTR_RO(REDHsrPrpStatusRaw);

static ssize_t REDHsrPrpTimeoutA_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_status_read(hpsw);

	return sysfs_emit(buf, "%u\n", !!(val & HPSW_STATUS_TOA));
}

static ssize_t REDHsrPrpTimeoutA_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "0") || sysfs_streq(buf, "clear")) {
		rzt2n_hpsw_status_clear_timeout(hpsw, true, false);
		return count;
	}

	return -EINVAL;
}
static DEVICE_ATTR_RW(REDHsrPrpTimeoutA);

static ssize_t REDHsrPrpTimeoutB_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_status_read(hpsw);

	return sysfs_emit(buf, "%u\n", !!(val & HPSW_STATUS_TOB));
}

static ssize_t REDHsrPrpTimeoutB_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "0") || sysfs_streq(buf, "clear")) {
		rzt2n_hpsw_status_clear_timeout(hpsw, false, true);
		return count;
	}

	return -EINVAL;
}
static DEVICE_ATTR_RW(REDHsrPrpTimeoutB);

static ssize_t REDHsrPrpLinkStateA_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_status_read(hpsw);

	rzt2n_hpsw_check_sv_timeout(hpsw, val);

	return sysfs_emit(buf, "%u\n", !!(val & HPSW_STATUS_LINKA));
}
static DEVICE_ATTR_RO(REDHsrPrpLinkStateA);

static ssize_t REDHsrPrpLinkStateB_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_status_read(hpsw);

	rzt2n_hpsw_check_sv_timeout(hpsw, val);

	return sysfs_emit(buf, "%u\n", !!(val & HPSW_STATUS_LINKB));
}
static DEVICE_ATTR_RO(REDHsrPrpLinkStateB);

static ssize_t REDHsrPrpLinkStateC_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_status_read(hpsw);

	rzt2n_hpsw_check_sv_timeout(hpsw, val);

	return sysfs_emit(buf, "%u\n", !!(val & HPSW_STATUS_LINKC));
}
static DEVICE_ATTR_RO(REDHsrPrpLinkStateC);

static ssize_t REDHsrPrpVersion_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_read(hpsw, HPSW_REG_VERSION);

	return sysfs_emit(buf, "RED HsrPrp Version 0x%x\n", val);
}
static DEVICE_ATTR_RO(REDHsrPrpVersion);

static ssize_t REDHsrPrpMacAddress_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%pM\n", hpsw->mac_addr);
}
static DEVICE_ATTR_RO(REDHsrPrpMacAddress);

static ssize_t REDHsrPrpMode_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u32 val = rzt2n_hpsw_read(hpsw, HPSW_REG_CFGMODE);
	u32 mode = FIELD_GET(GENMASK(2, 0), val);
	const char *name;

	switch (mode) {
	case 0:
		name = "No profile";
		break;
	case 1:
		name = "PRP";
		break;
	case 2:
		name = "HSR";
		break;
	default:
		name = "Unknown";
		break;
	}

	return sysfs_emit(buf, "%u (%s)\n", mode, name);
}

static ssize_t REDHsrPrpMode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev);
	u8 new_mode = 0;
	int ret = 0;

	if (sysfs_streq(buf, "hsr") || sysfs_streq(buf, "HSR") ||
	    sysfs_streq(buf, "2"))
		new_mode = 2;
	else if (sysfs_streq(buf, "prp") || sysfs_streq(buf, "PRP") ||
		 sysfs_streq(buf, "1"))
		new_mode = 1;
	else if (sysfs_streq(buf, "0"))
		new_mode = 0;
	else
		return -EINVAL;

	rzt2n_hpsw_write(hpsw, HPSW_REG_CTRL, 0);
	hpsw->mode = new_mode;
	rzt2n_hpsw_setup_mode(hpsw);
	rzt2n_hpsw_clear_counters(hpsw);
	rzt2n_hpsw_write(hpsw, HPSW_REG_CTRL, HPSW_CTRL_EN);
	dev_info(hpsw->dev, "HPSW running in mode 0x%x: %s\n",
		 hpsw->mode, rzt2n_hpsw_mode_str(hpsw->mode));

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(REDHsrPrpMode);

/* Per-port RX/TX (n=0:A,1:B,2:C) */
#define HPSW_DEVATTR_RXTX(_name, _off) \
static ssize_t _name##_show(struct device *dev, \
			    struct device_attribute *attr, \
			    char *buf) \
{ \
	struct rzt2n_hpsw *hpsw = dev_get_drvdata(dev); \
	u32 val = rzt2n_hpsw_read(hpsw, _off); \
	return sysfs_emit(buf, "%u\n", val); \
} \
static DEVICE_ATTR_RO(_name)

/* Frame Count */
HPSW_DEVATTR_RXTX(REDHsrPrpRxA, HPSW_REG_RXCNT(0));
HPSW_DEVATTR_RXTX(REDHsrPrpTxA, HPSW_REG_TXCNT(0));
HPSW_DEVATTR_RXTX(REDHsrPrpRxB, HPSW_REG_RXCNT(1));
HPSW_DEVATTR_RXTX(REDHsrPrpTxB, HPSW_REG_TXCNT(1));
HPSW_DEVATTR_RXTX(REDHsrPrpRxC, HPSW_REG_RXCNT(2));
HPSW_DEVATTR_RXTX(REDHsrPrpTxC, HPSW_REG_TXCNT(2));

/* Error Count */
HPSW_DEVATTR_RXTX(REDHsrPrpRxErrA, HPSW_REG_RXERR(0));
HPSW_DEVATTR_RXTX(REDHsrPrpTxErrA, HPSW_REG_TXERR(0));
HPSW_DEVATTR_RXTX(REDHsrPrpRxErrB, HPSW_REG_RXERR(1));
HPSW_DEVATTR_RXTX(REDHsrPrpTxErrB, HPSW_REG_TXERR(1));
HPSW_DEVATTR_RXTX(REDHsrPrpRxErrC, HPSW_REG_RXERR(2));
HPSW_DEVATTR_RXTX(REDHsrPrpTxErrC, HPSW_REG_TXERR(2));

static struct attribute *register_attrs[] = {
	&dev_attr_REDHsrPrpStatusRaw.attr,
	&dev_attr_REDHsrPrpTimeoutA.attr,
	&dev_attr_REDHsrPrpTimeoutB.attr,
	&dev_attr_REDHsrPrpVersion.attr,
	&dev_attr_REDHsrPrpLinkStateA.attr,
	&dev_attr_REDHsrPrpLinkStateB.attr,
	&dev_attr_REDHsrPrpLinkStateC.attr,
	&dev_attr_REDHsrPrpMacAddress.attr,
	&dev_attr_REDHsrPrpMode.attr,
	&dev_attr_REDHsrPrpRxA.attr, &dev_attr_REDHsrPrpTxA.attr,
	&dev_attr_REDHsrPrpRxB.attr, &dev_attr_REDHsrPrpTxB.attr,
	&dev_attr_REDHsrPrpRxC.attr, &dev_attr_REDHsrPrpTxC.attr,
	&dev_attr_REDHsrPrpRxErrA.attr, &dev_attr_REDHsrPrpTxErrA.attr,
	&dev_attr_REDHsrPrpRxErrB.attr, &dev_attr_REDHsrPrpTxErrB.attr,
	&dev_attr_REDHsrPrpRxErrC.attr, &dev_attr_REDHsrPrpTxErrC.attr,
	NULL,
};

static const struct attribute_group hpsw_group = {
	.name  = "registers",
	.attrs = register_attrs,
};

static int rzt2n_hpsw_init(struct rzt2n_hpsw *hpsw)
{
	struct device_node *np = hpsw->dev->of_node;
	u8 mac[ETH_ALEN];
	u32 cfg = 0;

	/* defaults */
	hpsw->mode		= 0;	/* HPSW Operating Profile */
	hpsw->cut_through	= 1;	/* Enable Cut Through Frame */
	hpsw->no_forward	= 0;	/* Enable Forward between Port A/B */

	cfg |= (u32)(hpsw->mode & 0x7);

	if (hpsw->no_forward)
		cfg |= HPSW_CFGMODE_NOFORWARD;

	if (hpsw->cut_through)
		cfg |= HPSW_CFGMODE_CUTTHROUGH;

	rzt2n_hpsw_write(hpsw, HPSW_REG_CFGMODE, cfg);

	if (!of_get_mac_address(np, mac))
		ether_addr_copy(hpsw->mac_addr, mac);

	return 0;
}

static int rzt2n_hpsw_probe(struct platform_device *pdev)
{
	struct rzt2n_hpsw *hpsw;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	hpsw = devm_kzalloc(&pdev->dev, sizeof(*hpsw), GFP_KERNEL);
	if (!hpsw)
		return -ENOMEM;
	hpsw->dev = &pdev->dev;
	spin_lock_init(&hpsw->reg_lock);
	platform_set_drvdata(pdev, hpsw);

	hpsw->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hpsw->base)) {
		ret = PTR_ERR(hpsw->base);
		return ret;
	}

	/* Get clock */
	hpsw->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(hpsw->clk)) {
		ret = PTR_ERR(hpsw->clk);
		dev_err(&pdev->dev, "failed to get hpsw clock: %d\n", ret);
		return ret;
	}

	/* Reset */
	clk_prepare_enable(hpsw->clk);
	hpsw->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(hpsw->rst))
		dev_err(&pdev->dev, "failed to get reset\n");

	reset_control_deassert(hpsw->rst);

	/* Release HPSW from Bus Stop Request State */
	ret = rzt2n_hpsw_release_bus_stop(&pdev->dev);
	if (ret)
		return ret;

	/* Mode valid */
	rzt2n_hpsw_write(hpsw, HPSW_REG_CFGCTL, HPSW_CFGCTL_MODEVAL);

	/* HPSW init */
	rzt2n_hpsw_init(hpsw);

	/* Set MAC address*/
	rzt2n_hpsw_get_mac_fallback(hpsw);
	rzt2n_hpsw_setup_mac(hpsw);

	/* Get pcs */
	ret = rzt2n_hpsw_pcs_get(hpsw);
	if (ret)
		goto free_pcs;

	/* Disable before programming */
	rzt2n_hpsw_write(hpsw, HPSW_REG_CTRL, 0);

	rzt2n_hpsw_setup_mode(hpsw);
	rzt2n_hpsw_clear_counters(hpsw);
	rzt2n_hpsw_write(hpsw, HPSW_REG_CTRL, HPSW_CTRL_EN);

	/* sysfs: create attributes */
	ret = sysfs_create_group(&pdev->dev.kobj, &hpsw_group);
	if (ret) {
		dev_err(&pdev->dev, "sysfs_create_group failed: %d\n", ret);
		return ret;
	}

	/* Initialize supervision cache to current status */
	hpsw->sv_last_status = rzt2n_hpsw_read(hpsw, HPSW_REG_STATUS);

	platform_set_drvdata(pdev, hpsw);

	dev_info(&pdev->dev, "HPSW running in mode 0x%x: %s\n",
		 hpsw->mode, rzt2n_hpsw_mode_str(hpsw->mode));
	dev_info(&pdev->dev, "HPSW Switch probed\n");

	return 0;

free_pcs:
	rzt2n_hpsw_pcs_free(hpsw);
	return ret;

}

static void rzt2n_hpsw_remove(struct platform_device *pdev)
{
	struct rzt2n_hpsw *hpsw = platform_get_drvdata(pdev);

	rzt2n_hpsw_pcs_free(hpsw);
	sysfs_remove_group(&pdev->dev.kobj, &hpsw_group);
	clk_disable_unprepare(hpsw->clk);
}

static const struct of_device_id rzt2n_hpsw_of_match[] = {
	{ .compatible = "renesas,rzt2n-hpsw" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzt2n_hpsw_of_match);

static struct platform_driver rzt2n_hpsw_driver = {
	.driver = {
		.name		= "rzt2n_hpsw",
		.of_match_table	= rzt2n_hpsw_of_match,
	},
	.probe	= rzt2n_hpsw_probe,
	.remove	= rzt2n_hpsw_remove,
};
module_platform_driver(rzt2n_hpsw_driver);

MODULE_DESCRIPTION("Renesas RZ/T2N HSR/PRP Switch (HPSW)");
MODULE_AUTHOR("Nhat Nguyen <nhat.nguyen.xb@renesas.com>");
MODULE_LICENSE("GPL");

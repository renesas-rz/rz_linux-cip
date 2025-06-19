/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef __RZT2_ETHSS_H__
#define __RZT2_ETHSS_H__

/**
 * struct ethss - MII converter structure
 * @base: base address of the MII converter
 * @dev: Device associated to the MII converter
 * @lock: Lock used for read-modify-write access
 */
struct ethss {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
	struct reset_control *rst_ethss;
	struct reset_control *rst_conv;
	spinlock_t lock;
};

struct phylink;
struct device_node;

struct phylink_pcs *ethss_create(struct device *dev, struct device_node *np);
void ethss_destroy(struct phylink_pcs *pcs);

#endif /* __RZT2_ETHSS_H__ */

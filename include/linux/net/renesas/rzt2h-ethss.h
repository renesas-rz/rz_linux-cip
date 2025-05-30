/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef __RZT2H_ETHSS_H__
#define __RZT2H_ETHSS_H__

/**
 * struct ethss - MII converter structure
 * @base: base address of the MII converter
 * @dev: Device associated to the MII converter
 * @lock: Lock used for read-modify-write access
 */
struct ethss {
	void __iomem *base;
	void __iomem *ethsw_base;
	struct device *dev;
	struct clk *clk;
	struct reset_control *rst_ethss;
	struct reset_control *rst_conv;
	spinlock_t lock;
};

/**
 * struct ethss_port - Per port MII converter struct
 * @ethss: backiling to MII converter structure
 * @port: port number
 * @interface: interface mode of the port
 */
struct ethss_port {
	struct ethss *ethss;
	struct phylink_pcs pcs;
	int port;
	phy_interface_t interface;
};

static inline struct ethss_port *phylink_pcs_to_ethss_port(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct ethss_port, pcs);
}

struct phylink;
struct device_node;

struct phylink_pcs *ethss_create(struct device *dev, struct device_node *np);
void ethss_destroy(struct phylink_pcs *pcs);
void ethss_switchcore_adjust(struct phylink_pcs *pcs, int duplex, int speed);
int ethss_gmac_ptp_timer(struct ethss *ethss, int gmac, int ethsw_timer);

/**
 * struct renesas_rzt2h_eqos - GMAC ethernet struct
 */
struct renesas_rzt2h_eqos {
	struct device *dev;
	void __iomem *regs;

	struct clk *clk;
	struct reset_control *rst_h;
	struct reset_control *rst_m;
	struct gpio_desc *reset;

	struct ethss *ethss;
	struct ethss_port *ethss_port;
	int ethsw_ptp_timer;
};

#endif /* __RZT2H_ETHSS_H__ */


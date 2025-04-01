/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef __RZT2H_ETHSS_H__
#define __RZT2H_ETHSS_H__

struct phylink;
struct device_node;

struct phylink_pcs *ethss_create(struct device *dev, struct device_node *np);

void ethss_destroy(struct phylink_pcs *pcs);

void ethss_switchcore_adjust(struct phylink_pcs *pcs, int duplex, int speed);

#endif /* __RZT2H_ETHSS_H__ */


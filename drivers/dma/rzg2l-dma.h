/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ/G2L DMA Controller Header
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 */

#ifndef _RZG2L_DMA_H
#define _RZG2L_DMA_H

#include <linux/types.h>
#include <linux/kernel.h>

struct rzg2l_dma_conf {
	const struct dmac_mod	*mod_dmac;
	unsigned int    	nids;
};

struct dmac_mod {
        const unsigned int slave_id;
        const u32 addr_slave;
        const unsigned int chcfg;
        const unsigned int dmars;
};

extern const struct rzg2l_dma_conf r9a07g044l_dma_config;

#endif

/*
 * Renesas SPI driver
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_SPI_RENESAS_SPI_H__
#define __LINUX_SPI_RENESAS_SPI_H__

enum {
	RSPI_SPI_MASTER,
	RSPI_SPI_SLAVE,
};

struct rspi_plat_data {
	unsigned int dma_tx_id;
	unsigned int dma_rx_id;

	u8 mode;
	u16 num_chipselect;
};

#endif

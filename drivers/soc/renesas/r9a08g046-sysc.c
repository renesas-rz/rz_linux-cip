// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3L System controller driver
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/io.h>

#include <linux/soc/renesas/rz-sysc.h>

#include "rz-sysc.h"

#define SYS_IPCONT_SEL_CLONECH_OFFSET          0xe2c

enum sysc_clone_channel {
	RZ_CLONE_CH_I2C2  = 0,
	RZ_CLONE_CH_I2C3  = 1,
	RZ_CLONE_CH_SCIF3 = 4,
	RZ_CLONE_CH_SCIF4 = 5,
	RZ_CLONE_CH_SCIF5 = 6,
	RZ_CLONE_CH_RSPI1 = 8,
	RZ_CLONE_CH_RSPI2 = 9,
	RZ_CLONE_CH_RSCI1  = 12,
	RZ_CLONE_CH_RSCI2  = 13,
	RZ_CLONE_CH_RSCI3  = 14,
};

static const u32 valid_channels[] = {
	RZ_CLONE_CH_I2C2, RZ_CLONE_CH_I2C3,
	RZ_CLONE_CH_SCIF3, RZ_CLONE_CH_SCIF4, RZ_CLONE_CH_SCIF5,
	RZ_CLONE_CH_RSPI1, RZ_CLONE_CH_RSPI2,
	RZ_CLONE_CH_RSCI1, RZ_CLONE_CH_RSCI2, RZ_CLONE_CH_RSCI3
};

static bool rzg3l_check_valid_clone_channel(u32 channel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(valid_channels); i++) {
		if (channel == valid_channels[i])
			return true;
	}

	return false;
}

int rzg3l_sysc_set_clone_channel(void *context, u32 channel)
{
	struct regmap *syscon = context;
	unsigned int clonech;
	int ret = 0;

	if (!rzg3l_check_valid_clone_channel(channel))
		return -EINVAL;

	ret = regmap_read(syscon, SYS_IPCONT_SEL_CLONECH_OFFSET, &clonech);
	if (ret)
		return ret;

	regmap_write(syscon, SYS_IPCONT_SEL_CLONECH_OFFSET,
				(clonech | (1 << channel)));

	return ret;
}

int rzg3l_sysc_get_clone_channel(void *context, u32 channel)
{
	struct regmap *syscon = context;
	unsigned int clonech;
	int ret, bit;

	ret = regmap_read(syscon, SYS_IPCONT_SEL_CLONECH_OFFSET, &clonech);
	if (ret)
		return ret;

	if (rzg3l_check_valid_clone_channel(channel)) {
		bit = channel;
		if (clonech & (1 << bit))
			return channel;
		else
			return -1;
	} else
		return -EINVAL;
}

static const struct rz_sysc_soc_id_init_data rzg3l_sysc_soc_id_init_data __initconst = {
	.family = "RZ/G3L",
	.id = 0x87d9447,
	.devid_offset = 0xa04,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
};

const struct rz_sysc_init_data rzg3l_sysc_init_data = {
	.soc_id_init_data = &rzg3l_sysc_soc_id_init_data,
	.max_register = 0xe2c,
};

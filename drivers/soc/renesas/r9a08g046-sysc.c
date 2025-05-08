// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3L System controller driver
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/bits.h>
#include <linux/init.h>

#include "rz-sysc.h"

static const struct rz_sysc_soc_id_init_data rzg3l_sysc_soc_id_init_data __initconst = {
	.family = "RZ/G3L",
	.id = 0x87d9447,
	.devid_offset = 0xa04,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
};

const struct rz_sysc_init_data rzg3l_sysc_init_data = {
	.soc_id_init_data = &rzg3l_sysc_soc_id_init_data,
	.max_register = 0xe24,
};

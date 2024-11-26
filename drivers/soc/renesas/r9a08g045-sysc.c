// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3S System controller driver
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/kernel.h>
#include <linux/bits.h>
#include <linux/init.h>

#include "rz-sysc.h"

#define SYS_USB_PWRRDY		0xd70
#define SYS_USB_PWRRDY_PWRRDY_N	BIT(0)
#define SYS_MAX_REG		0xe20

static const struct rz_sysc_signal_init_data rzg3s_sysc_signals_init_data[] __initconst = {
	{
		.name = "usb-pwrrdy",
		.offset = SYS_USB_PWRRDY,
		.mask = SYS_USB_PWRRDY_PWRRDY_N,
		.refcnt_incr_val = 0
	}
};

const struct rz_sysc_init_data rzg3s_sysc_init_data = {
	.signals_init_data = rzg3s_sysc_signals_init_data,
	.num_signals = ARRAY_SIZE(rzg3s_sysc_signals_init_data),
	.max_register_offset = SYS_MAX_REG,
};

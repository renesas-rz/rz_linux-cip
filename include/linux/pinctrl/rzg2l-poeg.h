/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_RENESAS_RZG2L_POEG_H__
#define __LINUX_RENESAS_RZG2L_POEG_H__

#include <linux/types.h>

#define RZG2L_POEG_OUTPUT_DISABLE_USR_DISABLE_CMD	0
#define RZG2L_POEG_OUTPUT_DISABLE_USR_ENABLE_CMD	1

struct poeg_cmd {
	__u32 val;
	__u8 channel;
};

#endif /* __LINUX_RENESAS_RZG2L_POEG_H__ */

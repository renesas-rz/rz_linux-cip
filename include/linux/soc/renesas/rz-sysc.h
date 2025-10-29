/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SOC_RENESAS_RZ_SYSC_H__
#define __LINUX_SOC_RENESAS_RZ_SYSC_H__

#include <linux/regmap.h>
#include <linux/device.h>
#include <linux/err.h>

#ifdef CONFIG_SYSC_R9A08G046
/**
 * rzg3l_sysc_set_clone_channel - Enable a clone channel in RZ/G3L Socs
 * @context: pointer to struct rz_syscon containing syscon context
 * @channel: channel from 0-15 to enable, corresponding to bit
 *          position in SYS_IPCONT_SEL_CLONECH
 * @return: return 0 is setting successfully, otherwise return negative
 * 	    if channel is not supported.
 */
int rzg3l_sysc_set_clone_channel(void *context, u32 channel);

/**
 * rzg3l_sysc_get_clone_channel - Get the active clone channel from sysc register
 * @context: pointer to struct rz_syscon containing syscon context
 * @return: channel number (0-15) if a single bit is active, -EINVAL otherwise
 */
int rzg3l_sysc_get_clone_channel(void *context, u32 channel);
#endif

#endif /* __LINUX_SOC_RENESAS_RZ_SYSC_H__ */

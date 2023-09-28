// SPDX-License-Identifier: GPL-2.0 OR MIT

#ifndef __ICU_T2H_H__
#define __ICU_T2H_H__

int register_dmac_req_signal(struct platform_device *icu_dev,
					unsigned int dmac, unsigned int channel, int dmac_req);

#endif


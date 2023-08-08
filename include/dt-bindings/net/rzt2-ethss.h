/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2022 Schneider-Electric
 *
 * Long Luu <long.luu.ur@renesas.com>
 */

#ifndef _DT_BINDINGS_RZT2_ETHSS
#define _DT_BINDINGS_RZT2_ETHSS

/*
 * Reefer to the datasheet Internal Connection of Ethernet
 * Ports to check the available combination
 */

#define ETHSS_GMAC0_PORT		0
#define ETHSS_GMAC1_PORT		1
#define ETHSS_GMAC2_PORT		2
#define ETHSS_ETHERCAT_PORT0		3
#define ETHSS_ETHERCAT_PORT1		4
#define ETHSS_ETHERCAT_PORT2		5
#define ETHSS_SWITCH_PORT0		6
#define ETHSS_SWITCH_PORT1		7
#define ETHSS_SWITCH_PORT2		8
#define ETHSS_ESWM_PORT0		9
#define ETHSS_ESWM_PORT1		10
#define ETHSS_HPSW_PORTA		11
#define ETHSS_HPSW_PORTB		12
#define ETHSS_HPSW_PORTC		13

#endif

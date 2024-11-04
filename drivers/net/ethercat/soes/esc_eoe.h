/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/** \file
 * \brief
 * Headerfile for esc_eoe.c
 */

#ifndef __esc_eoe__
#define __esc_eoe__

#include "include/sys/gcc/cc.h"

struct eoe_pbuf_t {
	/** Pointer to frame buffer type used by a TCP/IP stack. (Not mandatory) */
	void *pbuf;
	/** Pointer to frame buffer to send or read from */
	u8 *payload;
	/** Length of data in frame buffer */
	size_t len;
};

struct eoe_cfg_t {
	/** Callback function to get a frame buffer for storage of received frame */
	void (*get_buffer)(struct eoe_pbuf_t *ebuf);
	/** Callback function to free a frame buffer */
	void (*free_buffer)(struct eoe_pbuf_t *ebuf);
	/** Callback function to read local settings and update EtherCAT variables
	 *  to be delivered to the EtherCAT Master
	 */
	int (*load_eth_settings)(void);
	/** Callback function to read settings provided by the EtherCAT master
	 * and store to local settings.
	 */
	int (*store_ethernet_settings)(void);
	/** Callback to frame receive function in TCP(IP stack,
	 *  caller should free the buffer
	 */
	void (*handle_recv_buffer)(u8 port, struct eoe_pbuf_t *ebuf);
	/** Callback to fetch a buffer to send */
	int (*fetch_send_buffer)(u8 port, struct eoe_pbuf_t *ebuf);
	/** Callback to notify the application fragment sent */
	void (*fragment_sent_event)(void);
};

int EOE_ecat_get_mac(u8 port, u8 mac[]);
int EOE_ecat_get_ip(u8 port, u32 *ip);
int EOE_ecat_get_subnet(u8 port, u32 *subnet);
int EOE_ecat_get_gateway(u8 port, u32 *default_gateway);
int EOE_ecat_get_dns_ip(u8 port, u32 *dns_ip);
int EOE_ecat_get_dns_name(u8 port, char *dns_name);
int EOE_ecat_set_mac(u8 port, u8 mac[]);
int EOE_ecat_set_ip(u8 port, u32 ip);
int EOE_ecat_set_subnet(u8 port, u32 subnet);
int EOE_ecat_set_gateway(u8 port, u32 default_gateway);
int EOE_ecat_set_dns_ip(u8 port, u32 dns_ip);
int EOE_ecat_set_dns_name(u8 port, char *dns_name);

void EOE_config(struct eoe_cfg_t *cfg);
void EOE_init(void);
void ESC_eoeprocess(void);
void ESC_eoeprocess_tx(void);

#endif

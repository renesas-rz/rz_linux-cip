/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/** \file
 * \brief
 * Headerfile for esc_eep.c
 */

#ifndef __esc_eep__
#define __esc_eep__

#include "include/sys/gcc/cc.h"
#include "esc.h"

/* EEPROM commands */
#define EEP_CMD_IDLE                    0x0
#define EEP_CMD_READ                    0x1
#define EEP_CMD_WRITE                   0x2
#define EEP_CMD_RELOAD                  0x4

/* write size */
#define EEP_WRITE_SIZE                  2

/* EEPROm word offset */
#define EEP_CONFIG_ALIAS_WORD_OFFSET    4

/* CONSTAT register content */
struct eep_stat_t {
	union {
		u16 reg;
		struct {
			u8 wrEnable:1;
			u8 reserved:4;
			u8 eeEmulated:1;
			u8 eightByteRead:1;
			u8 twoByteAddr:1;
			u8 cmdReg:3;
			u8 csumErr:1;
			u8 eeLoading:1;
			u8 ackErr:1;
			u8 wrErr:1;
			u8 busy:1;
		} bits;
	} contstat;

	u32 addr;
} CC_PACKED;

/**
 * ECAT EEPROM configuration area data structure
 */
union eep_config_t {
	struct {
		u16 pdi_control;
		u16 pdi_configuration;
		u16 sync_impulse_len;
		u16 pdi_configuration2;
		u16 configured_station_alias;
		u8  reserved[4];
		u16 checksum;
	};
	u32 dword[4];
	/**< Four 32 bit double word equivalent
	 * to 8 16 bit configuration area word.
	 */
};

/* periodic task */
void EEP_process(void);

/**
 * Application Notes: EEPROM emulation
 *
 * NOTE: Special handling needed when 4 Byte read is supported.
 *
 * Ref. ET1100 Datasheet sec2_registers_3i0, chapter 2.45.1,
 * "EEPROM emulation with 32 bit EEPROM data register (0x0502[6]=0)".
 *
 * For a Reload command, fill the EEPROM Data register with the
 * values shown in the chapter 2.45.1 before acknowledging
 * the command. These values are automatically transferred to the
 * designated registers after the Reload command is acknowledged.
 *
 * NOTE: When 4 Byte read is supported, EEP_process will only load
 * config alias on reload.
 *
 * NOTE: EEP_process support implementing a custom reload function
 * for both 4 Byte and 8 Byte read support.
 *
 * NOTE: Code snippet for custom reload function when 4 Byte read is supported.
 *
 * void reload_ptr(struct eep_stat_t *stat)
 * {
 *     eep_config_t ee_cfg;
 *
 *     // Read configuration area
 *     EEP_read(0, &ee_cfg, sizeof(ee_cfg);
 *
 *     // Check CRC
 *     if(is_crc_ok(&ee_cfg) == true)
 *     {
 *        // Write config alias to EEPROM data registers.
 *        // Will be loaded to 0x12:13 on command ack.
 *        ESC_write(ESCREG_EEDATA,
 *             &ee_cfg.configured_station_alias,
 *             sizeof(configured_station_alias));
 *     }
 *     else
 *     {
 *        // Indicate CRC error
 *        stat->contstat.bits.csumErr = 1;
 *        stat->contstat.bits.ackErr = 1;
 *     }
 * }
 * NOTE: Code snippet for custom reload function when 8 Byte read is supported.
 *
 * void reload_ptr(struct eep_stat_t *stat)
 * {
 *     eep_config_t ee_cfg;
 *
 *     // Read configuration area
 *     EEP_read(0, &ee_cfg, sizeof(ee_cfg);
 *
 *     // Check CRC
 *     if(is_crc_ok(&ee_cfg) == true)
 *     {
 *         // Load EEPROM data at requested EEPROM address
 *         EEP_read (stat->addr * sizeof(u16), eep_buf, 8U);
 *         // Write loaded data to EEPROM data registers
 *         ESC_write(ESCREG_EEDATA, eep_buf, 8U);
 *     }
 *     else
 *     {
 *        // Indicate CRC error
 *        stat->contstat.bits.csumErr = 1;
 *        stat->contstat.bits.ackErr = 1;
 *     }
 * }
 */

/* Set eep internal variables */
void EEP_set_read_size(u16 read_size);
void EEP_set_reload_function_pointer(void (*reload_ptr)(struct eep_stat_t *stat));

/* From hardware file */
void EEP_init(void);
s8 EEP_read(u32 addr, u8 *data, u16 size);
s8 EEP_write(u32 addr, u8 *data, u16 size);

#endif

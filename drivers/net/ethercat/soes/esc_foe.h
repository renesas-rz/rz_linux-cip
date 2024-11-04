/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/** \file
 * \brief
 * Headerfile for esc_foe.c
 */

#ifndef __esc_foe__
#define __esc_foe__

#include "include/sys/gcc/cc.h"

/** Maximum number of characters allowed in a file name. */
#define FOE_FN_MAX      31

struct foe_file_cfg_t {
	/** Name of file to receive from master */
	const char *name;
	/** Size of file,sizeof data we can recv */
	u32       max_data;
	/** Where to store the data initially */
	u32       dest_start_address;
	/** Current address during write of file */
	u32       address_offset;
	/** Calculated size of file received */
	u32       total_size;
	/** FoE password */
	u32       filepass;
	/** This file can be written only in BOOT state. Intended for FW files */
	u8        write_only_in_boot;
	/** for feature use */
	u32       padding:24;
	/** Pointer to application foe write function */
	u32       (*write_function)(struct foe_file_cfg_t *self, u8 *data, size_t length);
};

struct foe_cfg_t {
	/** Allocate static in caller func to fit buffer_size */
	u8 *fbuffer;
	/** Buffer size before we flush to destination */
	u32  buffer_size;
	/** Number of files used in firmware update */
	u32  n_files;
	/** Pointer to files configured to be used by FoE */
	struct foe_file_cfg_t *files;
};

struct _FOEvar {
	/** Current FoE state, ex. Waiting for ACK, Waiting for DATA */
	u8  foestate;
	/** Current file buffer position, evaluated against foe file buffer size
	 * when to flush
	 */
	u16 fbufposition;
	/** Frame number in read or write sequence */
	u32 foepacket;
	/** Current position in file to be handled by FoE request */
	u32 fposition;
	/** Previous position in file to be handled by FoE request */
	u32 fprevposition;
	/** End position of allocated disk space for FoE requested file  */
	u32 fend;
} CC_PACKED;

/* Initializes FoE state. */
void FOE_config(struct foe_cfg_t *cfg);
void FOE_init(void);
void ESC_foeprocess(void);

#endif

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define AT25QL128A_OP_PP_1_4_4		0x33	/* Quad page program */

/*
 * The Atmel AT25FS010/AT25FS040 parts have some weird configuration for the
 * block protection bits. We don't support them. But legacy behavior in linux
 * is to unlock the whole flash array on startup. Therefore, we have to support
 * exactly this operation.
 */
static int atmel_at25fs_lock(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	return -EOPNOTSUPP;
}

static int atmel_at25fs_unlock(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	int ret;

	/* We only support unlocking the whole flash array */
	if (ofs || len != nor->params->size)
		return -EINVAL;

	/* Write 0x00 to the status register to disable write protection */
	ret = spi_nor_write_sr_and_check(nor, 0);
	if (ret)
		dev_dbg(nor->dev, "unable to clear BP bits, WP# asserted?\n");

	return ret;
}

static int atmel_at25fs_is_locked(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	return -EOPNOTSUPP;
}

static const struct spi_nor_locking_ops atmel_at25fs_locking_ops = {
	.lock = atmel_at25fs_lock,
	.unlock = atmel_at25fs_unlock,
	.is_locked = atmel_at25fs_is_locked,
};

static void atmel_at25fs_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/*
	 * AT25QL128A supports 1S-4S-4S for both read and write.
	 * However, only 1S-4S-4S read command (0xEBh) is declared in spi-nor
	 * opcodes, 1S-4S-4S write command (0x33h) is not declared yet, so to
	 * workaround it, the fixup below is used for setting 1S-4S-4S write
	 * command.
	 */
	if (strcmp(nor->info->name, "at25ql128a") == 0) {
		if (nor->info->flags & SPI_NOR_QUAD_READ) {
			params->hwcaps.mask |= SNOR_HWCAPS_PP_1_4_4;
			spi_nor_set_pp_settings
				(&params->page_programs[SNOR_CMD_PP_1_4_4],
				 AT25QL128A_OP_PP_1_4_4,
				 SNOR_PROTO_1_4_4);
		}
	} else
		nor->params->locking_ops = &atmel_at25fs_locking_ops;
}

static const struct spi_nor_fixups atmel_at25fs_fixups = {
	.default_init = atmel_at25fs_default_init,
};

static const struct flash_info atmel_parts[] = {
	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",  INFO(0x1f6601, 0, 32 * 1024,   4, SECT_4K | SPI_NOR_HAS_LOCK)
		.fixups = &atmel_at25fs_fixups },
	{ "at25fs040",  INFO(0x1f6604, 0, 64 * 1024,   8, SECT_4K | SPI_NOR_HAS_LOCK)
		.fixups = &atmel_at25fs_fixups },

	{ "at25df041a", INFO(0x1f4401, 0, 64 * 1024,   8, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25df321",  INFO(0x1f4700, 0, 64 * 1024,  64, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25df321a", INFO(0x1f4701, 0, 64 * 1024,  64, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25df641",  INFO(0x1f4800, 0, 64 * 1024, 128, SECT_4K | SPI_NOR_HAS_LOCK) },

	/* Dialog */
	{ "at25ql128a",	 INFO(0x1f4218, 0, 64 * 1024,  256, SECT_4K | SPI_NOR_QUAD_READ)
		.fixups = &atmel_at25fs_fixups },

	{ "at25sl321",	INFO(0x1f4216, 0, 64 * 1024, 64,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },

	{ "at26f004",   INFO(0x1f0400, 0, 64 * 1024,  8, SECT_4K) },
	{ "at26df081a", INFO(0x1f4501, 0, 64 * 1024, 16, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at26df161a", INFO(0x1f4601, 0, 64 * 1024, 32, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at26df321",  INFO(0x1f4700, 0, 64 * 1024, 64, SECT_4K | SPI_NOR_HAS_LOCK) },

	{ "at45db081d", INFO(0x1f2500, 0, 64 * 1024, 16, SECT_4K) },
};

const struct spi_nor_manufacturer spi_nor_atmel = {
	.name = "atmel",
	.parts = atmel_parts,
	.nparts = ARRAY_SIZE(atmel_parts),
};

// SPDX-License-Identifier: GPL-2.0
//
// Renesas RZ ASoC Serial Sound Interface (SPDIF) Driver
//
// Copyright (C) 2023 Renesas Electronics Corp.
//

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/dmaengine.h>
#include <sound/asoundef.h>
#include <linux/dma-mapping.h>
#include "rcar/rsnd.h"

/* Register offset	*/
#define SPDIF_TLCA		0x00
#define SPDIF_TRCA		0x04
#define SPDIF_TLCS		0x08
#define SPDIF_TRCS		0x0c
#define SPDIF_TUI		0x10
#define SPDIF_RLCA		0x14
#define SPDIF_RRCA		0x18
#define SPDIF_RLCS		0x1c
#define SPDIF_RRCS		0x20
#define SPDIF_RUI		0x24
#define SPDIF_CTRL		0x28
#define SPDIF_STAT		0x2c
#define SPDIF_TDAD		0x30
#define SPDIF_RDAD		0x34

/* Stat bits		 */
#define SPDIF_RIS_BIT		(0x00008000)
#define SPDIF_TIS_BIT		(0x00004000)
#define SPDIF_UBO_BIT		(0x00002000)
#define SPDIF_UBU_BIT		(0x00001000)
#define SPDIF_CE_BIT		(0x00000800)
#define SPDIF_PARE_BIT		(0x00000400)
#define SPDIF_PREE_BIT		(0x00000200)
#define SPDIF_CSE_BIT		(0x00000100)
#define SPDIF_ABO_BIT		(0x00000080)
#define SPDIF_ABU_BIT		(0x00000040)
#define SPDIF_RUIR_BIT		(0x00000020)
#define SPDIF_TUIR_BIT		(0x00000010)
#define SPDIF_CSRX_BIT		(0x00000008)
#define SPDIF_CBRX_BIT		(0x00000004)
#define SPDIF_CSTX_BIT		(0x00000002)
#define SPDIF_CBTX_BIT		(0x00000001)

/* Ctrl bits		*/
#define SPDIF_PB_BIT		(0x04000000)
#define SPDIF_RDE_BIT		(0x00200000)
#define SPDIF_TDE_BIT		(0x00100000)
#define SPDIF_NCSI_BIT		(0x00080000)
#define SPDIF_AOS_BIT		(0x00040000)
#define SPDIF_RME_BIT		(0x00020000)
#define SPDIF_TME_BIT		(0x00010000)
#define SPDIF_REIE_BIT		(0x00008000)
#define SPDIF_TEIE_BIT		(0x00004000)
#define SPDIF_UBOI_BIT		(SPDIF_UBO_BIT)
#define SPDIF_UBUI_BIT		(SPDIF_UBU_BIT)
#define SPDIF_CREI_BIT		(0x00000800)
#define SPDIF_PAEI_BIT		(0x00000400)
#define SPDIF_PREI_BIT		(0x00000200)
#define SPDIF_ABOI_BIT		(SPDIF_ABO_BIT)
#define SPDIF_ABUI_BIT		(SPDIF_ABU_BIT)
#define SPDIF_RUII_BIT		(SPDIF_RUIR_BIT)
#define SPDIF_TUII_BIT		(SPDIF_TUIR_BIT)
#define SPDIF_RCSI_BIT		(SPDIF_CSRX_BIT)
#define SPDIF_RCBI_BIT		(SPDIF_CBRX_BIT)
#define SPDIF_TCSI_BIT		(SPDIF_CSTX_BIT)
#define SPDIF_TCBI_BIT		(SPDIF_CBTX_BIT)

#define FS_MASK			(0x0F000000)
#define FS_32K			(12 << 24)
#define FS_44K			(0 << 24)
#define FS_48K			(4 << 24)
#define CALC_MASK		(0x30000000)
#define CALC_LV2		(0 << 28)
#define CALC_LV1		(1 << 28)
#define CALC_LV3		(2 << 28)

#define WIDTH_MASK		(0xF << 22)
#define WIDTH_16		(0 << 22)
#define WIDTH_24		(0xA << 22)

#define SPDIF_NUMOF_FRM		(192)
#define SPDIF_NUMOF_CH		(2)
#define SPDIF_AUDIO_BUFSZ	(SPDIF_NUMOF_FRM * SPDIF_NUMOF_CH)
#define SPDIF_USER_BUFSZ	((SPDIF_NUMOF_FRM * SPDIF_NUMOF_CH) / 32)
#define SPDIF_CH1		(0)
#define SPDIF_CH2		(1)
/* Pre allocated buffers sizes */
#define PREALLOC_BUFFER		(SZ_32K)
#define PREALLOC_BUFFER_MAX	(SZ_32K)

#define RZ_SPDIF_RATES		(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
				SNDRV_PCM_RATE_48000)
#define RZ_SPDIF_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static uint8_t user_msg[] = "Renesas SPDIF Interface Message";

struct spdif_t {
	union {
		uint32_t data32[SPDIF_USER_BUFSZ];
		unsigned char data[SPDIF_USER_BUFSZ * 4];
	} u_buf;
	int32_t  u_idx;				/* Index of User data		*/
	uint32_t s_buf[SPDIF_NUMOF_CH];		/* Channel Status Buffer	*/
};

struct rz_spdif_stream {
	struct spdif_dev_data *priv;
	struct snd_pcm_substream *substream;
	struct dma_chan *dma_ch;
	bool running;
	int pad_period_pos;
	int pad_periods;

	u32 *dma_pad_buf_tx;
	dma_addr_t dma_pad_addr_tx;
	u32 *dma_pad_buf_rx;
	dma_addr_t dma_pad_addr_rx;
	size_t dma_buf_size;
};

struct rz_spdif_ops {
	int (*clk_init)(struct spdif_dev_data *spdif, struct device *dev);
	int (*set_clk)(struct spdif_dev_data *spdif, unsigned int rate, bool enable);
};

struct rz_spdif_soc_data {
	const struct rz_spdif_ops *ops;
	u8 max_channels;

	/* SoC-specific extra controls */
	const struct snd_kcontrol_new *extra_controls;
	unsigned int num_extra_controls;
};

struct spdif_dev_data {
	void __iomem *base;
	struct reset_control *rstc;
	struct device *dev;
	struct clk *clk;
	phys_addr_t phys;

	const struct rz_spdif_soc_data *soc_data;

	/* clock */
	unsigned long audio_clk_1;
	unsigned long audio_clk_2;

	struct device *adg_dev;
	int spdif_id;

	spinlock_t lock;

	u32 mode;
	u32 rate;
	u32 bit_width;

	struct rz_spdif_stream playback;
	struct rz_spdif_stream capture;

	struct spdif_t spdin;
	struct spdif_t spdout;

	/* for PIO */
	int count;
	int tx_byte_pos;
	int tx_byte_per_period;
	int tx_next_period_byte;

	int rx_byte_pos;
	int rx_byte_per_period;
	int rx_next_period_byte;

	bool is_dma;
	bool is_passback;
};

static void rz_spdif_dma_complete(void *data);
static int rz_spdif_dma_transfer(struct spdif_dev_data *spdif,
				 struct rz_spdif_stream *strm);
static void rz_spdif_pad_tx_period(struct spdif_dev_data *spdif,
				   struct rz_spdif_stream *strm,
				   unsigned int period);
static void rz_spdif_unpad_rx_period(struct spdif_dev_data *spdif,
				     struct rz_spdif_stream *strm,
				     unsigned int period);

static inline struct rz_spdif_stream *
rz_spdif_stream_get(struct spdif_dev_data *spdif, struct snd_pcm_substream *substream)
{
	struct rz_spdif_stream *stream = &spdif->playback;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		stream = &spdif->capture;

	return stream;
}

static void rz_spdif_set_substream(struct rz_spdif_stream *strm,
				 struct snd_pcm_substream *substream)
{
	strm->substream = substream;
	strm->pad_period_pos = 0;
	strm->pad_periods = 0;
}

static void rz_spdif_reg_writel(struct spdif_dev_data *priv, uint reg, u32 data)
{
	writel(data, (priv->base + reg));
}

static u32 rz_spdif_reg_readl(struct spdif_dev_data *priv, uint reg)
{
	return readl(priv->base + reg);
}

static void rz_spdif_status_clear(struct spdif_dev_data *spdif)
{
	rz_spdif_reg_writel(spdif, SPDIF_STAT, 0);
}

static u32 rz_spdif_status_get(struct spdif_dev_data *priv)
{
	return rz_spdif_reg_readl(priv, SPDIF_STAT) & (rz_spdif_reg_readl(priv, SPDIF_CTRL)
			& 0x00003FFF);
}

static inline bool rz_spdif_stream_is_play(struct snd_pcm_substream *substream)
{
	return substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
}

static inline bool rz_spdif_is_stream_running(struct rz_spdif_stream *strm)
{
	return strm->substream && strm->running;
}

static void rz_spdif_bset(struct spdif_dev_data *priv, uint reg, uint bit_mask, u32 data)
{
	u32 ret;

	ret = rz_spdif_reg_readl(priv, reg);
	ret = ret & ~bit_mask;
	ret = ret | data;
	rz_spdif_reg_writel(priv, reg, ret);
}

static int rz_spdif_wait_stat(struct spdif_dev_data *spdif, u32 bit, bool set)
{
	bool is_tx = bit == SPDIF_TIS_BIT;
	u32 val;
	int ret;

	ret = readl_poll_timeout_atomic(spdif->base + SPDIF_STAT, val,
					set ? (val & bit) : !(val & bit),
					10, 10000);
	if (ret)
		dev_warn(spdif->dev, "timeout waiting %s on %s\n",
			 set ? "idle" : "active",
			 is_tx ? "TX" : "RX");

	return ret;
}

static int rz_spdif_stop(struct spdif_dev_data *spdif, struct rz_spdif_stream *strm)
{
	strm->running = 0;

	/* Cancel all remaining DMA transactions */
	if (spdif->is_dma)
		dmaengine_terminate_async(strm->dma_ch);

	/* Disable SPDIF to idle state */
	if (rz_spdif_is_stream_running(&spdif->playback)) {
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TME_BIT, 0);
		rz_spdif_wait_stat(spdif, SPDIF_TIS_BIT, true);
	}
	if (rz_spdif_is_stream_running(&spdif->capture)) {
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RME_BIT, 0);
		rz_spdif_wait_stat(spdif, SPDIF_RIS_BIT, true);
	}

	return 0;
}

static void rz_spdif_pio_init(struct spdif_dev_data *spdif,
			      struct  rz_spdif_stream *strm)
{
	struct snd_pcm_runtime *runtime = strm->substream->runtime;

	spdif->tx_byte_pos		= 0;
	spdif->tx_byte_per_period	= runtime->period_size *
					  runtime->channels *
					  samples_to_bytes(runtime, 1);
	spdif->tx_next_period_byte	= spdif->tx_byte_per_period;

	spdif->rx_byte_pos		= 0;
	spdif->rx_byte_per_period	= runtime->period_size *
					  runtime->channels *
					  samples_to_bytes(runtime, 1);
	spdif->rx_next_period_byte	= spdif->rx_byte_per_period;
}

static int rz_spdif_start(struct spdif_dev_data *spdif, struct rz_spdif_stream *strm)
{
	u32 ctrl;
	int ret;

	spdif->spdout.u_idx = 0;
	spdif->spdin.u_idx = 0;
	spdif->count = 2;
	strm->running = 1;

	/* PIO init only */
	if (!spdif->is_dma)
		rz_spdif_pio_init(spdif, strm);

	if (rz_spdif_is_stream_running(&spdif->playback)) {
		/* Enable transmitter module */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TME_BIT, SPDIF_TME_BIT);
		ret = rz_spdif_wait_stat(spdif, SPDIF_TIS_BIT, false);
		if (ret) {
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TME_BIT, 0);
			return ret;
		}
	}
	if (rz_spdif_is_stream_running(&spdif->capture)) {
		/* Enable receiver module */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RME_BIT, SPDIF_RME_BIT);
		ret = rz_spdif_wait_stat(spdif, SPDIF_RIS_BIT, false);
		if (ret) {
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RME_BIT, 0);
			return ret;
		}
	}
	/* Setting TASS, RASS */
	ctrl = rz_spdif_reg_readl(spdif, SPDIF_CTRL);

	switch (spdif->bit_width) {
	case 16:
		ctrl = (ctrl & (~WIDTH_MASK)) | WIDTH_16;
		break;
	case 24:
		ctrl = (ctrl & (~WIDTH_MASK)) | WIDTH_24;
		break;
	default:
		dev_err(spdif->dev, "SPDIF only support 16,24 bit\n");
		return -EINVAL;
	}

	/* Configure SPDIF settings for passback mode */
	ctrl |= spdif->is_passback ? SPDIF_PB_BIT : 0;
	rz_spdif_reg_writel(spdif, SPDIF_CTRL, ctrl);

	return 0;
}

static int rz_spdif_irq(struct spdif_dev_data *spdif,
			struct rz_spdif_stream *strm, int enable)
{
	if (enable) {
		if (rz_spdif_is_stream_running(&spdif->playback)) {
			/* Channel status information */
			rz_spdif_reg_writel(spdif, SPDIF_TLCS, spdif->spdout.s_buf[SPDIF_CH1]);
			rz_spdif_reg_writel(spdif, SPDIF_TRCS, spdif->spdout.s_buf[SPDIF_CH2]);
			while (rz_spdif_reg_readl(spdif, SPDIF_STAT) & SPDIF_CSTX_BIT)
				;

			/* Enable interrupt (User data empty) */
			if (spdif->count > 0)
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TUII_BIT, SPDIF_TUII_BIT);

			/* Enable error interrupt */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TEIE_BIT, SPDIF_TEIE_BIT);

			if (spdif->is_dma) {
				/* Enable underrun interrupt */
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_ABUI_BIT, SPDIF_ABUI_BIT);
				/* Enable DMA transmitter */
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TDE_BIT, SPDIF_TDE_BIT);
			} else
				/* Enable transmitter interrupt */
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TCBI_BIT, SPDIF_TCBI_BIT);
		}
		if (rz_spdif_is_stream_running(&spdif->capture)) {
			/* Enable interrupt (Channel status full) */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RCSI_BIT, SPDIF_RCSI_BIT);

			/* Enable interrupt (User data full) */
			if (spdif->spdin.u_idx < SPDIF_USER_BUFSZ)
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RUII_BIT, SPDIF_RUII_BIT);

			/* Enable error interrupt */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_REIE_BIT, SPDIF_REIE_BIT);
			if (spdif->is_dma) {
				/* Enable overrun interrupt */
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_ABOI_BIT, SPDIF_ABOI_BIT);
			} else
				/* Enable receiver interrupt */
				rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RCBI_BIT, SPDIF_RCBI_BIT);
		}
	} else {
		if (rz_spdif_is_stream_running(&spdif->playback)) {
			/* Disable error interrupt */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TEIE_BIT | SPDIF_ABUI_BIT, 0);

			/* Disable interrupt  */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TCBI_BIT | SPDIF_TUII_BIT, 0);
			/* Disable DMA transmitter */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TDE_BIT, 0);
		}
		if (rz_spdif_is_stream_running(&spdif->capture)) {
			/* Disable error interrupt */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_REIE_BIT | SPDIF_ABOI_BIT, 0);

			/* Disable interrupt */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RCBI_BIT | SPDIF_RCSI_BIT |
					SPDIF_RUII_BIT, 0);
			/* Disable DMA receiver */
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RDE_BIT, 0);
		}
	}

	return 0;
}

static void rz_spdif_quit(struct rz_spdif_stream *strm)
{
	rz_spdif_set_substream(strm, NULL);
}

static int rz_spdif_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct spdif_dev_data *spdif = dev_get_drvdata(dai->dev);
	struct rz_spdif_stream *strm = rz_spdif_stream_get(spdif, substream);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		/* Set substream to priv data */
		rz_spdif_set_substream(strm, substream);

		ret = rz_spdif_start(spdif, strm);
		if (ret < 0)
			goto dai_trigger_end;

		ret = rz_spdif_irq(spdif, strm, 1);
		if (ret < 0)
			goto dai_trigger_end;

		if (spdif->is_dma) {
			ret = rz_spdif_dma_transfer(spdif, strm);
			if (ret)
				goto dai_trigger_end;
		}

		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = rz_spdif_irq(spdif, strm, 0);
		rz_spdif_stop(spdif, strm);
		/* Continue running incase of Suspending */
		if (cmd == SNDRV_PCM_TRIGGER_SUSPEND)
			strm->running = 1;
		else
			rz_spdif_quit(strm);
		break;
	default:
		ret = -EINVAL;
	}

dai_trigger_end:
	return ret;
}

static int rz_spdif_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct spdif_dev_data *spdif = snd_soc_dai_get_drvdata(dai);
	struct rz_spdif_stream *strm = rz_spdif_stream_get(spdif, substream);
	u32 channel_status1, channel_status2;
	int ret;

	spdif->rate = params_rate(params);
	spdif->bit_width = params_width(params);

	if (params_channels(params) != 2) {
		dev_err(spdif->dev, "SPDIF only support 2 channel\n");
		return -EIO;
	}

	ret = spdif->soc_data->ops->set_clk(spdif, spdif->rate, true);
	if (ret)
		return ret;

	/*	Transmitter Channel X Status Register
	 *	b31:b30 Reserved - Read only bit       - 00:
	 *	b29:b28 CLAC     - Clock Accuracy      - 01:   Level 1 (50 ppm)
	 *	b27:b24 FS       - Sample Frequency    - 0000: 44.1kHz
	 *	b23:b20 CHNO     - Channel Number      - 0001: Left (0010: Right)
	 *	b19:b16 SRCNO    - Source Number       - 0000:
	 *	b15:b8  CATCD    - Category Code       - 00000000: General 2-channel format
	 *	b7:b6   Reserved - Read only bit       - 00:
	 *	b5:b1   CTL      - Control             - 0:  2 channel Audio
	 *	b0      Reserved - Read only bit       - 0:
	 */

	channel_status1 = 0x10100000;
	channel_status2 = 0x10200000;

	switch (spdif->rate) {
	case 32000:
		channel_status1 = (channel_status1 & (~FS_MASK)) | FS_32K;
		channel_status2 = (channel_status2 & (~FS_MASK)) | FS_32K;
		break;
	case 44100:
		channel_status1 = (channel_status1 & (~FS_MASK)) | FS_44K;
		channel_status2 = (channel_status2 & (~FS_MASK)) | FS_44K;
		break;
	case 48000:
		channel_status1 = (channel_status1 & (~FS_MASK)) | FS_48K;
		channel_status2 = (channel_status2 & (~FS_MASK)) | FS_48K;
		break;
	default:
		dev_err(spdif->dev, "SPDIF only support 32,44,48 KHz\n");
		ret = -EINVAL;
		goto err_disable_clk;
	}

	/* Buffer clear */
	memset(&spdif->spdout, 0, sizeof(struct spdif_t));
	memset(&spdif->spdin,  0, sizeof(struct spdif_t));

	/* Initialize user data */
	memcpy(spdif->spdout.u_buf.data, user_msg, sizeof(user_msg));

	spdif->spdout.s_buf[SPDIF_CH1] = channel_status1;
	spdif->spdout.s_buf[SPDIF_CH2] = channel_status2;

	/*
	 * Allocate a DMA buffer used specifically for S16_LE format in DMA transfer mode.
	 * This buffer provides the required padding when converting 16-bit samples
	 * to the 32-bit data width expected by the SPDIF hardware.
	 */
	if (spdif->is_dma && spdif->bit_width == 16) {
		strm->dma_buf_size = params_period_size(params) *
				     params_channels(params) *
				     params_periods(params) * sizeof(u32);

		if (rz_spdif_stream_is_play(substream)) {
			strm->dma_pad_buf_tx = dma_alloc_coherent(spdif->dev,
								  strm->dma_buf_size,
								  &strm->dma_pad_addr_tx,
								  GFP_KERNEL);

			if (!strm->dma_pad_buf_tx)
				goto err_disable_clk;
		} else {
			strm->dma_pad_buf_rx = dma_alloc_coherent(spdif->dev,
								  strm->dma_buf_size,
								  &strm->dma_pad_addr_rx,
								  GFP_KERNEL);

			if (!strm->dma_pad_buf_rx)
				goto err_disable_clk;
		}
	}
	return 0;

err_disable_clk:
	spdif->soc_data->ops->set_clk(spdif, spdif->rate, false);

	return ret;
}

static int rz_spdif_dai_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct spdif_dev_data *spdif = snd_soc_dai_get_drvdata(dai);
	struct rz_spdif_stream *strm = rz_spdif_stream_get(spdif, substream);
	int ret;

	if (spdif->soc_data->ops && spdif->soc_data->ops->set_clk) {
		ret = spdif->soc_data->ops->set_clk(spdif, spdif->rate, false);
		if (ret)
			return ret;
	}

	if (spdif->is_dma && strm->dma_ch) {
		dmaengine_terminate_sync(strm->dma_ch);
		dmaengine_synchronize(strm->dma_ch);
	}

	/* Free padding buffer after use */
	if (spdif->is_dma && spdif->bit_width == 16) {
		if (strm->dma_pad_buf_tx) {
			dma_free_coherent(spdif->dev, strm->dma_buf_size,
					  strm->dma_pad_buf_tx, strm->dma_pad_addr_tx);
			strm->dma_pad_buf_tx = NULL;
		}
		if (strm->dma_pad_buf_rx) {
			dma_free_coherent(spdif->dev, strm->dma_buf_size,
					  strm->dma_pad_buf_rx, strm->dma_pad_addr_rx);
			strm->dma_pad_buf_rx = NULL;
		}
	}

	strm->dma_buf_size = 0;
	strm->pad_period_pos = 0;
	strm->pad_periods = 0;

	return 0;
}

static const struct snd_soc_dai_ops rz_spdif_dai_ops = {
	.trigger = rz_spdif_dai_trigger,
	.hw_params = rz_spdif_dai_hw_params,
	.hw_free = rz_spdif_dai_hw_free,
};

static const struct snd_pcm_hardware rz_spdif_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED	|
				  SNDRV_PCM_INFO_MMAP		|
				  SNDRV_PCM_INFO_MMAP_VALID,
	.buffer_bytes_max	= PREALLOC_BUFFER,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.channels_min		= 2,
	.channels_max		= 2,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 32 * 2,
};

static int rz_spdif_pcm_new(struct snd_soc_component *component,
			    struct snd_soc_pcm_runtime *rtd)
{
	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       rtd->card->snd_card->dev,
				       PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);

	return 0;
}

static snd_pcm_uframes_t rz_spdif_pcm_pointer(struct snd_soc_component *component,
					      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct spdif_dev_data *spdif = dev_get_drvdata(dai->dev);
	struct rz_spdif_stream *strm = rz_spdif_stream_get(spdif, substream);

	/* Handle buffer pos for DMA transfer */
	if (spdif->is_dma) {
		/* Fallback: period-level pointer */
		return (strm->pad_period_pos % runtime->periods) *
		       runtime->period_size;
	}

	if (rz_spdif_is_stream_running(&spdif->playback))
		return bytes_to_frames(runtime, READ_ONCE(spdif->tx_byte_pos));
	if (rz_spdif_is_stream_running(&spdif->capture))
		return bytes_to_frames(runtime, READ_ONCE(spdif->rx_byte_pos));

	return 0;
}

static int rz_spdif_pcm_open(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	snd_soc_set_runtime_hwparams(substream, &rz_spdif_pcm_hardware);

	return snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
}

static struct snd_soc_dai_driver rz_spdif_soc_dai = {
	.name = "rz_spdif_dai",
	.playback = {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= RZ_SPDIF_RATES,
		.formats	= RZ_SPDIF_FORMATS,
	},
	.capture = {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= RZ_SPDIF_RATES,
		.formats	= RZ_SPDIF_FORMATS,
	},
	.ops = &rz_spdif_dai_ops,
};

static int rz_spdif_cs_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int rz_spdif_ub_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(user_msg);

	return 0;
}

static int rz_spdif_kctrl_cs_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct device *dev = component->dev;
	struct spdif_dev_data *spdif = dev_get_drvdata(dev);

	ucontrol->value.iec958.status[0] = IEC958_AES0_CON_NOT_COPYRIGHT |
					   IEC958_AES0_CON_EMPHASIS_NONE;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_GENERAL;
	ucontrol->value.iec958.status[2] = IEC958_AES2_CON_SOURCE_UNSPEC |
					   IEC958_AES2_CON_CHANNEL_UNSPEC;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_NOTID |
					   IEC958_AES3_CON_CLOCK;

	if (spdif->spdin.s_buf[SPDIF_CH1] == 0)
		return 0;

	switch (spdif->spdin.s_buf[SPDIF_CH1] & FS_MASK) {
	case FS_32K:
		ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_32000;
		break;
	case FS_44K:
		ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_44100;
		break;
	case FS_48K:
		ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_48000;
		break;
	}

	switch (spdif->spdin.s_buf[SPDIF_CH1] & CALC_MASK) {
	case CALC_LV2:
		ucontrol->value.iec958.status[3] |= IEC958_AES3_CON_CLOCK_1000PPM;
		break;
	case CALC_LV1:
		ucontrol->value.iec958.status[3] |= IEC958_AES3_CON_CLOCK_50PPM;
		break;
	case CALC_LV3:
		ucontrol->value.iec958.status[3] |= IEC958_AES3_CON_CLOCK_VARIABLE;
		break;
	}

	return 0;
}

static int rz_spdif_kctrl_ub_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct device *dev = component->dev;
	struct spdif_dev_data *spdif = dev_get_drvdata(dev);

	memcpy(ucontrol->value.bytes.data, spdif->spdin.u_buf.data, SPDIF_USER_BUFSZ * 4);

	return 0;
}

static int rz_spdif_kctrl_passback_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct device *dev = component->dev;
	struct spdif_dev_data *spdif = dev_get_drvdata(dev);

	ucontrol->value.integer.value[0] = spdif->is_passback;

	return 0;
}

static int rz_spdif_kctrl_passback_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct device *dev = component->dev;
	struct spdif_dev_data *spdif = dev_get_drvdata(dev);
	int mode = ucontrol->value.integer.value[0];

	spdif->is_passback = mode;

	return 0;
}

static const struct snd_kcontrol_new rz_spdif_common_controls[] = {
	/* Chanel status controller */
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.access	= SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info	= rz_spdif_cs_info,
		.get	= rz_spdif_kctrl_cs_get,
	},
	/* User bits controller */
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "IEC958 User Data Receive",
		.access	= SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info	= rz_spdif_ub_info,
		.get	= rz_spdif_kctrl_ub_get,
	},
};

static const struct snd_kcontrol_new rz_spdif_rzg3s_controls[] = {
	/* Pass Back mode controller */
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "Renesas SPDIF Pass Back Mode",
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_ctl_boolean_mono_info,
		.get	= rz_spdif_kctrl_passback_get,
		.put	= rz_spdif_kctrl_passback_put,
	},
};

static int rz_spdif_component_probe(struct snd_soc_component *component)
{
	struct spdif_dev_data *spdif = snd_soc_component_get_drvdata(component);
	const struct rz_spdif_soc_data *soc = spdif->soc_data;
	int ret;

	/* Common controls */
	ret = snd_soc_add_component_controls(component,
					     rz_spdif_common_controls,
					     ARRAY_SIZE(rz_spdif_common_controls));
	if (ret)
		return ret;

	/* SoC-specific extra controls */
	if (soc->extra_controls && soc->num_extra_controls) {
		ret = snd_soc_add_component_controls(component,
						     soc->extra_controls,
						     soc->num_extra_controls);
		if (ret)
			return ret;
	}

	return 0;
}


static const struct snd_soc_component_driver rz_spdif_component = {
	.name		= "rz-spdif",
	.probe		= rz_spdif_component_probe,
	.open		= rz_spdif_pcm_open,
	.pointer	= rz_spdif_pcm_pointer,
	.pcm_construct	= rz_spdif_pcm_new,
};

static bool rz_spdif_pio_recv(int irq, struct spdif_dev_data *spdif)
{
	struct snd_pcm_runtime *runtime = spdif->capture.substream->runtime;
	snd_pcm_format_t fmt = runtime->format;
	int byte_pos;
	bool elapsed = false;

	do {
		switch (fmt) {
		case SNDRV_PCM_FORMAT_S24_LE: {
			u64 *rx_buf = (u64 *)(runtime->dma_area + spdif->rx_byte_pos);

			*rx_buf = ((u64)(rz_spdif_reg_readl(spdif, SPDIF_RLCA) & 0xFFFFFF) << 32) |
					 rz_spdif_reg_readl(spdif, SPDIF_RRCA);

			byte_pos = spdif->rx_byte_pos + sizeof(*rx_buf);
			break;
		} case SNDRV_PCM_FORMAT_S16_LE: {
			u32 *rx_buf = (u32 *)(runtime->dma_area + spdif->rx_byte_pos);

			*rx_buf = ((rz_spdif_reg_readl(spdif, SPDIF_RLCA) & 0xFFFF) << 16) |
				   (rz_spdif_reg_readl(spdif, SPDIF_RRCA) & 0xFFFF);
			byte_pos = spdif->rx_byte_pos + sizeof(*rx_buf);
			break;
		} default:
			return false;
		}

	} while (rz_spdif_reg_readl(spdif, SPDIF_STAT) & SPDIF_CBRX_BIT);

	if (byte_pos >= spdif->rx_next_period_byte) {
		int period_pos = byte_pos / spdif->rx_byte_per_period;

		if (period_pos >= runtime->periods) {
			byte_pos = 0;
			period_pos = 0;
		}
		spdif->rx_next_period_byte = (period_pos + 1) * spdif->rx_byte_per_period;

		elapsed = true;
	}

	WRITE_ONCE(spdif->rx_byte_pos, byte_pos);

	return elapsed;
}

static bool rz_spdif_pio_send(int irq, struct spdif_dev_data *spdif)
{
	struct snd_pcm_runtime *runtime = spdif->playback.substream->runtime;
	snd_pcm_format_t fmt = runtime->format;
	int byte_pos;
	bool elapsed = false;
	u32 data;

	do {
		switch (fmt) {
		case SNDRV_PCM_FORMAT_S24_LE: {
			u64 *tx_buf = (u64 *)(runtime->dma_area + spdif->tx_byte_pos);

			data = ((*tx_buf) >> 32 & 0xFFFFFF);
			rz_spdif_reg_writel(spdif, SPDIF_TLCA, data);
			data = ((*tx_buf) & 0xFFFFFF);
			rz_spdif_reg_writel(spdif, SPDIF_TRCA, data);

			byte_pos = spdif->tx_byte_pos + sizeof(*tx_buf);
			break;
		} case SNDRV_PCM_FORMAT_S16_LE: {
			u32 *tx_buf = (u32 *)(runtime->dma_area + spdif->tx_byte_pos);

			data = ((*tx_buf) >> 16 & 0xFFFF);
			rz_spdif_reg_writel(spdif, SPDIF_TLCA, data);
			data = ((*tx_buf) & 0xFFFF);
			rz_spdif_reg_writel(spdif, SPDIF_TRCA, data);

			byte_pos = spdif->tx_byte_pos + sizeof(*tx_buf);
			break;
		} default:
			return false;
		}
	} while (rz_spdif_reg_readl(spdif, SPDIF_STAT) & SPDIF_CBTX_BIT);

	if (byte_pos >= spdif->tx_next_period_byte) {
		int period_pos = byte_pos / spdif->tx_byte_per_period;

		if (period_pos >= runtime->periods) {
			byte_pos = 0;
			period_pos = 0;
		}
		spdif->tx_next_period_byte = (period_pos + 1) * spdif->tx_byte_per_period;

		elapsed = true;
	}

	WRITE_ONCE(spdif->tx_byte_pos, byte_pos);

	return elapsed;
}

static void rz_spdif_out_restart(struct spdif_dev_data *spdif)
{
	int ret;

	if (spdif->is_dma)
		/* Disable DMA transmitter */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TDE_BIT, 0);
	else
		/* Disable Transmitter interrupt */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TCBI_BIT, 0);

	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TME_BIT, 0);
	ret = rz_spdif_wait_stat(spdif, SPDIF_TIS_BIT, true);
	if (ret) {
		dev_warn(spdif->dev, "out_restart aborted: TX not idle\n");
		return;
	}

	rz_spdif_status_clear(spdif);

	/* Retransmit */
	/* Enable transmitter module */
	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TME_BIT, SPDIF_TME_BIT);
	ret = rz_spdif_wait_stat(spdif, SPDIF_TIS_BIT, false);
	if (ret) {
		dev_warn(spdif->dev, "out_restart: TX not active\n");
		return;
	}

	/* Enable interrupt (User data empty) */
	if (spdif->count > 0)
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TUII_BIT, SPDIF_TUII_BIT);

	/* Enable error interrupt */
	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TEIE_BIT, SPDIF_TEIE_BIT);

	if (spdif->is_dma) {
		/* Enable underrun interrupt */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_ABUI_BIT, SPDIF_ABUI_BIT);
		/* Enable DMA transmitter */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TDE_BIT, SPDIF_TDE_BIT);
	} else
		/* Enable transmitter interrupt */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TCBI_BIT, SPDIF_TCBI_BIT);
}

static void rz_spdif_in_restart(struct spdif_dev_data *spdif)
{
	int ret;

	if (spdif->is_dma)
		/* Disable DMA receiver */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RDE_BIT, 0);
	else
		/* Disable Receiver interrupt */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RCBI_BIT, 0);
	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RME_BIT, 0);
	ret = rz_spdif_wait_stat(spdif, SPDIF_RIS_BIT, true);
	if (ret) {
		dev_warn(spdif->dev, "in_restart aborted: RX not idle\n");
		return;
	}

	rz_spdif_status_clear(spdif);

	/* Retransmit and enable receiver module */
	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RME_BIT, SPDIF_RME_BIT);
	ret = rz_spdif_wait_stat(spdif, SPDIF_RIS_BIT, false);
	if (ret) {
		dev_warn(spdif->dev, "in_restart: RX not active\n");
		return;
	}


	/* Enable interrupt (Channel status full) */
	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RCSI_BIT, SPDIF_RCSI_BIT);

	/* Enable interrupt (User data full) */
	if (spdif->spdin.u_idx < SPDIF_USER_BUFSZ)
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RUII_BIT, SPDIF_RUII_BIT);

	/* Enable error interrupt */
	rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_REIE_BIT, SPDIF_REIE_BIT);

	if (spdif->is_dma) {
		/* Enable overrun interrupt */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_ABOI_BIT, SPDIF_ABOI_BIT);
		/* Enable DMA receiver */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RDE_BIT, SPDIF_RDE_BIT);
	} else
		/* Enable Receiver interrupt */
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RCBI_BIT, SPDIF_RCBI_BIT);
}

static irqreturn_t rz_spdif_irq_handler(int irq, void *arg)
{
	struct spdif_dev_data *spdif = arg;
	struct rz_spdif_stream *strm_playback = NULL;
	struct rz_spdif_stream *strm_capture = NULL;
	u32 stat, udata;
	bool tx_error = false;
	bool rx_error = false;

	spin_lock(&spdif->lock);
	if (spdif->playback.substream)
		strm_playback = &spdif->playback;
	if (spdif->capture.substream)
		strm_capture = &spdif->capture;

	if (!strm_playback && !strm_capture) {
		spin_unlock(&spdif->lock);
		return IRQ_HANDLED;
	}

	stat = rz_spdif_status_get(spdif);

	if (spdif->is_dma) {
		/* Clear error status */
		if (stat & SPDIF_ABU_BIT) {
			rz_spdif_bset(spdif, SPDIF_STAT, SPDIF_ABU_BIT, 0);
			tx_error = true;
		}
		if (stat & SPDIF_ABO_BIT) {
			rz_spdif_bset(spdif, SPDIF_STAT, SPDIF_ABO_BIT, 0);
			rx_error = true;
		}
	} else {
		if (stat & SPDIF_CBTX_BIT)
			if (rz_spdif_pio_send(irq, spdif))
				snd_pcm_period_elapsed(strm_playback->substream);

		if (stat & SPDIF_CBRX_BIT)
			if (rz_spdif_pio_recv(irq, spdif))
				snd_pcm_period_elapsed(strm_capture->substream);
	}

	/* Receiver channel status interrupt (CSRX) */
	if (stat & SPDIF_CSRX_BIT) {
		do {
			/* Read the status data */
			spdif->spdin.s_buf[SPDIF_CH1] = rz_spdif_reg_readl(spdif, SPDIF_RLCS);
			spdif->spdin.s_buf[SPDIF_CH2] = rz_spdif_reg_readl(spdif, SPDIF_RRCS);
		} while (rz_spdif_reg_readl(spdif, SPDIF_STAT) & SPDIF_CSRX_BIT);
	}

	/* Transmitter user information interrupt (TUIR) */
	if (stat & SPDIF_TUIR_BIT) {
		do
			rz_spdif_reg_writel(spdif, SPDIF_TUI,
					spdif->spdout.u_buf.data32[spdif->spdout.u_idx]);
		while (rz_spdif_reg_readl(spdif, SPDIF_STAT) & SPDIF_TUIR_BIT);

		spdif->spdout.u_idx++;
		if (spdif->spdout.u_idx >= SPDIF_USER_BUFSZ) {
			spdif->spdout.u_idx = 0;
			spdif->count--;
		}
		if (spdif->count < 0)
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_TUII_BIT, 0);
	}

	/* Receiver user information interrupt (RUIR) */
	if (stat & SPDIF_RUIR_BIT) {
		do
			udata = rz_spdif_reg_readl(spdif, SPDIF_RUI);
		while (rz_spdif_reg_readl(spdif, SPDIF_STAT) & SPDIF_RUIR_BIT);

		/* Detect the end and Store the data */
		if (udata == 0)
			spdif->spdin.u_idx = 0;
		else
			spdif->spdin.u_buf.data32[spdif->spdin.u_idx++] = udata;
		if (spdif->spdin.u_idx > SPDIF_USER_BUFSZ)
			rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RUII_BIT, 0);
	}

	rz_spdif_status_clear(spdif);

	spin_unlock(&spdif->lock);

	if (tx_error)
		rz_spdif_out_restart(spdif);
	if (rx_error)
		rz_spdif_in_restart(spdif);

	return IRQ_HANDLED;
}

static void rz_spdif_pad_tx_period(struct spdif_dev_data *spdif,
				   struct rz_spdif_stream *strm,
				   unsigned int period)
{
	struct snd_pcm_runtime *runtime = strm->substream->runtime;
	unsigned int samples = runtime->period_size * runtime->channels;
	const s16 *src = (const s16 *)runtime->dma_area + period * samples;
	u32 *dst = strm->dma_pad_buf_tx + period * samples;
	unsigned int i;

	for (i = 0; i < samples; i++)
		dst[i] = (u32)(u16)src[i] & 0x0000FFFF;
}

static void rz_spdif_unpad_rx_period(struct spdif_dev_data *spdif,
				     struct rz_spdif_stream *strm,
				     unsigned int period)
{
	struct snd_pcm_runtime *runtime = strm->substream->runtime;
	unsigned int samples = runtime->period_size * runtime->channels;
	const u32 *src = strm->dma_pad_buf_rx + period * samples;
	s16 *dst = (s16 *)runtime->dma_area + period * samples;
	unsigned int i;

	for (i = 0; i < samples; i++)
		dst[i] = (s16)(src[i] & 0x0000FFFF);
}

static int rz_spdif_dma_transfer(struct spdif_dev_data *spdif,
				 struct rz_spdif_stream *strm)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config cfg = {};
	bool is_play = rz_spdif_stream_is_play(substream);
	bool need_pad = (runtime->format == SNDRV_PCM_FORMAT_S16_LE);
	dma_addr_t buf_phys;
	size_t period_bytes;
	unsigned int i;
	int ret;

	period_bytes = runtime->period_size * runtime->channels * sizeof(u32);

	strm->pad_period_pos = 0;
	strm->pad_periods = runtime->periods;

	/* As mentioned in the HW manual, SPDIF registers are longword registers
	* and must be accessed using 32-bit operations. DMA transfers audio
	* by sending channel 1 first, then channel 2.
	*
	* However, with the S16_LE format, ALSA sends 32 bits per frame
	* (16 bits per channel). Therefore, when using 16-bit audio formats,
	* we must pad each 16-bit channel sample to 32 bits as the HW requirement.
	*/
	if (need_pad)
		buf_phys = is_play ? strm->dma_pad_addr_tx
				   : strm->dma_pad_addr_rx;
	else
		buf_phys = runtime->dma_addr;

	if (is_play) {
		cfg.direction = DMA_MEM_TO_DEV;
		cfg.src_addr  = buf_phys;
		cfg.dst_addr  = spdif->phys + SPDIF_TDAD;

		/* Pre-pad all periods before starting DMA (S16_LE only) */
		if (need_pad)
			for (i = 0; i < strm->pad_periods; i++)
				rz_spdif_pad_tx_period(spdif, strm, i);
	} else {
		cfg.direction = DMA_DEV_TO_MEM;
		cfg.src_addr  = spdif->phys + SPDIF_RDAD;
		cfg.dst_addr  = buf_phys;
	}
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.src_maxburst   = 1;
	cfg.dst_maxburst   = 1;

	ret = dmaengine_slave_config(strm->dma_ch, &cfg);
	if (ret < 0)
		return ret;

	desc = dmaengine_prep_dma_cyclic(strm->dma_ch, buf_phys,
					 period_bytes * strm->pad_periods,
					 period_bytes, cfg.direction,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(spdif->dev, "spdif: prep cyclic failed\n");
		return -ENOMEM;
	}

	desc->callback       = rz_spdif_dma_complete;
	desc->callback_param = strm;

	if (dmaengine_submit(desc) < 0) {
		dev_err(spdif->dev, "dmaengine_submit() fail\n");
		return -EIO;
	}

	dma_async_issue_pending(strm->dma_ch);

	/* Enable DMA receiver after DMA is running to avoid early overrun */
	if (!is_play)
		rz_spdif_bset(spdif, SPDIF_CTRL, SPDIF_RDE_BIT, SPDIF_RDE_BIT);

	return 0;
}

static void rz_spdif_dma_complete(void *data)
{
	struct rz_spdif_stream *strm = (struct rz_spdif_stream *)data;
	struct spdif_dev_data *spdif = strm->priv;
	struct snd_pcm_runtime *runtime;

	if (!strm->running)
		return;

	if (!strm->substream || !strm->substream->runtime)
		return;

	runtime = strm->substream->runtime;

	if (runtime->format == SNDRV_PCM_FORMAT_S16_LE) {
		if (rz_spdif_stream_is_play(strm->substream)) {
			strm->pad_period_pos =
				(strm->pad_period_pos + 1) % strm->pad_periods;
			rz_spdif_pad_tx_period(spdif, strm,
					       strm->pad_period_pos);
		} else {
			rz_spdif_unpad_rx_period(spdif, strm,
						 strm->pad_period_pos);
			strm->pad_period_pos =
				(strm->pad_period_pos + 1) % strm->pad_periods;
		}
		snd_pcm_period_elapsed(strm->substream);
		return;
	}

	if (runtime->format == SNDRV_PCM_FORMAT_S24_LE) {
		/* S24_LE: DMA runs directly on dma_area, just advance pos */
		strm->pad_period_pos = (strm->pad_period_pos + 1) % strm->pad_periods;
		snd_pcm_period_elapsed(strm->substream);
		return;
	}
}

static void rz_spdif_release_dma_channels(struct spdif_dev_data *spdif)
{
	if (spdif->playback.dma_ch) {
		dma_release_channel(spdif->playback.dma_ch);
		spdif->playback.dma_ch = NULL;
	}
	if (spdif->capture.dma_ch) {
		dma_release_channel(spdif->capture.dma_ch);
		spdif->capture.dma_ch = NULL;
	}
}

static int rz_spdif_dma_request(struct spdif_dev_data *spdif, struct device *dev)
{

	spdif->playback.dma_ch = dma_request_chan(dev, "tx");
	if (IS_ERR(spdif->playback.dma_ch))
		spdif->playback.dma_ch = NULL;

	spdif->capture.dma_ch = dma_request_chan(dev, "rx");
	if (IS_ERR(spdif->capture.dma_ch))
		spdif->capture.dma_ch = NULL;

	if (!(spdif->playback.dma_ch && spdif->capture.dma_ch))
		goto no_dma;

	return 0;

no_dma:
	rz_spdif_release_dma_channels(spdif);

	return -ENODEV;
}

static int rz_spdif_rzg3s_clk_init(struct spdif_dev_data *spdif,
				    struct device *dev)
{
	struct clk *audio_clk;

	spdif->clk = devm_clk_get(dev, "spdif-tx-rx");
	if (IS_ERR(spdif->clk))
		return PTR_ERR(spdif->clk);

	audio_clk = devm_clk_get(dev, "audio_clk1");
	if (IS_ERR(audio_clk))
		return dev_err_probe(dev, PTR_ERR(audio_clk),
				     "no audio clk1");
	spdif->audio_clk_1 = clk_get_rate(audio_clk);

	audio_clk = devm_clk_get(dev, "audio_clk2");
	if (IS_ERR(audio_clk))
		return dev_err_probe(dev, PTR_ERR(audio_clk),
				     "no audio clk2");
	spdif->audio_clk_2 = clk_get_rate(audio_clk);

	if (!(spdif->audio_clk_1 || spdif->audio_clk_2))
		return dev_err_probe(dev, -EINVAL,
				     "no audio clk1 or audio clk2");
	return 0;
}

static int rz_spdif_rzv2h_clk_init(struct spdif_dev_data *spdif,
				    struct device *dev)
{
	struct device_node *np;
	struct platform_device *adg_pdev;
	char clk_name[16];
	u32 channel_id;

	if (!spdif->soc_data || !spdif->soc_data->max_channels)
		return dev_err_probe(dev, -EINVAL, "invalid max_channels\n");

	if (of_property_read_u32(dev->of_node, "renesas,channel-id", &channel_id))
		return dev_err_probe(dev, -EINVAL,
				     "missing renesas,channel-id in DT\n");

	if (channel_id >= spdif->soc_data->max_channels)
		return dev_err_probe(dev, -EINVAL,
				     "invalid renesas,channel-id %u (max %u)\n",
				     channel_id,
				     spdif->soc_data->max_channels - 1);

	spdif->spdif_id = channel_id;

	snprintf(clk_name, sizeof(clk_name), "spdif-%d", spdif->spdif_id);
	spdif->clk = devm_clk_get(dev, clk_name);
	if (IS_ERR(spdif->clk))
		return dev_err_probe(dev, PTR_ERR(spdif->clk),
				     "failed to get %s\n", clk_name);

	np = of_parse_phandle(dev->of_node, "renesas,adg", 0);
	if (!np)
		return dev_err_probe(dev, -ENODEV,
				     "missing renesas,adg\n");

	adg_pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!adg_pdev)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "ADG device not ready\n");

	if (!dev_get_drvdata(&adg_pdev->dev)) {
		put_device(&adg_pdev->dev);
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "ADG driver data not ready\n");
	}

	spdif->adg_dev = &adg_pdev->dev;
	put_device(&adg_pdev->dev);

	return 0;
}

static int rz_spdif_rzv2h_set_clk(struct spdif_dev_data *spdif,
				   unsigned int rate, bool enable)
{
	int ret;

	if (enable) {
		ret = rsnd_adg_spdif_clk_start(spdif->adg_dev,
					      spdif->spdif_id,
					      (unsigned long)rate * 512);
		if (ret)
			return dev_err_probe(spdif->dev, ret,
					     "ADG clock config failed\n");

		return 0;
	}

	return rsnd_adg_spdif_clk_stop(spdif->adg_dev, spdif->spdif_id);
}

static int rz_spdif_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spdif_dev_data *spdif;
	const struct rz_spdif_soc_data *soc_data;
	struct resource *res;
	int ret;

	soc_data = of_device_get_match_data(dev);
	if (!soc_data)
		return -ENODEV;

	spdif = devm_kzalloc(dev, sizeof(*spdif), GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	spdif->dev = dev;
	spdif->soc_data = soc_data;
	spdif->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(spdif->base))
		return PTR_ERR(spdif->base);

	spdif->phys = res->start;
	if (spdif->soc_data->ops && spdif->soc_data->ops->clk_init) {
		ret = soc_data->ops->clk_init(spdif, dev);
		if (ret)
			return ret;
	}

	/* Detect DMA support */
	ret = rz_spdif_dma_request(spdif, &pdev->dev);
	if (ret < 0) {
		dev_warn(&pdev->dev, "DMA not available, using PIO\n");
		spdif->is_dma = false;
	} else {
		dev_info(&pdev->dev, "DMA enabled");
		spdif->is_dma = true;
	}

	spdif->playback.priv = spdif;
	spdif->capture.priv = spdif;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto probe_err;

	ret = devm_request_irq(dev, ret, rz_spdif_irq_handler,
				0, dev_name(dev), spdif);
	if (ret < 0) {
		dev_err(dev, "spdif irq request failed\n");
		goto probe_err;
	}

	spin_lock_init(&spdif->lock);
	dev_set_drvdata(dev, spdif);

	spdif->rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(spdif->rstc))
		return PTR_ERR(spdif->rstc);

	reset_control_deassert(spdif->rstc);
	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_resume_and_get failed\n");
		goto probe_err;
	}

	ret = devm_snd_soc_register_component(dev, &rz_spdif_component,
					      &rz_spdif_soc_dai, 1);
	if (ret < 0) {
		dev_err(dev, "failed to register SPDIF snd component\n");
		goto probe_err;
	}

	return ret;

probe_err:
	pm_runtime_put(spdif->dev);
	pm_runtime_disable(spdif->dev);
	reset_control_assert(spdif->rstc);

	return ret;
}

static void rz_spdif_remove(struct platform_device *pdev)
{
	struct spdif_dev_data *spdif = dev_get_drvdata(&pdev->dev);

	pm_runtime_put(spdif->dev);
	pm_runtime_disable(spdif->dev);
	reset_control_assert(spdif->rstc);
}

static int __maybe_unused rz_spdif_suspend(struct device *dev)
{
	struct spdif_dev_data *spdif = dev_get_drvdata(dev);
	bool is_running;
	int ret;

	is_running = spdif->playback.running || spdif->capture.running;
	if (is_running) {
		ret = spdif->soc_data->ops->set_clk(spdif, spdif->rate, false);
		if (ret)
			return ret;
	}

	if (spdif->rstc)
		reset_control_assert(spdif->rstc);

	pm_runtime_put_sync(spdif->dev);

	return 0;
}

static int __maybe_unused rz_spdif_resume(struct device *dev)
{
	struct spdif_dev_data *spdif = dev_get_drvdata(dev);
	bool is_running;
	int ret;

	if (spdif->rstc) {
		ret = reset_control_deassert(spdif->rstc);
		if (ret)
			return ret;
	}

	pm_runtime_get_sync(spdif->dev);

	is_running = spdif->playback.running || spdif->capture.running;

	if (is_running) {
		ret = spdif->soc_data->ops->set_clk(spdif, spdif->rate, true);
		if (ret)
			return ret;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(rz_spdif_pm_ops, rz_spdif_suspend,
			 rz_spdif_resume);

static const struct rz_spdif_ops rzg3s_spdif_ops = {
	.clk_init = rz_spdif_rzg3s_clk_init,
};

static const struct rz_spdif_ops rzv2h_spdif_ops = {
	.clk_init = rz_spdif_rzv2h_clk_init,
	.set_clk = rz_spdif_rzv2h_set_clk,
};

static const struct rz_spdif_soc_data rzg3s_soc_data = {
	.ops			= &rzg3s_spdif_ops,
	.max_channels		= 1,
	.extra_controls		= rz_spdif_rzg3s_controls,
	.num_extra_controls	= ARRAY_SIZE(rz_spdif_rzg3s_controls),
};

static const struct rz_spdif_soc_data rzv2h_soc_data = {
	.ops = &rzv2h_spdif_ops,
	.max_channels = 3,
};

static const struct of_device_id rz_spdif_of_match[] = {
	{ .compatible = "renesas,r9a08g045-spdif", .data = &rzg3s_soc_data },
	{ .compatible = "renesas,r9a09g057-spdif", .data = &rzv2h_soc_data },
	{/* Sentinel */},
};
MODULE_DEVICE_TABLE(of, rz_spdif_of_match);

static struct platform_driver rz_spdif_driver = {
	.driver	= {
		.name	= "rz-spdif",
		.of_match_table = rz_spdif_of_match,
		.pm = &rz_spdif_pm_ops,
	},
	.probe		= rz_spdif_probe,
	.remove		= rz_spdif_remove,
};

module_platform_driver(rz_spdif_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas RZ ASoC Sony/Philips Digital Interface Format Driver");
MODULE_AUTHOR("Duy Dang <duy.dang.yb@renesas.com>");

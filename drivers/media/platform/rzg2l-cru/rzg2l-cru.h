/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Renesas RZ/G2L CRU
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * Based on the rcar_vin driver
 */

#ifndef __RZG2L_CRU__
#define __RZG2L_CRU__

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <linux/reset.h>
#include <linux/clk.h>

/*
 * CRU Image Processing register subset common for all platforms.
 * Not all registers will exist on all platforms.
 */
enum {
	CRUnCTRL,	/* CRU Control Register */
	CRUnIE,		/* CRU Interrupt Enable Register */
	CRUnIE1,	/* CRU Interrupt Enable 1 Register */
	CRUnIE2,	/* CRU Interrupt Enable 2 Register */
	CRUnINTS,	/* CRU Interrupt Status Register */
	CRUnINTS1,	/* CRU Interrupt Status 1 Register */
	CRUnINTS2,	/* CRU Interrupt Status 2 Register */
	CRUnRST,	/* CRU Reset Register */
	CRUnCOM,	/* CRU General Read/Write Register */
	AMnMB1ADDRL,	/* VD Memory Bank Base Address (Lower) Register */
	AMnMB1ADDRH,	/* VD Memory Bank Base Address (Higher) Register */
	AMnUVAOFL,	/* UV-VD Data Address Offset Lower Register */
	AMnUVAOFH,	/* UV-VD Data Address Offset Higher Register */
	AMnMBVALID,	/* Memory Bank Enable Register for CRU Image Data */
	AMnMBS,		/* Memory Bank Status Register for CRU Image Data */
	AMnMADRSL,	/* VD Memory Address Lower Status Register */
	AMnMADRSH,	/* VD Memory Address Higher Status Register */
	AMnNIAOFL,	/* Non Image Process Address Offset Lower Register */
	AMnNIAOFH,	/* Non Image Process Address Offset Higher Register */
	AMnSVCAOFL,	/* SVC Data Address Offset Lower Register */
	AMnSVCAOFH,	/* SVC Data Address Offset Higher Register */
	AMnIVT0ADDRL,	/* AXI-VD Bus Transfer Completion Event Address 0 Lower Register */
	AMnIVT0ADDRH,	/* AXI-VD Bus Transfer Completion Event Address 0 Higher Register */
	AMnFIFO,	/* AXI-VD Master FIFO Setting Register */
	AMnFIFOPNTR,	/* AXI-VD Bus Master FIFO Pointer Register */
	AMnAXISTP,	/* AXI-VD Bus Master Transfer Stop Register */
	AMnAXISTPACK,	/* AXI-VD Bus Master Transfer Stop Status Register */
	AMnIS,		/* Image Stride Setting Register */
	ICnEN,		/* CRU Image Converter Enable Register */
	ICnSVCNUM,	/* CRU SVC Number Register */
	ICnSVC,		/* CRU VC Select Register */
	ICnMC,		/* CRU Image Converter Main Control Register */
	ICnIPMC_C0,	/* CRU Image Converter Main Control 0 Register */
	ICnIPMC_C1,	/* CRU Image Converter Main Control 1 Register */
	ICnIPMC_C2,	/* CRU Image Converter Main Control 2 Register */
	ICnIPMC_C3,	/* CRU Image Converter Main Control 3 Register */
	ICnPIFC,	/* CRU Parallel I/F Control Register */
	ICnMS,		/* CRU Module Status Register */
	ICnDMR,		/* CRU Output Image Format Register */
	ICnTICTRL1,	/* CRU Test Image Generation Control 1 Register */
	ICnTICTRL2,	/* CRU Test Image Generation Control 2 Register */
	ICnTISIZE1,	/* CRU Test Image Size Setting 1 Register */
	ICnTISIZE2,	/* CRU Test Image Size Setting 2 Register */

	CRUn_NR_REGS,
};

struct regs_offset {
	u32 offset;
};

static const struct regs_offset  rzg2l_cru_regs_offset[] = {
	[CRUnCTRL]	=	{ .offset = 0x000, },
	[CRUnIE]	=	{ .offset = 0x004, },
	[CRUnINTS]	=	{ .offset = 0x008, },
	[CRUnRST]	=	{ .offset = 0x00C, },
	[CRUnCOM]	=	{ .offset = 0x080, },
	[AMnMB1ADDRL]	=	{ .offset = 0x100, },
	[AMnMB1ADDRH]	=	{ .offset = 0x104, },
	[AMnUVAOFL]	=	{ .offset = 0x140, },
	[AMnUVAOFH]	=	{ .offset = 0x144, },
	[AMnMBVALID]	=	{ .offset = 0x148, },
	[AMnMBS]	=	{ .offset = 0x14C, },
	[AMnFIFO]	=	{ .offset = 0x160, },
	[AMnFIFOPNTR]	=	{ .offset = 0x168, },
	[AMnAXISTP]	=	{ .offset = 0x174, },
	[AMnAXISTPACK]	=	{ .offset = 0x178, },
	[ICnEN]		=	{ .offset = 0x200, },
	[ICnMC]		=	{ .offset = 0x208, },
	[ICnPIFC]	=	{ .offset = 0x250, },
	[ICnMS]		=	{ .offset = 0x254, },
	[ICnDMR]	=	{ .offset = 0x26C, },
	[ICnTICTRL1]	=	{ .offset = 0x2C0, },
	[ICnTICTRL2]	=	{ .offset = 0x2C4, },
	[ICnTISIZE1]	=	{ .offset = 0x2C8, },
	[ICnTISIZE2]	=	{ .offset = 0x2CC, },
};

static const struct regs_offset rzv2h_cru_regs_offset[] = {
	[CRUnCTRL]	=	{ .offset = 0x000, },
	[CRUnIE1]	=	{ .offset = 0x004, },
	[CRUnIE2]	=	{ .offset = 0x008, },
	[CRUnINTS1]	=	{ .offset = 0x00C, },
	[CRUnINTS2]	=	{ .offset = 0x010, },
	[CRUnRST]	=	{ .offset = 0x018, },
	[CRUnCOM]	=	{ .offset = 0x02C, },
	[AMnMB1ADDRL]	=	{ .offset = 0x040, },
	[AMnMB1ADDRH]	=	{ .offset = 0x044, },
	[AMnUVAOFL]	=	{ .offset = 0x080, },
	[AMnUVAOFH]	=	{ .offset = 0x084, },
	[AMnMBVALID]	=	{ .offset = 0x088, },
	[AMnMADRSL]	=	{ .offset = 0x08C, },
	[AMnMADRSH]	=	{ .offset = 0x090, },
	[AMnNIAOFL]	=	{ .offset = 0x094, },
	[AMnNIAOFH]	=	{ .offset = 0x098, },
	[AMnSVCAOFL]	=	{ .offset = 0x09C, },
	[AMnSVCAOFH]	=	{ .offset = 0x0A0, },
	[AMnIVT0ADDRL]	=	{ .offset = 0x0B4, },
	[AMnIVT0ADDRH]	=	{ .offset = 0x0B8, },
	[AMnFIFO]	=	{ .offset = 0x0F0, },
	[AMnFIFOPNTR]	=	{ .offset = 0x0F8, },
	[AMnAXISTP]	=	{ .offset = 0x110, },
	[AMnAXISTPACK]	=	{ .offset = 0x114, },
	[AMnIS]		=	{ .offset = 0x128, },
	[ICnEN]		=	{ .offset = 0x1F0, },
	[ICnSVCNUM]	=	{ .offset = 0x1F8, },
	[ICnSVC]	=	{ .offset = 0x1FC, },
	[ICnIPMC_C0]	=	{ .offset = 0x200, },
	[ICnIPMC_C1]	=	{ .offset = 0x258, },
	[ICnIPMC_C2]	=	{ .offset = 0x25C, },
	[ICnIPMC_C3]	=	{ .offset = 0x260, },
	[ICnMS]		=	{ .offset = 0x2D8, },
	[ICnDMR]	=	{ .offset = 0x304, },
	[ICnTICTRL1]	=	{ .offset = 0x35C, },
	[ICnTICTRL2]	=	{ .offset = 0x360, },
	[ICnTISIZE1]	=	{ .offset = 0x364, },
	[ICnTISIZE2]	=	{ .offset = 0x368, },
};

/* Number of CRU channels */
#define CRU_CHANNEL_MAX		4

/* Number of HW buffers */
#define HW_BUFFER_MAX		8
#define HW_BUFFER_DEFAULT	4

/* Number of HW Bus Transfer Completion Events */
#define HW_IVT_MAX		5

/* Address alignment mask for HW buffers */
#define HW_BUFFER_MASK	0x1ff

/* Maximum bumber of CSI2 virtual channels */
#define CSI2_VCHANNEL	4

/* Time until source device reconnects */
#define CONNECTION_TIME		2000
#define SETUP_WAIT_TIME		3000

/*
 * The base for the RZ/G2L CRU driver controls.
 * We reserve 16 controls for this driver
 * The last USER-class private control IDs is V4L2_CID_USER_ATMEL_ISC_BASE.
 */

#define V4L2_CID_USER_CRU_BASE	(V4L2_CID_USER_BASE + 0x10e0)

/* V4L2 private controls */
#define V4L2_CID_CRU_FRAME_SKIP	(V4L2_CID_USER_CRU_BASE + 0)

#define V4L2_CID_CRU_LIMIT	1

static const struct v4l2_ctrl_ops rzg2l_cru_ctrl_ops;

static const struct v4l2_ctrl_config rzg2l_cru_ctrls[V4L2_CID_CRU_LIMIT] = {
	{
		.id = V4L2_CID_CRU_FRAME_SKIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.ops = &rzg2l_cru_ctrl_ops,
		.name = "Skipping Frames Enable/Disable",
		.max = 1,
		.min = 0,
		.step = 1,
		.def = 0,
		.is_private = 1,
	}
};

/* Minimum skipping frame for camera sensors stability */
#define CRU_FRAME_SKIP		3

/**
 * STOPPED  - No operation in progress
 * STARTING - Capture starting up
 * RUNNING  - Operation in progress have buffers
 * STOPPING - Stopping operation
 */
enum rzg2l_cru_dma_state {
	STOPPED = 0,
	STARTING,
	RUNNING,
	STOPPING,
};

enum rzg2l_cru_fmt_types {
	YUV = 0,
	RGB,
	BAYER_RAW,
	USER_DEFINED,
};

/**
 * struct rzg2l_cru_video_format - Data format stored in memory
 * @fourcc:	Pixelformat
 * @bpp:	Bytes per pixel
 */
struct rzg2l_cru_video_format {
	u32 fourcc;
	u8 bpp;
};

/**
 * struct rzg2l_cru_parallel_entity - Parallel video input endpoint descriptor
 * @asd:	sub-device descriptor for async framework
 * @subdev:	subdevice matched using async framework
 * @mbus_type:	media bus type
 * @mbus_flags:	media bus configuration flags
 * @source_pad:	source pad of remote subdevice
 * @sink_pad:	sink pad of remote subdevice
 *
 */
struct rzg2l_cru_parallel_entity {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;

	enum v4l2_mbus_type mbus_type;
	unsigned int mbus_flags;

	unsigned int source_pad;
	unsigned int sink_pad;
};

enum rz_cru_type {
	RZ_CRU_G2L,
	RZ_CRU_V2H,
};

/**
 * struct rzg2l_cru_info - Information about the particular CRU implementation
 * @max_width:		max input width the CRU supports
 * @max_height:		max input height the CRU supports
 * @regs_offset:	array of CRU hardware registers
 */
struct rzg2l_cru_info {
	unsigned int max_width;
	unsigned int max_height;

	const struct regs_offset *regs;
	enum rz_cru_type type;
};

/**
 * struct rzg2l_cru_dev - Renesas CRU device structure
 * @dev:		(OF) device
 * @base:		device I/O register space remapped to virtual memory
 * @info:		info about CRU instance
 *
 * @vdev:		V4L2 video device associated with CRU
 * @v4l2_dev:		V4L2 device
 * @ctrl_handler:	V4L2 control handler
 * @notifier:		V4L2 asynchronous subdevs notifier
 *
 * @parallel:		parallel input subdevice descriptor
 *
 * @group:		Gen3 CSI group
 * @pad:		media pad for the video device entity
 *
 * @lock:		protects @queue
 * @queue:		vb2 buffers queue
 * @scratch:		cpu address for scratch buffer
 * @scratch_phys:	physical address of the scratch buffer
 *
 * @qlock:		protects @queue_buf, @buf_list, @sequence
 *			@state
 * @queue_buf:		Keeps track of buffers given to HW slot
 * @buf_list:		list of queued buffers
 * @sequence:		V4L2 buffers sequence number
 * @state:		keeps track of operation state
 *
 * @is_csi:		flag to mark the CRU as using a CSI-2 subdevice
 *
 * @input_is_yuv:	flag to mark the input format of CRU
 * @output_is_yuv:	flag to mark the output format of CRU
 *
 * @mbus_code:		media bus format code
 * @format:		active V4L2 pixel format
 *
 * @crop:		active cropping
 * @source:		active size of the video source
 * @std:		active video standard of the video source
 *
 * @work_queue:		work queue at resuming
 * @rzg2l_cru_resume:	delayed work at resuming
 * @setup_wait:		wait queue used to setup VIN
 * @suspend:		suspend flag
 */
struct rzg2l_cru_dev {
	struct device *dev;
	void __iomem *base;
	const struct rzg2l_cru_info *info;

	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	u8 num_buf;
	struct v4l2_async_notifier notifier;

	struct rzg2l_cru_parallel_entity *parallel;

	struct rzg2l_cru_group *group;
	struct media_pad pad;

	struct mutex lock;
	struct vb2_queue queue;
	void *scratch;
	dma_addr_t scratch_phys;

	spinlock_t qlock;
	struct vb2_v4l2_buffer *queue_buf[HW_BUFFER_MAX];
	struct list_head buf_list;
	unsigned int sequence;
	enum rzg2l_cru_dma_state state;

	bool is_csi;

	enum rzg2l_cru_fmt_types input_fmt;
	enum rzg2l_cru_fmt_types output_fmt;

	u32 mbus_code;
	struct v4l2_pix_format format;

	struct v4l2_rect crop;
	struct v4l2_rect compose;
	struct v4l2_rect source;
	v4l2_std_id std;

	struct {
		struct reset_control *presetn;
		struct reset_control *aresetn;
	} rstc;

	struct workqueue_struct *work_queue;
	struct delayed_work rzg2l_cru_resume;
	wait_queue_head_t setup_wait;
	bool suspend;
	bool is_frame_skip;

	struct task_struct *retry_thread;

	u32 id;
};

#define cru_to_source(cru)		((cru)->parallel->subdev)

/* Debug */
#define cru_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define cru_info(d, fmt, arg...)	dev_info(d->dev, fmt, ##arg)
#define cru_warn(d, fmt, arg...)	dev_warn(d->dev, fmt, ##arg)
#define cru_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

/**
 * struct rzg2l_cru_group - CRU CSI2 group information
 * @mdev:	media device which represents the group
 *
 * @notifier:	group notifier for CSI-2 async subdevices
 * @cru:	CRU instances which are part of the group
 * @csi:	array of pairs of fwnode and subdev pointers
 *		to all CSI-2 subdevices.
 */
struct rzg2l_cru_group {
	struct media_device mdev;

	struct v4l2_async_notifier notifier;
	struct rzg2l_cru_dev *cru;

	struct {
		struct fwnode_handle *fwnode;
		struct v4l2_subdev *subdev;

		u32 channel;
	} csi;
};

int rzg2l_cru_dma_register(struct rzg2l_cru_dev *cru, int irq);
void rzg2l_cru_dma_unregister(struct rzg2l_cru_dev *cru);

int rzg2l_cru_v4l2_register(struct rzg2l_cru_dev *cru);
void rzg2l_cru_v4l2_unregister(struct rzg2l_cru_dev *cru);

const struct rzg2l_cru_video_format
*rzg2l_cru_format_from_pixel(u32 pixelformat);

void rzg2l_cru_resume_start_streaming(struct work_struct *work);
void rzg2l_cru_suspend_stop_streaming(struct rzg2l_cru_dev *cru);

int rzg2l_cru_init_csi_dphy(struct v4l2_subdev *sd);
#endif

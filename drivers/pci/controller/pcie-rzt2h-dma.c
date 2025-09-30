// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe DMA engine for PCIe on Renesas RZ/T2H Series SoCs
 *
 * Based on pcie-rzv2h-dma.c
 *
 * Copyright (C) 2025 Renesas Electronics Europe Ltd
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>

#include "pcie-rzt2h.h"

static inline struct rzt2h_pcie_dma_chan *to_rzt2h_pcie_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct rzt2h_pcie_dma_chan, vc.chan);
};

static inline struct rzt2h_pcie_dma_desc *to_rzt2h_pcie_dma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct rzt2h_pcie_dma_desc, vd);
};

static int rzt2h_pcie_dma_xfer_desc(struct rzt2h_pcie_dma_chan *chan)
{
	struct rzt2h_pcie *pcie = chan->dmac->pcie;
	struct virt_dma_desc *vdesc;
	dma_addr_t dsa;
	u32 mask;

	vdesc = vchan_next_desc(&chan->vc);
	if (!vdesc)
		return 0;

	chan->desc = to_rzt2h_pcie_dma_desc(vdesc);
	/* Set DMAC PCIe Max Read Request Size */
	rzt2h_pci_write_reg(pcie, DMA_D_PMRS_256B, DMA_CONTROL_REG);
	/* Enable DMA INT */
	mask = (DMA_END_EN | DMA_STOP_EN | DMA_ERR_EN) << (chan->index * 4);
	//mask = DMA_INTERRUPT_ENABLE_INT;
	rzt2h_rmw(pcie, DMA_INTERRUPT_ENABLE_REG, mask, mask);
	/* Set start address of Descriptor List */
	dsa = chan->desc->node[0].pdesc;
	if (!IS_ALIGNED(dsa, SZ_16))
		return -EINVAL;

	rzt2h_pci_write_reg(pcie, lower_32_bits(dsa), DMA_DESCRIPTORL_ADDR_LOWER_REG(chan->index));
	rzt2h_pci_write_reg(pcie, upper_32_bits(dsa), DMA_DESCRIPTORL_ADDR_UPPER_REG(chan->index));

	/* Set QUE Entry */
	rzt2h_pci_write_reg(pcie, QUE_INTERRUPT_ON, QUE_ENTRY_REG(chan->index));
	/* Start xfer */
	rzt2h_pci_write_reg(pcie, DMA_QUE_EN, DMA_CHANNEL_CONTROL_REG(chan->index));

	return 0;
}

static struct rzt2h_pcie_dma_desc *rzt2h_pcie_dma_alloc_desc(struct rzt2h_pcie_dma_chan *chan,
							     int sg_len)
{
	struct rzt2h_pcie_dma_desc *desc;
	int i;

	desc = kzalloc(struct_size(desc, node, sg_len), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->chan = chan;
	desc->n_nodes = sg_len;
	for (i = 0; i < sg_len; i++) {
		desc->node[i].desc = dma_pool_alloc(chan->pool,
				GFP_NOWAIT, &desc->node[i].pdesc);
		if (!desc->node[i].desc)
			goto err;
	}

	return desc;
err:
	while (--i >= 0)
		dma_pool_free(chan->pool, desc->node[i].desc,
			      desc->node[i].pdesc);
	kfree(desc);

	return NULL;
}

static void rzt2h_pcie_dma_release_desc(struct rzt2h_pcie_dma_desc *desc)
{
	struct rzt2h_pcie_dma_chan *chan = desc->chan;
	int i = 0;

	if (!desc)
		return;

	while (i < desc->n_nodes) {
		dma_pool_free(chan->pool, desc->node[i].desc,
			      desc->node[i].pdesc);
		i++;
	}

	kfree(desc);
}

static int rzt2h_pcie_dma_alloc_chan_resources(struct dma_chan *ch)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);

	/* Create the dma pool for descriptor allocation */
	chan->pool = dma_pool_create(dev_name(&ch->dev->device),
					    chan->dmac->dev,
					    sizeof(struct rzt2h_pcie_dma_hw_node),
					    __alignof__(struct rzt2h_pcie_dma_hw_node),
					    0);

	if (!chan->pool) {
		dev_err(chan->dmac->dev, "unable to allocate desc pool\n");
		return -ENOMEM;
	}

	dev_dbg(chan->dmac->dev, "alloc ch_id:%d", chan->vc.chan.chan_id);

	return 0;
}

static void rzt2h_pcie_dma_free_chan_resources(struct dma_chan *ch)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);
	unsigned long flags;

	dev_dbg(chan->dmac->dev, "%s: freeing chan:%d\n",
		__func__, chan->vc.chan.chan_id);

	spin_lock_irqsave(&chan->vc.lock, flags);
	chan->desc = NULL;
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	dma_pool_destroy(chan->pool);
	chan->pool = NULL;
}

static size_t rzt2h_pcie_dma_get_residue(struct rzt2h_pcie_dma_chan *chan,
					 struct virt_dma_desc *vdesc,
					 bool in_progress)
{
	struct rzt2h_pcie_dma_desc *desc = chan->desc;
	struct rzt2h_pcie *pcie = chan->dmac->pcie;
	size_t residue = 0;
	u32 reg;
	int i;

	if (in_progress) {
		reg = rzt2h_pci_read_reg(pcie, DMA_REST_SIZE_REG(chan->index));
		residue += reg;
	} else {
		for (i = 0; i < desc->n_nodes; i++)
			residue += desc->node[i].desc->size;
	}

	return residue;
}

static enum dma_status rzt2h_pcie_dma_tx_status(struct dma_chan *ch,
						dma_cookie_t cookie,
						struct dma_tx_state *txstate)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(ch, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_find_desc(&chan->vc, cookie);
	if (chan->desc && cookie == chan->desc->vd.tx.cookie)
		txstate->residue = rzt2h_pcie_dma_get_residue(chan, vd, true);
	else if (vd)
		txstate->residue = rzt2h_pcie_dma_get_residue(chan, vd, false);
	else
		txstate->residue = 0;

	spin_unlock_irqrestore(&chan->vc.lock, flags);

	return ret;
}

static struct dma_async_tx_descriptor *
rzt2h_pcie_dma_prep_slave_sg(struct dma_chan *ch, struct scatterlist *sgl,
			     unsigned int sg_len,
			     enum dma_transfer_direction direction,
			     unsigned long flags, void *context)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);
	struct rzt2h_pcie_dma_hw_node *hw_node;
	struct rzt2h_pcie_dma_desc *desc;
	struct scatterlist *sg;
	dma_addr_t remote_addr;
	int i;

	if (!is_slave_direction(direction)) {
		dev_err(chan->dmac->dev, "bad direction?\n");
		return NULL;
	}

	desc = rzt2h_pcie_dma_alloc_desc(chan, sg_len);
	if (!desc) {
		dev_err(chan->dmac->dev, "no memory for desc\n");
		return NULL;
	}

	/* Remote address is memory address of RC if device using this DMA is EP
	 * or, is address in EP BAR's mapped memory region if device using this DMA is RC
	 */
	remote_addr = (direction == DMA_MEM_TO_DEV) ? chan->scfg.dst_addr : chan->scfg.src_addr;

	for_each_sg(sgl, sg, sg_len, i) {
		/* Scatter-gather address must be 16-byte aligned */
		if (!IS_ALIGNED(sg_dma_address(sg), SZ_16) || !IS_ALIGNED(sg_dma_len(sg), SZ_16))
			goto err;

		hw_node = desc->node[i].desc;

		hw_node->param0 = PCIE_DMA_DSCFM | PCIE_DMA_WBD | PCIE_DMA_LV;
		if (i == (sg_len - 1))
			hw_node->param0 |= PCIE_DMA_LE;

		if (direction == DMA_MEM_TO_DEV) {
			hw_node->param1 = PCIE_DMA_CCH_L(PCIE_DMA_CCH_L_DEFAULT) |
					  PCIE_DMA_CCH_D(PCIE_DMA_CCH_D_AXI_TO_PCIE) |
					  PCIE_DMA_TC(PCIE_DMA_TC_DEFAULT) |
					  PCIE_DMA_ATB(PCIE_DMA_ATB_DEFAULT) |
					  PCIE_DMA_FUNC(PCIE_DMA_FUNC_0) |
					  PCIE_DMA_DIR(PCIE_DMA_DIR_AXI_TO_PCIE);
			hw_node->daddr_L = lower_32_bits(remote_addr);
			hw_node->daddr_U = upper_32_bits(remote_addr);

			hw_node->saddr_L = lower_32_bits(sg_dma_address(sg));
			hw_node->saddr_U = upper_32_bits(sg_dma_address(sg));
		} else {
			hw_node->param1 = PCIE_DMA_CCH_L(PCIE_DMA_CCH_L_DEFAULT) |
					  PCIE_DMA_CCH_D(PCIE_DMA_CCH_D_PCIE_TO_AXI) |
					  PCIE_DMA_TC(PCIE_DMA_TC_DEFAULT) |
					  PCIE_DMA_ATB(PCIE_DMA_ATB_DEFAULT) |
					  PCIE_DMA_FUNC(PCIE_DMA_FUNC_0) |
					  PCIE_DMA_DIR(PCIE_DMA_DIR_PCIE_TO_AXI);
			hw_node->saddr_L = lower_32_bits(remote_addr);
			hw_node->saddr_U = upper_32_bits(remote_addr);

			hw_node->daddr_L = lower_32_bits(sg_dma_address(sg));
			hw_node->daddr_U = upper_32_bits(sg_dma_address(sg));
		}

		hw_node->size = sg_dma_len(sg);
		hw_node->next_L = lower_32_bits(desc->node[(i + 1) % sg_len].pdesc);
		hw_node->next_U = upper_32_bits(desc->node[(i + 1) % sg_len].pdesc);

		/* Unlike the typical assumption by other drivers/IPs,
		 * the peripheral memory isn't a FIFO memory, in this case, it's a
		 * linear memory and that why the source/destination addresses are increased
		 * by the same portion (data length)
		 */
		remote_addr += sg_dma_len(sg);
	}

	return vchan_tx_prep(&chan->vc, &desc->vd, flags);
err:
	dev_err(chan->dmac->dev, "scatter data must be 16-byte aligned\n");
	rzt2h_pcie_dma_release_desc(desc);

	return NULL;
}

static int rzt2h_pcie_dma_terminate_all(struct dma_chan *ch)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);
	struct rzt2h_pcie *pcie = chan->dmac->pcie;
	unsigned long flags;
	LIST_HEAD(head);
	int ch_id;

	ch_id = chan->vc.chan.chan_id;
	dev_dbg(chan->dmac->dev, "terminate chan:%d\n", ch_id);

	spin_lock_irqsave(&chan->vc.lock, flags);

	rzt2h_pci_write_reg(pcie, DMA_QUE_CLR, DMA_CHANNEL_CONTROL_REG(chan->index));

	chan->desc = NULL;
	vchan_get_all_descriptors(&chan->vc, &head);

	spin_unlock_irqrestore(&chan->vc.lock, flags);

	vchan_dma_desc_free_list(&chan->vc, &head);

	return 0;
}

static void rzt2h_pcie_dma_issue_pending(struct dma_chan *ch)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);

	if (vchan_issue_pending(&chan->vc) && !chan->desc)
		if (rzt2h_pcie_dma_xfer_desc(chan) < 0)
			dev_warn(chan->dmac->dev, "ch: %d couldn't issue DMA xfer\n",
				 chan->index);

	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static int rzt2h_pcie_dma_config(struct dma_chan *ch,
				 struct dma_slave_config *config)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);

	if (config->direction == DMA_MEM_TO_DEV) {
		if (!config->dst_addr)
			return -EINVAL;

		if (!IS_ALIGNED(config->dst_addr, SZ_16)) {
			dev_err(chan->dmac->dev, "dst_addr must be 16-byte alignment\n");
			return -EINVAL;
		}
	} else {
		if (!config->src_addr)
			return -EINVAL;

		if (!IS_ALIGNED(config->src_addr, SZ_16)) {
			dev_err(chan->dmac->dev, "src_addr must be 16-byte alignment\n");
			return -EINVAL;
		}
	}

	memcpy(&chan->scfg, config, sizeof(chan->scfg));
	return 0;
}

static int rzt2h_pcie_dma_pause(struct dma_chan *ch)
{
	struct rzt2h_pcie_dma_chan *chan = to_rzt2h_pcie_dma_chan(ch);
	struct rzt2h_pcie *pcie = chan->dmac->pcie;
	u32 reg;

	reg = rzt2h_pci_read_reg(pcie, DMA_CHANNEL_CONTROL_REG(chan->index));
	reg &= (~DMA_QUE_EN);
	rzt2h_pci_write_reg(pcie, reg, DMA_CHANNEL_CONTROL_REG(chan->index));

	return 0;
}

static void rzt2h_pcie_dma_free_desc(struct virt_dma_desc *vdesc)
{
	struct rzt2h_pcie_dma_desc *desc = to_rzt2h_pcie_dma_desc(vdesc);
	int i;

	for (i = 0; i < desc->n_nodes; i++)
		dma_pool_free(desc->chan->pool, desc->node[i].desc,
			      desc->node[i].pdesc);
	kfree(desc);
}

static irqreturn_t rzt2h_pcie_dma_irq_handler(int irq, void *dev_id)
{
	struct rzt2h_pcie *pcie = dev_id;
	struct rzt2h_pcie_dmac *dmac = &pcie->dmac;
	struct rzt2h_pcie_dma_chan *chan;
	unsigned long dma_status;
	u32 reg;
	int i;

	reg = rzt2h_pci_read_reg(pcie, DMA_INTERRUPT_STATUS_REG);
	for (i = 0; i < dmac->n_channels; i++) {
		dma_status = (reg >> (i * 4)) & 0xFF;
		chan = &dmac->channels[i];

		if (!dma_status) {
			chan->status = DMA_IN_PROGRESS;
			continue;
		} else if ((dma_status & RZT2H_PCIE_DMA_END) && !(dma_status & RZT2H_PCIE_DMA_STOP))
			chan->status = DMA_COMPLETE;
		else if (dma_status & RZT2H_PCIE_DMA_ERR)
			chan->status = DMA_ERROR;
		else if (dma_status & RZT2H_PCIE_DMA_STOP)
			chan->status = (dma_status & RZT2H_PCIE_DMA_END) ?
							DMA_COMPLETE : DMA_PAUSED;

		rzt2h_rmw(pcie, DMA_INTERRUPT_STATUS_REG,
				(RZT2H_PCIE_DMA_STATUS_ALL << (i * 4)),
				(RZT2H_PCIE_DMA_STATUS_ALL << (i * 4)));


		rzt2h_rmw(pcie, DMA_INTERRUPT_ENABLE_REG,
				(RZT2H_PCIE_DMA_STATUS_ALL << (i * 4)),
				(RZT2H_PCIE_DMA_STATUS_ALL << (i * 4)));
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rzt2h_pcie_dma_irq_handler_thread(int irq, void *dev_id)
{
	struct rzt2h_pcie *pcie = dev_id;
	struct rzt2h_pcie_dmac *dmac = &pcie->dmac;
	struct rzt2h_pcie_dma_chan *chan;
	unsigned long flags;
	int i;

	for (i = 0; i < dmac->n_channels; i++) {
		chan = &dmac->channels[i];
		if (chan->status == DMA_IN_PROGRESS)
			continue;
		else if (chan->status == DMA_COMPLETE) {
			spin_lock_irqsave(&chan->vc.lock, flags);
			if (chan->desc) {
				list_del(&chan->desc->vd.node);
				vchan_cookie_complete(&chan->desc->vd);

				chan->status = DMA_IN_PROGRESS;
				chan->desc = NULL;
				/* Start the next descriptor (if available) */
				if (rzt2h_pcie_dma_xfer_desc(chan) < 0)
					dev_err(pcie->dev, "ch: %d cannot execute next transfer\n",
							chan->index);
			}
			spin_unlock_irqrestore(&chan->vc.lock, flags);
		} else if (chan->status == DMA_ERROR) {
			dev_info(pcie->dev, "error PCIe DMA\n");
			dev_info(pcie->dev, "DMA_REST_SIZE_REG = 0x%08x\n",
				rzt2h_pci_read_reg(pcie, DMA_REST_SIZE_REG(i)));
			dev_info(pcie->dev, "AXI_REQUEST_ADDR_LOWER_REG = 0x%08x\n",
				rzt2h_pci_read_reg(pcie, AXI_REQUEST_ADDR_LOWER_REG(i)));
			dev_info(pcie->dev, "AXI_REQUEST_ADDR_UPPER_REG = 0x%08x\n",
				rzt2h_pci_read_reg(pcie, AXI_REQUEST_ADDR_UPPER_REG(i)));
			dev_info(pcie->dev, "PCIE_REQUEST_ADDR_LOWER_REG = 0x%08x\n",
				rzt2h_pci_read_reg(pcie, PCIE_REQUEST_ADDR_LOWER_REG(i)));
			dev_info(pcie->dev, "PCIE_REQUEST_ADDR_UPPER_REG = 0x%08x\n",
				rzt2h_pci_read_reg(pcie, PCIE_REQUEST_ADDR_UPPER_REG(i)));
			dev_info(pcie->dev, "DMAC_ERROR_STATUS_REG = 0x%08x\n",
				rzt2h_pci_read_reg(pcie, DMAC_ERROR_STATUS_REG(i)));
		}
	}

	return IRQ_HANDLED;
}

int rzt2h_pcie_dma_init(struct rzt2h_pcie *pcie)
{
	struct rzt2h_pcie_dmac *dmac = &pcie->dmac;
	struct dma_device *engine = &dmac->engine;
	struct device *dev = pcie->dev;
	int ret = 0;
	int i;

	dmac->dma_irq = irq_of_parse_and_map(dev->of_node, 1);
	if (!dmac->dma_irq) {
		dev_err(dev, "cannot get DMA INT irq for PCIe\n");
		ret = -ENOENT;
		goto err;
	}

	ret = devm_request_threaded_irq(dev, dmac->dma_irq, rzt2h_pcie_dma_irq_handler,
					rzt2h_pcie_dma_irq_handler_thread,
					IRQF_SHARED, "PCIE DMA", pcie);
	if (ret) {
		dev_err(dev, "cannot request DMA INT irq\n");
		goto err;
	}

	dmac->n_channels = PCIE_DMA_MAX_CHANNEL;
	dmac->channels = devm_kcalloc(dev, dmac->n_channels,
				   sizeof(struct rzt2h_pcie_dma_chan), GFP_KERNEL);
	if (!dmac->channels) {
		ret = -ENOMEM;
		goto err;
	}

	dmac->pcie = pcie;
	dmac->dev = dev;

	INIT_LIST_HEAD(&dmac->engine.channels);

	for (i = 0; i < dmac->n_channels; i++) {
		struct rzt2h_pcie_dma_chan *chan = &dmac->channels[i];

		chan->dmac = dmac;
		chan->index = i;
		chan->desc = NULL;
		chan->vc.desc_free = rzt2h_pcie_dma_free_desc;
		vchan_init(&chan->vc, engine);
	}

	/* Set DMA channel capabilities */
	dma_cap_zero(engine->cap_mask);
	dma_cap_set(DMA_SLAVE, engine->cap_mask);
	engine->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	engine->dev = dev;

	engine->device_alloc_chan_resources = rzt2h_pcie_dma_alloc_chan_resources;
	engine->device_free_chan_resources = rzt2h_pcie_dma_free_chan_resources;
	engine->device_tx_status = rzt2h_pcie_dma_tx_status;
	engine->device_prep_slave_sg = rzt2h_pcie_dma_prep_slave_sg;
	engine->device_config = rzt2h_pcie_dma_config;
	engine->device_terminate_all = rzt2h_pcie_dma_terminate_all;
	engine->device_issue_pending = rzt2h_pcie_dma_issue_pending;
	engine->device_pause = rzt2h_pcie_dma_pause;
	dma_set_max_seg_size(engine->dev, U32_MAX);

	ret = dma_async_device_register(engine);
	if (ret < 0) {
		dev_err(dev, "unable to register DMA engine\n");
		goto err;
	}

	dev_info(dev, "successfully register private DMA for PCIe\n");
err:
	return ret;
}

void rzt2h_pcie_dma_remove(struct rzt2h_pcie *pcie)
{
	struct rzt2h_pcie_dmac *dmac = &pcie->dmac;
	int i;

	dma_async_device_unregister(&dmac->engine);

	for (i = 0; i < dmac->n_channels; i++) {
		struct rzt2h_pcie_dma_chan *chan = &dmac->channels[i];

		if (!chan->desc)
			rzt2h_pcie_dma_release_desc(chan->desc);
	}
}

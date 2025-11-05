#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/debugfs.h>
#include <linux/pci-epf.h>
#include <linux/msi.h>
#include <linux/bitfield.h>
#include <linux/dma/pcie-rzg3s-dma.h>

#define PCI_DEVICE_ID_RENESAS_R9A09057          0x003a

#define DMA_CHAN_NUM	2
#define DMA_IRQ_NUM	1

#define DMA_REMOTE	0
#define DMA_LOCAL	1

#define RZ_BLOCK(a, b, c) \
	{ \
		.bar = a, \
		.off = b, \
		.sz = c, \
	},

struct rz_dma_block {
	enum pci_barno			bar;
	off_t				off;
	size_t				sz;
};

struct rz_dma_pcie_data {
	/* DMA AXI Bridge registers location */
	struct rz_dma_block		reg;
	/* DMA Linked List location */
	struct rz_dma_block		ll[RZG3S_PCI_DMA_MAX_CHANNEL];
	/* DMA Data location */
	struct rz_dma_block		dt[RZG3S_PCI_DMA_MAX_CHANNEL];
	/* Number of irqs to be allocated */
	u8				irq;
	/* Number of DMA channels to be used */
	u16				ch_cnt;
};

struct rz_dma_pcie_test {
	struct pci_dev 		*pdev;
	struct dma_chan 	*local_chan[DMA_CHAN_NUM];
	struct dma_chan		*remote_chan[DMA_CHAN_NUM];
	struct dma_chan		*current_chan;
	struct completion	transfer_complete;
	struct dentry		*debugfs;
	struct rz_pcie		*pcie;
	dma_cookie_t		transfer_cookie;
	enum dma_status		transfer_status;
	u32			dma_size;
	u32			stress_count;
	u8			local_ch_cnt;
	u8			remote_ch_cnt;
	u8			chan;
	bool			mode;
};

static const struct rz_dma_pcie_data rzv2h_pcie_test_data = {
	/* DMA AXI Bridge registers location */
	.reg.bar			= BAR_4,
	.reg.off			= 0x00000000,
	.reg.sz				= 0x00001200,
	/* DMA Linked List location */
	.ll = {
		/* Channel 0 - BAR 0, offset 0x800, size 0x1000 */
		RZ_BLOCK(BAR_0, 0x00000800, 0x00001000)
		/* Channel 1 - BAR 0, offset 0x800, size 0x1000 */
		RZ_BLOCK(BAR_0, 0x00002000, 0x00001000)
	},
	/* DMA Data Location */
	.dt = {
		/* Channel 0 - BAR 0, offset 0x5000, size 0x100000 */
		RZ_BLOCK(BAR_0, 0x00005000, 0x00100000)
		/* Channel 1 - BAR 0, offset 0x106000, size 0x100000 */
		RZ_BLOCK(BAR_0, 0x00106000, 0x00100000)
	},
	/* Others */
	.irq				= DMA_IRQ_NUM,
	.ch_cnt				= DMA_CHAN_NUM,
};

struct pci_dma_filter {
	struct device *dev;
	u32 dma_mask;
};

static bool rz_dma_pcie_filter_fn(struct dma_chan *chan, void *node)
{
	struct pci_dma_filter *filter = node;
	struct dma_slave_caps caps;

	memset(&caps, 0, sizeof(caps));
	dma_get_slave_caps(chan, &caps);

	return chan->device->dev == filter->dev
	       && (filter->dma_mask & caps.directions);
}

static int rz_dma_pcie_request_dma_chan(struct rz_dma_pcie_test *test)
{
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	struct pci_dma_filter filter;
	struct pci_host_bridge *host;
	struct dma_chan *dma_chan;
	dma_cap_mask_t mask;
	int i;

	filter.dma_mask = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Request local dma channel */
	host = pci_find_host_bridge(pdev->bus);
	filter.dev = host->dev.parent;
	for (i = 0; i < DMA_CHAN_NUM; i++) {
		dma_chan = dma_request_channel(mask, rz_dma_pcie_filter_fn, &filter);
		if (!dma_chan) {
			break;
		}

		test->local_chan[i] = dma_chan;
		test->local_ch_cnt++;
	}

	/* Request remote dma channel */
	filter.dev = dev;
	for (i = 0; i < DMA_CHAN_NUM; i++) {
		dma_chan = dma_request_channel(mask, rz_dma_pcie_filter_fn, &filter);
		if (!dma_chan) {
			break;
		}

		test->remote_chan[i] = dma_chan;
		test->remote_ch_cnt++;
	}

	if (test->local_ch_cnt == 0 && test->remote_ch_cnt == 0) {
		pci_err(pdev, "DMA local and remote not available\n");
		goto err;
	} else if (test->local_ch_cnt == 0) {
		pci_err(pdev, "DMA local not available\n");
	} else if (test->remote_ch_cnt == 0) {
		pci_err(pdev, "DMA remote not available\n");
	}

	return 0;
err:
	return -ENODEV;
}

static void rz_dma_pcie_clean_dma_chan(struct rz_dma_pcie_test *test)
{
	int i;

	for (i = 0; i < test->local_ch_cnt; i++) {
		dma_release_channel(test->local_chan[i]);
		test->local_chan[i] = NULL;
	}

	for (i = 0; i < test->remote_ch_cnt; i++) {
		dma_release_channel(test->remote_chan[i]);
		test->remote_chan[i] = NULL;
	}
}

static void rz_dma_pcie_callback(void *param)
{
	struct rz_dma_pcie_test *test = param;
	struct dma_tx_state state;

	test->transfer_status =
		dmaengine_tx_status(test->current_chan,
				    test->transfer_cookie, &state);
	if (test->transfer_status == DMA_COMPLETE ||
	    test->transfer_status == DMA_ERROR)
		complete(&test->transfer_complete);
}

static int rz_dma_pcie_dma_transfer(struct rz_dma_pcie_test *test,
				    dma_addr_t dma_local, dma_addr_t dma_remote,
				    size_t len, enum dma_transfer_direction dir)
{
	struct dma_chan *chan = test->current_chan;
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config sconf = {};
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct device *dev = &test->pdev->dev;
	int ret = 0;

	sconf.direction = dir;
	if (dir == DMA_MEM_TO_DEV)
		sconf.dst_addr = dma_remote;
	else
		sconf.src_addr = dma_remote;

	if (dmaengine_slave_config(chan, &sconf)) {
		return -EIO;
	}

	tx = dmaengine_prep_slave_single(chan, dma_local, len, dir, flags);
	if (!tx) {
		return -EIO;
	}

	reinit_completion(&test->transfer_complete);
	tx->callback = rz_dma_pcie_callback;
	tx->callback_param = test;
	test->transfer_cookie = dmaengine_submit(tx);
	ret = dma_submit_error(test->transfer_cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA tx_submit %d\n", ret);
		goto terminate;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_timeout(&test->transfer_complete, msecs_to_jiffies(1000));
	if (ret < 0) {
		dev_err(dev, "DMA wait_for_completion interrupted\n");
		ret = -ETIME;
		goto terminate;
	}

	if (test->transfer_status == DMA_ERROR) {
		dev_err(dev, "DMA transfer failed\n");
		ret = -EIO;
		goto terminate;
	}

	return 0;
terminate:
	dmaengine_terminate_sync(chan);

	return ret;
}

static void rz_dma_pcie_print_test_log(struct rz_dma_pcie_test *test, u64 write_ns,
					u64 read_ns)
{
	struct device *dev = &test->pdev->dev;
	u64 write_rate = 0, read_rate = 0;

	if (write_ns && read_ns) {
		write_rate = div64_u64(test->dma_size * NSEC_PER_SEC, write_ns * 1000);
		read_rate = div64_u64(test->dma_size * NSEC_PER_SEC, read_ns * 1000);
	}

	dev_info(dev, "Mode: %s DMA WRITE\t Size: %u bytes\t Avg.rate: %llu KB/s\n",
		 (test->mode == DMA_LOCAL) ? "LOCAL" : "REMOTE",
		 test->dma_size, write_rate);

	dev_info(dev, "Mode: %s DMA READ\t Size: %u bytes\t Avg.rate: %llu KB/s\n",
		 (test->mode == DMA_LOCAL) ? "LOCAL" : "REMOTE",
		 test->dma_size, read_rate);
}

static int rz_dma_pcie_start_test(struct seq_file *s, void *data)
{
	struct rz_dma_pcie_test *test = (struct rz_dma_pcie_test*)dev_get_drvdata(s->private);
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	struct timespec64 start, end;
	struct timespec64 write_ts, read_ts;
	void *write_buf;
	void *read_buf;
	phys_addr_t src_phys_addr, dst_phys_addr;
	u64 ep_test_addr;
	size_t size;
	int index, ret = 0;
	int pass_count = 0;
	u64 write_ns, read_ns;

	index = test->chan;
	size = test->dma_size;

	if (test->mode == DMA_REMOTE) {
		struct rzg3s_pcie_dma_region *dt_region;
		if (index > test->remote_ch_cnt - 1) {
			pci_err(pdev, "remote channel %d not available\n", index);
			ret = -EINVAL;
			goto err;
		}

		test->current_chan = test->remote_chan[index];
		dt_region = (struct rzg3s_pcie_dma_region *)test->current_chan->private;
		if (!dt_region) {
			pci_err(pdev, "missing data region\n");
			ret = -ENOMEM;
			goto err;
		}

		if (size > dt_region->sz) {
			pci_err(pdev, "size too large\n");
			ret = -EINVAL;
			goto err;
		}
		/* For remote DMA, use reserved data region as test memory */
		ep_test_addr = dt_region->paddr;
	} else if (test->mode == DMA_LOCAL) {
		if (index > test->local_ch_cnt - 1) {
			pci_err(pdev, "local channel %d not available", index);
			ret = -EINVAL;
			goto err;
		}

		test->current_chan = test->local_chan[index];
		/* For local DMA, use memory starting at offset 0x400 from BAR0 */
		ep_test_addr = pci_resource_start(pdev, BAR_0) + SZ_1K;
	}

        write_buf = dma_alloc_coherent(dev, size, &src_phys_addr, GFP_KERNEL);
        if (!write_buf) {
                ret = -ENOMEM;
                goto err;
        }

        read_buf = dma_alloc_coherent(dev, size, &dst_phys_addr, GFP_KERNEL);
        if (!read_buf) {
                ret = -ENOMEM;
                goto err;
        }

	get_random_bytes(write_buf, size);

	for (int i = 0; i < test->stress_count; i++) {
		/* Transfer data from rootcomplex to endpoint */
		ktime_get_ts64(&start);
		if (rz_dma_pcie_dma_transfer(test, src_phys_addr, ep_test_addr, size, DMA_MEM_TO_DEV))
			continue;

		ktime_get_ts64(&end);
		write_ts = timespec64_sub(end, start);

		/* Transfer data from endpoint to rootcomplex */
		ktime_get_ts64(&start);
		if (rz_dma_pcie_dma_transfer(test, dst_phys_addr, ep_test_addr, size, DMA_DEV_TO_MEM))
			continue;

		ktime_get_ts64(&end);
		read_ts = timespec64_sub(end, start);

		if (memcmp(write_buf, read_buf, size) == 0) {
			/* If success, print test log */
			pass_count++;
			write_ns = timespec64_to_ns(&write_ts);
			read_ns = timespec64_to_ns(&read_ts);
			rz_dma_pcie_print_test_log(test, write_ns, read_ns);
		}
	}

	dev_info(dev, "Pass: %u/%u\n", pass_count, test->stress_count);

	dma_free_coherent(dev, size, write_buf, src_phys_addr);
	dma_free_coherent(dev, size, read_buf, dst_phys_addr);
err:
	return ret;
}

static void rz_dma_pcie_init_debugfs(struct rz_dma_pcie_test *test)
{
	struct pci_dev *pdev = test->pdev;
	char *name;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s", pdev->driver->name);
	test->debugfs = debugfs_create_dir(name, NULL);

	debugfs_create_devm_seqfile(&pdev->dev, "test", test->debugfs, rz_dma_pcie_start_test);
	debugfs_create_u8("channel", 0644, test->debugfs, &test->chan);
	test->chan = 0;

	debugfs_create_u32("stress_count", 0644, test->debugfs, &test->stress_count);
	test->stress_count = 10;

	debugfs_create_u32("dma_size", 0644, test->debugfs, &test->dma_size);
	test->dma_size = SZ_1M;

	debugfs_create_bool("dma_mode", 0644, test->debugfs, &test->mode);
	test->mode = DMA_REMOTE;
}

static void rz_dma_pcie_remove_debugfs(struct rz_dma_pcie_test *test)
{
	debugfs_remove_recursive(test->debugfs);
}

static int rz_dma_pcie_irq_vector(struct device *dev, unsigned int nr)
{
	return pci_irq_vector(to_pci_dev(dev), nr);
}

static int rz_dma_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct rz_dma_pcie_data *pdata = (void *)pid->driver_data;
	struct device *dev = &pdev->dev;
	struct rz_pcie *pcie;
	struct rz_dma_pcie_test *test;
	int i, ret, mask, nr_irqs;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	test = devm_kzalloc(dev, sizeof(*test), GFP_KERNEL);
	if (!test)
		return -ENOMEM;

	test->pdev = pdev;
	test->pcie = pcie;
	test->local_ch_cnt = 0;
	test->remote_ch_cnt = 0;
	init_completion(&test->transfer_complete);
	/* Enable PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "enabling device failed\n");
		return ret;
	}

	/* Mapping PCI BAR region */
	mask = BIT(pdata->reg.bar);
	for (i = 0; i < pdata->ch_cnt; i++) {
		mask |= BIT(pdata->ll[i].bar);
		mask |= BIT(pdata->dt[i].bar);
	}

	ret = pcim_iomap_regions(pdev, mask, pci_name(pdev));
	if (ret) {
		pci_err(pdev, "DMA BAR I/O remapping failed\n");
		return ret;
	}

	pci_set_master(pdev);

	/* DMA mask configuration */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		pci_err(pdev, "DMA mask 64 set failed\n");
		return ret;
	}

	/* MSI IRQs allocation */
	nr_irqs = pci_alloc_irq_vectors(pdev, 1, pdata->irq, PCI_IRQ_MSI);
	if (nr_irqs < 1) {
		pci_err(pdev, "Fail to alloc IRQ vector (number of IRQs=%u)\n",
								nr_irqs);
		return -EPERM;
	}

	/* Data structure initialization */
	pcie->dev = dev;
	pcie->nr_irqs = nr_irqs;
	pcie->ch_cnt = pdata->ch_cnt;
	pcie->base = pcim_iomap_table(pdev)[pdata->reg.bar];
	pcie->irq_vector = rz_dma_pcie_irq_vector;

	if (!pcie->base)
		return -ENOMEM;

	if (pci_resource_len(pdev, pdata->reg.bar) < pdata->reg.sz)
		return -EINVAL;

	for (i = 0; i < pdata->ch_cnt; i++) {
		struct rzg3s_pcie_dma_region *ll_region = &pcie->ll_region[i];
		struct rzg3s_pcie_dma_region *dt_region = &pcie->dt_region[i];
		struct rz_dma_block *ll_block = &pdata->ll[i];
		struct rz_dma_block *dt_block = &pdata->dt[i];
		/* Virtual address and size for linked list memory */
		ll_region->bar = ll_block->bar;
		ll_region->vaddr = pcim_iomap_table(pdev)[ll_block->bar];
		if (!ll_region->vaddr)
			return -ENOMEM;

		ll_region->vaddr += ll_block->off;
		ll_region->off = ll_block->off;
		ll_region->sz = ll_block->sz;

		/* Virtual address and size for data memory */
		dt_region->bar = dt_block->bar;
		dt_region->vaddr = pcim_iomap_table(pdev)[dt_block->bar];
		if (!dt_region->vaddr)
			return -ENOMEM;

		dt_region->vaddr += dt_block->off;
		dt_region->off = dt_block->off;
		dt_region->sz = dt_block->sz;
	}

	/* Validating if PCI interrupts were enabled */
	if (!pci_dev_msi_enabled(pdev)) {
		pci_err(pdev, "Enable interrupt failed\n");
		return -EPERM;
	}

	/* Saving data structure reference */
	pci_set_drvdata(pdev, test);

	/* Register remote DMA Engine for this PCIe driver.
	 * If remote DMA Engine probes failed, use local DMA only.
	 */
	ret = rzg3s_pcie_dma_probe(pcie, true);
	if (ret && ret != -ENODEV) {
		pci_err(pdev, "Failed to register remote DMA for PCIe\n");
	}

	ret = rz_dma_pcie_request_dma_chan(test);
	if (ret) {
		goto err_request_dma;
	}

	rz_dma_pcie_init_debugfs(test);

	/* Print debug info */
	pci_dbg(pdev, "Control region:\tBAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%px)\n",
		pdata->reg.bar, pdata->reg.off, pdata->reg.sz,
		pcie->base);

	for (i = 0; i < pdata->ch_cnt; i++) {
		pci_dbg(pdev, "Linked List region:\tchan%.2u, BAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%px, p=0x%llx)\n",
			i, pdata->ll[i].bar,
			pdata->ll[i].off, pcie->ll_region[i].sz,
			pcie->ll_region[i].vaddr, pcie->ll_region[i].paddr);

		pci_dbg(pdev, "Data region:\tchan%.2u, BAR=%u, off=0x%.8lx, sz=0x%zx bytes, addr(v=%px, p=0x%llx))\n",
			i, pdata->dt[i].bar,
			pdata->dt[i].off, pcie->dt_region[i].sz,
			pcie->dt_region[i].vaddr, pcie->dt_region[i].paddr);
	}

	return 0;
err_request_dma:
	rz_dma_pcie_clean_dma_chan(test);
	return ret;
}

static void rz_dma_pcie_remove(struct pci_dev *pdev)
{

	struct rz_dma_pcie_test *test = pci_get_drvdata(pdev);
	struct rz_pcie *pcie = test->pcie;

	rz_dma_pcie_remove_debugfs(test);

	rz_dma_pcie_clean_dma_chan(test);

	rzg3s_pcie_dma_remove(pcie);

	/* Freeing IRQs */
	pci_free_irq_vectors(pdev);
}

static const struct pci_device_id rz_dma_pcie_id_table[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_R9A09057),
		  .driver_data = (kernel_ulong_t)&rzv2h_pcie_test_data,
		},
		{},
};

MODULE_DEVICE_TABLE(pci, rz_dma_pcie_id_table);

static struct pci_driver rz_pci_dma_test_driver = {
		.name		= "rz-dma-pcie-test",
		.id_table	= rz_dma_pcie_id_table,
		.probe		= rz_dma_pcie_probe,
		.remove		= rz_dma_pcie_remove,
};

module_pci_driver(rz_pci_dma_test_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas DMA PCIe test driver");

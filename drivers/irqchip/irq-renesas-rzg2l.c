// SPDX-License-Identifier: GPL-2.0
/*
 * RZG2L External Interrupt Controller
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 *
 * Base on irq-renesas-irqc.c
 *
 * Copyright (C) 2013 Magnus Damm
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/of_device.h>

#include <linux/irqchip/icu-v2h.h>

/* Register Offset and Bit mask */
#define NSCR		0x00	/* NMI Status Control Register */
#define NCSR_NSMON	BIT(16)
#define NCSR_NSTAT	BIT(0)
#define NSCLR		0x04	/* NMI status clear register */
#define NSCLR_NCLR	BIT(0)
#define NITSR(x)	(0x04 + 4 * (x)) /* NMI Interrupt Type Selection Register */
#define NITSR_NTSEL	BIT(0)
#define ISCR		0x10	/* IRQ Status Control Register */
#define ISCR_ISTAT(x)	BIT(x)
#define ISCLR		0x14	/* IRQ Status Clear Register */
#define ISCLR_ICLR(x)	BIT(x)
#define IITSR(x)	(0x14 + 4 * (x)) /* IRQ Interrupt Type Selection Register */

#define DMxSELy(x, y)	(0x0420 + (x) * 0x0020 + (y) * 0x0004) /* DMACx Factor Selection Register y */
#define DMACKSEL(x)	(0x0500 + (x) * 0x0004) /* DMAC ACK Selection Register x */
#define DMTENDSEL(x)	(0x055C + (x) * 0x0004) /* DMAC TEND Selection Register x */

/* Maximum 16 IRQ per driver instance */
#define IRQC_IRQ_MAX	16
/* Maximum 1 NMI per driver instance */
#define IRQC_NMI_MAX	1

#define IITSR_INIT	(0xffffffff) /* All IRQs set to Both Edge Detection */

/* Interrupt type support */
enum {
	IRQC_IRQ,
	IRQC_NMI,
};

struct rzg2l_hw_info {
	bool has_clrsr_reg;
	u16 iptsr_offset;
};

struct irqc_backup_data {
	u32 iitsr;
	u32 iptsr;
};

struct irqc_irq {
	int hw_irq;
	int requested_irq;
	int type;
	struct irqc_priv *priv;
};

struct irqc_priv {
	void __iomem *base;
	struct irqc_irq irq[IRQC_IRQ_MAX + IRQC_NMI_MAX];
	unsigned int number_of_irqs;
	struct irq_chip_generic *gc;
	struct irq_domain *irq_domain;
	atomic_t wakeup_path;
	struct reset_control *rstc;
	const struct rzg2l_hw_info *data;
	void *backup_data;
};

static struct irqc_priv *irq_data_to_priv(struct irq_data *data)
{
	return data->domain->host_data;
}

static unsigned char irqc_irq_sense[IRQ_TYPE_SENSE_MASK + 1] = {
	/*
	 * As HW manual, can not clear low level detection interrupt,
	 * Therefore, it should not be supported in driver
	 */

	[IRQ_TYPE_LEVEL_LOW]	= 0x00,
	[IRQ_TYPE_EDGE_FALLING]	= 0x01,
	[IRQ_TYPE_EDGE_RISING]	= 0x02,
	[IRQ_TYPE_EDGE_BOTH]	= 0x03,
};

static unsigned char irqc_nmi_sense[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_EDGE_FALLING]	= 0x0,
	[IRQ_TYPE_EDGE_RISING]	= 0x1,
};

static int irqc_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irqc_priv *priv = irq_data_to_priv(d);
	int hw_irq = irqd_to_hwirq(d);

	if (priv->irq[hw_irq].type == IRQC_NMI) {
		writel(irqc_nmi_sense[type],
		       priv->base + NITSR(priv->data->has_clrsr_reg));
	} else if (priv->irq[hw_irq].type == IRQC_IRQ) {
		unsigned char value = irqc_irq_sense[type];
		u32 tmp;

		tmp = readl(priv->base + IITSR(priv->data->has_clrsr_reg));
		tmp &= ~(0x3 << (hw_irq * 2));
		tmp |= (value << (hw_irq * 2));
		writel(tmp, priv->base + IITSR(priv->data->has_clrsr_reg));
	}

	return 0;
}

static int irqc_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct irqc_priv *priv = irq_data_to_priv(d);
	int hw_irq = irqd_to_hwirq(d);

	irq_set_irq_wake(priv->irq[hw_irq].requested_irq, on);
	if (on)
		atomic_inc(&priv->wakeup_path);
	else
		atomic_dec(&priv->wakeup_path);

	return 0;
}

int register_dmac_req_signal(struct platform_device *icu_dev, unsigned int dmac,
				unsigned int channel, int dmac_req)
{
	struct irqc_priv *priv = platform_get_drvdata(icu_dev);
	u32 y, low_up, dmsel;
	u32 mask = 0x0000FFFF;

	if ((dmac_req < 0) || (dmac_req > 0x1B4))
		dev_dbg(&icu_dev->dev, "%s: Disable dmac req signal\n", __func__);

	if ((channel < 0) || (channel > 15)) {
		dev_dbg(&icu_dev->dev, "%s: Invalid channel\n", __func__);
		return -EINVAL;
	}

	y = channel / 2;
	low_up = channel % 2;

	dmsel = readl(priv->base + DMxSELy(dmac, y));

	if (low_up) {
		dmac_req <<= 16;
		mask <<= 16;
	}

	dmsel = (dmsel & (~mask)) | dmac_req;

	writel(dmsel, priv->base + DMxSELy(dmac, y));

	return 0;
}

EXPORT_SYMBOL(register_dmac_req_signal);

int register_dmac_ack_signal(struct platform_device *icu_dev, int dmac_ack, int dmac_ack_channel)
{
	struct irqc_priv *priv = platform_get_drvdata(icu_dev);
	u32 reg_position, dmacksel, mask;

	if ((dmac_ack_channel < 0) || (dmac_ack_channel > 0x4F))
		dev_dbg(&icu_dev->dev, "%s: Disable dmac ack signal\n", __func__);

	if ((dmac_ack < 0) || (dmac_ack > 88))
		dev_dbg(&icu_dev->dev, "%s: Not use dmac ack\n", __func__);

	reg_position = dmac_ack / 4;
	dmacksel = readl(priv->base + DMACKSEL(reg_position));

	mask = 0x7F << (8 * (dmac_ack % 4));
	dmac_ack_channel <<= (8 * (dmac_ack % 4));
	dmacksel = (dmacksel  & (~mask)) | dmac_ack_channel;

	writel(dmacksel, priv->base + DMACKSEL(reg_position));

	return 0;
}

EXPORT_SYMBOL(register_dmac_ack_signal);

static irqreturn_t irqc_irq_handler(int irq, void *dev_id)
{
	struct irqc_irq *irqc = (struct irqc_irq *)dev_id;
	struct irqc_priv *priv = irqc->priv;
	u32 bit, val, reg;

	if (irqc->type == IRQC_NMI) {
		reg = NSCR;
		bit = NCSR_NSTAT;
	} else if (irqc->type == IRQC_IRQ) {
		reg = ISCR;
		bit = BIT(irqc->hw_irq);
	} else
		return IRQ_NONE;
	val = readl(priv->base + reg);
	if (val & bit) {
		if (priv->data->has_clrsr_reg) {
			if (irqc->type == IRQC_NMI)
				reg = NSCLR;
			else
				reg = ISCLR;
		}
		writel((priv->data->has_clrsr_reg) ? bit : (~bit), priv->base + reg);
		generic_handle_irq(irq_find_mapping(priv->irq_domain,
                                                irqc->hw_irq));
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int irqc_request_irq(struct platform_device *pdev,
			int irq_type,
			const char * const irqs_name[], int n)
{
	struct irqc_priv *priv = (struct irqc_priv *)platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int k, irq;

	for (k = 0 ; k < n; k++) {
		irq = platform_get_irq_byname(pdev, irqs_name[k]);
		if (irq < 0) {
			dev_err(dev, "No IRQ resource\n");
			break;
		}

		priv->irq[k + priv->number_of_irqs].priv = priv;
		priv->irq[k + priv->number_of_irqs].hw_irq =
						k + priv->number_of_irqs;
		priv->irq[k + priv->number_of_irqs].requested_irq = irq;
		priv->irq[k + priv->number_of_irqs].type = irq_type;
	}

	priv->number_of_irqs += k;

	return k;
}

static int irqc_prepare_mem_suspend(struct platform_device *pdev,
					struct irqc_priv *priv)
{
	struct irqc_backup_data *irqc_backup;

	irqc_backup = devm_kzalloc(&pdev->dev, sizeof(*irqc_backup), GFP_KERNEL);
	if (!irqc_backup)
		return -ENOMEM;

	priv->backup_data = irqc_backup;
	return 0;
}

static int irqc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct irqc_priv *priv;
	struct resource *res;
	const char *name = dev_name(dev);
	int k, ret;
	const char * const irqs_name[] = {"irq0", "irq1", "irq2", "irq3",
					"irq4", "irq5", "irq6", "irq7",
					"irq8", "irq9", "irq10", "irq11",
					"irq12", "irq13", "irq14", "irq15"};
	const char * const nmis_name[] = {"nmi"};
	int irq_numbers;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(&pdev->dev);
	if (!priv->data)
		return -EINVAL;

	ret = irqc_prepare_mem_suspend(pdev, priv);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, priv);

	priv->rstc = devm_reset_control_get(dev, NULL);

	if (IS_ERR(priv->rstc))
		dev_warn(dev, "failed to get reset controller\n");
	else
		reset_control_deassert(priv->rstc);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/* Count irq numbers including IRQC and NMI */
	irq_numbers = of_property_count_strings(dev->of_node,
						"interrupt-names");
	if ((irq_numbers < 0) ||
	    (irq_numbers > (IRQC_IRQ_MAX + IRQC_NMI_MAX))) {
		dev_err(dev, "wrong number of IRQs resources\n");
		ret = -EINVAL;
		goto err1;
	}

	/* allow any number of IRQs between 1 and IRQC_IRQ_MAX */
	irqc_request_irq(pdev, IRQC_IRQ, irqs_name, irq_numbers - 1);
	/* allow any number of IRQs between 1 and IRQC_NMI_MAX */
	irqc_request_irq(pdev, IRQC_NMI, nmis_name, IRQC_NMI_MAX);

	if (priv->number_of_irqs < 1) {
		dev_err(dev, "not enough IRQ resources\n");
		ret = -EINVAL;
		goto err1;
	}

	priv->irq_domain = irq_domain_add_linear(pdev->dev.of_node,
					      priv->number_of_irqs,
					      &irq_generic_chip_ops, priv);
	if (!priv->irq_domain) {
		ret = -ENXIO;
		dev_err(dev, "cannot initialize irq domain\n");
		goto err1;
	}

	ret = irq_alloc_domain_generic_chips(priv->irq_domain,
					     priv->number_of_irqs,
					     1, name, handle_level_irq,
					     0, 0, IRQ_GC_INIT_NESTED_LOCK);
	if (ret) {
		dev_err(&pdev->dev, "cannot allocate generic chip\n");
		goto err1;
	}

	priv->gc = irq_get_domain_generic_chip(priv->irq_domain, 0);
	priv->gc->reg_base = priv->base;
	priv->gc->chip_types[0].chip.irq_set_type = irqc_irq_set_type;
	priv->gc->chip_types[0].chip.irq_set_wake = irqc_irq_set_wake;
	priv->gc->chip_types[0].chip.parent_device = dev;
	priv->gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
	priv->gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
	/* Support Edge detection only */
	priv->gc->chip_types[0].chip.flags = IRQCHIP_SET_TYPE_MASKED;

	/* Initialized with BOTH_EDGE_LEVEL */
	writel(IITSR_INIT, priv->base + IITSR(priv->data->has_clrsr_reg));

	/* request interrupts one by one */
	for (k = 0; k < priv->number_of_irqs; k++) {
		if (request_irq(priv->irq[k].requested_irq, irqc_irq_handler,
				0, name, &priv->irq[k])) {
			dev_err(&pdev->dev, "failed to request IRQ\n");
			ret = -ENOENT;
			goto err2;
		}
	}

	dev_info(&pdev->dev, "driving %d irqs\n", priv->number_of_irqs);

	return 0;
err2:
	while (--k >= 0)
		free_irq(priv->irq[k].requested_irq, &priv->irq[k]);

	irq_domain_remove(priv->irq_domain);
err1:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int irqc_remove(struct platform_device *pdev)
{
	struct irqc_priv *p = platform_get_drvdata(pdev);
	int k;

	for (k = 0; k < p->number_of_irqs; k++)
		free_irq(p->irq[k].requested_irq, &p->irq[k]);

	irq_domain_remove(p->irq_domain);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static void irqc_backup(struct irqc_priv *p)
{
	struct irqc_backup_data *irqc_backup = p->backup_data;
	const struct rzg2l_hw_info *priv_data = p->data;

	irqc_backup->iitsr = readl(p->base + IITSR(priv_data->has_clrsr_reg));

	if (priv_data->iptsr_offset)
		irqc_backup->iptsr = readl(p->base + priv_data->iptsr_offset);
}

static void irqc_restore(struct irqc_priv *p)
{
	struct irqc_backup_data *irqc_backup = p->backup_data;
	const struct rzg2l_hw_info *priv_data = p->data;

	if (priv_data->iptsr_offset)
		writel(irqc_backup->iptsr, p->base + priv_data->iptsr_offset);

	writel(irqc_backup->iitsr, p->base + IITSR(priv_data->has_clrsr_reg));
}

static int __maybe_unused irqc_suspend(struct device *dev)
{
	struct irqc_priv *p = dev_get_drvdata(dev);

	irqc_backup(p);

	if (atomic_read(&p->wakeup_path))
		device_set_wakeup_path(dev);

	return 0;
}

int __maybe_unused irqc_resume(struct device *dev)
{
	struct irqc_priv *p = dev_get_drvdata(dev);

	irqc_restore(p);

	return 0;
}

static const struct dev_pm_ops irqc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(irqc_suspend, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(NULL, irqc_resume)
};

static const struct rzg2l_hw_info rzg2l_params = {
	.has_clrsr_reg = false,
	.iptsr_offset = 0,
};

static const struct rzg2l_hw_info rzv2h_params = {
	.has_clrsr_reg = true,
	.iptsr_offset = 0x60,
};

static const struct of_device_id irqc_dt_ids[] = {
	{ .compatible = "renesas,rzg2l-irqc", .data = &rzg2l_params, },
	{ .compatible = "renesas,rzv2l-irqc", .data = &rzg2l_params, },
	{ .compatible = "renesas,rzg2ul-irqc", .data = &rzg2l_params,},
	{ .compatible = "renesas,rzv2h-irqc", .data = &rzv2h_params, },
	{},
};
MODULE_DEVICE_TABLE(of, irqc_dt_ids);

static struct platform_driver irqc_device_driver = {
	.probe		= irqc_probe,
	.remove		= irqc_remove,
	.driver		= {
		.name	= "rzg2l_irqc",
		.of_match_table	= irqc_dt_ids,
		.pm	= &irqc_pm_ops,
	}
};
module_platform_driver(irqc_device_driver);

MODULE_DESCRIPTION("RZG2L IRQC Driver");
MODULE_LICENSE("GPL v2");

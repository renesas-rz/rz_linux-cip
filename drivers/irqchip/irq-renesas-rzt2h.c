// SPDX-License-Identifier: GPL-2.0
/*
 * RZT2H External Interrupt Controller
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 *
 * Base on irq-renesas-irqc.c
 *
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
#include <linux/irqchip/icu-t2h.h>

/* Maximum 16 IRQ per driver instance */
#define IRQC_IRQ_MAX	16

#define NS_PORTNF_MD		0x0C
#define S_PORTNF_MD		0x0C
#define NS_PORTNF_MD_INIT	(0x3fffffff)
#define S_PORTNF_MD_INIT	(0x3F)
#define DMAC0_RSSELi(i)		(0x7D0 + 0x04 * (i)) /* DMAC Unit 0 Resource Select Register i */
#define DMAC1_RSSELi(i)        (0x7E8 + 0x04 * (i)) /* DMAC Unit 1 Resource Select Register i */
#define DMAC2_RSSELi(i)        (0x800 + 0x04 * (i)) /* DMAC Unit 2 Resource Select Register i */

/* Interrupt type support */
enum {
	IRQC_IRQ,
};

struct irqc_irq {
	int hw_irq;
	int requested_irq;
	int type;
	struct irqc_priv *priv;
};

struct irqc_priv {
	void __iomem *base, *base1;
	struct irqc_irq irq[IRQC_IRQ_MAX];
	unsigned int number_of_irqs;
	struct irq_chip_generic *gc;
	struct irq_domain *irq_domain;
	atomic_t wakeup_path;
	bool safety_region;
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

static int irqc_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irqc_priv *priv = irq_data_to_priv(d);
	int hw_irq = irqd_to_hwirq(d);

	if (priv->irq[hw_irq].type == IRQC_IRQ) {
		unsigned char value = irqc_irq_sense[type];
		u32 tmp;

		if (hw_irq < 14) {
			tmp = readl(priv->base + NS_PORTNF_MD);
			tmp &= ~(0x3 << (hw_irq * 2));
			tmp |= (value << (hw_irq * 2));
			writel(tmp, priv->base + NS_PORTNF_MD);
		} else {
			tmp = readl(priv->base1 + S_PORTNF_MD);
			tmp &= ~(0x3 << (hw_irq * 2));
			tmp |= (value << (hw_irq * 2));
			writel(tmp, priv->base1 + S_PORTNF_MD);
		}
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
	u32 i, balance, dmsel;
	u32 mask = 0x000003FF;

	if ((channel < 0) || (channel > 15)) {
		dev_dbg(&icu_dev->dev, "%s: Invalid channel\n", __func__);
		return -EINVAL;
	}
	i = channel / 3;
	balance = channel % 3;
	switch (dmac) {

	case 0:
		dmsel = readl(priv->base + DMAC0_RSSELi(i));
		if (balance == 1) {
					dmac_req <<= 10;
					mask <<= 10;
		}
		if (balance == 2) {
					dmac_req <<= 20;
					mask <<= 20;
			}
		dmsel = (dmsel & (~mask)) | dmac_req;
			writel(dmsel, priv->base + DMAC0_RSSELi(i));
		break;
	case 1:
		dmsel = readl(priv->base + DMAC1_RSSELi(i));
		if (balance == 1) {
			dmac_req <<= 10;
			mask <<= 10;
		}
		if (balance == 2) {
			dmac_req <<= 20;
			mask <<= 20;
		}
		dmsel = (dmsel & (~mask)) | dmac_req;
		writel(dmsel, priv->base + DMAC1_RSSELi(i));
		break;
	case 2:
		dmsel = readl(priv->base + DMAC2_RSSELi(i));
		if (balance == 1) {
			dmac_req <<= 10;
			mask <<= 10;
		}
		if (balance == 2) {
			dmac_req <<= 20;
			mask <<= 20;
		}
		dmsel = (dmsel & (~mask)) | dmac_req;
		writel(dmsel, priv->base + DMAC2_RSSELi(i));
		break;
	}
	return 0;
}

EXPORT_SYMBOL(register_dmac_req_signal);

static irqreturn_t irqc_irq_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
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
	int irq_numbers;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->base1 = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base1))
		return PTR_ERR(priv->base1);

	/* Count irq numbers including IRQC and NMI */
	irq_numbers = of_property_count_strings(dev->of_node,
						"interrupt-names");
	if ((irq_numbers < 0) ||
	    (irq_numbers > IRQC_IRQ_MAX)) {
		dev_err(dev, "wrong number of IRQs resources\n");
		ret = -EINVAL;
		goto err1;
	}

	/* allow any number of IRQs between 1 and IRQC_IRQ_MAX */
	irqc_request_irq(pdev, IRQC_IRQ, irqs_name, irq_numbers);
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
	priv->gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
	priv->gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
	/* Support Edge detection only */
	priv->gc->chip_types[0].chip.flags = IRQCHIP_SET_TYPE_MASKED;

	/* Initialized with BOTH_EDGE_LEVEL */
	writel(NS_PORTNF_MD_INIT, priv->base + NS_PORTNF_MD);
	writel(S_PORTNF_MD_INIT, priv->base1 + S_PORTNF_MD);

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

static void irqc_remove(struct platform_device *pdev)
{
	struct irqc_priv *p = platform_get_drvdata(pdev);
	int k;

	for (k = 0; k < p->number_of_irqs; k++)
		free_irq(p->irq[k].requested_irq, &p->irq[k]);

	irq_domain_remove(p->irq_domain);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused irqc_suspend(struct device *dev)
{
	struct irqc_priv *p = dev_get_drvdata(dev);

	if (atomic_read(&p->wakeup_path))
		device_set_wakeup_path(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(irqc_pm_ops, irqc_suspend, NULL);

static const struct of_device_id irqc_dt_ids[] = {
	{ .compatible = "renesas,rzt2h-irqc", },
	{},
};
MODULE_DEVICE_TABLE(of, irqc_dt_ids);

static struct platform_driver irqc_device_driver = {
	.probe		= irqc_probe,
	.remove		= irqc_remove,
	.driver		= {
		.name	= "rzt2h_irqc",
		.of_match_table	= irqc_dt_ids,
		.pm	= &irqc_pm_ops,
	}
};
module_platform_driver(irqc_device_driver);

MODULE_DESCRIPTION("RZT2H IRQC Driver");
MODULE_LICENSE("GPL v2");

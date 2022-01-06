// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas PLIC Driver
 *
 *  Copyright (C) 2022 LI NAQIN
 */

#define pr_fmt(fmt) "plic: " fmt
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <asm/smp.h>

/*
 * This driver implements a version of the RISC-V PLIC.
 * 'RZFIve_NCEPLIC100SS'
 */

#define MAX_DEVICES			543
#define MAX_IRQ_LINE			1023
#define MAX_THRESHOLD			255

/*
 * This register enables preemptive priority interrupt feature and the vector mode.
 * [0]: vectored, vector mode enable
 * 	value 0: disabled
 * 	value 1: enabled
 * [1]: preempt, Preemptive priority interrupt enable
 * 	value 0: disabled
 * 	value 1: enabled
 */
#define FEATURE_ENABLE_REG		0x0

/*
 * Each interrupt source has a 32-bit priority register associated with it.
 * 	value 0:     never interrupt
 * 	value 1-255: larger value, higher priority
 */
#define PRIORITY_BASE			0x4
#define PRIORITY_PER_ID			4
#define PRIORITY_END                    0x880 

/*
 * Each interrupt source has a pending bit associated with it.
 * 	value 0: interrupt pending
 * 	value 1: interrupt not pending 
 */
#define PENDING_ARRAY_BASE		0x1000
#define PENDING_ARRAY_END		0x1080

/* 
 * Each interrupt source has a trigger type bit associated with it.
 * 	value 0: level-triggered
 * 	value 1: edge-triggered
 */
#define TRIGGER_TYPE_ARRAY_BASE		0x1080
#define TRIGGER_TYPE_ARRAY_END		0x1100

/*
 * This register indicates the number of supported interrupt sources and supported targets.
 * [31-16]: The number of supported targets
 * [15- 0]: The number of supported interrupt sources
 */
#define IRQ_TARGET_NUMBER_REG		0x1100

/*
 * This register indicates the version and the maximum priority of PLIC implementation.
 * [31-16]: The maximum priority supported
 * [15- 0]: The version of the PLIC design
 */
#define VERSION_AND_MAX_PRIORITY_REG 0x1104

/*
 * These registers control the routing of interrupt source n to target m.
 * (1 <=  n <=  1023 and m >=  0)
 * Each bit controls one interrupt source.
 */
#define ENABLE_BASE						0x2000
#define ENABLE_PER_HART					0x80

/*
 * Context space for each target
 */
#define CONTEXT_BASE	0x200000
#define CONTEXT_PER_HART	0x1000

/*
 * Each interrupt target m (m >= 0) is associated with one Priority Threshold Register.
 * Only active interrupts with priorities strictly greater than the threshold will 
 * cause an interrupt notification to be sent to the target.
 */
#define THRESHOLD_REG			0x0
#define CLAIM_COMPLETE_REG		0x4
#define PREEMPTED_PRIORITY_STACK_REG	0x400

#define PLIC_DISABLE_THRESHOLD		0xFF
#define PLIC_ENABLE_THRESHOLD		0x0


struct plic_priv {
	struct cpumask lmask;
	struct irq_domain *irqdomain;
	void __iomem *regs;
};

struct plic_handler {
	bool			present;
	void __iomem		*hart_base;
	/*
	 * Protect mask operations on the registers given that we can't
	 * assume atomic memory operations work on them.
	 */
	raw_spinlock_t		enable_lock;
	void __iomem		*enable_base;
	void __iomem		*trigger_type_base;
	struct plic_priv	*priv;
};
static int plic_parent_irq;
static bool plic_cpuhp_setup_done;
static DEFINE_PER_CPU(struct plic_handler, plic_handlers);

static inline void plic_toggle(struct plic_handler *handler,
				int hwirq, int enable)
{
	u32 __iomem *reg = handler->enable_base + (hwirq / 32) * sizeof(u32);
	u32 hwirq_mask = 1 << (hwirq % 32);

	raw_spin_lock(&handler->enable_lock);
	if (enable)
		writel(readl(reg) | hwirq_mask, reg);
	else
		writel(readl(reg) & ~hwirq_mask, reg);
	raw_spin_unlock(&handler->enable_lock);
}

static inline void plic_irq_toggle(const struct cpumask *mask,
				   struct irq_data *d, int enable)
{
	int cpu;
	struct plic_priv *priv = irq_data_get_irq_chip_data(d);

	writel(enable, priv->regs + PRIORITY_BASE + d->hwirq * PRIORITY_PER_ID);
	for_each_cpu(cpu, mask) {
		struct plic_handler *handler = per_cpu_ptr(&plic_handlers, cpu);

		if (handler->present &&
		    cpumask_test_cpu(cpu, &handler->priv->lmask))
			plic_toggle(handler, d->hwirq, enable);
	}
}

static void plic_irq_unmask(struct irq_data *d)
{
	struct cpumask amask;
	unsigned int cpu;
	struct plic_priv *priv = irq_data_get_irq_chip_data(d);

	cpumask_and(&amask, &priv->lmask, cpu_online_mask);
	cpu = cpumask_any_and(irq_data_get_affinity_mask(d),
					   &amask);
	if (WARN_ON_ONCE(cpu >= nr_cpu_ids))
		return;
	plic_irq_toggle(cpumask_of(cpu), d, 1);
}

static void plic_irq_mask(struct irq_data *d)
{
	struct plic_priv *priv = irq_data_get_irq_chip_data(d);

	plic_irq_toggle(&priv->lmask, d, 0);
}

#ifdef CONFIG_SMP
static int plic_set_affinity(struct irq_data *d,
			     const struct cpumask *mask_val, bool force)
{
	unsigned int cpu;
	struct cpumask amask;
	struct plic_priv *priv = irq_data_get_irq_chip_data(d);

	cpumask_and(&amask, &priv->lmask, mask_val);

	if (force)
		cpu = cpumask_first(&amask);
	else
		cpu = cpumask_any_and(&amask, cpu_online_mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	plic_irq_toggle(&priv->lmask, d, 0);
	plic_irq_toggle(cpumask_of(cpu), d, !irqd_irq_masked(d));

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK_DONE;
}
#endif

static void plic_irq_eoi(struct irq_data *d)
{
	struct plic_handler *handler = this_cpu_ptr(&plic_handlers);
	
	writel(d->hwirq, handler->hart_base + CLAIM_COMPLETE_REG);
}

static inline unsigned int plic_hwirq(struct irq_data *d)
{
	return d->hwirq;
}

static int plic_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned int hwirq = 0;
	u32 hwirq_mask = 0;
	u32 __iomem *reg = NULL;
	struct plic_handler *handler = this_cpu_ptr(&plic_handlers);

	hwirq = plic_hwirq(d);
	if(!hwirq || (plic_hwirq(d) > MAX_IRQ_LINE))
		return -EINVAL;

	/* Be sensitive for the edge-trigger, and set the level-trigger as default */
	reg = handler->trigger_type_base + (hwirq / 32) * sizeof(u32);
	hwirq_mask = (1 << (hwirq % 32));
  
	if(type & IRQ_TYPE_EDGE_BOTH)
		writel(readl(reg) | hwirq_mask, reg);	
	else
		writel(readl(reg) & ~hwirq_mask, reg);
	
	return 0;
}

static struct irq_chip plic_chip = {
	.name		= "RZ/Five PLIC",
	.irq_mask	= plic_irq_mask,
	.irq_unmask	= plic_irq_unmask,
	.irq_eoi	= plic_irq_eoi,
	.irq_set_type   = plic_irq_type,
#ifdef CONFIG_SMP
	.irq_set_affinity = plic_set_affinity,
#endif
};

static int plic_irqdomain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	struct plic_priv *priv = d->host_data;

	irq_domain_set_info(d, irq, hwirq, &plic_chip, d->host_data,
			    handle_fasteoi_irq, NULL, NULL);
	irq_set_noprobe(irq);
	irq_set_affinity(irq, &priv->lmask);
	return 0;
}

static int plic_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type;
	struct irq_fwspec *fwspec = arg;

	ret = irq_domain_translate_twocell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		ret = plic_irqdomain_map(domain, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct irq_domain_ops plic_irqdomain_ops = {
	.translate	= irq_domain_translate_twocell,
	.alloc		= plic_irq_domain_alloc,
	.free		= irq_domain_free_irqs_top,
};

/*
 * Handling an interrupt is a two-step process: first you claim the interrupt
 * by reading the claim register, then you complete the interrupt by writing
 * that source ID back to the same claim register.  This automatically enables
 * and disables the interrupt, so there's nothing else to do.
 */
static void plic_handle_irq(struct irq_desc *desc)
{
	struct plic_handler *handler = this_cpu_ptr(&plic_handlers);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	void __iomem *claim = handler->hart_base + CLAIM_COMPLETE_REG;
	unsigned int is_edge_trigger = 0;
	irq_hw_number_t hwirq;

	WARN_ON_ONCE(!handler->present);

	chained_irq_enter(chip, desc);

	while ((hwirq = readl(claim))) {
		int irq = irq_find_mapping(handler->priv->irqdomain, hwirq);
		
		/* Get the IRQ trigger type, if is edge-triggered, then complete the IRQ */
		is_edge_trigger = irq_get_trigger_type(irq) & IRQ_TYPE_EDGE_BOTH;
		if(is_edge_trigger)
			chained_irq_exit(chip, desc);

		if (unlikely(irq <= 0))
			pr_warn_ratelimited("can't find mapping for hwirq %lu\n",
					hwirq);
		else
			generic_handle_irq(irq);
	}

	/* If the trigger type is level-triggered, then complete it at the end */
	if(!is_edge_trigger)
		chained_irq_exit(chip, desc);
}

static void plic_set_threshold(struct plic_handler *handler, u32 threshold)
{
	/* priority must be > threshold to trigger an interrupt 
	 * NCEPLIC100SS allows max threshold is 255
	 * */
	if(threshold <= MAX_THRESHOLD)
		writel(threshold, handler->hart_base + THRESHOLD_REG);
}

static int plic_dying_cpu(unsigned int cpu)
{
	if (plic_parent_irq)
		disable_percpu_irq(plic_parent_irq);

	return 0;
}

static int plic_starting_cpu(unsigned int cpu)
{
	struct plic_handler *handler = this_cpu_ptr(&plic_handlers);

	if (plic_parent_irq)
		enable_percpu_irq(plic_parent_irq,
				  irq_get_trigger_type(plic_parent_irq));
	else
		pr_warn("cpu%d: parent irq not available\n", cpu);
	plic_set_threshold(handler, PLIC_ENABLE_THRESHOLD);

	return 0;
}

static int __init plic_init(struct device_node *node,
		struct device_node *parent)
{
	int error = 0, nr_contexts, nr_handlers = 0, i;
	u32 nr_irqs;
	struct plic_priv *priv;
	struct plic_handler *handler;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = of_iomap(node, 0);
	if (WARN_ON(!priv->regs)) {
		error = -EIO;
		goto out_free_priv;
	}

	error = -EINVAL;
	of_property_read_u32(node, "riscv,ndev", &nr_irqs);
	if (WARN_ON(!nr_irqs))
		goto out_iounmap;

	nr_contexts = of_irq_count(node);
	if (WARN_ON(!nr_contexts))
		goto out_iounmap;

	error = -ENOMEM;
	priv->irqdomain = irq_domain_add_linear(node, nr_irqs + 1,
			&plic_irqdomain_ops, priv);
	if (WARN_ON(!priv->irqdomain))
		goto out_iounmap;

	for (i = 0; i < nr_contexts; i++) {
		struct of_phandle_args parent;
		irq_hw_number_t hwirq;
		int cpu, hartid;

		if (of_irq_parse_one(node, i, &parent)) {
			pr_err("failed to parse parent for context %d.\n", i);
			continue;
		}

		/*
		 * Skip contexts other than external interrupts for our
		 * privilege level.
		 */
		if (parent.args[0] != RV_IRQ_EXT)
			continue;

		hartid = riscv_of_parent_hartid(parent.np);
		if (hartid < 0) {
			pr_warn("failed to parse hart ID for context %d.\n", i);
			continue;
		}

		cpu = riscv_hartid_to_cpuid(hartid);
		if (cpu < 0) {
			pr_warn("Invalid cpuid for context %d\n", i);
			continue;
		}

		/* Find parent domain and register chained handler */
		if (!plic_parent_irq && irq_find_host(parent.np)) {
			plic_parent_irq = irq_of_parse_and_map(node, i);
			if (plic_parent_irq)
				irq_set_chained_handler(plic_parent_irq,
							plic_handle_irq);
		}

		/*
		 * When running in M-mode we need to ignore the S-mode handler.
		 * Here we assume it always comes later, but that might be a
		 * little fragile.
		 */
		handler = per_cpu_ptr(&plic_handlers, cpu);
		if (handler->present) {
			pr_warn("handler already present for context %d.\n", i);
			plic_set_threshold(handler, PLIC_DISABLE_THRESHOLD);
			goto done;
		}

		cpumask_set_cpu(cpu, &priv->lmask);
		handler->present = true;
		handler->hart_base =
			priv->regs + CONTEXT_BASE + i * CONTEXT_PER_HART;
		raw_spin_lock_init(&handler->enable_lock);
		handler->enable_base =
			priv->regs + ENABLE_BASE + i * ENABLE_PER_HART;
		handler->trigger_type_base = 
			priv->regs + TRIGGER_TYPE_ARRAY_BASE;
		handler->priv = priv;
done:
		for (hwirq = 1; hwirq <= nr_irqs; hwirq++)
			plic_toggle(handler, hwirq, 0);
		nr_handlers++;
	}

	/*
	 * We can have multiple PLIC instances so setup cpuhp state only
	 * when context handler for current/boot CPU is present.
	 */
	handler = this_cpu_ptr(&plic_handlers);
	if (handler->present && !plic_cpuhp_setup_done) {
		cpuhp_setup_state(CPUHP_AP_IRQ_SIFIVE_PLIC_STARTING,
				  "irqchip/sifive/plic:starting",
				  plic_starting_cpu, plic_dying_cpu);
		plic_cpuhp_setup_done = true;
	}

	pr_info("%pOFP: mapped %d interrupts with %d handlers for"
		" %d contexts.\n", node, nr_irqs, nr_handlers, nr_contexts);
	return 0;

out_iounmap:
	iounmap(priv->regs);
out_free_priv:
	kfree(priv);
	return error;
}

IRQCHIP_DECLARE(renesas_plic, "renesas,plic-100ss", plic_init);
//IRQCHIP_DECLARE(riscv_plic0, "riscv,plic0", plic_init); /* for legacy systems */

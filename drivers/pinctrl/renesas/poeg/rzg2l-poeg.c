// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L Port Output Enable for GPT (POEG) driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/rzg2l-poeg.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/rzg2l-poeg.h>
#include <linux/poll.h>
#include <linux/reset.h>
#include <linux/wait.h>

#define POEGG_IOCE	BIT(5)
#define POEGG_PIDE	BIT(4)
#define POEGG_SSF	BIT(3)
#define POEGG_IOCF	BIT(1)
#define POEGG_PIDF	BIT(0)

#define RZG2L_POEG_MAX_INDEX		3

#define RZG2L_GPT_MAX_HW_CHANNELS	8
#define RZG2L_GPT_INVALID_CHANNEL	0xff

enum poeg_conf {
	POEG_USER_CTRL = BIT(0),
	POEG_GPT_BOTH_HIGH = BIT(1),
	POEG_GPT_BOTH_LOW = BIT(2),
	POEG_GPT_DEAD_TIME = BIT(3),
	POEG_EXT_PIN_CTRL = BIT(4),
	POEG_GPT_BOTH_HIGH_LOW = BIT(1) | BIT(2),
	POEG_GPT_BOTH_HIGH_DEAD_TIME = BIT(1) | BIT(3),
	POEG_GPT_BOTH_LOW_DEAD_TIME = BIT(2) | BIT(3),
	POEG_GPT_ALL = BIT(1) | BIT(2) | BIT(3)
};

static struct class *poeg_class;
static dev_t g_poeg_dev;

struct rzg2l_poeg_chip {
	struct device *gpt_dev;
	struct reset_control *rstc;
	void __iomem *mmio;
	struct cdev poeg_cdev;
	u32 cfg;
	int minor_n;
	u8 gpt_channels[RZG2L_GPT_MAX_HW_CHANNELS];
	u8 index;
};

static void rzg2l_poeg_write(struct rzg2l_poeg_chip *chip, u32 data)
{
	writel(data, chip->mmio);
}

static u32 rzg2l_poeg_read(struct rzg2l_poeg_chip *chip)
{
	return readl(chip->mmio);
}

static int rzg2l_poeg_output_disable_user(struct rzg2l_poeg_chip *chip, bool enable)
{
	u32 reg_val;

	reg_val = rzg2l_poeg_read(chip);
	if (enable)
		reg_val |= POEGG_SSF;
	else
		reg_val &= ~POEGG_SSF;

	rzg2l_poeg_write(chip, reg_val);

	return 0;
}

static irqreturn_t rzg2l_poeg_irq(int irq, void *ptr)
{
	struct rzg2l_poeg_chip *chip = ptr;
	u32 val;

	val = rzg2l_poeg_read(chip);
	if (val & POEGG_PIDF)
		val &= ~POEGG_PIDF;

	rzg2l_poeg_write(chip, val);

	return IRQ_HANDLED;
}

static ssize_t rzg2l_poeg_chrdev_write(struct file *filp, const char __user *buf,
				       size_t len, loff_t *f_ps)
{
	struct rzg2l_poeg_chip *const chip = filp->private_data;
	struct poeg_cmd cmd;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	switch (cmd.val) {
	case RZG2L_POEG_OUTPUT_DISABLE_USR_ENABLE_CMD:
		rzg2l_poeg_output_disable_user(chip, true);
		break;
	case RZG2L_POEG_OUTPUT_DISABLE_USR_DISABLE_CMD:
		rzg2l_poeg_output_disable_user(chip, false);
		break;
	default:
		return -EINVAL;
	}

	return len;
}

static int rzg2l_poeg_chrdev_open(struct inode *inode, struct file *filp)
{
	struct rzg2l_poeg_chip *const chip = container_of(inode->i_cdev, typeof(*chip),
							  poeg_cdev);

	filp->private_data = chip;

	return nonseekable_open(inode, filp);
}

static int rzg2l_poeg_chrdev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;

	return 0;
}

static const struct file_operations poeg_fops = {
	.owner = THIS_MODULE,
	.write = rzg2l_poeg_chrdev_write,
	.open = rzg2l_poeg_chrdev_open,
	.release = rzg2l_poeg_chrdev_release,
};

static bool rzg2l_poeg_get_linked_gpt_channels(struct platform_device *pdev,
					       struct rzg2l_poeg_chip *chip,
					       struct device_node *gpt_np,
					       u8 poeg_id)
{
	struct of_phandle_args of_args;
	bool ret = false;
	unsigned int i;
	u32 poeg_grp;
	int cells;
	int err;

	cells = of_property_count_u32_elems(gpt_np, "renesas,poegs");
	if (cells == -EINVAL)
		return ret;

	for (i = 0 ; i < RZG2L_GPT_MAX_HW_CHANNELS; i++)
		chip->gpt_channels[i] = RZG2L_GPT_INVALID_CHANNEL;

	cells >>= 1;
	for (i = 0; i < cells; i++) {
		err = of_parse_phandle_with_fixed_args(gpt_np, "renesas,poegs",
						       1, i, &of_args);
		if (err) {
			dev_err_probe(&pdev->dev, err,
				      "Failed to parse 'renesas,poegs' property\n");
			break;
		}

		if (of_args.args[0] >= RZG2L_GPT_MAX_HW_CHANNELS) {
			dev_err(&pdev->dev, "Invalid channel %d >= %d\n",
				of_args.args[0], RZG2L_GPT_MAX_HW_CHANNELS);
			of_node_put(of_args.np);
			break;
		}

		if (!of_property_read_u32(of_args.np, "renesas,poeg-id", &poeg_grp)) {
			if (poeg_grp == poeg_id) {
				chip->gpt_channels[of_args.args[0]] = poeg_id;
				ret = true;
			}
		}

		of_node_put(of_args.np);
	}

	return ret;
}

static const struct of_device_id rzg2l_poeg_of_table[] = {
	{ .compatible = "renesas,rzg2l-poeg", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_poeg_of_table);

static void rzg2l_poeg_cleanup(void *data)
{
	struct rzg2l_poeg_chip *chip = data;

	put_device(chip->gpt_dev);
}

static int rzg2l_poeg_probe(struct platform_device *pdev)
{
	struct platform_device *gpt_pdev = NULL;
	struct device *dev = &pdev->dev;
	struct rzg2l_poeg_chip *chip;
	bool gpt_linked = false;
	struct device_node *np;
	struct device *cdev;
	u32 cfg, val;
	int ret, irq;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (!of_property_read_u32(dev->of_node, "renesas,poeg-id", &val))
		chip->index = val;

	if (chip->index > RZG2L_POEG_MAX_INDEX)
		return -EINVAL;

	np = of_parse_phandle(dev->of_node, "renesas,gpt", 0);
	if (np)
		gpt_pdev = of_find_device_by_node(np);

	gpt_linked = rzg2l_poeg_get_linked_gpt_channels(pdev, chip, np, chip->index);
	of_node_put(np);
	if (!gpt_pdev || !gpt_linked)
		return -ENODEV;

	chip->gpt_dev = &gpt_pdev->dev;
	ret = devm_add_action_or_reset(dev, rzg2l_poeg_cleanup, chip);
	if (ret < 0)
		return ret;

	chip->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->mmio))
		return PTR_ERR(chip->mmio);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rzg2l_poeg_irq, 0, dev_name(dev), chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "cannot get irq\n");

	chip->rstc = devm_reset_control_get_exclusive_deasserted(dev, NULL);
	if (IS_ERR(chip->rstc))
		return dev_err_probe(dev, PTR_ERR(chip->rstc), "get deasserted reset failed\n");

	platform_set_drvdata(pdev, chip);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "pm_runtime_resume_get failed\n");

	ret = of_property_read_u32(dev->of_node, "renesas,poeg-config", &cfg);
	if (ret)
		goto err_pm;

	switch (cfg) {
	case POEG_USER_CTRL:
		rzg2l_poeg_write(chip, 0);
		break;
	case POEG_EXT_PIN_CTRL:
		rzg2l_poeg_write(chip, POEGG_PIDE);
		break;
	default:
		ret = -EINVAL;
		goto err_pm;
	}

	chip->cfg = cfg;

	cdev_init(&chip->poeg_cdev, &poeg_fops);
	chip->poeg_cdev.owner = THIS_MODULE;
	ret = cdev_add(&chip->poeg_cdev, MKDEV(MAJOR(g_poeg_dev), chip->index), 1);
	if (ret)
		goto err_pm;

	cdev = device_create(poeg_class, NULL, MKDEV(MAJOR(g_poeg_dev), chip->index),
			     NULL, "poeg%d", chip->index);
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		dev_err_probe(dev, ret, "Error %d creating device for port\n", chip->index);
		goto free_cdev;
	}

	chip->minor_n = chip->index;

	return ret;

free_cdev:
	cdev_del(&chip->poeg_cdev);
err_pm:
	pm_runtime_put(&pdev->dev);
	return ret;
}

static void rzg2l_poeg_remove(struct platform_device *pdev)
{
	struct rzg2l_poeg_chip *chip = platform_get_drvdata(pdev);

	device_destroy(poeg_class, MKDEV(MAJOR(g_poeg_dev), chip->minor_n));
	cdev_del(&chip->poeg_cdev);
	pm_runtime_put(&pdev->dev);
}

static struct platform_driver rzg2l_poeg_driver = {
	.driver = {
		.name = "rzg2l-poeg",
		.of_match_table = rzg2l_poeg_of_table
	},
	.probe = rzg2l_poeg_probe,
	.remove = rzg2l_poeg_remove
};

static int rzg2l_poeg_device_init(void)
{
	int err;

	err = alloc_chrdev_region(&g_poeg_dev, 0, 1, "poeg");
	if (err)
		goto out;

	poeg_class = class_create("poeg");
	if (IS_ERR(poeg_class)) {
		err = PTR_ERR(poeg_class);
		goto err_free_chrdev;
	}

	err = platform_driver_register(&rzg2l_poeg_driver);
	if (err)
		goto err_class_destroy;

	return 0;

err_class_destroy:
	class_destroy(poeg_class);
err_free_chrdev:
	unregister_chrdev_region(g_poeg_dev, 1);
out:
	return err;
}

static void rzg2l_poeg_device_exit(void)
{
	platform_driver_unregister(&rzg2l_poeg_driver);
	class_destroy(poeg_class);
	unregister_chrdev_region(g_poeg_dev, 1);
}

module_init(rzg2l_poeg_device_init);
module_exit(rzg2l_poeg_device_exit);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L POEG Driver");
MODULE_LICENSE("GPL");

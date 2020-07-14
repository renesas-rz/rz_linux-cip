#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#define DEV_NAME	"fd800000.sgx"

static struct clk *clk;

static int r8a7745_sgx_notifier_call(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct device *dev = data;

	switch (event) {
	case BUS_NOTIFY_BIND_DRIVER:
		if (!strcmp(DEV_NAME, dev_name(dev))) {

			if (IS_ERR(clk))
				break;
			if (clk_prepare_enable(clk) < 0)
				pr_warn("SGX Quirk can not enable clk\n");
			pr_info("SGX quirk clk is enabled\n");
		}
		break;

	case BUS_NOTIFY_UNBOUND_DRIVER:
		if (!strcmp(DEV_NAME, dev_name(dev))) {
			if (IS_ERR(clk))
				break;
			clk_disable_unprepare(clk);
			pr_info("SGX quirk clk is disabled\n");
		}
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block platform_nb = {
	.notifier_call = r8a7745_sgx_notifier_call,
};

static int __init sgx_r8a7745_quirk_init(void)
{
	static int once;

	if (once++)
		return -ENOMEM;

	if (!of_machine_is_compatible("renesas,r8a7745"))
		return -ENODEV;

	clk = clk_get(NULL, "vcp0");
	if (IS_ERR(clk)) {
		pr_warn("SGX Quirk can not get clk\n");
		return -ENOENT;
	}

	bus_register_notifier(&platform_bus_type, &platform_nb);

	pr_info("Installing sgx clock quirk\n");
	return 0;
}

arch_initcall(sgx_r8a7745_quirk_init);

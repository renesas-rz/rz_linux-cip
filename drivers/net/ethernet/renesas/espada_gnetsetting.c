/* Renesas Standby response extension setting module driver
 *
 * Copyright (C) 2017 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/bitops.h>

#include "espada_gnetsetting.h"

#define ESPADA_GNETSETTING_DRV_NAME "etheravb-espada"

#define ESPADA_GNETSETTING_MAX_DATA_SIZE 1500
#define ESPADA_GNETSETTING_MAX_DATA_SIZE_INT (ESPADA_GNETSETTING_MAX_DATA_SIZE \
								/ sizeof(u32))
#define ESPADA_GNETSETTING_BIT_SIZE 32
#define ESPADA_GNETSETTING_MAX_REG 512

#define _ESPADA_GNETSETTING_BYTE1(x) (x         & 0xFF)
#define _ESPADA_GNETSETTING_BYTE2(x) ((x >>  8) & 0xFF)
#define _ESPADA_GNETSETTING_BYTE3(x) ((x >> 16) & 0xFF)
#define _ESPADA_GNETSETTING_BYTE4(x) ((x >> 24) & 0xFF)

#define ESPADA_GNETSETTING_BYTE_SWAP_32(x) \
	((uint32_t)(_ESPADA_GNETSETTING_BYTE1(x)<<24 | \
		    _ESPADA_GNETSETTING_BYTE2(x)<<16 | \
		    _ESPADA_GNETSETTING_BYTE3(x)<<8 | \
		    _ESPADA_GNETSETTING_BYTE4(x)))

#define	ESPADA_GNETSETTING_BIT_GET_F(nr, f) ((f[nr] - f[0]) / sizeof(u32))

struct espada_gnetsetting_data {
	void __iomem *baseaddr;
	const u32 *reglist;
	unsigned int reglistsize;
	unsigned int offset;
	DECLARE_BITMAP(chkreglist, ESPADA_GNETSETTING_MAX_REG);
	u32 bit_ptn[10];
	unsigned int addr;
	unsigned int size;
	unsigned int datacnt;
	unsigned int data[ESPADA_GNETSETTING_MAX_DATA_SIZE_INT];
};

struct espada_gnetsetting_periodicalsending_data {
	void __iomem *baseaddr;
	unsigned long time;
	unsigned int size;
	unsigned int data[ESPADA_GNETSETTING_MAX_DATA_SIZE_INT];
};

struct espada_gnetsetting_device {
	struct espada_gnetsetting_data	filtering;
	struct espada_gnetsetting_data	autoresponse;
	struct espada_gnetsetting_periodicalsending_data periodicalsending;
};

static struct espada_gnetsetting_device *gnetsetting_device;

static struct class *espada_class;
static struct device *gnetsetting_dev;

static int espada_gnetsetting_get_data(const char **buf,
				       struct espada_gnetsetting_data *data)
{
	char		*strvalue;
	unsigned int	offset;
	unsigned int	size;
	unsigned long	value;
	unsigned int	datacnt;
	unsigned int	storedata[ESPADA_GNETSETTING_MAX_DATA_SIZE_INT];
	int ret;

	data->addr = 0;
	data->size = 0;
	data->datacnt = 0;

	/* Obtain offset address */
	strvalue = strsep((char **)buf, ",");
	if (!strvalue)
		return -EINVAL;

	ret = kstrtoul(strvalue, 16, &value);
	if (ret)
		return ret;
	offset = value;

	/* Obtain size */
	strvalue = strsep((char **)buf, ",");
	if (!strvalue)
		return -EINVAL;

	ret = kstrtoul(strvalue, 10, &value);
	if (ret)
		return ret;

	size = value;

	if ((offset % sizeof(u32) != 0)
	 || (size % sizeof(u32) != 0) || (size == 0))
		return -EINVAL;

	datacnt = 0;
	while ((strvalue = strsep((char **)buf, ",")) != NULL) {
		ret = kstrtoul(strvalue, 16, &value);
		if (ret)
			return ret;

		storedata[datacnt] = value;

		datacnt++;
	}
	if (datacnt != 0) {
		if ((size / sizeof(u32)) != datacnt)
			return -EINVAL;

		memcpy(data->data, storedata, size);
	}

	data->addr = offset;
	data->size = size;
	data->datacnt = datacnt;

	return 0;
}

static void espada_gnetsetting_set_chkreglist(
			struct espada_gnetsetting_data *data)
{
	int i;

	bitmap_zero(data->chkreglist, ESPADA_GNETSETTING_MAX_REG);

	for (i = 0; i < data->reglistsize; i++)
		set_bit(((data->reglist[i] - data->offset) / sizeof(u32)),
							data->chkreglist);
}

static bool espada_gnetsetting_chk_reg(
				struct espada_gnetsetting_data *data,
				u32 reg)
{
	bool ret;

	if (test_bit((reg - data->offset) / sizeof(u32), data->chkreglist))
		ret = true;
	else
		ret = false;

	return ret;
}

static int espada_gnetsetting_write_data(struct espada_gnetsetting_data *data)
{
	int i;

	/* range check */
	if ((data->reglist[0] > data->addr)
	 || (data->reglist[data->reglistsize - 1] + 4
	     < (data->addr + data->size))) {
		return -EINVAL;
	}

	/* write register */
	/* Skip addresses that do not exist in the register list. */
	for (i = 0; i < data->size; i += sizeof(u32)) {
		if (espada_gnetsetting_chk_reg(data, data->addr+i)) {
			espada_gnetsetting_write(data->baseaddr,
						 data->data[i/sizeof(u32)],
						 data->addr+i);
		}
	}

	return 0;
}

static int espada_gnetsetting_read_data(struct espada_gnetsetting_data *data)
{
	int i;

	/* range check */
	if ((data->reglist[0] > data->addr)
	 || (data->reglist[data->reglistsize - 1] + 4
	     < (data->addr + data->size))) {
		return -EINVAL;
	}

	for (i = 0; i < data->size; i += sizeof(u32)) {
		if (espada_gnetsetting_chk_reg(data, data->addr + i))
			data->data[i/sizeof(u32)]
				= espada_gnetsetting_read(data->baseaddr,
							  data->addr+i);
		else
			data->data[i/sizeof(u32)] = 0;
	}

	return 0;
}

static ssize_t espada_gnetsetting_show_data(
	struct espada_gnetsetting_data *data, char *buf)
{
	int i;
	char *cp;

	cp = buf;
	for (i = 0; i < data->size; i += sizeof(u32)) {
		cp += snprintf(cp,
			       PAGE_SIZE - (cp - buf),
			       "%8x,",
			       data->data[i/sizeof(u32)]);
	}
	cp--;
	cp += snprintf(cp, PAGE_SIZE - (cp - buf), "\n");
	return cp - buf;
}

static ssize_t espada_gnetsetting_filtering_show(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 char *buf)
{
	int ret;

	ret = espada_gnetsetting_read_data(&(gnetsetting_device->filtering));
	if (ret)
		return ret;

	return espada_gnetsetting_show_data(&(gnetsetting_device->filtering),
					    buf);
}

static ssize_t espada_gnetsetting_filtering_store(struct kobject *kobj,
						  struct kobj_attribute *attr,
						  const char *buf,
						  size_t count)
{
	int ret;

	ret = espada_gnetsetting_get_data(&buf,
		&gnetsetting_device->filtering);
	if (ret)
		return ret;
	gnetsetting_device->filtering.addr
		+= gnetsetting_device->filtering.offset;

	if (gnetsetting_device->filtering.datacnt != 0) {
		ret = espada_gnetsetting_write_data(
			&(gnetsetting_device->filtering));
		if (ret)
			return ret;
	}

	return count;
}

static ssize_t espada_gnetsetting_autoresponse_show(struct kobject *kobj,
						    struct kobj_attribute *attr,
						    char *buf)
{
	int ret;

	ret = espada_gnetsetting_read_data(
		&(gnetsetting_device->autoresponse));
	if (ret)
		return ret;

	return espada_gnetsetting_show_data(
		&(gnetsetting_device->autoresponse), buf);
}

static ssize_t espada_gnetsetting_autoresponse_store(
					struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf,
					size_t count)
{
	int ret;

	ret = espada_gnetsetting_get_data(&buf,
		&gnetsetting_device->autoresponse);
	if (ret)
		return ret;

	gnetsetting_device->autoresponse.addr
		+= gnetsetting_device->autoresponse.offset;

	if (gnetsetting_device->autoresponse.datacnt != 0) {
		ret = espada_gnetsetting_write_data(
			&(gnetsetting_device->autoresponse));
		if (ret)
			return ret;
	}

	return count;
}

static ssize_t espada_gnetsetting_periodicalsending_show(
						struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int i;
	char *cp;
	struct espada_gnetsetting_periodicalsending_data *data
		= &(gnetsetting_device->periodicalsending);

	cp = buf;
	cp += snprintf(cp, PAGE_SIZE - (cp - buf), "%ld,", data->time);
	cp += snprintf(cp, PAGE_SIZE - (cp - buf), "%4d,", data->size);

	if (data->size != 0) {
		for (i = 0; i < data->size; i += sizeof(u32))
			cp += snprintf(cp,
				      PAGE_SIZE - (cp - buf),
				      "%8X,", data->data[i/sizeof(u32)]);
	}
	cp--;
	cp += snprintf(cp, PAGE_SIZE - (cp - buf), "\n");
	return cp-buf;
}

static ssize_t espada_gnetsetting_periodicalsending_store(
						struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf,
						size_t count)
{

	char		*strvalue;
	unsigned long	time;
	unsigned int	size;
	unsigned long	value;
	unsigned int	datacnt;
	unsigned int	cnt;
	unsigned int	storedata[ESPADA_GNETSETTING_MAX_DATA_SIZE_INT];
	int ret;
	struct espada_gnetsetting_periodicalsending_data *data
		= &(gnetsetting_device->periodicalsending);

	strvalue = strsep((char **)&buf, ",");
	if (!strvalue)
		return -EINVAL;

	ret = kstrtoul(strvalue, 10, &value);
	if (ret)
		return ret;

	time = value;

	if (time == 0) {
		data->time = 0;
		data->size = 0;
		return count;
	}

	strvalue = strsep((char **)&buf, ",");
	if (!strvalue)
		return -EINVAL;

	ret = kstrtoul(strvalue, 10, &value);
	if (ret)
		return ret;

	size = value;
	if ((size == 0)
	 || (size > ESPADA_GNETSETTING_MAX_DATA_SIZE))
		return -EINVAL;

	/* Obtain data */
	datacnt = 0;
	cnt = 0;
	while ((strvalue = strsep((char **)&buf, ",")) != NULL) {
		ret = kstrtoul(strvalue, 16, &value);
		if (ret)
			return ret;

		storedata[cnt] = value;
		cnt++;
		datacnt += strlen(strvalue);
	}

	if (size != ((datacnt - 1) / 2))
		return -EINVAL;

	data->time = time;
	data->size = size;
	memcpy(data->data, storedata, size);

	return count;
}

void espada_gnetsetting_set_gberam(void)
{
	int i;

	struct espada_gnetsetting_periodicalsending_data *data
		= &(gnetsetting_device->periodicalsending);

	/* GbE RAM Write */
	espada_gnetsetting_write(data->baseaddr, data->time, GBE_RAM_TIME);
	espada_gnetsetting_write(data->baseaddr, data->size, GBE_RAM_SIZE);

	for (i = 0; i < ((data->size / sizeof(u32))
		+ ((data->size % sizeof(u32) ? 1 : 0))); i++)
		espada_gnetsetting_write(
			data->baseaddr,
			ESPADA_GNETSETTING_BYTE_SWAP_32(data->data[i] <<
				(((i + 1) * sizeof(u32) > data->size)
				? (sizeof(u32) - (data->size % sizeof(u32))) * 8
				: 0)),
			GBE_RAM_SEND_DATA + (i * sizeof(u32)));
}
EXPORT_SYMBOL_GPL(espada_gnetsetting_set_gberam);

void espada_gnetsetting_set_gberam_suspend(unsigned int data)
{
	struct espada_gnetsetting_periodicalsending_data *periodicalsending_data
		= &(gnetsetting_device->periodicalsending);

	/* GbE RAM Write */
	espada_gnetsetting_write(periodicalsending_data->baseaddr, data, GBE_RAM_SUSPEND);
}
EXPORT_SYMBOL_GPL(espada_gnetsetting_set_gberam_suspend);

unsigned int espada_gnetsetting_get_gberam_suspend(void)
{
	struct espada_gnetsetting_periodicalsending_data *periodicalsending_data
		= &(gnetsetting_device->periodicalsending);

	/* GbE RAM Read */
	return espada_gnetsetting_read(periodicalsending_data->baseaddr, GBE_RAM_SUSPEND);
}
EXPORT_SYMBOL_GPL(espada_gnetsetting_get_gberam_suspend);

static struct kobj_attribute espada_gnetsetting_filtering_attribute
	= __ATTR(filtering, 0644, espada_gnetsetting_filtering_show,
				espada_gnetsetting_filtering_store);
static struct kobj_attribute espada_gnetsetting_autoresponse_attribute
	= __ATTR(autoresponse, 0644, espada_gnetsetting_autoresponse_show,
				espada_gnetsetting_autoresponse_store);
static struct kobj_attribute espada_gnetsetting_periodicalsending_attribute
	= __ATTR(periodicalsending, 0644,
			espada_gnetsetting_periodicalsending_show,
			espada_gnetsetting_periodicalsending_store);

static struct attribute *espada_gnetsetting_attrs[] = {
	&espada_gnetsetting_filtering_attribute.attr,
	&espada_gnetsetting_autoresponse_attribute.attr,
	&espada_gnetsetting_periodicalsending_attribute.attr,
	NULL,
};

static struct attribute_group espada_gnetsetting_attr_group = {
	.attrs = espada_gnetsetting_attrs,
};

static const struct of_device_id
	espada_gnetsetting_dt_ids[] __initdata = {
		{.compatible = "renesas,espada_gnetsetting",},
		{},
};

static int __init espada_gnetsetting_init(void)
{
	int retval;
	struct device_node *np;
	struct resource res;
	resource_size_t size;

	espada_class = class_create(THIS_MODULE, "espada");
	if (IS_ERR(espada_class)) {
		retval = -ENOMEM;
		goto exit;
	}

	gnetsetting_dev =
		device_create(espada_class, NULL, 0, NULL, "gnetsetting");
	if (!gnetsetting_dev) {
		retval = -ENOMEM;
		goto err_alloc_device;
	}

	retval = sysfs_create_group(&gnetsetting_dev->kobj,
				&espada_gnetsetting_attr_group);
	if (retval) {
		retval = -ENOMEM;
		goto err_alloc;
	}

	gnetsetting_device = kzalloc(sizeof(struct espada_gnetsetting_device),
				     GFP_KERNEL);
	if (!gnetsetting_device) {
		retval = -ENOMEM;
		goto err_alloc;
	}

	for_each_matching_node(np, espada_gnetsetting_dt_ids) {
		retval = of_address_to_resource(np, 0, &res);
		if (retval)
			goto err_alloc;

		size = resource_size(&res);

		gnetsetting_device->filtering.baseaddr
			= ioremap_nocache(res.start, size);

		if (!gnetsetting_device->filtering.baseaddr) {
			retval = -ENOMEM;
			goto err_alloc;
		}

		gnetsetting_device->filtering.reglist
			= espada_gnetsetting_filtering_reg;
		gnetsetting_device->filtering.reglistsize
			= ARRAY_SIZE(espada_gnetsetting_filtering_reg);
		gnetsetting_device->filtering.offset
			= ESPADA_GNETSETTING_FILTERING_REG_OFFSET;
		espada_gnetsetting_set_chkreglist(
			&gnetsetting_device->filtering);

		gnetsetting_device->autoresponse.baseaddr
			= gnetsetting_device->filtering.baseaddr;
		gnetsetting_device->autoresponse.reglist
			= espada_gnetsetting_autoresponse_reg;
		gnetsetting_device->autoresponse.reglistsize
			= ARRAY_SIZE(espada_gnetsetting_autoresponse_reg);
		gnetsetting_device->autoresponse.offset
			= ESPADA_GNETSETTING_AUTORESPONSE_REG_OFFSET;
		espada_gnetsetting_set_chkreglist(
			&gnetsetting_device->autoresponse);

		retval = of_address_to_resource(np, 1, &res);
		if (retval)
			goto err_alloc;

		size = resource_size(&res);
		if (!request_mem_region(res.start, size,
				ESPADA_GNETSETTING_DRV_NAME)) {
			retval = -EBUSY;
			goto err_alloc;
		}

		gnetsetting_device->periodicalsending.baseaddr
			= ioremap(res.start, size);
		if (!(gnetsetting_device->autoresponse.baseaddr)) {
			retval = -ENOMEM;
			goto err_alloc;
		}

		gnetsetting_device->periodicalsending.time = 0;
		gnetsetting_device->periodicalsending.size = 0;
		memset(gnetsetting_device->periodicalsending.data, 0,
				ESPADA_GNETSETTING_MAX_DATA_SIZE_INT);
	}

	return 0;

err_alloc:
	device_destroy(espada_class, 0);
err_alloc_device:
	class_destroy(espada_class);
	kfree(gnetsetting_device);
exit:
	return retval;
}

static void __exit espada_gnetsetting_exit(void)
{
	device_destroy(espada_class, 0);
	class_destroy(espada_class);
	kfree(gnetsetting_device);
}

module_init(espada_gnetsetting_init);
module_exit(espada_gnetsetting_exit);

MODULE_DESCRIPTION("Renesas Standby response extension setting module driver");
MODULE_LICENSE("GPL v2");

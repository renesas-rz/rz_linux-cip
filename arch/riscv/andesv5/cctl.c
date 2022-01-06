/*
 *  Copyright (C) 2009 Andes Technology Corporation
 *  Copyright (C) 2019 Andes Technology Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <asm/andesv5/csr.h>
#include <asm/andesv5/proc.h>

#define INPUTLEN 32

struct entry_struct{

	char *name;
	int perm;
	struct file_operations *fops;
};

static struct proc_dir_entry *proc_cctl;

#define DEBUG( enable, tagged, ...)				\
	do{							\
		if(enable){					\
			if(tagged)				\
			printk( "[ %30s() ] ", __func__);	\
			printk( __VA_ARGS__);			\
		}						\
	} while( 0)

static int debug = 0;
module_param(debug, int, 0);

void cpu_icache_smp_enable(void)
{
    int cpu_num = num_online_cpus();
    int id = smp_processor_id();
    int i, ret;

    for(i = 0; i < cpu_num; i++){
		if(i == id)
			continue;
        ret = smp_call_function_single(i, cpu_icache_enable,
                                        NULL, true);
        if(ret)
            pr_err("Core %d enable I-cache Fail\n"
                    "Error Code:%d \n", i, ret);
    }
    cpu_icache_enable(NULL);
}

void cpu_icache_smp_disable(void)
{
    int cpu_num = num_online_cpus();
    int id = smp_processor_id();
    int i, ret;

    for(i = 0; i < cpu_num; i++){
        if(i == id)
            continue;
        ret = smp_call_function_single(i, cpu_icache_disable,
                                        NULL, true);
        if(ret)
            pr_err("Core %d disable I-cache Fail \n"
                    "Error Code:%d \n", i, ret);
    }
    cpu_icache_disable(NULL);
}

void cpu_dcache_smp_enable(void)
{
    int cpu_num = num_online_cpus();
    int id = smp_processor_id();
    int i, ret;

    for(i = 0; i < cpu_num; i++){
        if(i == id)
            continue;
        ret = smp_call_function_single(i, cpu_dcache_enable,
                                        NULL, true);
        if(ret)
            pr_err("Core %d disable D-cache Fail \n"
                    "Error Code:%d \n", i, ret);
    }
    cpu_dcache_enable(NULL);
}

void cpu_dcache_smp_disable(void)
{
    int cpu_num = num_online_cpus();
    int id = smp_processor_id();
    int i, ret;

    for(i = 0; i < cpu_num; i++){
        if(i == id)
            continue;
        ret = smp_call_function_single(i, cpu_dcache_disable,
                                        NULL, true);
        if(ret)
            pr_err("Core %d disable D-cache Fail \n"
                    "Error Code:%d \n", i, ret);
    }
    cpu_dcache_disable(NULL);
}

static ssize_t proc_read_cache_en(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
    int ret;
    char buf[18];
    if (!strncmp(file->f_path.dentry->d_name.name, "ic_en", 7))
        ret = sprintf(buf, "I-cache: %s\n", (cpu_l1c_status() & CACHE_CTL_mskIC_EN) ? "Enabled" : "Disabled");
    else if(!strncmp(file->f_path.dentry->d_name.name, "dc_en", 7))
        ret = sprintf(buf, "D-cache: %s\n", (cpu_l1c_status() & CACHE_CTL_mskDC_EN) ? "Enabled" : "Disabled");
	else
		return -EFAULT;

    return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static ssize_t proc_write_cache_en(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{

	unsigned long en;
	char inbuf[INPUTLEN];

	if (count > INPUTLEN - 1)
		count = INPUTLEN - 1;

	if (copy_from_user(inbuf, buffer, count))
		return -EFAULT;

	inbuf[count] = '\0';

	if (!sscanf(inbuf, "%lu", &en) || en > 1)
		return -EFAULT;

	if (!strncmp(file->f_path.dentry->d_name.name, "ic_en", 7)) {
		if (en && !(cpu_l1c_status() & CACHE_CTL_mskIC_EN)) {
#ifdef CONFIG_SMP
			cpu_icache_smp_enable();
#else
			cpu_icache_enable(NULL);
#endif
			DEBUG(debug, 1, "I-cache: Enabled\n");
		} else if (!en && (cpu_l1c_status() & CACHE_CTL_mskIC_EN)) {
#ifdef CONFIG_SMP
			cpu_icache_smp_disable();
#else
			cpu_icache_disable(NULL);
#endif
			DEBUG(debug, 1, "I-cache: Disabled\n");
		}
	} else if(!strncmp(file->f_path.dentry->d_name.name, "dc_en", 7)) {
		if (en && !(cpu_l1c_status() & CACHE_CTL_mskDC_EN)) {
#ifdef CONFIG_SMP
			cpu_dcache_smp_enable();
#else
			cpu_dcache_enable(NULL);
#endif
			DEBUG(debug, 1, "D-cache: Enabled\n");
		} else if (!en && (cpu_l1c_status() & CACHE_CTL_mskDC_EN)) {
#ifdef CONFIG_SMP
			cpu_dcache_smp_disable();
#else
			cpu_dcache_disable(NULL);
#endif
			DEBUG(debug, 1, "D-cache: Disabled\n");
		}
	}else{
		return -EFAULT;
	}

	return count;
}

static struct file_operations en_fops = {
	.open = simple_open,
	.read = proc_read_cache_en,
	.write = proc_write_cache_en,
};

static void create_seq_entry(struct entry_struct *e, mode_t mode,
			     struct proc_dir_entry *parent)
{

	struct proc_dir_entry *entry = proc_create(e->name, mode, parent, e->fops);

	if (!entry)
		printk(KERN_ERR "invalid %s register.\n", e->name);
}

static void install_proc_table(struct entry_struct *table)
{
	while (table->name) {

		create_seq_entry(table, table->perm, proc_cctl);
		table++;
	}
}

static void remove_proc_table(struct entry_struct *table)
{

	while (table->name) {
		remove_proc_entry(table->name, proc_cctl);
		table++;
	}
}

struct entry_struct proc_table_cache[] = {

	{"ic_en", 0644, &en_fops},
	{"dc_en", 0644, &en_fops},
};
static int __init init_cctl(void)
{

	DEBUG(debug, 0, "CCTL module registered\n");

	if(!(proc_cctl = proc_mkdir("cctl", NULL)))
		return -ENOMEM;

	install_proc_table(proc_table_cache);

	return 0;
}

static void __exit cleanup_cctl(void)
{

	remove_proc_table(proc_table_cache);
	remove_proc_entry("cctl", NULL);

	DEBUG(debug, 1, "CCTL module unregistered\n");
}

module_init(init_cctl);
module_exit(cleanup_cctl);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Userspace Cache Control Module");

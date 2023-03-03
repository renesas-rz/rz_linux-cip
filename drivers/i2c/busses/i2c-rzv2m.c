/*
 * Driver for the Renesas RZ/V2M I2C unit
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>

/* Register offsets */
#define IICB0DAT       0x00    /* Data Register */
#define IICB0SVA       0x04    /* Slave Address Register */
#define IICB0CTL0      0x08    /* Control Register 0 */
#define IICB0TRG       0x0C    /* Trigger Register */
#define IICB0STR0      0x10    /* Status Register 0 */
#define IICB0STR1      0x14    /* Status Register 1 */
#define IICB0STRC      0x18    /* Status Clear Register */
#define IICB0CTL1      0x20    /* Control Register 1 */
#define IICB0WL                0x24    /* Low Level Width Setting Register */
#define IICB0WH                0x28    /* How Level Width Setting Register */


/* IICB0CTL0 8bit*/
#define IICB0IICE      (1 << 7)        /* I2C Enable */
#define IICB0MDTX1     (1 << 4)        /* Transfer Mode(Slave) */
#define IICB0MDTX0     (1 << 3)        /* Transfer Mode(Master) */
#define IICB0SLSI      (1 << 2)        /* IICBTIS Signal Enable */
#define IICB0SLWT      (1 << 1)        /* Interrupt Request Timing */
#define IICB0SLAC      (1 << 0)        /* Acknowledge */

/* IICB0TRG 8bit */
#define IICB0LRET      (1 << 3)        /* Communication to Wait Trigger */
#define IICB0WRET      (1 << 2)        /* Quit Wait Trigger */
#define IICB0STT       (1 << 1)        /* Create Start Condition Trigger */
#define IICB0SPT       (1 << 0)        /* Create Stop Condition Trigger */

/* IICB0STR0 16bit-access only */
#define IICB0SSMS      (1 << 15)       /* Master Flag : 1: work or 0: not */
#define IICB0SSDR      (1 << 13)       /* IICB0DAT Flag (No Care) */
#define IICB0SSWT      (1 << 12)       /* Wait Flag */
#define IICB0SSEX      (1 << 11)       /* Extend Code Flag */
#define IICB0SSCO      (1 << 10)       /* Address Match Flag  */
#define IICB0SSTR      (1 << 9)        /* Send Flag */
#define IICB0SSAC      (1 << 8)        /* Ack Flag */
#define IICB0SSRS      (1 << 7)        /* Communication Reserve Flag */
#define IICB0SSBS      (1 << 6)        /* Bus Flag */
#define IICB0SSST      (1 << 5)        /* Start Condition Flag */
#define IICB0SSSP      (1 << 4)        /* Stop Condition Flag */
#define IICB0STCF      (1 << 1)        /* IICB0TRG.IICB0STT Clear Flag */
#define IICB0ALDF      (1 << 0)        /* Arbitration Fall Flag (No Care) */

/* IICB0CTL1 8bit*/
#define IICB0MDSC      (1 << 7)        /* Bus Mode */
#define IICB0LGDF      (1 << 4)        /* Digital filter */
#define IICB0MDLB      (1 << 3)        /* Loop-back mode */
#define IICB0SLSE      (1 << 1)        /* Start condition output */
#define IICB0SLRS      (1 << 0)        /* Disable Communication Reserve */

/* Baudrate calulate */
#define RZV2M_I2C_TWODIGIT     100     /* 2-digit decimal to integer */
#define RZV2M_I2C_NINEDIGIT    1000000000/* 9-digit decimal to integer */

/* RZ/V2M I2C struct */
struct rzv2m_i2c_priv {
       void __iomem *base;
       struct i2c_adapter adap;
       struct clk *clk;
       int bus_mode;
       struct completion msg_tia_done;
       u16 iicb0wl;
       u16 iicb0wh;
};

/* Transfer speed index*/
enum bcr_index {
       RZV2M_I2C_BCR_100K = 0,
       RZV2M_I2C_BCR_400K,
};

/* Config */
struct bcr_cfg {
       int lcoeff;
       int hcoeff;
       int hdtim;
};

/* Config table */
static const struct bcr_cfg bcr_cfg_table[] = {
       {47, 53, 3450},
       {52, 48, 900},
};

/* Tia interrupt callback */
static irqreturn_t rzv2m_i2c_tia_irq_handler(int this_irq, void *dev_id)
{
       struct rzv2m_i2c_priv *priv = dev_id;

       /* Set flag */
       complete(&priv->msg_tia_done);

       return IRQ_HANDLED;
}

/* Caluate IICB0WL and IICB0WH by clock */
static int rzv2m_i2c_clock_calculate(struct device *dev,
                       struct rzv2m_i2c_priv *priv)
{
       const struct bcr_cfg *sbcr_cfg;
       int ret;
       unsigned long input_rate;
       u32 bus_clk_rate, scl_rise_ns, scl_fall_ns, sum;
       u64 div;
       u64 iicb0wl_temp, iicb0wh_temp;
       int temp;

       /* Get input clock:PCLK */
       input_rate = clk_get_rate(priv->clk);

       /* Get the bus rate property from dts */
       if (of_property_read_u32(dev->of_node, "clock-frequency",
                                               &bus_clk_rate)) {
               dev_warn(dev,
                        "Could not read clock-frequency property, Instead,100K is be to set\n");
               bus_clk_rate = 100000;
       }

       if (of_property_read_u32(dev->of_node, "i2c-scl-rising-time-ns",
                                               &scl_rise_ns))
               scl_rise_ns = 0;

       if (of_property_read_u32(dev->of_node, "i2c-scl-falling-time-ns",
                                               &scl_fall_ns))
               scl_fall_ns = 0;

       sum = scl_rise_ns + scl_fall_ns;

       /* Config setting */
       switch (bus_clk_rate) {
       case 400000:
               sbcr_cfg = &bcr_cfg_table[RZV2M_I2C_BCR_400K];
               priv->bus_mode = RZV2M_I2C_BCR_400K;
               break;
       case 100000:
               sbcr_cfg = &bcr_cfg_table[RZV2M_I2C_BCR_100K];
               priv->bus_mode = RZV2M_I2C_BCR_100K;
               break;
       default:
               dev_err(dev, "transfer speed is invaild\n");
               return -EINVAL;
       }

       /*
        * Caculate IICB0WL and IICBOWH by the following
        * IICB0WL = lcoeff / bus_clk * input_rate
        * IICB0WH = ( hcoeff / bus_clk  -tr -tf ) * input_rate
        */

       /* IICB0WL */
       /* Change order for compute calculation */
       iicb0wl_temp = (u64)sbcr_cfg->lcoeff * input_rate;
       /* The orginal lcoeff is two digits after the decimal point.
        * Thus divided by 100.
        */
       div = bus_clk_rate * RZV2M_I2C_TWODIGIT;
       if (iicb0wl_temp % div != 0)
               iicb0wl_temp = iicb0wl_temp / div + 1;
       else
               iicb0wl_temp = iicb0wl_temp / div;

       /*
        * Data hold time must be less than 0.9us in fast mode
        * and 3.45us in standard mode.
        * Data hold time = IICB0WL[9:2] / PCLK
        */
       temp = (u64)(iicb0wl_temp >> 2) * RZV2M_I2C_NINEDIGIT / input_rate;
       if (temp > sbcr_cfg->hdtim) {
               dev_err(dev, "data hold time : %d[ns] is over the %d\n",
                       temp, sbcr_cfg->hdtim);
               return -EINVAL;
       }

       /* IICB0WH */
       iicb0wh_temp = (u64)input_rate * sbcr_cfg->hcoeff;

       /* Round up */
       if (iicb0wh_temp % div != 0)
               iicb0wh_temp = iicb0wh_temp / div + 1;
       else
               iicb0wh_temp = iicb0wh_temp / div;

       /* The unit ofsum is [ns] so that divied 10^9 */
       iicb0wh_temp = iicb0wh_temp - (u64)sum * input_rate
                                       / RZV2M_I2C_NINEDIGIT;

       /* Save IICB0WL IICB0WH setting */
       priv->iicb0wl = (u16)iicb0wl_temp;
       priv->iicb0wh = (u16)iicb0wh_temp;

       return 0;
}

/* I2c init */
static void rzv2m_i2c_init(struct rzv2m_i2c_priv *priv)
{
       u8 i2c_ctl0 = 0;
       u8 i2c_ctl1 = 0;

       /* i2c disable */
//     writeb(0, priv->base + IICB0CTL0);
       writel(0, priv->base + IICB0CTL0);

       /* IICB0CTL1 setting */
       i2c_ctl1 = (priv->bus_mode ? IICB0MDSC : 0)
                        | IICB0SLSE;
//     writeb(i2c_ctl1, priv->base + IICB0CTL1);
       writel(i2c_ctl1, priv->base + IICB0CTL1);

       /* IICB0WL IICB0WH setting */
//     writew(priv->iicb0wl, priv->base + IICB0WL);
//     writew(priv->iicb0wh, priv->base + IICB0WH);
       writel(priv->iicb0wl, priv->base + IICB0WL);
       writel(priv->iicb0wh, priv->base + IICB0WH);

       /* i2c enable after setting */
       i2c_ctl0 = IICB0SLWT | IICB0SLAC | IICB0IICE;
//     writeb(i2c_ctl0, priv->base + IICB0CTL0);
       writel(i2c_ctl0, priv->base + IICB0CTL0);
}

/* I2c write(one byte) */
static int rzv2m_i2c_writeb(struct rzv2m_i2c_priv *priv, u8 data)
{
       unsigned long time_left;

       /* Flag clear*/
       reinit_completion(&priv->msg_tia_done);

       /* Write data */
//     writeb(data, priv->base + IICB0DAT);
       writel(data, priv->base + IICB0DAT);

       /* Wait for transaction */
       time_left = wait_for_completion_timeout(&priv->msg_tia_done,
                       priv->adap.timeout);
       if (!time_left)
               return -ETIMEDOUT;

       /* Confirm ACK */
       if ((readw(priv->base + IICB0STR0) & IICB0SSAC) != IICB0SSAC)
               return -ENXIO;

       return 0;
}

/* I2c read(one byte) */
static int rzv2m_i2c_readb(struct rzv2m_i2c_priv *priv, u8 *data, bool last)
{
       unsigned long time_left;
       u8 i2c_ctl0;

       /* Flag clear*/
       reinit_completion(&priv->msg_tia_done);

       /*  Interrupt request timing : 8th clock */
       i2c_ctl0 = readb(priv->base + IICB0CTL0);
       i2c_ctl0 &= ~IICB0SLWT;
//     writeb(i2c_ctl0, priv->base + IICB0CTL0);
       writel(i2c_ctl0, priv->base + IICB0CTL0);

       /* Exit the wait state */
//     writeb(IICB0WRET, priv->base + IICB0TRG);
       writel(IICB0WRET, priv->base + IICB0TRG);

       /* Wait for transaction */
       time_left = wait_for_completion_timeout(&priv->msg_tia_done,
                               priv->adap.timeout);
       if (!time_left)
               return -ETIMEDOUT;

       if (!last) {/* Not last */
               /* Read data */
               *data = readb(priv->base + IICB0DAT);
       } else { /* Last */
               /* Disable ACK */
               i2c_ctl0 = readb(priv->base + IICB0CTL0);
               i2c_ctl0 &= ~IICB0SLAC;
//             writeb(i2c_ctl0, priv->base + IICB0CTL0);
               writel(i2c_ctl0, priv->base + IICB0CTL0);

               /* Read data*/
               *data = readb(priv->base + IICB0DAT);

               /* Interrupt request timing : 9th clock */
               i2c_ctl0 = readb(priv->base + IICB0CTL0);
               i2c_ctl0 |= IICB0SLWT;
//             writeb(i2c_ctl0, priv->base + IICB0CTL0);
               writel(i2c_ctl0, priv->base + IICB0CTL0);

               /* Exit the wait state */
//             writeb(IICB0WRET, priv->base + IICB0TRG);
               writel(IICB0WRET, priv->base + IICB0TRG);

               /* Wait for transaction */
               time_left = wait_for_completion_timeout(&priv->msg_tia_done,
                                       priv->adap.timeout);
               if (!time_left)
                       return -ETIMEDOUT;

               /* Enable ACK */
               i2c_ctl0 = readb(priv->base + IICB0CTL0);
               i2c_ctl0 |= IICB0SLAC;
//             writeb(i2c_ctl0, priv->base + IICB0CTL0);
               writel(i2c_ctl0, priv->base + IICB0CTL0);
       }

       return 0;
}


/* I2c write */
static int rzv2m_i2c_write(struct rzv2m_i2c_priv *priv, struct i2c_msg *msg,
                       int *count)
{
       int i, ret = 0;

       for (i = 0; i < msg->len; i++) {
               ret = rzv2m_i2c_writeb(priv, msg->buf[i]);
               if (ret < 0)
                       break;
       }
       /* Save count */
       *count = i;

       return ret;
}

/* I2c read */
static int rzv2m_i2c_read(struct rzv2m_i2c_priv *priv, struct i2c_msg *msg,
                       int *count)
{
       int i, ret = 0;

       for (i = 0; i < msg->len; i++) {
               ret = rzv2m_i2c_readb(priv, &msg->buf[i],
                                       ((msg->len - 1) == i));
               if (ret < 0)
                       break;
       }

       /* Save count */
       *count = i;

       return ret;
}

/* Send slave address */
static int rzv2m_i2c_send_address(struct rzv2m_i2c_priv *priv,
                       struct i2c_msg *msg, int read)
{
       u8 addr;
       int ret;

       if (msg->flags & I2C_M_TEN) {
               /* 10-bit address
                *   addr_1: 5'b11110 | addr[9:8] | (R/nW)
                *   addr_2: addr[7:0]
                */
               addr = 0xF0 | ((msg->addr >> 7) & 0x06);
               addr |= read;
               /* Send 1st address(extend code) */
               ret = rzv2m_i2c_writeb(priv, addr);
               if (ret < 0)
                       return ret;
               /* Send 2nd address */
               ret = rzv2m_i2c_writeb(priv, msg->addr & 0xFF);
               if (ret < 0)
                       return ret;
       } else {
               /* 7-bit address */
               addr = (msg->addr << 1) | read;
               ret = rzv2m_i2c_writeb(priv, addr);
               if (ret < 0)
                       return ret;
       }

       return 0;
}

static void rzv2m_i2c_stop_condition(struct rzv2m_i2c_priv *priv)
{
       unsigned long timeout, current_time;

       timeout = jiffies + msecs_to_jiffies(priv->adap.timeout);

       /* Send stop condition */
//     writeb(IICB0SPT, priv->base + IICB0TRG);
       writel(IICB0SPT, priv->base + IICB0TRG);
       do {
               current_time = jiffies;
               if ((readw(priv->base + IICB0STR0) & IICB0SSSP) == IICB0SSSP)
                       break;
       } while (time_before(current_time, timeout));

}

/* I2c main transfer */
static int __rzv2m_i2c_master_xfer(struct rzv2m_i2c_priv *priv,
                       struct i2c_msg *msg, int stop)
{
       int count = 0;
       int ret, read = !!(msg->flags & I2C_M_RD);

       /* Send start condition */
//     writeb(IICB0STT, priv->base + IICB0TRG);
       writel(IICB0STT, priv->base + IICB0TRG);

       /* Send slave address and R/W type */
       ret = rzv2m_i2c_send_address(priv, msg, read);
       if (ret == -ENXIO)
               goto out;
       else if (ret < 0)
               goto out_reset;

       if (read) {
               ret = rzv2m_i2c_read(priv, msg, &count);
               if (ret < 0)
                       goto out_reset;
       } else {
               /* Write data */
               ret = rzv2m_i2c_write(priv, msg, &count);
               if (ret == -ENXIO)
                       goto out;
               else if (ret < 0)
                       goto out_reset;
       }

       if (stop)
               /* Send stop condition */
               rzv2m_i2c_stop_condition(priv);

       return count;

out_reset:
       rzv2m_i2c_init(priv);
       return ret;
out:
       rzv2m_i2c_stop_condition(priv);
       return ret;
}

/* I2c transfer */
static int rzv2m_i2c_master_xfer(struct i2c_adapter *adap,
                       struct i2c_msg *msgs, int num)
{
       struct rzv2m_i2c_priv *priv = i2c_get_adapdata(adap);
       int ret, i;

       if (readw(priv->base + IICB0STR0) & IICB0SSBS)
               return -EAGAIN;

       /* I2C main transfer  */
       for (i = 0; i < num; i++) {
               ret = __rzv2m_i2c_master_xfer(priv, &msgs[i],
                                       (i == (num - 1)));
               if (ret < 0)
                       return ret;
       }

       return num;
}

/* Return i2c function which is susported by RZ/V2M */
static u32 rzv2m_i2c_func(struct i2c_adapter *adap)
{
       return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR;
}

/* I2c algorithm struct */
static struct i2c_algorithm rzv2m_i2c_algo = {
       .master_xfer = rzv2m_i2c_master_xfer,
       .functionality = rzv2m_i2c_func,
};


/* I2c driver probe */
static int rzv2m_i2c_probe(struct platform_device *pdev)
{
       struct rzv2m_i2c_priv *priv;
       struct i2c_adapter *adap;
       struct resource *res;
       struct device *dev = &pdev->dev;
       int irq_tia, ret;

       priv = devm_kzalloc(dev, sizeof(struct rzv2m_i2c_priv), GFP_KERNEL);
       if (!priv)
               return -ENOMEM;

       res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
       priv->base = devm_ioremap_resource(dev, res);
       if (IS_ERR(priv->base)) {
               dev_err(dev, "found no memory resource\n");
               return PTR_ERR(priv->base);
       }

       priv->clk = devm_clk_get(dev, NULL);
       if (IS_ERR(priv->clk)) {
               dev_err(dev, "cannot get clock\n");
               return PTR_ERR(priv->clk);
       }

       /* Adapter setting */
       adap = &priv->adap;
       adap->nr = pdev->id;
       adap->algo = &rzv2m_i2c_algo;
       adap->class = I2C_CLASS_DEPRECATED;
       adap->dev.parent = dev;
       adap->dev.of_node = dev->of_node;
       adap->owner = THIS_MODULE;
       i2c_set_adapdata(adap, priv);
       strlcpy(adap->name, pdev->name, sizeof(adap->name));
       init_completion(&priv->msg_tia_done);
       /* Caculate bus clock rate */
       ret = rzv2m_i2c_clock_calculate(dev, priv);
       if (ret < 0)
               return ret;
       /* I2c HW init */
       rzv2m_i2c_init(priv);

       /* Request irq */
       irq_tia = platform_get_irq(pdev, 0);
       ret = devm_request_irq(dev, irq_tia, rzv2m_i2c_tia_irq_handler, 0,
                                               dev_name(dev), priv);
       if (ret < 0) {
               dev_err(dev, "Unable to request irq %d\n", irq_tia);
               return ret;
       }

       /* Save point of priv to driver data of platform*/
       platform_set_drvdata(pdev, priv);

       /* Request adapter */
       ret = i2c_add_numbered_adapter(adap);
       if (ret < 0) {
               dev_err(dev, "reg adap failed: %d\n", ret);
               return ret;
       }

       dev_info(dev, "probed\n");

       return 0;
}

/* I2c driver remove */
static int rzv2m_i2c_remove(struct platform_device *pdev)
{
       struct rzv2m_i2c_priv *priv = platform_get_drvdata(pdev);
       u8 i2c_ctl0;

       /* Delete adapter */
       i2c_del_adapter(&priv->adap);

       /* I2c disable */
       i2c_ctl0 = readb(priv->base + IICB0CTL0);
       i2c_ctl0 &= ~IICB0IICE;
//     writeb(i2c_ctl0, priv->base + IICB0CTL0);
       writel(i2c_ctl0, priv->base + IICB0CTL0);

       return 0;
}

#ifdef CONFIG_PM_SLEEP
/* I2c suspend */
static int rzv2m_i2c_suspend(struct device *dev)
{
       struct rzv2m_i2c_priv *priv = dev_get_drvdata(dev);
       u8 i2c_ctl0;

       /* I2c disable */
       i2c_ctl0 = readb(priv->base + IICB0CTL0);
       i2c_ctl0 &= ~IICB0IICE;
//     writeb(i2c_ctl0, priv->base + IICB0CTL0);
       writel(i2c_ctl0, priv->base + IICB0CTL0);

       return 0;
}

/* I2c resume */
static int rzv2m_i2c_resume(struct device *dev)
{
       int ret;
       struct rzv2m_i2c_priv *priv = dev_get_drvdata(dev);

       /* Caculate bus clock rate */
       ret = rzv2m_i2c_clock_calculate(dev, priv);
       if (ret < 0)
               return ret;
       /* I2c init */
       rzv2m_i2c_init(priv);

       return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rzv2m_i2c_pm, rzv2m_i2c_suspend,
                        rzv2m_i2c_resume);

static const struct of_device_id rzv2m_i2c_ids[] = {
       { .compatible = "renesas,rzv2m-i2c", },
       { }
};
MODULE_DEVICE_TABLE(of, rzv2m_i2c_ids);

static struct platform_driver rzv2m_i2c_driver = {
       .driver = {
               .name   = "rzv2m-i2c",
               .pm = &rzv2m_i2c_pm,
               .of_match_table = rzv2m_i2c_ids,
       },
       .probe          = rzv2m_i2c_probe,
       .remove         = rzv2m_i2c_remove,
};
module_platform_driver(rzv2m_i2c_driver);

MODULE_DESCRIPTION("RZ/V2M I2C bus driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL v2");

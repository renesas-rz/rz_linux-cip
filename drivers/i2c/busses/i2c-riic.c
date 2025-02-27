// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RIIC driver
 *
 * Copyright (C) 2013 Wolfram Sang <wsa@sang-engineering.com>
 * Copyright (C) 2013 Renesas Solutions Corp.
 */

/*
 * This i2c core has a lot of interrupts, namely 8. We use their chaining as
 * some kind of state machine.
 *
 * 1) The main xfer routine kicks off a transmission by putting the start bit
 * (or repeated start) on the bus and enabling the transmit interrupt (TIE)
 * since we need to send the slave address + RW bit in every case.
 *
 * 2) TIE sends slave address + RW bit and selects how to continue.
 *
 * 3a) Write case: We keep utilizing TIE as long as we have data to send. If we
 * are done, we switch over to the transmission done interrupt (TEIE) and mark
 * the message as completed (includes sending STOP) there.
 *
 * 3b) Read case: We switch over to receive interrupt (RIE). One dummy read is
 * needed to start clocking, then we keep receiving until we are done. Note
 * that we use the RDRFS mode all the time, i.e. we ACK/NACK every byte by
 * writing to the ACKBT bit. I tried using the RDRFS mode only at the end of a
 * message to create the final NACK as sketched in the datasheet. This caused
 * some subtle races (when byte n was processed and byte n+1 was already
 * waiting), though, and I started with the safe approach.
 *
 * 4) If we got a NACK somewhere, we flag the error and stop the transmission
 * via NAKIE.
 *
 * Also check the comments in the interrupt routines for some gory details.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/delay.h>

#define ICFER_FMPE	0x80
#define ICFER_MALE	0x02
#define ICFER_SCLE	0x40
#define ICFER_NFE	0x20

#define ICCR1_ICE	0x80
#define ICCR1_IICRST	0x40
#define ICCR1_CLO	0x20
#define ICCR1_SOWP	0x10
#define ICCR1_SDAO	0x04
#define ICCR1_SCLI	0x02
#define ICCR1_SDAI	0x01

#define ICCR2_BBSY	0x80
#define ICCR2_MST	0x40
#define ICCR2_TRS	0x20
#define ICCR2_SP	0x08
#define ICCR2_RS	0x04
#define ICCR2_ST	0x02

#define ICMR1_CKS_MASK	0x70
#define ICMR1_BCWP	0x08
#define ICMR1_CKS(_x)	((((_x) << 4) & ICMR1_CKS_MASK) | ICMR1_BCWP)

#define ICMR3_RDRFS	0x20
#define ICMR3_ACKWP	0x10
#define ICMR3_ACKBT	0x08

#define ICSER_SAR0	0x01
#define ICSER_SAR1	0x02
#define ICSER_SAR2	0x04
#define ICSER_GCE	0x08
#define ICSER_HOAE	0x80

#define ICIER_TIE	0x80
#define ICIER_TEIE	0x40
#define ICIER_RIE	0x20
#define ICIER_NAKIE	0x10
#define ICIER_SPIE	0x08
#define ICIER_STIE	0x04

#define ICSR1_AAS0	0x01
#define ICSR1_AAS1	0x02
#define ICSR1_AAS2	0x04

#define ICSR2_TDRE	0x80
#define ICSR2_TEND	0x40
#define ICSR2_RDRF	0x20
#define ICSR2_NACKF	0x10
#define ICSR2_STOP	0x08
#define ICSR2_START	0x04

#define ICSAR_FS	0x8000

#define ICBR_RESERVED	0xe0 /* Should be 1 on writes */

#define RIIC_INIT_MSG	-1

#define RIIC_RECOVERY_CLK_CNT  9

#define MAX_SLAVE_DEVICE 3

struct riic_regs {
	u8 iccr1;
	u8 iccr2;
	u8 icmr1;
	u8 icmr3;
	u8 icfer;
	u8 icser;
	u8 icier;
	u8 icsr1;
	u8 icsr2;
	u16 icsar0;
	u16 icsar1;
	u16 icsar2;
	u8 icbrl;
	u8 icbrh;
	u8 icdrt;
	u8 icdrr;
};

struct riic_platform_info {
	unsigned int max_speed;
	const struct riic_regs *regs;
};

struct riic_dev {
	void __iomem *base;
	u8 *buf;
	u8 first_transmit;
	u8 first_receive;
	struct i2c_msg *msg;
	int bytes_left;
	int num_slave;
	int err;
	int is_last;
	struct completion msg_done;
	struct i2c_adapter adapter;
	struct clk *clk;
	struct reset_control *rstc;
	struct i2c_timings i2c_t;

	struct riic_platform_info *info;
	struct i2c_client *slave[MAX_SLAVE_DEVICE];
};

struct riic_irq_desc {
	int res_num;
	irq_handler_t isr;
	char *name;
};

static inline void riic_clear_set_bit(struct riic_dev *riic, u8 clear, u8 set, u8 reg)
{
	writeb((readb(riic->base + reg) & ~clear) | set, riic->base + reg);
}

static int riic_bus_barrier(struct riic_dev *riic)
{
	int ret;
	u8 val;

	/*
	 * The SDA line can still be low even when BBSY = 0. Therefore, after checking
	 * the BBSY flag, also verify that the SDA and SCL lines are not being held low.
	 */
	ret = readb_poll_timeout(riic->base + riic->info->regs->iccr2, val,
				!(val & ICCR2_BBSY), 10, riic->adapter.timeout);
	if (ret)
		goto i2c_recover;

	if (!(readb(riic->base + riic->info->regs->iccr1) & ICCR1_SDAI) ||
	    !(readb(riic->base + riic->info->regs->iccr1) & ICCR1_SCLI))
		goto i2c_recover;

	return 0;

i2c_recover:
	return i2c_recover_bus(&riic->adapter);
}

static int riic_xfer_atomic(struct i2c_adapter *adap, struct i2c_msg msgs[],
			    int num)
{
	struct riic_dev *riic = i2c_get_adapdata(adap);
	struct device *dev = adap->dev.parent;
	unsigned long time_left;
	int i, ret;
	u8 start_bit, val;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	if (riic->slave[0] || riic->slave[1] || riic->slave[2]) {
		dev_dbg(&adap->dev, "Bus busy due to in slave mode\n");
		riic->err = -EBUSY;
		goto out;
	}

	riic->err = riic_bus_barrier(riic);
	if (riic->err)
		goto out;

	riic->err = 0;

	writeb(0, riic->base + riic->info->regs->icsr2);

	for (i = 0, start_bit = ICCR2_ST; i < num; i++) {
		riic->bytes_left = RIIC_INIT_MSG;
		riic->buf = msgs[i].buf;
		riic->msg = &msgs[i];
		riic->is_last = (i == num - 1);

		writeb(start_bit, riic->base + riic->info->regs->iccr2);

		/*
		 * Before setting slave address to ICDRT:
		 * - STAT and TDRE should be raised
		 * - SDAO and SDAI should be at low level.
		 */
		ret = readb_poll_timeout_atomic(riic->base + riic->info->regs->icsr2,
						val, (val & ICSR2_TDRE) && (val & ICSR2_TDRE), 10, 1000);
		ret |= readb_poll_timeout_atomic(riic->base + riic->info->regs->iccr1,
						val, !(val & (ICCR1_SDAO | ICCR1_SDAI)), 10, 1000);
		if (ret) {
			riic->err = -ETIMEDOUT;
			break;
		}

		/* Write data to I2C Bus Transmit Data Register */
		val = i2c_8bit_addr_from_msg(riic->msg);
		writeb(val, riic->base + riic->info->regs->icdrt);

		if (riic->msg->flags & I2C_M_RD) {
			/* On read */
			ret = readb_poll_timeout_atomic(riic->base + riic->info->regs->icsr2,
							val, val & ICSR2_RDRF, 10, 1000);
			if (ret) {
				riic->err = -ETIMEDOUT;
				break;
			}

			val = readb(riic->base + riic->info->regs->icdrr);	/* dummy read */
			riic->bytes_left = riic->msg->len;

			while (riic->bytes_left) {
				ret = readb_poll_timeout_atomic(riic->base + riic->info->regs->icsr2,
								val, val & ICSR2_RDRF, 10, 1000);
				if (ret) {
					riic->err = -ETIMEDOUT;
					break;
				}

				if (riic->bytes_left == 1) {
					if (riic->is_last) {
						 /* STOP must come before we set ACKBT! */
						writeb(ICCR2_SP, riic->base + riic->info->regs->iccr2);
					}
					riic_clear_set_bit(riic, 0, ICMR3_ACKBT, riic->info->regs->icmr3);
				} else
					riic_clear_set_bit(riic, ICMR3_ACKBT, 0, riic->info->regs->icmr3);

				*riic->buf = readb(riic->base + riic->info->regs->icdrr);
				riic->bytes_left--;
				riic->buf++;
			}

			break;
		} else {
			/* On write, initialize length */
			riic->bytes_left = riic->msg->len;

			while (riic->bytes_left) {
				ret = readb_poll_timeout_atomic(riic->base + riic->info->regs->icsr2,
								val, val & ICSR2_TDRE, 10, 1000);
				if (ret) {
					riic->err = -ETIMEDOUT;
					break;
				}

				val = *riic->buf;
				riic->buf++;
				riic->bytes_left--;
				writeb(val, riic->base + riic->info->regs->icdrt);
			}


			ret = readb_poll_timeout_atomic(riic->base + riic->info->regs->icsr2,
							val, val & ICSR2_TEND, 10, 1000);
			if (ret) {
				riic->err = -ETIMEDOUT;
				break;
			}

			if (riic->is_last || riic->err)
				writeb(ICCR2_SP, riic->base + riic->info->regs->iccr2);
		}

		if (riic->err)
			break;

		start_bit = ICCR2_RS;
		writeb(0, riic->base + riic->info->regs->icsr2);
		readb(riic->base + riic->info->regs->icsr2);
	}

	writeb(0, riic->base + riic->info->regs->icsr2);
	readb(riic->base + riic->info->regs->icsr2);

	/* Should check bus state after finishing transfer */
	if (!riic->err) {
		time_left = readb_poll_timeout_atomic(riic->base + riic->info->regs->iccr2,
						      val, !(val & ICCR2_BBSY), 10, 1000);
		if (time_left)
			dev_warn(riic->adapter.dev.parent,
				 "The i2c bus is still busy\n");
	}

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return riic->err ?: num;
}

static int riic_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct riic_dev *riic = i2c_get_adapdata(adap);
	struct device *dev = adap->dev.parent;
	unsigned long time_left;
	int i, ret;
	u8 start_bit, val;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	if (riic->slave[0] || riic->slave[1] || riic->slave[2]) {
		dev_dbg(&adap->dev, "Bus busy due to in slave mode\n");
		riic->err = -EBUSY;
		goto out;
	}

	riic->err = riic_bus_barrier(riic);
	if (riic->err)
		goto out;

	reinit_completion(&riic->msg_done);
	riic->err = 0;

	writeb(0, riic->base + riic->info->regs->icsr2);

	for (i = 0, start_bit = ICCR2_ST; i < num; i++) {
		riic->bytes_left = RIIC_INIT_MSG;
		riic->buf = msgs[i].buf;
		riic->msg = &msgs[i];
		riic->is_last = (i == num - 1);

		writeb(ICIER_NAKIE | ICIER_TIE, riic->base + riic->info->regs->icier);

		writeb(start_bit, riic->base + riic->info->regs->iccr2);

		time_left = wait_for_completion_timeout(&riic->msg_done, riic->adapter.timeout);
		if (time_left == 0)
			riic->err = -ETIMEDOUT;

		if (riic->err)
			break;

		start_bit = ICCR2_RS;
	}

	/* Should check bus state after finishing transfer */
	if (!riic->err) {
		time_left = readb_relaxed_poll_timeout(riic->base + riic->info->regs->iccr2,
						       val, !(val & ICCR2_BBSY), 10, 100);
		if (time_left)
			dev_warn(riic->adapter.dev.parent,
				 "The i2c bus is still busy\n");
	}

 out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return riic->err ?: num;
}

static irqreturn_t riic_tdre_isr(int irq, void *data)
{
	struct riic_dev *riic = data;
	u8 val;
	int timeout = 30;

	if (readb(riic->base + riic->info->regs->icsr1) == ICSR1_AAS0)
		riic->num_slave = 0;
	if (readb(riic->base + riic->info->regs->icsr1) == ICSR1_AAS1)
		riic->num_slave = 1;
	if (readb(riic->base + riic->info->regs->icsr1) == ICSR1_AAS2)
		riic->num_slave = 2;

	if (riic->slave[riic->num_slave]) {
		riic_clear_set_bit(riic, 0, ICIER_TEIE, riic->info->regs->icier);
		if (!riic->first_transmit) {
			i2c_slave_event(riic->slave[riic->num_slave],
						I2C_SLAVE_READ_REQUESTED, &val);
			riic->first_transmit++;
		} else {
			i2c_slave_event(riic->slave[riic->num_slave],
						I2C_SLAVE_READ_PROCESSED, &val);
		}
		/* Stop transfer if receive NACK signal from master */
		while (--timeout) {
			udelay(1);
			if ((readb(riic->base + riic->info->regs->icsr2) & ICSR2_NACKF))
				return IRQ_HANDLED;
		}
		writeb(val, riic->base + riic->info->regs->icdrt);

		return IRQ_HANDLED;
	}

	if (!riic->bytes_left)
		return IRQ_NONE;

	if (riic->bytes_left == RIIC_INIT_MSG) {
		if (riic->msg->flags & I2C_M_RD)
			/* On read, switch over to receive interrupt */
			riic_clear_set_bit(riic, ICIER_TIE, ICIER_RIE,
					   riic->info->regs->icier);
		else
			/* On write, initialize length */
			riic->bytes_left = riic->msg->len;

		val = i2c_8bit_addr_from_msg(riic->msg);
	} else {
		val = *riic->buf;
		riic->buf++;
		riic->bytes_left--;
	}

	/*
	 * Switch to transmission ended interrupt when done. Do check here
	 * after bytes_left was initialized to support SMBUS_QUICK (new msg has
	 * 0 length then)
	 */
	if (riic->bytes_left == 0)
		riic_clear_set_bit(riic, ICIER_TIE, ICIER_TEIE,
				   riic->info->regs->icier);

	/*
	 * This acks the TIE interrupt. We get another TIE immediately if our
	 * value could be moved to the shadow shift register right away. So
	 * this must be after updates to ICIER (where we want to disable TIE)!
	 */
	writeb(val, riic->base + riic->info->regs->icdrt);

	return IRQ_HANDLED;
}

static irqreturn_t riic_tend_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	if (readb(riic->base + riic->info->regs->icsr2) & ICSR2_NACKF) {
		/* We got a NACKIE */
		readb(riic->base + riic->info->regs->icdrr);	/* dummy read */
		riic_clear_set_bit(riic, ICSR2_NACKF, 0, riic->info->regs->icsr2);
		riic->err = -ENXIO;
	} else if (riic->bytes_left) {
		return IRQ_NONE;
	}

	if (riic->slave[riic->num_slave]) {
		if (readb(riic->base + riic->info->regs->icsr2) & ICSR2_TEND)
			readb(riic->base + riic->info->regs->icdrr);    /* dummy read */

		return IRQ_HANDLED;
	}


	if (riic->is_last || riic->err) {
		riic_clear_set_bit(riic, ICIER_TEIE, ICIER_SPIE, riic->info->regs->icier);
		writeb(ICCR2_SP, riic->base + riic->info->regs->iccr2);
	} else {
		/* Transfer is complete, but do not send STOP */
		riic_clear_set_bit(riic, ICIER_TEIE, 0, riic->info->regs->icier);
		complete(&riic->msg_done);
	}

	return IRQ_HANDLED;
}

static irqreturn_t riic_rdrf_isr(int irq, void *data)
{
	struct riic_dev *riic = data;
	u8 val;
	int ret;

	if (readb(riic->base + riic->info->regs->icsr1) == ICSR1_AAS0)
		riic->num_slave = 0;
	if (readb(riic->base + riic->info->regs->icsr1) == ICSR1_AAS1)
		riic->num_slave = 1;
	if (readb(riic->base + riic->info->regs->icsr1) == ICSR1_AAS2)
		riic->num_slave = 2;

	if (riic->slave[riic->num_slave]) {
		if (!riic->first_receive) {
			ret = i2c_slave_event(riic->slave[riic->num_slave],
						I2C_SLAVE_WRITE_REQUESTED, &val);
			if (ret < 0)
				riic_clear_set_bit(riic, ICMR3_RDRFS, ICMR3_ACKBT,
							riic->info->regs->icmr3);
			riic->first_receive++;
			readb(riic->base + riic->info->regs->icdrr);
		} else {
			val = readb(riic->base + riic->info->regs->icdrr);
			i2c_slave_event(riic->slave[riic->num_slave],
						I2C_SLAVE_WRITE_RECEIVED, &val);
			riic_clear_set_bit(riic, ICMR3_ACKBT, 0, riic->info->regs->icmr3);
		}

		return IRQ_HANDLED;
	}

	if (!riic->bytes_left)
		return IRQ_NONE;

	if (riic->bytes_left == RIIC_INIT_MSG) {
		riic->bytes_left = riic->msg->len;
		readb(riic->base + riic->info->regs->icdrr);	/* dummy read */
		return IRQ_HANDLED;
	}

	if (riic->bytes_left == 1) {
		/* STOP must come before we set ACKBT! */
		if (riic->is_last) {
			riic_clear_set_bit(riic, 0, ICIER_SPIE, riic->info->regs->icier);
			writeb(ICCR2_SP, riic->base + riic->info->regs->iccr2);
		}

		riic_clear_set_bit(riic, 0, ICMR3_ACKBT, riic->info->regs->icmr3);

	} else {
		riic_clear_set_bit(riic, ICMR3_ACKBT, 0, riic->info->regs->icmr3);
	}

	/* Reading acks the RIE interrupt */
	*riic->buf = readb(riic->base + riic->info->regs->icdrr);
	riic->buf++;
	riic->bytes_left--;

	return IRQ_HANDLED;
}

static irqreturn_t riic_start_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	riic_clear_set_bit(riic, ICIER_STIE, 0, riic->info->regs->icier);
	riic_clear_set_bit(riic, ICSR2_START, 0, riic->info->regs->icsr2);
	riic->first_transmit = 0;
	riic->first_receive = 0;
	riic->num_slave = -1;
	riic_clear_set_bit(riic, 0, ICMR3_RDRFS, riic->info->regs->icmr3);

	return IRQ_HANDLED;

}

static irqreturn_t riic_stop_isr(int irq, void *data)
{
	struct riic_dev *riic = data;
	u8 val;

	if ((readb(riic->base + riic->info->regs->icsar0) != 0) ||
		(readb(riic->base + riic->info->regs->icsar1) != 0) ||
			(readb(riic->base + riic->info->regs->icsar2) != 0)) {
		if (riic->num_slave > -1)
			i2c_slave_event(riic->slave[riic->num_slave], I2C_SLAVE_STOP, &val);
		if (readb(riic->base + riic->info->regs->icsr2) & ICSR2_RDRF)
			readb(riic->base + riic->info->regs->icdrr);
		writeb(0, riic->base + riic->info->regs->icsr2);
		readb(riic->base + riic->info->regs->icsr2);
		writeb(ICIER_NAKIE | ICIER_TIE | ICIER_RIE | ICIER_STIE | ICIER_SPIE,
				riic->base + riic->info->regs->icier);
		return IRQ_HANDLED;
	}
	/* read back registers to confirm writes have fully propagated */
	writeb(0, riic->base + riic->info->regs->icsr2);
	readb(riic->base + riic->info->regs->icsr2);
	writeb(0, riic->base + riic->info->regs->icier);
	readb(riic->base + riic->info->regs->icier);

	complete(&riic->msg_done);

	return IRQ_HANDLED;
}


static int riic_reg_slave(struct i2c_client *slave)
{
	struct riic_dev *riic = i2c_get_adapdata(slave->adapter);

	if (riic->slave[0] && riic->slave[1] && riic->slave[2])
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	/* Keep device active for slave address detection logic */
	pm_runtime_get_sync(riic->adapter.dev.parent);

	if (riic->slave[0] == NULL) {
		riic->slave[0] = slave;
		writew(riic->slave[0]->addr << 1, riic->base + riic->info->regs->icsar0);
		riic_clear_set_bit(riic, 0, ICSER_SAR0, riic->info->regs->icser);
	} else if (riic->slave[1] == NULL) {
		riic->slave[1] = slave;
		writew(riic->slave[1]->addr << 1, riic->base + riic->info->regs->icsar1);
		riic_clear_set_bit(riic, 0, ICSER_SAR1, riic->info->regs->icser);
	} else if (riic->slave[2] == NULL) {
		riic->slave[2] = slave;
		writew(riic->slave[2]->addr << 1, riic->base + riic->info->regs->icsar2);
		riic_clear_set_bit(riic, 0, ICSER_SAR2, riic->info->regs->icser);
	}
	/* read back registers to confirm writes have fully propagated */
	writeb(0, riic->base + riic->info->regs->icsr1);
	readb(riic->base + riic->info->regs->icsr1);
	writeb(0, riic->base + riic->info->regs->icsr2);
	readb(riic->base + riic->info->regs->icsr2);
	writeb(ICIER_NAKIE | ICIER_TIE  | ICIER_RIE | ICIER_STIE | ICIER_SPIE,
				riic->base + riic->info->regs->icier);
	riic->first_transmit = 0;
	riic->first_receive = 0;

	return 0;
}

static int riic_unreg_slave(struct i2c_client *slave)
{
	struct riic_dev *riic = i2c_get_adapdata(slave->adapter);

	/* read back registers to confirm writes have fully propagated */
	writeb(0, riic->base + riic->info->regs->icsr1);
	readb(riic->base + riic->info->regs->icsr1);
	writeb(0, riic->base + riic->info->regs->icsr2);
	readb(riic->base + riic->info->regs->icsr2);

	if ((riic->slave[0] != NULL) && (riic->slave[0]->addr == slave->addr)) {
		writew(0, riic->base + riic->info->regs->icsar0);
		riic_clear_set_bit(riic, ICSER_SAR0, 0, riic->info->regs->icser);
		riic->slave[0] = NULL;
	}
	if ((riic->slave[1] != NULL) && (riic->slave[1]->addr == slave->addr)) {
		writew(0, riic->base + riic->info->regs->icsar1);
		riic_clear_set_bit(riic, ICSER_SAR1, 0, riic->info->regs->icser);
		riic->slave[1] = NULL;
	}
	if ((riic->slave[2] != NULL) && (riic->slave[2]->addr == slave->addr)) {
		writew(0, riic->base + riic->info->regs->icsar2);
		riic_clear_set_bit(riic, ICSER_SAR2, 0, riic->info->regs->icser);
		riic->slave[2] = NULL;
	}

	pm_runtime_put(riic->adapter.dev.parent);

	return 0;
}

static u32 riic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm riic_algo = {
	.master_xfer		= riic_xfer,
	.master_xfer_atomic	= riic_xfer_atomic,
	.functionality		= riic_func,
	.reg_slave		= riic_reg_slave,
	.unreg_slave		= riic_unreg_slave,
};

static int riic_init_hw(struct riic_dev *riic, bool recover)
{
	int ret;
	unsigned long rate;
	int total_ticks, cks, brl, brh;
	struct i2c_timings *t = &riic->i2c_t;
	struct device *dev = riic->adapter.dev.parent;

	if (t->bus_freq_hz > riic->info->max_speed) {
		dev_err(riic->adapter.dev.parent,
			"unsupported bus speed (%dHz). %d max\n",
			t->bus_freq_hz, riic->info->max_speed);
		return -EINVAL;
	}

	if (t->bus_freq_hz == I2C_MAX_FAST_MODE_PLUS_FREQ)
		riic_clear_set_bit(riic, ICFER_FMPE, ICFER_FMPE,
				   riic->info->regs->icfer);

	rate = clk_get_rate(riic->clk);

	riic_clear_set_bit(riic, 0, ICFER_SCLE | ICFER_NFE,
			   riic->info->regs->icfer);

	/*
	 * Assume the default register settings:
	 *  FER.SCLE = 1 (SCL sync circuit enabled, adds 2 or 3 cycles)
	 *  FER.NFE = 1 (noise circuit enabled)
	 *  MR3.NF = 0 (1 cycle of noise filtered out)
	 *
	 * Freq (CKS=000) = (I2CCLK + tr + tf)/ (BRH + 3 + 1) + (BRL + 3 + 1)
	 * Freq (CKS!=000) = (I2CCLK + tr + tf)/ (BRH + 2 + 1) + (BRL + 2 + 1)
	 */

	/*
	 * Determine reference clock rate. We must be able to get the desired
	 * frequency with only 62 clock ticks max (31 high, 31 low).
	 * Aim for a duty of:
	 * - Below 50kHz: 50% LOW, 50% HIGH.
	 * - Above 50kHz: 60% LOW, 40% HIGH
	 */
	total_ticks = DIV_ROUND_UP(rate, t->bus_freq_hz ?: 1);

	for (cks = 0; cks < 8; cks++) {
		/*
		 * Period of low time (60% or 50%) must be less than BRL + 2 + 1
		 * BRL max register value is 0x1F.
		 */
		brl = ((total_ticks * ((t->bus_freq_hz >= 50000) ? 6: 5)) / 10);
		if (brl <= (0x1F + 3))
			break;

		total_ticks /= 2;
		rate /= 2;
	}

	if (brl > (0x1F + 3)) {
		dev_err(riic->adapter.dev.parent, "invalid speed (%lu). Too slow.\n",
			(unsigned long)t->bus_freq_hz);
		return -EINVAL;
	}

	brh = total_ticks - brl;

	/* Remove automatic clock ticks for sync circuit and NF */
	if (cks == 0) {
		brl -= 4;
		brh -= 4;
	} else {
		brl -= 3;
		brh -= 3;
	}

	/*
	 * Remove clock ticks for rise and fall times. Convert ns to clock
	 * ticks.
	 */
	brl -= t->scl_fall_ns / (1000000000 / rate);
	brh -= t->scl_rise_ns / (1000000000 / rate);

	/* Adjust for min register values for when SCLE=1 and NFE=1 */
	if (brl < 1)
		brl = 1;
	if (brh < 1)
		brh = 1;

	pr_debug("i2c-riic: freq=%lu, duty=%d, fall=%lu, rise=%lu, cks=%d, brl=%d, brh=%d\n",
		 rate / total_ticks, ((brl + 3) * 100) / (brl + brh + 6),
		 t->scl_fall_ns / (1000000000 / rate),
		 t->scl_rise_ns / (1000000000 / rate), cks, brl, brh);

	if (!recover) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret)
			return ret;
	}

	/* Changing the order of accessing IICRST and ICE may break things! */
	writeb(ICCR1_IICRST | ICCR1_SOWP, riic->base + riic->info->regs->iccr1);
	riic_clear_set_bit(riic, 0, ICCR1_ICE, riic->info->regs->iccr1);

	writeb(ICMR1_CKS(cks), riic->base + riic->info->regs->icmr1);
	writeb(brh | ICBR_RESERVED, riic->base + riic->info->regs->icbrh);
	writeb(brl | ICBR_RESERVED, riic->base + riic->info->regs->icbrl);

	writeb(0, riic->base + riic->info->regs->icser);
	writeb(ICMR3_ACKWP | ICMR3_RDRFS, riic->base + riic->info->regs->icmr3);

	riic_clear_set_bit(riic, ICCR1_IICRST, 0, riic->info->regs->iccr1);

	if (!recover) {
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	}
	return 0;
}

static int riic_recover_bus(struct i2c_adapter *adap)
{
	struct riic_dev *riic = i2c_get_adapdata(adap);
	struct device *dev = riic->adapter.dev.parent;
	int ret, i;
	u8 val;

	ret = riic_init_hw(riic, true);
	if (ret)
		return -EINVAL;

	/* output extra SCL clock cycles with master arbitration-lost detection disabled */
	riic_clear_set_bit(riic, ICFER_MALE, 0, riic->info->regs->icfer);

	for (i = 0; i < RIIC_RECOVERY_CLK_CNT; i++) {
		riic_clear_set_bit(riic, 0, ICCR1_CLO, riic->info->regs->iccr1);
		ret = readb_poll_timeout(riic->base + riic->info->regs->iccr1, val,
					 !(val & ICCR1_CLO), 0, 100);
		if (ret) {
			dev_err(dev, "SCL clock cycle timeout\n");
			return ret;
		}
	}

	/*
	 * The last clock cycle may have driven the SDA line high, so add a
	 * short delay to allow the line to stabilize before checking the status.
	 */
	udelay(5);

	/*
	 * If an incomplete byte write occurs, the SDA line may remain low
	 * even after 9 clock pulses, indicating the bus is not released.
	 * To resolve this, send an additional clock pulse to simulate a STOP
	 * condition and ensure proper bus release.
	 */
	if (!(readb(riic->base + riic->info->regs->iccr1) & ICCR1_SDAI) &&
	    (readb(riic->base + riic->info->regs->iccr1) & ICCR1_SCLI)) {
		riic_clear_set_bit(riic, 0, ICCR1_CLO, riic->info->regs->iccr1);
		ret = readb_poll_timeout(riic->base + riic->info->regs->iccr1, val,
					 !(val & ICCR1_CLO), 0, 100);
		if (ret) {
			dev_err(dev, "SCL clock cycle timeout occurred while issuing the STOP condition\n");
			return ret;
		}
		/* delay to make sure SDA line goes back HIGH again */
		udelay(5);
	}

	/* clear any flags set */
	writeb(0, riic->base + riic->info->regs->icsr2);
	/* read back register to confirm writes */
	readb(riic->base + riic->info->regs->icsr2);

	/* restore back ICFER_MALE */
	riic_clear_set_bit(riic, 0, ICFER_MALE, riic->info->regs->icfer);

	if (!(readb(riic->base + riic->info->regs->iccr1) & ICCR1_SDAI) ||
	    !(readb(riic->base + riic->info->regs->iccr1) & ICCR1_SCLI))
		return -EINVAL;

	return 0;
}

static struct riic_irq_desc riic_irqs[] = {
	{ .res_num = 0, .isr = riic_tend_isr, .name = "riic-tend" },
	{ .res_num = 1, .isr = riic_rdrf_isr, .name = "riic-rdrf" },
	{ .res_num = 2, .isr = riic_tdre_isr, .name = "riic-tdre" },
	{ .res_num = 3, .isr = riic_stop_isr, .name = "riic-stop" },
	{ .res_num = 4, .isr = riic_start_isr, .name = "riic-start" },
	{ .res_num = 5, .isr = riic_tend_isr, .name = "riic-nack" },
};

static struct i2c_bus_recovery_info riic_bri = {
	.recover_bus = riic_recover_bus,
};

static void riic_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int riic_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct riic_dev *riic;
	struct i2c_adapter *adap;
	struct resource *res;
	int i, ret;
	struct riic_platform_info *info;

	info = (struct riic_platform_info *)of_device_get_match_data(&pdev->dev);

	riic = devm_kzalloc(dev, sizeof(*riic), GFP_KERNEL);
	if (!riic)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	riic->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(riic->base))
		return PTR_ERR(riic->base);

	riic->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(riic->clk)) {
		dev_err(dev, "missing controller clock");
		return PTR_ERR(riic->clk);
	}

	riic->rstc = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(riic->rstc))
		return dev_err_probe(dev, PTR_ERR(riic->rstc),
				     "Error: missing reset ctrl\n");

	ret = reset_control_deassert(riic->rstc);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, riic_reset_control_assert, riic->rstc);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(riic_irqs); i++) {
		res = platform_get_resource(pdev, IORESOURCE_IRQ, riic_irqs[i].res_num);
		if (!res)
			return -ENODEV;

		ret = devm_request_irq(dev, res->start, riic_irqs[i].isr,
					0, riic_irqs[i].name, riic);
		if (ret) {
			dev_err(dev, "failed to request irq %s\n", riic_irqs[i].name);
			return ret;
		}
	}

	for (i = 0; i < MAX_SLAVE_DEVICE; i++)
		riic->slave[i] = NULL;
	riic->info = info;
	adap = &riic->adapter;
	i2c_set_adapdata(adap, riic);
	strlcpy(adap->name, "Renesas RIIC adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &riic_algo;
	adap->bus_recovery_info = &riic_bri;
	adap->dev.parent = dev;
	adap->dev.of_node = dev->of_node;

	init_completion(&riic->msg_done);

	i2c_parse_fw_timings(dev, &riic->i2c_t, true);

	/* Default 0 to save power. Can be overridden via sysfs for lower latency. */
	pm_runtime_set_autosuspend_delay(dev, 0);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	ret = riic_init_hw(riic, false);
	if (ret)
		goto out;

	ret = i2c_add_adapter(adap);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, riic);

	dev_info(dev, "registered with %dHz bus speed\n",
		 riic->i2c_t.bus_freq_hz);
	return 0;

out:
	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
	return ret;
}

static int riic_i2c_remove(struct platform_device *pdev)
{
	struct riic_dev *riic = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (!ret) {
		writeb(0, riic->base + riic->info->regs->icier);
		pm_runtime_put(dev);
	}
	i2c_del_adapter(&riic->adapter);
	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);

	return 0;
}

static const struct riic_regs common_riic_regs = {
	.iccr1 = 0x00,
	.iccr2 = 0x04,
	.icmr1 = 0x08,
	.icmr3 = 0x10,
	.icfer = 0x14,
	.icser = 0x18,
	.icier = 0x1c,
	.icsr1 = 0x20,
	.icsr2 = 0x24,
	.icsar0 = 0x28,
	.icsar1 = 0x2C,
	.icsar2 = 0x30,
	.icbrl = 0x34,
	.icbrh = 0x38,
	.icdrt = 0x3c,
	.icdrr = 0x40,
};

static const struct riic_regs rzg3s_riic_regs = {
	.iccr1 = 0x00,
	.iccr2 = 0x01,
	.icmr1 = 0x02,
	.icmr3 = 0x04,
	.icfer = 0x05,
	.icser = 0x06,
	.icier = 0x07,
	.icsr2 = 0x09,
	.icbrl = 0x10,
	.icbrh = 0x11,
	.icdrt = 0x12,
	.icdrr = 0x13,
};

static const struct riic_platform_info riic_rz_common_plat_data = {
	.max_speed = I2C_MAX_FAST_MODE_PLUS_FREQ,
	.regs = &common_riic_regs,
};

static const struct riic_platform_info riic_r7s72100_plat_data = {
	.max_speed = I2C_MAX_FAST_MODE_FREQ,
	.regs = &common_riic_regs,
};

static const struct riic_platform_info riic_rzg3s_plat_data = {
	.max_speed = I2C_MAX_FAST_MODE_PLUS_FREQ,
	.regs = &rzg3s_riic_regs,
};

static const struct of_device_id riic_i2c_dt_ids[] = {
	{ .compatible = "renesas,riic-r7s9210", .data = &riic_rz_common_plat_data },
	{ .compatible = "renesas,riic-r7s72100", .data = &riic_r7s72100_plat_data },
	{ .compatible = "renesas,riic-rz", .data = &riic_rz_common_plat_data },
	{ .compatible = "renesas,riic-r9a08g045", .data = &riic_rzg3s_plat_data },
	{ /* Sentinel */ },
};

static int riic_i2c_suspend(struct device *dev)
{
	struct riic_dev *riic = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	i2c_mark_adapter_suspended(&riic->adapter);

	/* Disable output on SDA, SCL pins. */
	riic_clear_set_bit(riic, ICCR1_ICE, 0, riic->info->regs->iccr1);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_sync(dev);

	return reset_control_assert(riic->rstc);
}

static int riic_i2c_resume(struct device *dev)
{
	struct riic_dev *riic = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(riic->rstc);
	if (ret)
		return ret;

	ret = riic_init_hw(riic, false);
	if (ret) {
		/*
		 * In case this happens there is no way to recover from this
		 * state. The driver will remain loaded. We want to avoid
		 * keeping the reset line de-asserted for no reason.
		 */
		reset_control_assert(riic->rstc);
		return ret;
	}

	i2c_mark_adapter_resumed(&riic->adapter);

	return 0;
}

static const struct dev_pm_ops riic_i2c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(riic_i2c_suspend, riic_i2c_resume)
};

static struct platform_driver riic_i2c_driver = {
	.probe		= riic_i2c_probe,
	.remove		= riic_i2c_remove,
	.driver		= {
		.name	= "i2c-riic",
		.of_match_table = riic_i2c_dt_ids,
		.pm	= pm_ptr(&riic_i2c_pm_ops),
	},
};

module_platform_driver(riic_i2c_driver);

MODULE_DESCRIPTION("Renesas RIIC adapter");
MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, riic_i2c_dt_ids);

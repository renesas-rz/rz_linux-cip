#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sh_dma.h>

#include <linux/spi/spi.h>
#include "linux/spi/rzv2m_csi.h"
#include "linux/spi/rzv2m_csi_regif.h"

#include <asm/unaligned.h>

#define MAX_BYTE_FIFO_SIZE 32 //Maximum 32 byte was stored in FIFO
#define CSI_WAIT_TIME 5000000 //5s
#define U_TIME_DELAY 1 //1us

enum {
       CSI_SPI_MASTER,
       CSI_SPI_SLAVE,
};

struct rzv2m_csi_info {
       int mode;
       unsigned int interval_time;
       unsigned int transmit_trigger_level;
       unsigned int receive_trigger_level;
};

struct rzv2m_csi_priv {
       struct spi_master *master;
       void __iomem *regs;
       struct clk *clk;
       struct clk *pclk;
       int irq;
       struct device *dev;
       const void *txbuf;
       void *rxbuf;
       int bytes_to_transfer;
       int bytes_to_receive;

       struct rzv2m_csi_info *info;

       struct platform_device *pdev;

       unsigned int min_div_pow;
       void *tx_dma_page;
       void *rx_dma_page;
       dma_addr_t tx_dma_addr;
       dma_addr_t rx_dma_addr;
       struct completion done;
       struct completion rx_done;
       struct completion done_txdma;

       unsigned short unused_ss;
       bool native_cs_inited;
       bool native_cs_high;

       bool slave_aborted;
       bool transmission_completed;
       uint32_t time_wait;
};


//-------------------prototype-------------------
static u32 reg_read(struct rzv2m_csi_priv *p, int reg_offs);
static void reg_write(struct rzv2m_csi_priv *p, int reg_offs, u32 value);
static u32 reg_read_bit(struct rzv2m_csi_priv *p, int reg_offs, int bit_mask);
static void reg_write_bit(struct rzv2m_csi_priv *p, int reg_offs, int bit_mask, u32 value);
static void rzv2m_csi_filltxfifo(struct rzv2m_csi_priv *p, int size);
static void rzv2m_csi_readrxfifo(struct rzv2m_csi_priv *p, u32 size);
static irqreturn_t rzv2m_csi_irq_handler(int irq, void *data);
static void rzv2m_csi_spi_set_clk_regs(struct rzv2m_csi_priv *p, unsigned long parent_rate, u32 spi_hz);
static int rzv2m_csi_setup_operation_mode(struct spi_master *master, struct spi_transfer *t);
static int rzv2m_csi_setup_clock_selection(struct spi_master *master, struct spi_device *spi);
static int rzv2m_csi_clear_all_irq(struct rzv2m_csi_priv *p);
static bool is_recv_only_mode(struct rzv2m_csi_priv *p);
static int rzv2m_csi_set_irq(struct spi_master *master, struct spi_transfer *t);
static int rzv2m_csi_clear_fifo_buffer(struct spi_master *master, struct spi_transfer *t);
static int rzv2m_csi_start_operation(struct rzv2m_csi_priv *p);
static int rzv2m_csi_stop_operation(struct rzv2m_csi_priv *p);
static bool is_16bit_data_leng(struct rzv2m_csi_priv *p);
static int rzv2m_csi_set_fifo_trg(struct spi_master *master, struct spi_transfer *t);
static void get_data_in_ififo(struct rzv2m_csi_priv *p);
static int rzv2m_csi_spi_setup(struct spi_device *spi);

#if 0
static struct dma_chan *rzv2m_csi_request_dma_chan(struct device *dev,\
       enum dma_transfer_direction dir, unsigned int id, dma_addr_t port_addr);
static void rzv2m_csi_release_dma(struct rzv2m_csi_priv *p);
static int rzv2m_csi_request_dma(struct rzv2m_csi_priv *p
);
#endif

static int rzv2m_csi_slave_abort(struct spi_master *master);
static struct rzv2m_csi_info* rzv2m_csi_parse_dt(struct device *dev);
//-------------------end prototype-------------------

spinlock_t lock_irq;

uint32_t salve_count = 0;

static u32 reg_read(struct rzv2m_csi_priv *p, int reg_offs){
       return ioread32(p->regs + reg_offs);
}

static void reg_write(struct rzv2m_csi_priv *p, int reg_offs, u32 value){
       iowrite32(value, p->regs + reg_offs);
}

static u32 reg_read_bit(struct rzv2m_csi_priv *p, int reg_offs, int bit_mask){
       u32 value;

       value = reg_read(p, reg_offs) & bit_mask;

       while((bit_mask%2) == 0){
           value = value >> 1;
           bit_mask = bit_mask >> 1;
       }

       return (u32) value;
}

static void reg_write_bit(struct rzv2m_csi_priv *p, int reg_offs, int bit_mask, u32 value){
       volatile u32 tmp;

       tmp = bit_mask;
       while((tmp%2) == 0){
           value = value << 1;
           tmp = tmp >> 1;
       }
       tmp = (reg_read(p, reg_offs) & ~bit_mask) | value;
       reg_write(p, reg_offs, tmp);
}

static void reg_clear_irq(struct rzv2m_csi_priv *p, int irq_msk){
        volatile u32 tmp;

        tmp = reg_read(p, CSI_INT);

        reg_write(p, CSI_INT, tmp & irq_msk);
}

static void rzv2m_csi_filltxfifo(struct rzv2m_csi_priv *p, int size)
{
       int i = 0;
       if (p->bytes_to_transfer <= 0){
               return; //fixme. Check byte transfer remain.
       }
       while(size + reg_read(p, CSI_OFIFOL) > MAX_BYTE_FIFO_SIZE){}

       if(is_16bit_data_leng(p)){
               u16 *buf = (u16 *)p->txbuf;
               for(i = 0; i < size/2;i++){
                       reg_write(p, CSI_OFIFO, buf[i]);
               }
       } else {
               u8 *buf = (u8 *)p->txbuf;
               for(i = 0; i < size;i++){
                       reg_write(p, CSI_OFIFO, buf[i]);
               }
       }
       p->txbuf += size;
       p->bytes_to_transfer -= size;
}

static void rzv2m_csi_readrxfifo(struct rzv2m_csi_priv *p, u32 size)
{
    int i = 0;
       if(is_16bit_data_leng(p)){
               u16 *buf = (u16 *)p->rxbuf;
               for(i = 0; i < size/2;i++){
                       buf[i] = (u16)reg_read(p, CSI_IFIFO);
               }
       } else {
               u8 *buf = p->rxbuf;
               for(i = 0; i < size;i++){
                       buf[i] = (u8)reg_read(p, CSI_IFIFO);
               }
       }
       p->rxbuf += size;
       p->bytes_to_receive += size;
}

static irqreturn_t rzv2m_csi_irq_handler(int irq, void *data){
       struct rzv2m_csi_priv *p = (struct rzv2m_csi_priv*)data;
       unsigned long flag;

       spin_lock_irqsave(&lock_irq, flag);
       p->time_wait = 0; //reset timeout wait

       if(reg_read_bit(p, CSI_INT, CSI_INT_R_TRGR) == 1){
               get_data_in_ififo(p);
               reg_clear_irq(p, CSI_INT_R_TRGR);

               //fixme. w/a for stopping transfer data in reception-only mode
               if(p->bytes_to_receive >= p->bytes_to_transfer){
                       rzv2m_csi_stop_operation(p);
               }

       }

       if((reg_read_bit(p, CSI_INT, CSI_INT_TREND) == 1)  || \
           (reg_read_bit(p, CSI_INT, CSI_INT_T_TRGR) == 1)){
               if(p->rxbuf != NULL){
                       get_data_in_ififo(p);
               }
               reg_clear_irq(p, CSI_INT_TREND);
               reg_clear_irq(p, CSI_INT_T_TRGR);
               rzv2m_csi_filltxfifo(p, min_t(int, p->bytes_to_transfer, MAX_BYTE_FIFO_SIZE));
       }

       if(reg_read_bit(p, CSI_INT, CSI_INT_OVERF) == 1){
               get_data_in_ififo(p);
               reg_clear_irq(p, CSI_INT_OVERF);
               dev_err(&p->pdev->dev, "Overflow error \n");
       } else if(reg_read_bit(p, CSI_INT, CSI_INT_UNDER) == 1){
        reg_write_bit(p, CSI_CNT, CSI_CNT_CSIRST, 0x1);
        reg_write_bit(p, CSI_CNT, CSI_CNT_CSIRST, 0x0);

               rzv2m_csi_filltxfifo(p, min_t(int, p->bytes_to_transfer, MAX_BYTE_FIFO_SIZE));

        rzv2m_csi_start_operation(p);

               dev_err(&p->pdev->dev, "Underrun error \n");
    }

       if(reg_read_bit(p, CSI_INT, CSI_INT_CSIEND) == 1){
               reg_clear_irq(p, CSI_INT_CSIEND);
       }

       spin_unlock_irqrestore(&lock_irq, flag);

       return IRQ_HANDLED;
}

static void rzv2m_csi_spi_set_clk_regs(struct rzv2m_csi_priv *p,
                                     unsigned long parent_rate, u32 spi_hz)
{
       u32 cks;

       if (!spi_hz || !parent_rate) {
               WARN(1, "Invalid clock rate parameters %lu and %u\n",
                    parent_rate, spi_hz);
               return;
       }
       cks = parent_rate/(spi_hz*2);

       if (cks > 0x3FFF) {
               dev_err(&p->pdev->dev,
                       "Requested SPI transfer rate %d is too low\n", spi_hz);
               cks = 0x3FFF;
       } else if(cks < 0x1) {
               dev_err(&p->pdev->dev,
                       "Requested SPI transfer rate %d is too large\n", spi_hz);
               cks = 0x1;
       }
       reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_CKS, cks);
}

static int rzv2m_csi_setup_operation_mode(struct spi_master *master, struct spi_transfer *t){
       int ret = 0;
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);
       const void *tx_buf = t->tx_buf;
       void *rx_buf = t->rx_buf;
       struct rzv2m_csi_info *info = p->info;

       /* Configure pins before asserting CS */
       if (!spi_controller_is_slave(p->master)){
               reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_SLAVE, 0x0); //master
               if((rx_buf != NULL) && (tx_buf == NULL)){
                       //reception-only mode
                       reg_write_bit(p, CSI_MODE, CSI_MODE_TRMD, 0x0);
               } else {
                       //send and receive
               reg_write_bit(p, CSI_MODE, CSI_MODE_TRMD, 0x1);
               }

               if(info->interval_time < 16){
                       reg_write_bit(p, CSI_MODE, CSI_MODE_DATWT, info->interval_time);
               } else {
                       ret = -EINVAL;
               }
       } else {
               reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_SLAVE, 0x1); //slave
               if((rx_buf != NULL) && (tx_buf == NULL)){
                       //reception-only mode. fixme
                       reg_write_bit(p, CSI_MODE, CSI_MODE_TRMD, 0x0);
               } else {
               //send and receive
                       reg_write_bit(p, CSI_MODE, CSI_MODE_TRMD, 0x1);
               }
       }

       //setup data length
       if(t->bits_per_word == 16){
           reg_write_bit(p, CSI_MODE, CSI_MODE_CCL, 0x1);
       } else {
        reg_write_bit(p, CSI_MODE, CSI_MODE_CCL, 0x0);
       }

       return ret;
}

static int rzv2m_csi_setup_clock_selection(struct spi_master *master, struct spi_device *spi){
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);
       u32 dap, ckp, lsb, ss_pol;

       dap = !!(spi->mode & SPI_CPHA);
       ckp = !!(spi->mode & SPI_CPOL);

       lsb = !!(spi->mode & SPI_LSB_FIRST);

       reg_write_bit(p, CSI_MODE, CSI_MODE_DIR, lsb);
       reg_write_bit(p, CSI_MODE, CSI_MODE_DIR, lsb);
       reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_CKP, ckp);
       reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_DAP, dap);

       if (spi_controller_is_slave(p->master)){
               //The slave select signal is used to select the slave that
               //communicates with the master in a system in
               //which multiple slaves are connected to one master.

               if(salve_count > 1){
                       reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_SS_ENA, 0x1);
               } else {
                       reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_SS_ENA, 0x0);
               }

               ss_pol = !!(spi->mode & SPI_CS_HIGH);
               reg_write_bit(p, CSI_CLKSEL, CSI_CLKSEL_SS_POL, ss_pol);
       }

       return 0;
}

static int rzv2m_csi_clear_all_irq(struct rzv2m_csi_priv *p){
       int ret = 0;

       reg_write(p, CSI_INT, 0x3311);

       return ret;
}

static bool is_recv_only_mode(struct rzv2m_csi_priv *p){
       bool ret = false;

       u32 tmp = reg_read_bit(p, CSI_MODE, CSI_MODE_TRMD);
       if(tmp != 0){
               ret = false;
       } else {
               ret = true;
       }

       return ret;
}

static int rzv2m_csi_set_irq(struct spi_master *master, struct spi_transfer *t){
       int ret = 0;
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);
       void *rx_buf = t->rx_buf;

    if(!spi_controller_is_slave(master)){
               if(is_recv_only_mode(p)){
                       reg_write_bit(p, CSI_CNT, CSI_CNT_CSIEND_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_R_TRGEN, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_R_TRGR_E, 0x1);
               } else {
                       reg_write_bit(p, CSI_CNT, CSI_CNT_TREND_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_T_TRGEN, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_T_TRGR_E, 0x1);
                       //fixme. conflict trans only - trans/recv
                       if(rx_buf != NULL){
                               reg_write_bit(p, CSI_CNT, CSI_CNT_OVERF_E, 0x1);
                       }
               }
       } else {
               if(is_recv_only_mode(p)){
                       //reg_write_bit(p, CSI_CNT, CSI_CNT_TREND_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_CSIEND_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_R_TRGEN, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_R_TRGR_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_OVERF_E, 0x1);
               } else {
                       reg_write_bit(p, CSI_CNT, CSI_CNT_TREND_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_T_TRGEN, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_T_TRGR_E, 0x1);
                       reg_write_bit(p, CSI_CNT, CSI_CNT_UNDER_E, 0x1);

                       //fixme. conflict trans only - trans/recv
                       if(rx_buf != NULL){
                               reg_write_bit(p, CSI_CNT, CSI_CNT_OVERF_E, 0x1);
                       }
               }
       }

       return ret;
}

static int rzv2m_csi_clear_fifo_buffer(struct spi_master *master, struct spi_transfer *t){
       int ret = 0;
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);

       reg_write(p, CSI_IFIFOL, 0x0);
       reg_write(p, CSI_OFIFOL, 0x0);

       return ret;
}

static int rzv2m_csi_start_operation(struct rzv2m_csi_priv *p){
       int ret = 0;
       bool flag_end = false;
       int count = 0;
       int tmp;

       do{
               reg_write_bit(p, CSI_MODE, CSI_MODE_CSIE, 0x1);
               tmp = reg_read_bit(p, CSI_MODE, CSI_MODE_CSOT);
               if((tmp != 0) || (count > TRY_TIMES)){
                       flag_end = true;
               }
               count++;
       } while(!flag_end);

       return ret;
}

static int rzv2m_csi_stop_operation(struct rzv2m_csi_priv *p){
       int ret = 0;
       bool flag_end = false;
       int count = 0;
       int tmp;

       do{
               reg_write_bit(p, CSI_MODE, CSI_MODE_CSIE, 0x0);
               tmp = reg_read_bit(p, CSI_MODE, CSI_MODE_CSOT);
               if((tmp == 0) || (count > TRY_TIMES)){
                       flag_end = true;
               }
               count++;
       } while(!flag_end);

       return ret;
}

static bool is_16bit_data_leng(struct rzv2m_csi_priv *p){
       u32 tmp;
       bool ret;

       tmp = reg_read_bit(p, CSI_MODE, CSI_MODE_CCL);
       if(tmp != 0){
               ret = true;
       } else {
               ret = false;
       }
       return ret;
}


static int rzv2m_csi_set_fifo_trg(struct spi_master *master, struct spi_transfer *t){
       int ret = 0;
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);
       struct rzv2m_csi_info *info = p->info;
       const void *tx_buf = t->tx_buf;
       void *rx_buf = t->rx_buf;

       if(!tx_buf){
               if(is_16bit_data_leng(p)){ //16 bit mode
                       if((info->transmit_trigger_level > 4)){
                               ret = -EINVAL;
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_T_TRG, 4);
                       } else {
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_T_TRG, info->transmit_trigger_level);
                       }
               } else {
                       if((info->transmit_trigger_level > 5)){
                               ret = -EINVAL;
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_T_TRG, 5);
                       } else {
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_T_TRG, info->transmit_trigger_level);
                       }
               }
       }

       if(!rx_buf){
               //setting for receive trigger level
               if(is_16bit_data_leng(p)){ //16 bit mode
                       if((info->receive_trigger_level > 4)){
                               ret = -EINVAL;
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_R_TRG, 4);
                       } else {
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_R_TRG, info->receive_trigger_level);
                       }
               } else {
                       if((info->receive_trigger_level > 5)){
                               ret = -EINVAL;
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_R_TRG, 5);
                       } else {
                               reg_write_bit(p, CSI_FIFOTRG, CSI_FIFOTRG_R_TRG, info->receive_trigger_level);
                       }
               }
       }
       return ret;
}

#if 0
static int rzv2m_csi_wait_for_completion(struct rzv2m_csi_priv *p,
                                       struct completion *x)
{
       if (spi_controller_is_slave(p->master)) {
               if (wait_for_completion_interruptible(x) ||
                   p->slave_aborted) {
                       dev_dbg(&p->pdev->dev, "interrupted\n");
                       return -EINTR;
               }
       } else {
               if (!wait_for_completion_timeout(x, HZ)) {
                       dev_err(&p->pdev->dev, "timeout\n");
                       return -ETIMEDOUT;
               }
       }

       return 0;
}
#endif

static void get_data_in_ififo(struct rzv2m_csi_priv *p){
       unsigned int tmp;

       while((tmp = reg_read(p, CSI_IFIFOL)) != 0){
               rzv2m_csi_readrxfifo(p, tmp);
       }

       return;
}

static int rzv2m_csi_start_transfer(struct spi_master *master,
                                     struct spi_device *qspi,
                                     struct spi_transfer *transfer)
{
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);
       struct platform_device *pdev = p->pdev;
       struct device *dev = &pdev->dev;
       int ret;

    p->txbuf = transfer->tx_buf;
       p->rxbuf = transfer->rx_buf;
    p->transmission_completed = false;

       /* Release of CSI reset and selection of data transfer method */
       reg_write(p, CSI_CNT, 0x0);

       /* setup operation mode */
       ret = rzv2m_csi_setup_operation_mode(master, transfer);
       if (ret) {
               dev_warn(dev, "rzv2m_csi_setup_operation_mode failed %d\n", ret);
       }

       ret = rzv2m_csi_setup_clock_selection(master, qspi);
       if (ret) {
               dev_warn(dev, "rzv2m_csi_setup_clock_selection failed %d\n", ret);
       }

       /* setup clocks frequent division */
       if (!spi_controller_is_slave(p->master)){
               rzv2m_csi_spi_set_clk_regs(p, clk_get_rate(p->clk), transfer->speed_hz);
       }

       /* Perform CSI reset and enable CSI_CLKSEL setting */
       reg_write_bit(p, CSI_CNT, CSI_CNT_CSIRST, 0x1);
       while(!(reg_read_bit(p, CSI_CNT, CSI_CNT_CSIRST) == 0x1)){
               udelay(10);
       }

       /* Canceling CSI reset */
       reg_write_bit(p, CSI_CNT, CSI_CNT_CSIRST, 0x0);

       while(!(reg_read_bit(p, CSI_CNT, CSI_CNT_CSIRST) == 0)){
               udelay(10);
       }

       //Clear all interrupt factor flags
       ret = rzv2m_csi_clear_all_irq(p);
       if (ret) {
               dev_warn(dev, "rzv2m_csi_clear_all_irq failed %d\n", ret);
       }

       //Set the receive trigger level
       ret = rzv2m_csi_set_fifo_trg(master, transfer);
       if (ret) {
               dev_warn(dev, "rzv2m_csi_clear_all_irq failed %d\n", ret);
       }

       //Enable interrupt
       ret = rzv2m_csi_set_irq(master, transfer);
       if (ret) {
               dev_warn(dev, "rzv2m_csi_set_irq failed %d\n", ret);
       }

       p->bytes_to_transfer = transfer->len;
       p->bytes_to_receive = 0;
       p->time_wait = 0;

       //clear receive buffer
       if(p->rxbuf != NULL){
               memset(p->rxbuf, 0, p->bytes_to_transfer);
       }

       //Clear the receive FIFO buffer
       ret = rzv2m_csi_clear_fifo_buffer(master, transfer);
       if (ret) {
               dev_warn(dev, "rzv2m_csi_clear_fifo_buffer failed %d\n", ret);
       }

       if(is_recv_only_mode(p)){
           ret = rzv2m_csi_start_operation(p);
               //stop function will be handled in IRQ Handler
       } else {
               rzv2m_csi_filltxfifo(p, min_t(int, p->bytes_to_transfer, MAX_BYTE_FIFO_SIZE));

           //Start transmission / reception operation
           ret = rzv2m_csi_start_operation(p);
           if (ret) {
               dev_err(dev, "rzv2m_csi_start_operation failed %d\n", ret);
           }
               if(p->rxbuf != NULL){
                       while(p->bytes_to_receive < transfer->len){
                               get_data_in_ififo(p);

                               if(p->time_wait++ > CSI_WAIT_TIME){
                                       break;
                               }
                               udelay(U_TIME_DELAY);
                       }
               } else {
                       while(p->bytes_to_transfer > 0){
                               rzv2m_csi_filltxfifo(p, min_t(int, p->bytes_to_transfer, MAX_BYTE_FIFO_SIZE));
                       }
               }


               if(p->bytes_to_receive < transfer->len){
               dev_err(dev, "Fail. Receive/Total = %d/%d byte\n", p->bytes_to_receive, transfer->len);
               }
        rzv2m_csi_stop_operation(p);
       }

       return ret;
}

static int rzv2m_csi_spi_setup(struct spi_device *spi){
       int ret = 0;

       return ret;
}

#if 0 //DMA hasn't supported for this version
static struct dma_chan *rzv2m_csi_request_dma_chan(struct device *dev,
       enum dma_transfer_direction dir, unsigned int id, dma_addr_t port_addr){
       dma_cap_mask_t mask;
       struct dma_chan *chan;
       struct dma_slave_config cfg;
       int ret;

    dma_cap_zero(mask);
       dma_cap_set(DMA_SLAVE, mask);

       chan = dma_request_slave_channel_compat(mask, shdma_chan_filter,
                               (void *)(unsigned long)id, dev,
                               dir == DMA_MEM_TO_DEV ? "tx" : "rx");
       if (!chan) {
               dev_warn(dev, "dma_request_slave_channel_compat failed\n");
               return NULL;
       }
       memset(&cfg, 0, sizeof(cfg));

       cfg.direction = dir;

       if (dir == DMA_MEM_TO_DEV) {
               cfg.dst_addr = port_addr;
               cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
       } else {
               cfg.src_addr = port_addr;
               cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
       }
       ret = dmaengine_slave_config(chan, &cfg);
       if (ret) {
               dev_warn(dev, "dmaengine_slave_config failed %d\n", ret);
               dma_release_channel(chan);
               return NULL;
       }

       return chan;
}

static void rzv2m_csi_release_dma(struct rzv2m_csi_priv *p)
{
       struct spi_master *master = p->master;

       if (!master->dma_tx)
               return;

       dma_unmap_single(master->dma_rx->device->dev, p->rx_dma_addr,
                        PAGE_SIZE, DMA_FROM_DEVICE);
       dma_unmap_single(master->dma_tx->device->dev, p->tx_dma_addr,
                        PAGE_SIZE, DMA_TO_DEVICE);
       free_page((unsigned long)p->rx_dma_page);
       free_page((unsigned long)p->tx_dma_page);
       dma_release_channel(master->dma_rx);
       dma_release_channel(master->dma_tx);
}
static int rzv2m_csi_request_dma(struct rzv2m_csi_priv *p){
       struct platform_device *pdev = p->pdev;
       struct device *dev = &pdev->dev;
       //const struct rzv2m_csi_info *info = dev_get_platdata(dev);
       unsigned int dma_tx_id, dma_rx_id;
       const struct resource *res;
       struct spi_master *master;
       struct device *tx_dev, *rx_dev;

#if 0
       if (dev->of_node) {
               /* In the OF case we will get the slave IDs from the DT */
               dma_tx_id = 0;
               dma_rx_id = 0;
       } else if (info && info->dma_tx_id && info->dma_rx_id) {
               dma_tx_id = info->dma_tx_id;
               dma_rx_id = info->dma_rx_id;
       } else {
               /* The driver assumes no error */
               return 0;
       }
#else
       dma_tx_id = 0;
       dma_rx_id = 0;
#endif
       /* The DMA engine uses the second register set, if present */
       res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
       if (!res)
               res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
       master = p->master;
       master->dma_tx = rzv2m_csi_request_dma_chan(dev, DMA_MEM_TO_DEV,
                                                  dma_tx_id,
                                                  res->start + CSI_OFIFO);
       if (!master->dma_tx)
               return -ENODEV;

       master->dma_rx = rzv2m_csi_request_dma_chan(dev, DMA_DEV_TO_MEM,
                                                  dma_rx_id,
                                                  res->start + CSI_IFIFO);
       if (!master->dma_rx)
               goto free_tx_chan;

       p->tx_dma_page = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
       if (!p->tx_dma_page)
               goto free_rx_chan;

       p->rx_dma_page = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
       if (!p->rx_dma_page)
               goto free_tx_page;

       tx_dev = master->dma_tx->device->dev;
       p->tx_dma_addr = dma_map_single(tx_dev, p->tx_dma_page, PAGE_SIZE,
                                       DMA_TO_DEVICE);
       if (dma_mapping_error(tx_dev, p->tx_dma_addr))
               goto free_rx_page;

       rx_dev = master->dma_rx->device->dev;
       p->rx_dma_addr = dma_map_single(rx_dev, p->rx_dma_page, PAGE_SIZE,
                                       DMA_FROM_DEVICE);
       if (dma_mapping_error(rx_dev, p->rx_dma_addr))
               goto unmap_tx_page;

       dev_info(dev, "DMA available");
       return 0;

unmap_tx_page:
       dma_unmap_single(tx_dev, p->tx_dma_addr, PAGE_SIZE, DMA_TO_DEVICE);
free_rx_page:
       free_page((unsigned long)p->rx_dma_page);
free_tx_page:
       free_page((unsigned long)p->tx_dma_page);
free_rx_chan:
       dma_release_channel(master->dma_rx);
free_tx_chan:
       dma_release_channel(master->dma_tx);
       master->dma_tx = NULL;
       return -ENODEV;
}
#endif

static int rzv2m_csi_slave_abort(struct spi_master *master)
{
       struct rzv2m_csi_priv *p = spi_master_get_devdata(master);

       p->slave_aborted = true;
       //complete(&p->done);

#if 0 //DMA hasn't support yet
       complete(&p->done_txdma);
#endif
       return 0;
}

static struct rzv2m_csi_info* rzv2m_csi_parse_dt(struct device *dev){
       struct rzv2m_csi_info *info;
       struct device_node *np = dev->of_node;

       info = devm_kzalloc(dev, sizeof(struct rzv2m_csi_info), GFP_KERNEL);
       if (!info)
               return NULL;

       info->mode = of_property_read_bool(np, "spi-slave") ? CSI_SPI_SLAVE : CSI_SPI_MASTER;

       if(info->mode == CSI_SPI_SLAVE){
               salve_count++;
       }

       of_property_read_u32(np, "interval_time", &info->interval_time);
       of_property_read_u32(np, "tx_trigger_lvl", &info->transmit_trigger_level);
       of_property_read_u32(np, "rx_trigger_lvl", &info->receive_trigger_level);

       return info;
}

static int rzv2m_csi_probe(struct platform_device *pdev){
       int ret = 0;
       int irq = 0;
       struct spi_master *master;
       struct resource *res;
       struct device *dev = &pdev->dev;
       struct rzv2m_csi_priv *p;
       struct rzv2m_csi_info *info;

       info = rzv2m_csi_parse_dt(&pdev->dev);
       if(!info){
               dev_err(&pdev->dev, "failed to obtain device info\n");
               return -ENXIO;
       }

       spin_lock_init(&lock_irq);

       if (info->mode == CSI_SPI_SLAVE) {
               master = spi_alloc_slave(&pdev->dev, sizeof(struct rzv2m_csi_priv));
       } else {
       master = spi_alloc_master(&pdev->dev, sizeof(struct rzv2m_csi_priv));
       }

       if (!master){
               dev_err(&pdev->dev, "failed to alloc master\n");
               return -ENOMEM;

       }
       p = spi_master_get_devdata(master);

       platform_set_drvdata(pdev, p);
       p->master = master;
       p->info = info;
       p->pdev = pdev;

       res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
       p->regs = devm_ioremap_resource(dev, res);
       if (IS_ERR(p->regs)) {
               ret = PTR_ERR(p->regs);
               goto clk_dis_all;
       }

       p->clk = devm_clk_get(&pdev->dev, NULL);
       if (IS_ERR(p->clk)) {
               dev_err(&pdev->dev, "cannot get csi_clk\n");
               ret = PTR_ERR(p->clk);
               goto clk_dis_clk;
       }

       ret = clk_prepare_enable(p->clk);
       if (ret) {
               dev_err(dev, "Unable to enable device clock.\n");
               goto clk_dis_clk;
       }

       irq = platform_get_irq(pdev, 0);
       if (irq < 0) {
               dev_err(&pdev->dev, "cannot get IRQ\n");
               ret = irq;
               goto clk_dis_all;
       }
       ret = devm_request_irq(&pdev->dev, irq, rzv2m_csi_irq_handler, 0,
                              dev_name(&pdev->dev), p);

       if(ret){
        dev_err(&pdev->dev, "cannot request IRQ\n");
        goto clk_dis_all;
       }

       /* init master code */
       master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH; //initial setting
       master->mode_bits |= SPI_LSB_FIRST | SPI_3WIRE;
       master->dev.of_node = pdev->dev.of_node;
       master->setup = rzv2m_csi_spi_setup;
       master->slave_abort = rzv2m_csi_slave_abort;
       master->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 16);
       master->transfer_one = rzv2m_csi_start_transfer;

#if 0 //DMA hasn't support yet
       ret = rzv2m_csi_request_dma(p);
       if (ret < 0)
               dev_warn(&pdev->dev, "DMA not available\n");
#endif

       ret = devm_spi_register_master(&pdev->dev, master);
       if (ret < 0) {
               dev_err(&pdev->dev, "spi_register_master error.\n");
               goto clk_dis_all;
       }

       dev_info(&pdev->dev, "probed\n");

       return 0;

clk_dis_all:
#if 0 //DMA hasn't support yet
       rzv2m_csi_release_dma(p);
#endif
       pm_runtime_set_suspended(&pdev->dev);
       pm_runtime_disable(&pdev->dev);
clk_dis_clk:
       clk_disable_unprepare(p->clk);
       spi_master_put(master);
       return ret;
}

static int rzv2m_csi_remove(struct platform_device *pdev)
{

#if 0 //DMA hasn't support yet
       rzv2m_csi_release_dma(p);
#endif
       pm_runtime_disable(&pdev->dev);
       return 0;
}

static const struct of_device_id rzv2m_csi_match[] = {
       { .compatible = "renesas,rzv2m-csi", },
       { /* End of table */ }
};
MODULE_DEVICE_TABLE(of, rzv2m_csi_match);

static struct platform_driver rzv2m_csi_drv = {
       .probe          = rzv2m_csi_probe,
       .remove         = rzv2m_csi_remove,
       .driver         = {
               .name           = "rzv2m_csi",
               .of_match_table = of_match_ptr(rzv2m_csi_match),
       },
};

module_platform_driver(rzv2m_csi_drv);

MODULE_DESCRIPTION("Clock Serial Interface Driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL v2");

#ifndef __CSI_H__
#define __CSI_H__


#define AMBA_CSI_CHANNEL 6

#define ON             1
#define OFF    0

#define CH_0   0
#define CH_1   1
#define CH_2   2
#define CH_3   3
#define CH_4   4
#define CH_5   5

#define CPG_CLK_ON15   0x438
#define CPG_RST6               0x614
#define CPG_RST_MON            0x680

#define PCLKSEL        0x0
#define CSICLKSEL      0x1

#define FIFOSize       32

#define Tx_FIFO_KIND 0
#define Rx_FIFO_KIND 1

#define FIFO_EMPTY  0
#define FIFO_FULL      1

#define        D_LENGTH_8BIT   0
#define        D_LENGTH_16BIT  1

#define        D_FORMAT_LSB    0
#define        D_FORMAT_MSB    1

#define MAX_FIFO_16BIT 16
#define MAX_FIFO_8BIT  32

#define OFIFO_MASK 0x0000FFFF
#define IFIFO_MASK 0x0000FFFF

#define TRY_TIMES 10

enum eFIFOTriggerLevel{
    emTriggerLevel       = 0
    ,emNoneTriggerLevel  = 1
    ,emUnderTriggerLevel = 2
    ,emUpperTriggerLevel = 3
    ,emTriggerRequestDMALevel = 4
};

enum interrupt_type{
    R_TRGR_IRQ,
    T_TRGR_IRQ,
    CSIEND_IRQ,
    TREND_IRQ,
    OVERF_IRQ,
    UNDER_IRQ
};

enum eTransMode{
    emReceiveOnly       = 0
    ,emTransmitReceive  = 1
};


#endif /*__CSI_H__*/

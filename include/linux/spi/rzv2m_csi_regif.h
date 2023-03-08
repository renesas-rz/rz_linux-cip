//TBD

//
// %MODULE csi
//     #              name     offset_size
//     %%REG_INSTANCE reg_def  6
//
// %REG_CHANNEL reg_def
//     %%TITLE    name         reg_name       size       length  offset    access   init       support  callback
//     %%REG      CSI_MODE     CSI_MODE       32         32       0x00      R|W     0          TRUE     -
//     %%REG      CSI_CLKSEL   CSI_CLKSEL     32         32       0x04      R|W     0x0000FFFE TRUE     -
//     %%REG      CSI_CNT      CSI_CNT        32         32       0x08      R|W     0x10200000 TRUE     -
//     %%REG      CSI_INT      CSI_INT        32         32       0x0C      R|W     0          TRUE     -
//     %%REG      CSI_IFIFOL   CSI_IFIFOL     32         32       0x10      R|W     0          TRUE     -
//     %%REG      CSI_OFIFOL   CSI_OFIFOL     32         32       0x14      R|W     0          TRUE     -
//     %%REG      CSI_IFIFO    CSI_IFIFO      32         32       0x18      R       0          TRUE     -
//     %%REG      CSI_OFIFO    CSI_OFIFO      32         32       0x1C      W       0          TRUE     -
//     %%REG      CSI_FIFOTRG  CSI_FIFOTRG    32         32       0x20      R|W     0          TRUE     -
//
// %REG_NAME CSI_MODE
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    DATWT          19     16     0     R|W     TRUE     -
//     %%BIT    CSIE           7      7      0     R|W     TRUE     W
//     %%BIT    TRMD           6      6      0     R|W     TRUE     -
//     %%BIT    CCL            5      5      0     R|W     TRUE     -
//     %%BIT    DIR            4      4      0     R|W     TRUE     -
//     %%BIT    CSOT           0      0      0     R       TRUE     -
//
// %REG_NAME CSI_CLKSEL
//     %%TITLE  name           upper  lower  init      access  support  callback    value
//     %%BIT    SS_ENA         19     19     0         R|W     TRUE     -              -
//     %%BIT    SS_POL         18     18     0         R|W     TRUE     -              -
//     %%BIT    CKP            17     17     0         R|W     TRUE     -              -
//     %%BIT    DAP            16     16     0         R|W     TRUE     -              -
//     %%BIT    SLAVE          15     15     1         R|W     TRUE     -              -
//     %%BIT    CKS            14     1      16383     R|W     TRUE     W          "0x1, 0x2, 0x3, 0x4, 0x8, 0x10, 0x100, 0x1000, 0x3FFF"
//
// %REG_NAME CSI_CNT
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    CSIRST         28     28     1     R|W     TRUE     W
//     %%BIT    T_TRGEN        27     27     0     R|W     TRUE     -
//     %%BIT    T_FIFOF        26     26     0     R       TRUE     -
//     %%BIT    T_DMAEN        24     24     0     R|W     TRUE     -
//     %%BIT    SS_MON         21     21     1     R       TRUE     -
//     %%BIT    R_TRGEN        19     19     0     R|W     TRUE     -
//     %%BIT    R_FIFOF        18     18     0     R       TRUE     -
//     %%BIT    R_DMAEN        16     16     0     R|W     TRUE     -
//     %%BIT    UNDER_E        13     13     0     R|W     TRUE     -
//     %%BIT    OVERF_E        12     12     0     R|W     TRUE     -
//     %%BIT    TREND_E        9      9      0     R|W     TRUE     -
//     %%BIT    CSIEND_E       8      8      0     R|W     TRUE     -
//     %%BIT    T_TRGR_E       4      4      0     R|W     TRUE     -
//     %%BIT    R_TRGR_E       0      0      0     R|W     TRUE     -
//
// %REG_NAME CSI_INT
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    UNDER          13     13     0     R|W     TRUE     W
//     %%BIT    OVERF          12     12     0     R|W     TRUE     -
//     %%BIT    TREND          9      9      0     R|W     TRUE     -
//     %%BIT    CSIEND         8      8      0     R|W     TRUE     -
//     %%BIT    T_TRGR         4      4      0     R|W     TRUE     -
//     %%BIT    R_TRGR         0      0      0     R|W     TRUE     -
//
// %REG_NAME CSI_IFIFOL
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    RFL            5      0      0     R|W     TRUE     W
//
// %REG_NAME CSI_OFIFOL
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    TFL            5      0      0     R|W     TRUE     W
//
// %REG_NAME CSI_IFIFO
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    IFIFO          15     0      0     R       TRUE     R
//
// %REG_NAME CSI_OFIFO
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    OFIFO          15     0      0     W       TRUE     W
//
// %REG_NAME CSI_FIFOTRG
//     %%TITLE  name           upper  lower  init  access  support  callback
//     %%BIT    T_TRG          10     8      0     R|W     TRUE     W
//     %%BIT    R_TRG          2      0      0     R|W     TRUE     -
////////////////////////////////////////////////////////////////////////////////

#ifndef __CSI_REGIF_H__
#define __CSI_REGIF_H__
#include <linux/bitops.h>

/* CSI register */
#define CSI_MODE                        0x00    /* CSI mode control                  */
#define CSI_CLKSEL                      0x04    /* CSI clock select                  */
#define CSI_CNT                         0x08    /* CSI control                       */
#define CSI_INT                         0x0C    /* CSI interrupt status              */
#define CSI_IFIFOL                      0x10    /* CSI receive FIFO level display    */
#define CSI_OFIFOL                      0x14    /* CSI transmit FIFO level display   */
#define CSI_IFIFO                       0x18    /* CSI receive window                */
#define CSI_OFIFO                       0x1C    /* CSI transmit window               */
#define CSI_FIFOTRG                     0x20    /* CSI FIFO trigger level            */

/* Bit definition*/
/* CSI_MODE */
#define CSI_MODE_DATWT          0xF0000 //BITS(16, 19)
#define CSI_MODE_CSIE           BIT(7)
#define CSI_MODE_TRMD           BIT(6)
#define CSI_MODE_CCL            BIT(5)
#define CSI_MODE_DIR            BIT(4)
#define CSI_MODE_CSOT           BIT(0)

/* CSI_CLKSEL */
#define CSI_CLKSEL_SS_ENA       BIT(19)
#define CSI_CLKSEL_SS_POL       BIT(18)
#define CSI_CLKSEL_CKP          BIT(17)
#define CSI_CLKSEL_DAP          BIT(16)
#define CSI_CLKSEL_SLAVE        BIT(15)
#define CSI_CLKSEL_CKS          0x7FFE //BITS(1,14)


/* CSI_CNT */
#define CSI_CNT_CSIRST          BIT(28)
#define CSI_CNT_T_TRGEN         BIT(27)
#define CSI_CNT_T_FIFOF         BIT(26)
#define CSI_CNT_T_DMAEN         BIT(24)
#define CSI_CNT_SS_MON          BIT(21)
#define CSI_CNT_R_TRGEN         BIT(19)
#define CSI_CNT_R_FIFOF         BIT(18)
#define CSI_CNT_R_DMAEN         BIT(16)
#define CSI_CNT_UNDER_E         BIT(13)
#define CSI_CNT_OVERF_E         BIT(12)
#define CSI_CNT_TREND_E         BIT(9)
#define CSI_CNT_CSIEND_E        BIT(8)
#define CSI_CNT_T_TRGR_E        BIT(4)
#define CSI_CNT_R_TRGR_E        BIT(0)

/* CSI_INT */
#define CSI_INT_UNDER           BIT(13)
#define CSI_INT_OVERF       BIT(12)
#define CSI_INT_TREND       BIT(9)
#define CSI_INT_CSIEND      BIT(8)
#define CSI_INT_T_TRGR      BIT(4)
#define CSI_INT_R_TRGR      BIT(0)

/* CSI_IFIFOL */
#define CSI_IFIFOL_RFL          0x3F //BITS(0,5)

/* CSI_OFIFOL */
#define CSI_OFIFOL_TFL          0x3F //BITS(0,5)

/* CSI_IFIFO */
#define CSI_IFIFO_IFIFO         0xffff //BITS(0,15)

/* CSI_OFIFO */
#define CSI_OFIFO_OFIFO         0xffff //BITS(0,15)

/* CSI_FIFO */
#define CSI_FIFO_16BIT                  0xffff //BITS(0,15)
#define CSI_FIFO_8BIT_MSB               0xff //BITS(0,15)
#define CSI_FIFO_8BIT_LSB               0xff00 //BITS(0,15)


/* CSI_FIFOTRG */
#define CSI_FIFOTRG_T_TRG       0x700 //BITS(8,10)
#define CSI_FIFOTRG_R_TRG       0x7 //BITS(0,2)

#endif /* __CSI_REGIF_H__ */

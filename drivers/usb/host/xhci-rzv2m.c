// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI host controller driver for RZV2M
 *
 * Copyright (C) 2019 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include "xhci.h"

/**** USB IP base address ***/
#define RZV2M_USBTEST_BASE     0xA3F90000
#define RZV2M_USBPERI_BASE     0x85070000
#define RZV2M_USBHOST_BASE     0x85060000

/*** Register Offset ***/
#define RZV2M_USB3_INTEN       0x1044  /* Interrupt Enable */

/*** Register Settings ***/
/* Interrupt Enable */
#define RZV2M_USB3_INT_XHC_ENA 0x00000001
#define RZV2M_USB3_INT_HSE_ENA 0x00000004
#define RZV2M_USB3_INT_ENA_VAL (RZV2M_USB3_INT_XHC_ENA \
                                | RZV2M_USB3_INT_HSE_ENA)

/*** USB TEST Register Offset ***/
#define RZV2M_USBTEST_HC_OFFSET        (RZV2M_USBTEST_BASE-RZV2M_USBHOST_BASE) /* USBTEST Offset */
#define RZV2M_USBTEST_RST1     0x000   /* Reset Register 1 */
#define RZV2M_USBTEST_RST2     0x004   /* Reset Register 2 */
#define RZV2M_USBTEST_CLKRST   0x308   /* Clock Reset Register */

/*** USB TEST Register Settings ***/
#define RZV2M_USBTEST_SSC_EN           0x00001000

#define RZV2M_USBTEST_PIPE0_RST        0x00000001
#define RZV2M_USBTEST_PORT_RST0        0x00000002
#define RZV2M_USBTEST_PHY_RST  	       0x00000004
#define RZV2M_USBTEST_PIPE0_ODEN       0x00000010
#define RZV2M_USBTEST_PORT_ODEN        0x00000020

#define RZV2M_USBTEST_REF_SSP_EN       0x00000001

/*** DRD Register Offset ***/
#define RZV2M_DRD_BASE_OFFSET  (RZV2M_USBPERI_BASE - RZV2M_USBHOST_BASE)
#define RZV2M_DRD_HC_OFFSET    (0x400+RZV2M_DRD_BASE_OFFSET)    /* DRD Offset */
#define RZV2M_DRD_DRDCON               (0x000)   /* DRDCON */

/*** USB TEST Register Settings ***/
#define RZV2M_DRD_PERI_RST     0x80000000 /* Pericon Rst */
#define RZV2M_DRD_HOST_RST     0x40000000 /* Hostcon Rst */
#define RZV2M_DRD_PERICON      0x01000000 /* PeriHost select*/

int xhci_rzv2m_drd_init(struct usb_hcd *hcd)
{
       /* DRD INIT */
       u32 temp;

       if (hcd->regs != NULL) {
               /* perecon hostcon reset */
               temp = readl(hcd->regs + RZV2M_DRD_HC_OFFSET +
                               RZV2M_DRD_DRDCON);
               temp |= (RZV2M_DRD_PERI_RST | RZV2M_DRD_HOST_RST);
               writel(temp, hcd->regs + RZV2M_DRD_HC_OFFSET +
                               RZV2M_DRD_DRDCON);

               /* Host role setting */
               temp = readl(hcd->regs + RZV2M_DRD_HC_OFFSET +
                               RZV2M_DRD_DRDCON);
               temp &= ~(RZV2M_DRD_PERICON | RZV2M_DRD_HOST_RST);
               writel(temp, hcd->regs + RZV2M_DRD_HC_OFFSET +
                               RZV2M_DRD_DRDCON);
       }
       return 0;
}

void xhci_rzv2m_start(struct usb_hcd *hcd)
{
       u32 temp;

       if (hcd->regs != NULL) {
               /* Interrupt Enable */
               temp = readl(hcd->regs + RZV2M_USB3_INTEN);
               temp |= RZV2M_USB3_INT_ENA_VAL;
               writel(temp, hcd->regs + RZV2M_USB3_INTEN);
       }
}


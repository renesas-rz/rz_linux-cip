// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/usb/host/xhci-rzv2m.h
 *
 * Copyright (C) 2019 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _XHCI_RZV2M_H
#define _XHCI_RZV2M_H

#if IS_ENABLED(CONFIG_USB_XHCI_RZV2M)
void xhci_rzv2m_start(struct usb_hcd *hcd);
void xhci_rzv2m_usbtest_init(struct usb_hcd *hcd);
int xhci_rzv2m_drd_init(struct usb_hcd *hcd);
#else
static inline void xhci_rzv2m_start(struct usb_hcd *hcd)
{
}
static inline void xhci_rzv2m_usbtest_init(struct usb_hcd *hcd)
{
}
static inline void xhci_rzv2m_drd_init(struct usb_hcd *hcd)
{
}
#endif
#endif /* _XHCI_RZV2M_H */

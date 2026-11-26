/**
 * @file usb_msc.h
 * @brief USB mass-storage disk proxy registration.
 */
#ifndef ZEPHYRFORGE_TRANSPORT_USB_MSC_H
#define ZEPHYRFORGE_TRANSPORT_USB_MSC_H

/** Register the USB disk proxy with disk access before the mass-storage class starts. */
int zf_usb_msc_register(void);

#endif

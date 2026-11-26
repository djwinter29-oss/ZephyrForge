/**
 * @file usb_msc.h
 * @brief USB mass-storage disk proxy hooks called by the storage service during card handoff.
 */
#ifndef ZEPHYRFORGE_TRANSPORT_USB_MSC_H
#define ZEPHYRFORGE_TRANSPORT_USB_MSC_H

/** Register the USB disk proxy with disk access; returns zero or a negative errno. */
int zf_usb_msc_register(void);
/** Restart USB after an ownership handoff so the host can remount its LUN; returns zero or a negative errno. */
int zf_usb_msc_reconnect(void);

#endif

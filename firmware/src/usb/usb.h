/**
 * @file usb.h
 * @brief USB HID management transport.
 */
#ifndef ZEPHYRFORGE_USB_H
#define ZEPHYRFORGE_USB_H

#include <stdbool.h>

/** Register the USB HID composite device. Returns 0 or a negative errno. */
int zf_usb_init(void);
/** True while the host has configured the USB device. */
bool zf_usb_configured(void);
/** Publish due Input reports; call from the main loop. */
void zf_usb_poll(void);

#endif

/**
 * @file usb_device.h
 * @brief USB HID management transport.
 */
#ifndef ZEPHYRFORGE_USB_DEVICE_H
#define ZEPHYRFORGE_USB_DEVICE_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "app/build_config.h"

#if ZF_ENABLE_USB_HID
/** Register the USB HID composite device. Returns 0 or a negative errno. */
int zf_usb_device_init(void);
/** True while the host has configured the USB device. */
bool zf_usb_device_configured(void);
/** Publish due Input reports; call from the main loop. */
void zf_usb_device_poll(void);
/** Return the next Input publication deadline, or INT64_MAX when USB is idle. */
int64_t zf_usb_device_next_deadline(void);
/** Restart the USB device after a storage ownership handoff so the host can remount its LUN. */
int zf_usb_device_reconnect(void);
#else
static inline int zf_usb_device_init(void)
{
	return 0;
}

static inline bool zf_usb_device_configured(void)
{
	return false;
}

static inline void zf_usb_device_poll(void)
{
}

static inline int64_t zf_usb_device_next_deadline(void)
{
	return INT64_MAX;
}

static inline int zf_usb_device_reconnect(void)
{
	return -ENOTSUP;
}
#endif

#endif

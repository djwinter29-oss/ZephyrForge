/**
 * @file usb_msc.h
 * @brief USB mass-storage disk proxy and re-enumeration on SD ownership changes.
 */
#ifndef ZEPHYRFORGE_TRANSPORT_USB_MSC_H
#define ZEPHYRFORGE_TRANSPORT_USB_MSC_H

#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZF_USB_MSC)
/**
 * @brief Register the USB disk proxy before the mass-storage class starts.
 * @return 0 when USB ownership can be granted, otherwise a negative errno; the caller then omits the class.
 */
int zf_usb_msc_init(void);
/** Queue USB re-enumeration after storage settles on a new owner; call from the main loop. */
void zf_usb_msc_poll(void);
#else
static inline int zf_usb_msc_init(void)
{
	return 0;
}

static inline void zf_usb_msc_poll(void)
{
}
#endif

#endif

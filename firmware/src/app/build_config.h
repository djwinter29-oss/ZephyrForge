/**
 * @file build_config.h
 * @brief Build-selected module switches shared by firmware and portable tests.
 */
#ifndef ZEPHYRFORGE_APP_BUILD_CONFIG_H
#define ZEPHYRFORGE_APP_BUILD_CONFIG_H

#include "protocol/wireless_types.h"

#ifndef ZF_ENABLE_LED
#define ZF_ENABLE_LED 1
#endif
#ifndef ZF_ENABLE_WIFI
#define ZF_ENABLE_WIFI 1
#endif
#ifndef ZF_ENABLE_BLUETOOTH
#define ZF_ENABLE_BLUETOOTH 1
#endif
#ifndef ZF_ENABLE_CPU_LOAD
#define ZF_ENABLE_CPU_LOAD 1
#endif
#ifndef ZF_ENABLE_STORAGE
#define ZF_ENABLE_STORAGE 1
#endif
#ifndef ZF_ENABLE_USB_HID
#define ZF_ENABLE_USB_HID 1
#endif
#ifndef ZF_ENABLE_USB_MSC
#define ZF_ENABLE_USB_MSC (ZF_ENABLE_STORAGE && ZF_ENABLE_USB_HID)
#endif
#ifndef ZF_ENABLE_STORAGE_BENCHMARK
#define ZF_ENABLE_STORAGE_BENCHMARK (ZF_ENABLE_STORAGE && ZF_ENABLE_USB_HID)
#endif
#ifndef ZF_ENABLE_SETTINGS_SAVE
#define ZF_ENABLE_SETTINGS_SAVE 1
#endif
#ifndef ZF_STORAGE_SPI
#define ZF_STORAGE_SPI 0
#endif
#ifndef ZF_RADIO_MASK
#define ZF_RADIO_MASK ((ZF_ENABLE_WIFI ? ZF_WIRELESS_WIFI : 0u) | \
		       (ZF_ENABLE_BLUETOOTH ? ZF_WIRELESS_BLE : 0u))
#endif

#endif

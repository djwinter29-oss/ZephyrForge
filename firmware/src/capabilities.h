/**
 * @file capabilities.h
 * @brief Board and module capabilities advertised by the selected firmware image.
 */
#ifndef ZEPHYRFORGE_CAPABILITIES_H
#define ZEPHYRFORGE_CAPABILITIES_H

#include "module_config.h"
#include "report/report.h"
#include "wireless/wireless_types.h"

#ifndef ZF_MCU
#define ZF_MCU ZF_MCU_ESP32S3
#endif
#ifndef ZF_BOARD_MODEL
#define ZF_BOARD_MODEL ZF_BOARD_DEVKITC
#endif
#ifndef ZF_RADIO_MASK
#define ZF_RADIO_MASK (ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE)
#endif
#ifndef ZF_BOARD_CAPS
#define ZF_BOARD_CAPS (ZF_CAP_LED | ZF_CAP_SETTINGS | ZF_CAP_WIRELESS)
#endif

#define ZF_BOARD_CAPS_ESP32S3 (ZF_CAP_LED | ZF_CAP_SETTINGS | ZF_CAP_WIRELESS)
#define ZF_BOARD_CAPS_ESP32C6 (ZF_CAP_SETTINGS | ZF_CAP_WIRELESS)
#define ZF_CAP_STORAGE (ZF_ENABLE_STORAGE ? (ZF_STORAGE_SPI ? ZF_CAP_SPI : ZF_CAP_SDMMC) : 0u)
#define ZF_CAPABILITIES \
	(ZF_BOARD_CAPS | ZF_CAP_REBOOT | ZF_CAP_STORAGE | \
	 (ZF_ENABLE_STORAGE && ZF_ENABLE_STORAGE_BENCHMARK ? ZF_CAP_BENCHMARK : 0))

#endif

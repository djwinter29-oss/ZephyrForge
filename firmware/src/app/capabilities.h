/**
 * @file capabilities.h
 * @brief Board and module capabilities advertised by the selected firmware image.
 */
#ifndef ZEPHYRFORGE_CAPABILITIES_H
#define ZEPHYRFORGE_CAPABILITIES_H

#include "app/build_config.h"
#include "protocol/report_codec.h"
#include "protocol/wireless_types.h"

#ifndef ZF_MCU
#define ZF_MCU ZF_MCU_ESP32S3
#endif
#ifndef ZF_BOARD_MODEL
#define ZF_BOARD_MODEL ZF_BOARD_DEVKITC
#endif
#define ZF_CAP_STORAGE (ZF_ENABLE_STORAGE ? (ZF_STORAGE_SPI ? ZF_CAP_SPI : ZF_CAP_SDMMC) : 0u)
#define ZF_SETTINGS_CAP (ZF_ENABLE_SETTINGS_SAVE ? ZF_CAP_SETTINGS : 0u)
#define ZF_BOARD_CAPS (ZF_SETTINGS_CAP | (ZF_ENABLE_LED ? ZF_CAP_LED : 0u) | \
		       (ZF_RADIO_MASK != 0 ? ZF_CAP_WIRELESS : 0u))
#define ZF_CAPABILITIES (ZF_BOARD_CAPS | ZF_CAP_REBOOT | ZF_CAP_STORAGE | \
			 (ZF_ENABLE_STORAGE_BENCHMARK ? ZF_CAP_BENCHMARK : 0u))

#endif

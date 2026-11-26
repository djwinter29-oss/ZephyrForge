/**
 * @file capabilities.h
 * @brief Board and module capabilities advertised by the selected firmware image.
 */
#ifndef ZEPHYRFORGE_CAPABILITIES_H
#define ZEPHYRFORGE_CAPABILITIES_H

#include <zephyr/sys/util.h>
#include "protocol/report_codec.h"

#ifndef ZF_MCU
#define ZF_MCU ZF_MCU_ESP32S3
#endif
#ifndef ZF_BOARD_MODEL
#define ZF_BOARD_MODEL ZF_BOARD_DEVKITC
#endif
/** ZF_WIRELESS_* radios built into this image; tests may override it. */
#ifndef ZF_RADIO_MASK
#define ZF_RADIO_MASK ((IS_ENABLED(CONFIG_ZF_WIFI) ? ZF_WIRELESS_WIFI : 0u) | \
		       (IS_ENABLED(CONFIG_ZF_BLUETOOTH) ? ZF_WIRELESS_BLE : 0u))
#endif
#define ZF_CAP_STORAGE (IS_ENABLED(CONFIG_ZF_STORAGE) ? (IS_ENABLED(CONFIG_SPI_SDHC) ? ZF_CAP_SPI : ZF_CAP_SDMMC) : 0u)
#define ZF_SETTINGS_CAP (IS_ENABLED(CONFIG_ZF_SETTINGS_SAVE) ? ZF_CAP_SETTINGS : 0u)
#define ZF_BOARD_CAPS (ZF_SETTINGS_CAP | (IS_ENABLED(CONFIG_ZF_LED) ? ZF_CAP_LED : 0u) | \
		       (ZF_RADIO_MASK != 0 ? ZF_CAP_WIRELESS : 0u))
#define ZF_CAPABILITIES (ZF_BOARD_CAPS | ZF_CAP_REBOOT | ZF_CAP_STORAGE | \
			 (IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK) ? ZF_CAP_BENCHMARK : 0u))

#endif

/**
 * @file management_protocol.h
 * @brief Wire-visible management commands, states and result values.
 */
#ifndef ZEPHYRFORGE_MANAGEMENT_PROTOCOL_H
#define ZEPHYRFORGE_MANAGEMENT_PROTOCOL_H

#include <stdint.h>

/** Feature report IDs on the ZephyrForge HID collection. */
#define ZF_REPORT_IDENTITY 1u /**< Report 1: identity and capabilities. */
#define ZF_REPORT_CONFIG 2u /**< Report 2: saved configuration and pin allocation. */
#define ZF_REPORT_TELEMETRY 3u /**< Report 3: telemetry snapshot. */
#define ZF_REPORT_STORAGE 10u /**< Report 10: storage snapshot. */
#define ZF_REPORT_BENCHMARK 11u /**< Report 11: benchmark progress. */
#define ZF_REPORT_WIRELESS 12u /**< Report 12: Wi-Fi, BLE and staging state. */
#define ZF_REPORT_MEMORY 13u /**< Read-only boot memory inventory. */
#define ZF_REPORT_CONTROL 20u /**< Feature 20: control request/result envelope. */

/** Wi-Fi radio switch bit in stored and commanded radio masks. */
#define ZF_WIRELESS_WIFI 1u
/** BLE radio switch bit in stored and commanded radio masks. */
#define ZF_WIRELESS_BLE 2u
/** Maximum Wi-Fi SSID length in bytes (IEEE 802.11 limit and wire field width). */
#define ZF_SSID_MAX_BYTES 32u

/** Payload sizes excluding the report ID, matching the encoded wire layout. */
#define ZF_IDENTITY_SIZE 26u /**< Report 1 payload. */
#define ZF_CONFIG_SIZE 17u /**< Report 2 payload. */
#define ZF_TELEMETRY_SIZE 22u /**< Report 3 payload. */
#define ZF_STORAGE_SIZE 34u /**< Report 10 payload. */
#define ZF_BENCHMARK_SIZE 32u /**< Report 11 Input payload. */
#define ZF_WIRELESS_SIZE 48u /**< Report 12 payload. */
#define ZF_MEMORY_SIZE 16u /**< Report 13 payload. */
#define ZF_CONTROL_SIZE 68u /**< Feature 20 control payload. */
#define ZF_BLE_CONTROL_SIZE 9u /**< BLE control characteristic request/result payload. */
#define ZF_SCAN_ENTRY_RESULT_SIZE 38u /**< Wi-Fi scan entry in a control result. */

/** Identity extension flag: Report 13 memory inventory is available. */
#define ZF_EXT_MEMORY 0x01u

/** Report 11 bus mode values. */
#define ZF_STORAGE_BUS_SPI 2u /**< SPI. */
#define ZF_STORAGE_BUS_SD4 3u /**< Native four-bit SD. */

/** Product ID advertised in Report 1. */
#define ZF_PRODUCT_ID 0x0003u
/** Report 1 MCU enum values. */
#define ZF_MCU_RP2040 1u /**< Raspberry Pi RP2040. */
#define ZF_MCU_RP2350 2u /**< Raspberry Pi RP2350. */
#define ZF_MCU_ESP32S3 3u /**< Espressif ESP32-S3. */
#define ZF_MCU_ESP32C6 4u /**< Espressif ESP32-C6. */
/** Report 1 board model enum values. */
#define ZF_BOARD_DEVKITC 1u /**< Espressif ESP32-S3-DevKitC-1 N16R8. */
#define ZF_BOARD_PICO 2u /**< Raspberry Pi Pico (RP2040). */
#define ZF_BOARD_PICO_W 3u /**< Raspberry Pi Pico W (RP2040). */
#define ZF_BOARD_PICO2 4u /**< Raspberry Pi Pico 2 (RP2350). */
#define ZF_BOARD_PICO2_W 5u /**< Raspberry Pi Pico 2 W (RP2350). */
#define ZF_BOARD_ESP32C6_DEVKITC 6u /**< Espressif ESP32-C6-DevKitC-1 N8. */
/** Report 1 capabilities bits. */
#define ZF_CAP_LED 0x01u /**< RGB LED control is available. */
#define ZF_CAP_REBOOT 0x02u /**< The reboot opcode is supported. */
#define ZF_CAP_SETTINGS 0x04u /**< Saved boot settings are supported. */
#define ZF_CAP_SPI 0x08u /**< SD memory-card transport over SPI. */
#define ZF_CAP_SDMMC 0x10u /**< Native SDMMC card transport. */
#define ZF_CAP_BENCHMARK 0x20u /**< Optional temporary-file benchmark capability. */
#define ZF_CAP_MSC 0x40u /**< USB mass-storage ownership control is available. */
#define ZF_CAP_WIRELESS 0x80u /**< Wireless management is supported. */

/** Board unique-identity length in bytes. */
#define ZF_UID_SIZE 8u
/** Report 3 load sentinel: no measurement window is available. */
#define ZF_LOAD_UNAVAILABLE 0xFFFFu
/** Report 2 pin sentinel: the function is not allocated on this board. */
#define ZF_GPIO_UNASSIGNED 0xFFu

/** Report 11 benchmark flags. */
#define ZF_BENCHMARK_FILE_REMAINS 1u /**< This run created a file that could not be removed. */
#define ZF_BENCHMARK_MOUNTED 2u /**< The firmware filesystem remains mounted. */

/** Feature 20 result status values. */
#define ZF_STATUS_IDLE 0u
#define ZF_STATUS_PENDING 1u
#define ZF_STATUS_COMPLETED 2u
#define ZF_STATUS_INVALID 3u
#define ZF_STATUS_UNSUPPORTED 4u
#define ZF_STATUS_BUSY 5u
#define ZF_STATUS_FAILED 6u

/** Feature 20 result error values. */
#define ZF_ERROR_NONE 0u
#define ZF_ERROR_MALFORMED 1u
#define ZF_ERROR_CONFIRMATION 2u
#define ZF_ERROR_ARGUMENT 3u
#define ZF_ERROR_CAPABILITY 4u
#define ZF_ERROR_BUSY 5u
#define ZF_ERROR_FAILED 6u
#define ZF_ERROR_OPCODE 7u

/** Feature 20 opcodes; 3, 4 and 6-12 are USB-only. */
#define ZF_OP_LED 1u
#define ZF_OP_REBOOT 2u
#define ZF_OP_MSC 3u
#define ZF_OP_BENCHMARK 4u
#define ZF_OP_SAVE 5u
#define ZF_OP_STAGE_SSID 6u
#define ZF_OP_STAGE_PASSWORD 7u
#define ZF_OP_STAGE_KEY 8u
#define ZF_OP_TEST_WIFI 9u
#define ZF_OP_CANCEL_TEST 10u
#define ZF_OP_SCAN 11u
#define ZF_OP_SCAN_ENTRY 12u

/**
 * Feature 20 detail values. Values may be reused by different opcodes; interpretation is opcode-specific.
 */
#define ZF_DETAIL_CONFLICT 1u /**< Storage worker busy or USB owns the medium. */
#define ZF_DETAIL_SSID_LENGTH 16u /**< Staged SSID exceeds ZF_SSID_MAX_BYTES. */
#define ZF_DETAIL_PASSWORD 17u /**< Staged password fails length or printable-byte rules. */
#define ZF_DETAIL_KEY_LENGTH 18u /**< Staged key length is neither 0 nor 32. */
#define ZF_DETAIL_PAIR_INCOMPLETE 19u /**< A new SSID lacks a staged password pair. */
#define ZF_DETAIL_EXPIRED 20u /**< The staging window expired before the action. */
#define ZF_DETAIL_NOTHING_STAGED 21u /**< No staged value exists to test or commit. */
#define ZF_DETAIL_SCAN_STALE 22u /**< Scan generation is stale or incomplete. */
#define ZF_DETAIL_SCAN_INDEX 23u /**< Scan index is out of range. */
#define ZF_DETAIL_TEST_ACTIVE 25u /**< A candidate Wi-Fi test is active. */
#define ZF_DETAIL_SCAN_ACTIVE 26u /**< A Wi-Fi scan is active. */
#define ZF_DETAIL_COMMIT_FLAG 27u /**< Staged values exist but the save commit flag is clear. */
#define ZF_DETAIL_USB_STAGED 28u /**< Unexpired USB staging blocks a BLE save or reboot. */
#define ZF_DETAIL_REBOOT_PENDING 29u /**< A scheduled reset blocks reboot, save and storage. */
#define ZF_DETAIL_SAVE_PENDING 30u /**< An asynchronous settings write is still in progress. */
#define ZF_DETAIL_SAVED_REBOOT_FAILED 31u /**< Settings were saved, but scheduling their reboot failed. */

/** Feature 12 BLE state values. */
#define ZF_BLE_OFF 0u /**< Bluetooth is not advertising. */
#define ZF_BLE_ADVERTISING 1u /**< Connectable advertising is running. */
#define ZF_BLE_CONNECTED 2u /**< A central holds the single connection. */

/** Feature 12 Wi-Fi scan_state values. */
#define ZF_SCAN_IDLE 0u /**< No scan has been requested since boot. */
#define ZF_SCAN_RUNNING 1u /**< A scan is in progress. */
#define ZF_SCAN_COMPLETE 2u /**< The last scan finished and its table is readable. */
#define ZF_SCAN_FAILED 3u /**< The last scan failed or timed out. */

/** Feature 12 Wi-Fi state values. */
#define ZF_WIFI_OFF 0u
#define ZF_WIFI_CONNECTING 1u
#define ZF_WIFI_CONNECTED 2u
#define ZF_WIFI_FAILED 3u
#define ZF_WIFI_TESTING 4u
#define ZF_WIFI_TEST_JOINED 5u
#define ZF_WIFI_TEST_FAILED 6u

/** Feature 12 Wi-Fi failure details. */
#define ZF_WIRELESS_DETAIL_NONE 0u
#define ZF_WIRELESS_DETAIL_RADIO 1u
#define ZF_WIRELESS_DETAIL_TIMEOUT 5u
#define ZF_WIRELESS_DETAIL_LINK_LOST 6u

/** Feature 12 scan security bits. */
#define ZF_WIFI_SECURITY_PRIVACY 0x01u
#define ZF_WIFI_SECURITY_WPA 0x02u
#define ZF_WIFI_SECURITY_RSN 0x04u

/** BLE ATT errors returned by the management characteristics. */
#define ZF_ATT_ERR_INVALID_OFFSET 0x07u
#define ZF_ATT_ERR_INVALID_LENGTH 0x0Du
#define ZF_ATT_ERR_UNLIKELY 0x0Eu
#define ZF_ATT_ERR_REJECTED 0x80u
#define ZF_ATT_ERR_ENVELOPE 0x81u
#define ZF_ATT_ERR_RATE_LIMITED 0x82u
#define ZF_ATT_ERR_UNAVAILABLE 0x83u

/** Benchmark lifecycle values encoded in Report 11. */
typedef enum {
	ZF_BENCHMARK_IDLE = 0, /**< No admitted run. */
	ZF_BENCHMARK_QUEUED = 1, /**< Storage reserved for execution. */
	ZF_BENCHMARK_PREPARING = 2, /**< Checking the mounted filesystem. */
	ZF_BENCHMARK_WRITING = 3, /**< Writing the generated pattern. */
	ZF_BENCHMARK_READING = 4, /**< Reading and verifying the pattern. */
	ZF_BENCHMARK_CLEANUP = 5, /**< Closing and removing this run's file. */
	ZF_BENCHMARK_PASSED = 6, /**< Verification and cleanup succeeded. */
	ZF_BENCHMARK_FAILED = 7, /**< Execution or cleanup failed. */
	ZF_BENCHMARK_CANCELLED = 8, /**< Cancellation and cleanup completed. */
} zf_benchmark_state_t;

/** Benchmark admission and execution details; zero means no error. */
typedef enum {
	ZF_BENCHMARK_ERROR_NONE = 0, /**< No error. */
	ZF_BENCHMARK_ERROR_BUSY = 1, /**< Another operation owns storage. */
	ZF_BENCHMARK_ERROR_USB = 2, /**< USB owns the card. */
	ZF_BENCHMARK_ERROR_SIZE = 3, /**< Size outside 1..1024 MiB. */
	ZF_BENCHMARK_ERROR_CARD = 4, /**< Medium absent or changed. */
	ZF_BENCHMARK_ERROR_SPACE = 5, /**< Insufficient filesystem space. */
	ZF_BENCHMARK_ERROR_EXISTS = 6, /**< Reserved file already exists. */
	ZF_BENCHMARK_ERROR_IO = 7, /**< Filesystem failure or short transfer. */
	ZF_BENCHMARK_ERROR_VERIFY = 8, /**< Pattern mismatch. */
	ZF_BENCHMARK_ERROR_CLEANUP = 9, /**< File close or removal failed. */
	ZF_BENCHMARK_ERROR_MOUNT = 10, /**< No firmware-owned filesystem mount. */
} zf_benchmark_error_t;

#endif

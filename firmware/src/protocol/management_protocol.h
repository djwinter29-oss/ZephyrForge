/**
 * @file management_protocol.h
 * @brief Wire-visible management commands, states and result values.
 */
#ifndef ZEPHYRFORGE_MANAGEMENT_PROTOCOL_H
#define ZEPHYRFORGE_MANAGEMENT_PROTOCOL_H

#include <stdint.h>

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

/**
 * @file report_codec.h
 * @brief Transport-independent encoders for the ZephyrForge HID/GATT report payloads.
 *
 * Payloads exclude the USB report ID. Portable: the Zephyr glue samples the board and fills these structs.
 */
#ifndef ZEPHYRFORGE_REPORT_CODEC_H
#define ZEPHYRFORGE_REPORT_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/management_protocol.h"

/** Feature report IDs on the ZephyrForge HID collection. */
#define ZF_REPORT_IDENTITY 1u /**< Report 1: identity and capabilities. */
#define ZF_REPORT_CONFIG 2u /**< Report 2: saved configuration and pin allocation. */
#define ZF_REPORT_TELEMETRY 3u /**< Report 3: telemetry snapshot. */
#define ZF_REPORT_STORAGE 10u /**< Report 10: storage snapshot. */
#define ZF_REPORT_BENCHMARK 11u /**< Report 11: benchmark progress. */
#define ZF_REPORT_WIRELESS 12u /**< Report 12: Wi-Fi, BLE and staging state. */
/** Read-only boot memory inventory Feature report. */
#define ZF_REPORT_MEMORY 13u
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
/** Benchmark Input payload size, excluding report ID. */
#define ZF_BENCHMARK_SIZE 32u
#define ZF_WIRELESS_SIZE 48u /**< Report 12 payload. */
/** Memory inventory payload size, excluding report ID. */
#define ZF_MEMORY_SIZE 16u
/** Feature 20 control request/result payload size, excluding the report ID. */
#define ZF_CONTROL_SIZE 68u
/** BLE control characteristic request/result size; only four result data bytes ride the long path. */
#define ZF_BLE_CONTROL_SIZE 9u
/** Wi-Fi scan-entry bytes embedded in a control result. */
#define ZF_SCAN_ENTRY_RESULT_SIZE 38u

/** Identity extension flag: Report 13 memory inventory is available. */
#define ZF_EXT_MEMORY 0x01u

/** Report 11 bus mode: SPI. */
#define ZF_STORAGE_BUS_SPI 2u
/** Report 11 bus mode: native four-bit SD. */
#define ZF_STORAGE_BUS_SD4 3u

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
/** SD memory-card transport over SPI, mutually exclusive with native SDMMC. */
#define ZF_CAP_SPI 0x08u
/** Native SDMMC card transport. */
#define ZF_CAP_SDMMC 0x10u
/** Optional temporary-file benchmark capability. */
#define ZF_CAP_BENCHMARK 0x20u
/** Wireless management (staging, tests, scans, BLE) is supported. */
#define ZF_CAP_WIRELESS 0x80u

/** Board unique-identity length in bytes. */
#define ZF_UID_SIZE 8u
/** Report 3 load sentinel: no measurement window is available. */
#define ZF_LOAD_UNAVAILABLE 0xFFFFu
/** Report 2 pin sentinel: the function is not allocated on this board. */
#define ZF_GPIO_UNASSIGNED 0xFFu

/** Feature 1 identity payload source. */
typedef struct {
	uint8_t uid[ZF_UID_SIZE]; /**< Boot-sampled unique device identity. */
	uint8_t firmware[3]; /**< Semantic version: major, minor, patch. */
	uint8_t mcu; /**< ZF_MCU_* family of this image. */
	uint8_t board_model; /**< ZF_BOARD_* model of this image. */
	uint8_t capabilities; /**< ZF_CAP_* bits implemented by this image. */
	uint32_t ram_total_bytes; /**< Total core RAM capacity. */
	uint32_t ram_reserved_bytes; /**< Linked core RAM regions. */
} zf_identity_t;

/** Boot-sampled physical capacities and linker-reserved external RAM, not live heap usage. */
typedef struct {
	bool psram_available; /**< PSRAM initialization succeeded. */
	bool flash_available; /**< Module flash capacity was read successfully. */
	uint32_t psram_total_bytes; /**< Detected PSRAM chip capacity. */
	uint32_t psram_reserved_bytes; /**< Linked PSRAM regions, including permanent heap pools. */
	uint32_t flash_total_bytes; /**< Detected module SPI flash capacity; not free filesystem space. */
} zf_memory_inventory_t;

/** Saved configuration and hardware pin allocation. */
typedef struct {
	uint16_t stored_clock_mhz; /**< Next-boot clock profile in MHz; zero selects the build default. */
	uint8_t stored_radios; /**< Saved ZF_WIRELESS_* bits, limited to radios built into this image. */
	uint8_t monitor_uart_index; /**< Console UART controller index, sampled from devicetree; 255 when unnamed. */
	uint8_t monitor_uart_tx_gpio; /**< Console TX pin from the chosen console's devicetree pinctrl. */
	uint8_t monitor_uart_rx_gpio; /**< Console RX pin from the chosen console's devicetree pinctrl. */
	uint8_t reset_cause; /**< Boot-sampled reset reason in the management wire enum. */
	bool card_detect_available; /**< A physical card-present GPIO is allocated. */
	uint8_t card_detect_gpio; /**< Board card-present input, valid when allocated. */
	bool msc_supported; /**< Native USB mass-storage class is enabled. */
	bool led_available; /**< Addressable RGB LED is allocated in hardware configuration. */
	uint8_t led_gpio; /**< LED data pin, valid when allocated. */
	uint8_t storage_gpios[6]; /**< Native CLK/CMD/DAT0-DAT3 or SPI SCK/MOSI/MISO/CS/255/255. */
} zf_config_t;

/** Feature 3 telemetry payload source; a live snapshot, not a retained history. */
typedef struct {
	bool temperature_valid; /**< A sensor reading is available; the centi-celsius field is then encoded. */
	int16_t temperature_centi_c; /**< Die temperature in hundredths of a degree. */
	uint64_t uptime_ms; /**< Milliseconds since boot. */
	uint16_t core0_load; /**< CPU load in basis points, or ZF_LOAD_UNAVAILABLE. */
	uint32_t system_clock_hz; /**< Running CPU clock. */
	uint8_t sequence; /**< Input publication sequence. */
	bool usb_configured; /**< The USB host has configured the device. */
	bool led_available; /**< Last LED transmission succeeded. */
	uint8_t led_colour; /**< Last transmitted RGB bitmask: red=1, green=2, blue=4. */
} zf_telemetry_t;

/** Feature 12 wireless payload source; states and details share the management wire enum. */
typedef struct {
	uint8_t wifi_state; /**< Wi-Fi state enum as documented for Report 12. */
	uint8_t detail; /**< Failure detail paired with the state; zero when the state needs none. */
	uint8_t ble_state; /**< ZF_BLE_OFF / ZF_BLE_ADVERTISING / ZF_BLE_CONNECTED. */
	uint8_t flags; /**< Feature 12 flag bits; reserved bits 2-3 are cleared on the wire. */
	uint8_t ipv4[4]; /**< Assigned address, all zero when unbound. */
	uint8_t ssid_length; /**< Length of ssid, clamped to ZF_SSID_MAX_BYTES on the wire. */
	uint8_t ssid[ZF_SSID_MAX_BYTES]; /**< Active or retained test SSID, otherwise the saved SSID. */
	uint8_t scan_state; /**< ZF_SCAN_* value of the scan table. */
	uint16_t scan_generation; /**< Identifies the scan result set entries belong to. */
	uint8_t scan_count; /**< Entries in the completed scan table. */
	uint8_t test_generation; /**< Candidate test generation counter, incremented on every test start. */
	uint8_t staged_state; /**< Feature 12 staged_state bits (staging-owned values). */
} zf_wireless_t;

/** Coherent storage snapshot; invalid samples carry no live SD fields on the wire. */
typedef struct {
	uint8_t sequence; /**< Published Input sequence. */
	bool configured; /**< Card detection and storage service are configured. */
	bool presence_known; /**< Debounced presence is known. */
	bool present; /**< Debounced card-present state. */
	bool sample_valid; /**< Firmware-owned, non-busy SD sample is available. */
	bool initialized; /**< Hardware initialization succeeded, independently of filesystem mounting. */
	bool capacity_valid; /**< Sector count belongs to the current medium. */
	bool cid_valid; /**< CID belongs to the initialized current medium. */
	uint8_t cid[16]; /**< Raw SD CID in wire byte order. */
	uint32_t bus_clock_hz; /**< Configured bus ceiling, not a measured clock. */
	uint8_t bus_mode; /**< ZF_STORAGE_BUS_* of the initialized medium, otherwise zero. */
	uint32_t block_count; /**< Whole-card capacity in 512-byte sectors. */
	uint8_t filesystem_format; /**< Mounted FAT format, or zero when unknown. */
	bool msc_enabled; /**< USB exclusively owns the raw medium. */
	bool msc_transition_pending; /**< An admitted ownership handoff is pending. */
	bool msc_request_rejected; /**< Last admitted handoff failed or was cancelled. */
	bool busy; /**< Storage worker is manipulating the medium; not encoded separately. */
} zf_storage_t;

/** This run created a file that could not be removed. */
#define ZF_BENCHMARK_FILE_REMAINS 1u
/** The firmware filesystem remains mounted. */
#define ZF_BENCHMARK_MOUNTED 2u

/** Coherent progress, retained until a subsequent admitted run or reboot. */
typedef struct {
	uint8_t sequence; /**< Input publication sequence. */
	zf_benchmark_state_t state; /**< Current or terminal lifecycle. */
	uint8_t flags; /**< FILE_REMAINS and MOUNTED bits. */
	uint32_t target_bytes; /**< Requested file length. */
	uint32_t written_bytes; /**< Bytes accepted by writes. */
	uint32_t read_bytes; /**< Bytes compared with the pattern. */
	uint32_t write_elapsed_ms; /**< Pattern generation, writes and final close/sync. */
	uint32_t read_elapsed_ms; /**< Reads, verification and final close. */
	uint32_t mismatch_count; /**< Differing bytes in the first mismatching chunk. */
	zf_benchmark_error_t error; /**< Execution failure, independent of admission errors. */
	uint8_t filesystem_result; /**< FatFs result when an I/O or cleanup failure supplies one. */
} zf_benchmark_t;

/** Encode Feature 1 from an identity snapshot. */
void zf_report_encode_identity(const zf_identity_t *identity, uint8_t out[ZF_IDENTITY_SIZE]);
/** Encode the boot memory inventory; unavailable components have zero wire values. */
void zf_report_encode_memory(const zf_memory_inventory_t *memory, uint8_t out[ZF_MEMORY_SIZE]);
/** Encode Feature 2 from a saved-configuration snapshot. */
void zf_report_encode_config(const zf_config_t *config, uint8_t out[ZF_CONFIG_SIZE]);
/** Encode Feature 3 from a telemetry snapshot. */
void zf_report_encode_telemetry(const zf_telemetry_t *telemetry, uint8_t out[ZF_TELEMETRY_SIZE]);
/** Encode Feature 10 from a coherent storage snapshot. */
void zf_report_encode_storage(const zf_storage_t *storage, uint8_t out[ZF_STORAGE_SIZE]);
/** Encode a coherent benchmark snapshot. */
void zf_report_encode_benchmark(const zf_benchmark_t *status, uint8_t out[ZF_BENCHMARK_SIZE]);
/** Encode Feature 12 from a wireless state snapshot. */
void zf_report_encode_wireless(const zf_wireless_t *wireless, uint8_t out[ZF_WIRELESS_SIZE]);

/**
 * @brief Encode one validated Wi-Fi scan entry in the control-result data layout.
 * @param out Receives generation, index, SSID, RSSI and security fields.
 * @param generation Scan-table generation.
 * @param index Entry index in that generation.
 * @param ssid Zero-padded SSID bytes.
 * @param ssid_length SSID byte length, clamped to ZF_SSID_MAX_BYTES.
 * @param rssi Signal strength in dBm.
 * @param security ZF scan security bits.
 */
void zf_report_encode_scan_entry_result(uint8_t out[ZF_SCAN_ENTRY_RESULT_SIZE], uint16_t generation, uint8_t index,
				 const uint8_t ssid[ZF_SSID_MAX_BYTES], uint8_t ssid_length, int8_t rssi,
				 uint8_t security);

/** Format the UID as the 16-character uppercase hex USB serial string, NUL-terminated. */
void zf_report_uid_hex(const uint8_t uid[ZF_UID_SIZE], char out[2 * ZF_UID_SIZE + 1]);

/** Map Zephyr hwinfo reset-cause bits to the Report 2 enum. */
uint8_t zf_report_reset_cause(uint32_t hwinfo_cause);

/** Service load in basis points from busy and total cycle deltas; ZF_LOAD_UNAVAILABLE without a window. */
uint16_t zf_report_load_basis_points(uint64_t busy_cycles, uint64_t total_cycles);

#endif

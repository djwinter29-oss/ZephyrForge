/**
 * @file zephyrforge.h
 * @brief Zephyr-side services shared by the USB HID, Bluetooth LE, Wi-Fi, storage and telemetry glue.
 */
#ifndef ZEPHYRFORGE_H
#define ZEPHYRFORGE_H

#include <stdbool.h>
#include <stdint.h>

#include "app/usb/hid_reports.h"

#ifndef ZEPHYRFORGE_VERSION
/** Semantic version stamped by the build (ZEPHYRFORGE_VERSION environment) or the development default. */
#define ZEPHYRFORGE_VERSION "0.1.0"
#endif

/** Grace period between an accepted reboot or save result and the reset. */
#define ZF_REBOOT_DELAY_MS 1000

/** Report 12 ble_state values. */
#define ZF_BLE_OFF 0u /**< Bluetooth is not advertising: disabled, stopped or between connections. */
#define ZF_BLE_ADVERTISING 1u /**< Connectable advertising is running at the fast or slow interval. */
#define ZF_BLE_CONNECTED 2u /**< A central holds the single connection. */

/** One recursive lock serializes the control dispatcher, staging and wireless state across threads. */
void zf_lock(void);
/** Release the recursive application lock taken by zf_lock(). */
void zf_unlock(void);

/** Sample identity, memory and reset cause before any transport starts. */
void zf_board_init(void);
/** Boot-sampled eight-byte unique identity; zeros if the hardware source failed. */
const uint8_t *zf_board_uid(void);
/** Uppercase hex USB serial derived from the UID, NUL-terminated. */
const char *zf_board_serial(void);

/** Encode Feature 1: identity, firmware version, MCU, board model and capabilities. */
void zf_identity_report(uint8_t out[ZF_IDENTITY_SIZE]);
/** Read the immutable boot-sampled module memory inventory. */
void zf_memory_report(uint8_t out[ZF_MEMORY_SIZE]);
/** Encode Feature 2: saved configuration and hardware pin allocation. */
void zf_config_report(uint8_t out[ZF_CONFIG_SIZE]);
/** Encode Feature 3: temperature, load, clock and last-LED telemetry with the given Input sequence. */
void zf_telemetry_report(uint8_t sequence, uint8_t out[ZF_TELEMETRY_SIZE]);
/** Encode Feature 10: the coherent storage snapshot with the given Input sequence. */
void zf_storage_report(uint8_t sequence, uint8_t out[ZF_STORAGE_SIZE]);
/** Encode Feature 12: Wi-Fi, BLE and staging state. */
void zf_wireless_report(uint8_t out[ZF_WIRELESS_SIZE]);

/**
 * @brief Apply the saved CPU clock profile before transports start.
 * @param mhz Saved clock profile; zero or the devicetree default leaves the clock unchanged.
 * @return Zero on success or no change, otherwise a negative errno.
 */
int zf_system_clock_apply(uint16_t mhz);
/** Running CPU clock in Hz, or the devicetree default when the clock driver cannot report it. */
uint32_t zf_system_clock_hz(void);

/** Start the temperature/load sampling windows. */
void zf_telemetry_init(void);
/** Refresh the cached temperature and load window; call from the main loop. */
void zf_telemetry_poll(void);

/** Register the storage service and start policy polling with the boot-sampled card state. */
void zf_storage_init(void);
/** Read retained benchmark progress without acquiring the media lock. */
void zf_benchmark_report(uint8_t sequence, uint8_t out[ZF_BENCHMARK_SIZE]);
/** Let the storage worker change owners after this Feature 20 result has left the control path. */
void zf_storage_result_read(uint8_t transaction);

/** Start the Wi-Fi state machine when the saved radio switch is on. */
void zf_wifi_init(bool enabled);
/** Advance connects, tests and scans; call from the main loop. */
void zf_wifi_poll(void);

/** Start Bluetooth when the saved switch is on and a Control key is saved. */
void zf_ble_init(bool enabled);
/** Current Report 12 ble_state value. */
uint8_t zf_ble_state(void);
/** True when the Bluetooth start failed terminally (no retry until reboot). */
bool zf_ble_failed(void);

/** Register the USB HID composite device. Returns 0 or a negative errno. */
int zf_usb_init(void);
/** True while the host has configured the USB device. */
bool zf_usb_configured(void);
/** Publish due Input reports; call from the main loop. */
void zf_usb_poll(void);

#endif

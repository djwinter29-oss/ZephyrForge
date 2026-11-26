/**
 * @file wifi.h
 * @brief Wi-Fi station: saved-network connection, candidate tests, scans and the Feature 12 Wi-Fi fields.
 *
 * A Wi-Fi-less image links stubs that report Wi-Fi as unavailable.
 */
#ifndef ZEPHYRFORGE_WIFI_H
#define ZEPHYRFORGE_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "report/report.h"

/** Feature 12 scan_state values. */
#define ZF_SCAN_IDLE 0u /**< No scan has been requested since boot. */
#define ZF_SCAN_RUNNING 1u /**< A scan is in progress; results are being collected. */
#define ZF_SCAN_COMPLETE 2u /**< The last scan finished and its table is readable. */
#define ZF_SCAN_FAILED 3u /**< The last scan failed or timed out; the table is stale. */

/** Start the Wi-Fi state machine when the saved radio switch is on. */
void zf_wifi_init(bool enabled);
/** Advance connects, tests and scans; call from the main loop. */
void zf_wifi_poll(void);

/**
 * @brief Fill the Wi-Fi fields of a Feature 12 snapshot; called with the application lock held.
 * @param status Snapshot whose ble_state and Bluetooth-failure detail are already set. The Wi-Fi image may
 *        replace detail with a Wi-Fi join detail; it fills wifi_state, SSID, IPv4 and scan fields.
 * @return True when a saved network is configured (Feature 12 flags bit 0).
 */
bool zf_wifi_status(zf_wireless_t *status);

/** True while a candidate test is joining (Feature 12 wifi_state 4). */
bool zf_wifi_test_active(void);
/** True while a candidate test has joined and holds the link (Feature 12 wifi_state 5). */
bool zf_wifi_test_joined(void);
/** True while a scan or its timed-out driver request is still active. */
bool zf_wifi_scan_active(void);

/** Start joining a candidate pair; password is NUL-terminated (empty for an open network). */
int zf_wifi_test_start(const uint8_t *ssid, uint8_t ssid_length, const char *password);
/** Drop any candidate test link and return to the saved network when Wi-Fi is enabled. */
void zf_wifi_test_cancel(void);

/** Request one scan; returns 0 when started, otherwise a negative errno. */
int zf_wifi_scan_start(void);
/** Current ZF_SCAN_* state. */
uint8_t zf_wifi_scan_state(void);
/** Generation counter identifying the current or last completed scan table. */
uint16_t zf_wifi_scan_generation(void);
/** Entry count of the completed scan table. */
uint8_t zf_wifi_scan_count(void);
/** Copy one completed scan entry; ssid must hold ZF_SSID_MAX_BYTES bytes. */
int zf_wifi_scan_entry(uint8_t index, uint8_t *ssid, uint8_t *ssid_length, int8_t *rssi, uint8_t *security);

#endif

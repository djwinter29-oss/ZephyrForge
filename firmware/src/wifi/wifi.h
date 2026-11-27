/**
 * @file wifi.h
 * @brief Wi-Fi station: saved-network connection, candidate tests, scans and the Feature 12 Wi-Fi fields.
 *
 * A Wi-Fi-less image compiles inline stubs that report Wi-Fi as unavailable and still expose the saved SSID.
 */
#ifndef ZEPHYRFORGE_WIFI_H
#define ZEPHYRFORGE_WIFI_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include "protocol/management_protocol.h"
#include "protocol/report_codec.h"
#include "settings/settings.h"

#if IS_ENABLED(CONFIG_ZF_WIFI)
/** Start the Wi-Fi state machine when the saved radio switch is on. */
void zf_wifi_init(bool enabled);
/** Advance connects, tests and scans; call from the main service loop. */
void zf_wifi_poll(void);
/** Return the next Wi-Fi retry, timeout or verification deadline. */
int64_t zf_wifi_next_deadline(void);

/**
 * @brief Fill the Wi-Fi fields of a Feature 12 snapshot; state access is serialized by the Wi-Fi module.
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
/**
 * @brief Copy one entry from a completed scan generation under one state lock.
 * @param generation Expected scan generation.
 * @param index Entry index.
 * @param ssid Receives ZF_SSID_MAX_BYTES bytes.
 * @param ssid_length Receives the SSID byte length.
 * @param rssi Receives signal strength.
 * @param security Receives the Feature 12 security mask.
 * @return 0 on success, -EBUSY while a scan is active, -ESTALE for an unavailable generation, or -EINVAL for an
 *         out-of-range index.
 */
int zf_wifi_scan_entry(uint16_t generation, uint8_t index, uint8_t *ssid, uint8_t *ssid_length, int8_t *rssi,
		       uint8_t *security);
#else
static inline void zf_wifi_init(bool enabled)
{
	(void)enabled;
}

static inline void zf_wifi_poll(void)
{
}

static inline int64_t zf_wifi_next_deadline(void)
{
	return INT64_MAX;
}

static inline bool zf_wifi_status(zf_wireless_t *status)
{
	status->ssid_length = zf_settings_wifi_ssid_bytes(status->ssid);
	return status->ssid_length != 0;
}

static inline bool zf_wifi_test_active(void)
{
	return false;
}

static inline bool zf_wifi_test_joined(void)
{
	return false;
}

static inline bool zf_wifi_scan_active(void)
{
	return false;
}

static inline int zf_wifi_test_start(const uint8_t *ssid, uint8_t ssid_length, const char *password)
{
	(void)ssid;
	(void)ssid_length;
	(void)password;
	return -ENOTSUP;
}

static inline void zf_wifi_test_cancel(void)
{
}

static inline int zf_wifi_scan_start(void)
{
	return -ENOTSUP;
}

static inline int zf_wifi_scan_entry(uint16_t generation, uint8_t index, uint8_t *ssid, uint8_t *ssid_length,
				     int8_t *rssi, uint8_t *security)
{
	(void)generation;
	(void)index;
	(void)ssid;
	(void)ssid_length;
	(void)rssi;
	(void)security;
	return -ENOTSUP;
}
#endif

#endif

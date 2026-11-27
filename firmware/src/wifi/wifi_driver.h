/**
 * @file wifi_driver.h
 * @brief Zephyr Wi-Fi request adapter for the station lifecycle policy.
 */
#ifndef ZEPHYRFORGE_WIFI_DRIVER_H
#define ZEPHYRFORGE_WIFI_DRIVER_H

#include <stdint.h>

#include <zephyr/sys/util.h>

#include <zephyr/net/net_if.h>

#include "protocol/report_codec.h"
#include "settings/settings.h"

/** Request operations executed by the Zephyr adapter. */
typedef enum {
	ZF_WIFI_DRIVER_DISCONNECT, /**< Request that the station leave its current connection. */
	ZF_WIFI_DRIVER_CONNECT, /**< Request association with the supplied profile. */
	ZF_WIFI_DRIVER_SCAN, /**< Request a driver scan. */
	ZF_WIFI_DRIVER_VERIFY_UP, /**< Require association and a preferred IPv4 address. */
	ZF_WIFI_DRIVER_VERIFY_DOWN, /**< Require a confirmed disconnected interface and no preferred IPv4 address. */
} zf_wifi_driver_action_t;

/** Credential profile passed to one connect request; callers wipe it after dispatch. */
typedef struct {
	uint8_t ssid[ZF_SSID_MAX_BYTES]; /**< Raw SSID bytes. */
	uint8_t ssid_length; /**< SSID length, 0..ZF_SSID_MAX_BYTES. */
	char password[ZF_WIFI_PASSWORD_SIZE]; /**< NUL-terminated credential. */
} zf_wifi_network_t;

/** Driver scan timeout; AIROC scans every channel more slowly than the Espressif driver. */
#define ZF_WIFI_DRIVER_SCAN_TIMEOUT_MS (IS_ENABLED(CONFIG_WIFI_AIROC) ? 15000 : 10000)

/** Driver notifications after vendor differences are normalized; delivered from net_mgmt context. */
typedef enum {
	ZF_WIFI_DRIVER_EVENT_UP, /**< Joined: association on Espressif, DHCP bound on AIROC. */
	ZF_WIFI_DRIVER_EVENT_CONNECT_FAILED, /**< A connect attempt failed without a following disconnect event. */
	ZF_WIFI_DRIVER_EVENT_DISCONNECTED, /**< The station left or lost its connection. */
	ZF_WIFI_DRIVER_EVENT_DISCONNECT_FAILED, /**< A disconnect result was missing or reported failure. */
	ZF_WIFI_DRIVER_EVENT_INTERFACE_DOWN, /**< The interface went down; any link must be rebuilt. */
	ZF_WIFI_DRIVER_EVENT_SCAN_DONE, /**< A scan finished with the given status. */
} zf_wifi_driver_event_t;

/** Receives one normalized driver event and its status (zero when the event carries none). */
typedef void (*zf_wifi_driver_event_cb_t)(zf_wifi_driver_event_t event, int status);
/** Receives one scan result with ZF_WIFI_SECURITY_* bits. */
typedef void (*zf_wifi_driver_scan_cb_t)(const uint8_t *ssid, uint8_t ssid_length, int8_t rssi, uint8_t security);

/**
 * @brief Subscribe to the interface's driver events; call once before the first request.
 * @param iface Wi-Fi interface whose events are reported; other interfaces are ignored.
 * @param on_event Normalized link and scan-completion events.
 * @param on_scan Individual scan results.
 */
void zf_wifi_driver_listen(struct net_if *iface, zf_wifi_driver_event_cb_t on_event, zf_wifi_driver_scan_cb_t on_scan);

/** Positive result indicating a successful status query confirmed the interface is disconnected. */
enum { ZF_WIFI_DRIVER_LINK_DOWN = 1 };

/**
 * @brief Execute one Wi-Fi operation through Zephyr net_mgmt.
 * @param action Operation to perform.
 * @param iface Wi-Fi interface selected by the application.
 * @param network Profile required for connect; ignored otherwise.
 * @return Zero on success, ZF_WIFI_DRIVER_LINK_DOWN for a positively observed down interface, or a negative errno.
 */
int zf_wifi_driver_request(zf_wifi_driver_action_t action, struct net_if *iface,
			   const zf_wifi_network_t *network);

#endif

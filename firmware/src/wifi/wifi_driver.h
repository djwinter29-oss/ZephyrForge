/**
 * @file wifi_driver.h
 * @brief Zephyr Wi-Fi request adapter for the station lifecycle policy.
 */
#ifndef ZEPHYRFORGE_WIFI_DRIVER_H
#define ZEPHYRFORGE_WIFI_DRIVER_H

#include <stdint.h>

#include <zephyr/net/net_if.h>

#include "protocol/report_codec.h"

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

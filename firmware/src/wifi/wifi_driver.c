/* SPDX-License-Identifier: BSD-3-Clause */
#include "wifi/wifi_driver.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(zf_wifi_driver, LOG_LEVEL_INF);

#define WIFI_EVENTS                                                                                           \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | NET_EVENT_WIFI_SCAN_RESULT |         \
	 NET_EVENT_WIFI_SCAN_DONE)

static struct net_if *listen_iface;
static zf_wifi_driver_event_cb_t event_cb;
static zf_wifi_driver_scan_cb_t scan_cb;
static struct net_mgmt_event_callback wifi_cb;
#if defined(CONFIG_WIFI_AIROC)
static struct net_mgmt_event_callback interface_cb;
static struct net_mgmt_event_callback dhcp_cb;
#endif

static uint8_t security_bits(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return 0;
	case WIFI_SECURITY_TYPE_WEP:
		return ZF_WIFI_SECURITY_PRIVACY;
	case WIFI_SECURITY_TYPE_WPA_PSK:
		return ZF_WIFI_SECURITY_PRIVACY | ZF_WIFI_SECURITY_WPA;
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_EAP:
		return ZF_WIFI_SECURITY_PRIVACY | ZF_WIFI_SECURITY_RSN;
	default:
		return ZF_WIFI_SECURITY_PRIVACY;
	}
}

static void event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
	const struct wifi_status *status = cb->info;

	if (iface != listen_iface) {
		return;
	}
	switch (mgmt_event) {
	case NET_EVENT_IF_DOWN:
		event_cb(ZF_WIFI_DRIVER_EVENT_INTERFACE_DOWN, 0);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (IS_ENABLED(CONFIG_WIFI_AIROC)) {
			/* AIROC reports association before DHCP; only the bound event completes the join. */
			if (status != NULL && status->status != 0) {
				event_cb(ZF_WIFI_DRIVER_EVENT_CONNECT_FAILED, status->status);
			}
		} else if (status != NULL && status->status == 0) {
			/* Failed connects are followed by a disconnect event. */
			event_cb(ZF_WIFI_DRIVER_EVENT_UP, 0);
		}
		break;
	case NET_EVENT_IPV4_DHCP_BOUND:
		event_cb(ZF_WIFI_DRIVER_EVENT_UP, 0);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		if (status == NULL) {
			event_cb(ZF_WIFI_DRIVER_EVENT_DISCONNECT_FAILED, -EIO);
		} else if (status->status != 0) {
			event_cb(ZF_WIFI_DRIVER_EVENT_DISCONNECT_FAILED, status->status);
		} else {
			event_cb(ZF_WIFI_DRIVER_EVENT_DISCONNECTED, 0);
		}
		break;
	case NET_EVENT_WIFI_SCAN_RESULT:
		if (cb->info != NULL && cb->info_length >= sizeof(struct wifi_scan_result)) {
			const struct wifi_scan_result *result = cb->info;

			scan_cb(result->ssid, result->ssid_length, result->rssi, security_bits(result->security));
		}
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		event_cb(ZF_WIFI_DRIVER_EVENT_SCAN_DONE,
			 status != NULL && cb->info_length >= sizeof(*status) ? status->status : -EIO);
		break;
	default:
		break;
	}
}

void zf_wifi_driver_listen(struct net_if *iface, zf_wifi_driver_event_cb_t on_event, zf_wifi_driver_scan_cb_t on_scan)
{
	listen_iface = iface;
	event_cb = on_event;
	scan_cb = on_scan;
	net_mgmt_init_event_callback(&wifi_cb, event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
#if defined(CONFIG_WIFI_AIROC)
	/* AIROC can drop the interface without a disconnect result and joins only after DHCP binds. */
	net_mgmt_init_event_callback(&interface_cb, event_handler, NET_EVENT_IF_DOWN);
	net_mgmt_add_event_callback(&interface_cb);
	net_mgmt_init_event_callback(&dhcp_cb, event_handler, NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&dhcp_cb);
#endif
}

int zf_wifi_driver_request(zf_wifi_driver_action_t action, struct net_if *iface,
			   const zf_wifi_network_t *network)
{
	if (iface == NULL || (action == ZF_WIFI_DRIVER_CONNECT && network == NULL)) {
		return -EINVAL;
	}

	switch (action) {
	case ZF_WIFI_DRIVER_DISCONNECT:
		return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	case ZF_WIFI_DRIVER_CONNECT: {
		struct wifi_connect_req_params params = {
			.ssid = network->ssid,
			.ssid_length = network->ssid_length,
			.psk = (const uint8_t *)network->password,
			.psk_length = (uint8_t)strlen(network->password),
			.security = network->password[0] ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
			.channel = WIFI_CHANNEL_ANY,
			.band = WIFI_FREQ_BAND_2_4_GHZ,
			.mfp = WIFI_MFP_OPTIONAL,
			.timeout = SYS_FOREVER_MS,
		};

		return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	}
	case ZF_WIFI_DRIVER_SCAN:
		return net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	case ZF_WIFI_DRIVER_VERIFY_UP: {
		struct wifi_iface_status status = {0};
		int err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));

		if (err == 0 &&
		    (status.state == WIFI_STATE_DISCONNECTED || status.state == WIFI_STATE_INTERFACE_DISABLED ||
		     status.state == WIFI_STATE_INACTIVE)) {
			err = ZF_WIFI_DRIVER_LINK_DOWN;
		} else if (err == 0 && status.state != WIFI_STATE_COMPLETED) {
			err = -EAGAIN;
		} else if (err == 0 && net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) == NULL) {
			err = -EADDRNOTAVAIL;
		}
		if (err != 0 && err != ZF_WIFI_DRIVER_LINK_DOWN) {
			LOG_WRN("Wi-Fi interface status request failed: %d", err);
		}
		return err;
	}
	case ZF_WIFI_DRIVER_VERIFY_DOWN: {
		struct wifi_iface_status status = {0};
		int err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));

		if (err == 0 && status.state != WIFI_STATE_DISCONNECTED) {
			err = -EBUSY;
		}
		if (err == 0 && net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL) {
			err = -EBUSY;
		}
		if (err != 0 && err != -EBUSY) {
			LOG_WRN("Wi-Fi disconnect verification failed: %d", err);
		}
		return err;
	}
	default:
		return -EINVAL;
	}
}

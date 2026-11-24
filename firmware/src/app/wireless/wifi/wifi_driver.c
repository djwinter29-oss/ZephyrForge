/* SPDX-License-Identifier: BSD-3-Clause */
#include "wifi_driver.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(zf_wifi_driver, LOG_LEVEL_INF);

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

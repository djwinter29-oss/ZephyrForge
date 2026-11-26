/* SPDX-License-Identifier: BSD-3-Clause */
#include "zephyrforge.h"
#include "report/report.h"
#include "settings/settings.h"
#include "wireless/ble/ble.h"
#include "wireless/wifi/wifi.h"
#include "wireless/wireless.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/ztest.h>

K_MUTEX_DEFINE(app_mutex);
static struct net_if *wifi_iface;
static struct wifi_iface_status station_status;
static unsigned int connect_requests;
static unsigned int scan_requests;
static unsigned int disconnect_requests;

static void iface_init(struct net_if *iface)
{
	net_if_flag_set(iface, NET_IF_NO_AUTO_START);
}

static const struct net_if_api iface_api = {.init = iface_init};
NET_DEVICE_INIT(wifi_test, "wifi_test", NULL, NULL, NULL, NULL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		&iface_api, DUMMY_L2, NET_L2_GET_CTX_TYPE(DUMMY_L2), 1500);

void zf_lock(void) { zassert_ok(k_mutex_lock(&app_mutex, K_FOREVER)); }
void zf_unlock(void) { zassert_ok(k_mutex_unlock(&app_mutex)); }
uint8_t zf_settings_wifi_ssid_bytes(uint8_t out[ZF_SSID_MAX_BYTES])
{
	memcpy(out, "saved", 5);
	return 5;
}
void zf_settings_wifi_password(char out[ZF_WIFI_PASSWORD_SIZE]) { out[0] = 0; }
bool zf_settings_ble_key_configured(void) { return false; }
bool zf_ble_failed(void) { return false; }
uint8_t zf_ble_state(void) { return 0; }
void zf_control_stage_status(uint8_t *flags, uint8_t *state) { *flags = 0; *state = 0; }

struct net_if *zf_test_wifi_iface(void) { return wifi_iface; }

int net_mgmt_NET_REQUEST_WIFI_CONNECT(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	zassert_equal(size, sizeof(struct wifi_connect_req_params));
	zassert_not_null(data);
	++connect_requests;
	station_status.state = WIFI_STATE_ASSOCIATING;
	return 0;
}

int net_mgmt_NET_REQUEST_WIFI_SCAN(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	++scan_requests;
	return 0;
}

int net_mgmt_NET_REQUEST_WIFI_IFACE_STATUS(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	zassert_equal(size, sizeof(station_status));
	zassert_not_null(data);
	memcpy(data, &station_status, sizeof(station_status));
	return 0;
}

int net_mgmt_NET_REQUEST_WIFI_DISCONNECT(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	struct wifi_status status = {.status = 0};

	ARG_UNUSED(request);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	++disconnect_requests;
	station_status.state = WIFI_STATE_DISCONNECTED;
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_DISCONNECT_RESULT, iface, &status, sizeof(status));
	return 0;
}

static uint8_t wireless_state(void)
{
	uint8_t report[ZF_WIRELESS_SIZE];

	zf_wireless_report(report);
	return report[1];
}

ZTEST(wifi_recovery, test_timed_out_scan_fences_automatic_saved_join)
{
	struct wifi_status failed_disconnect = {.status = -1};

	wifi_iface = net_if_get_default();
	zassert_not_null(wifi_iface);
	k_sleep(K_MSEC(1));
	zf_wifi_init(true);
	zf_wifi_poll();
	zassert_equal(connect_requests, 1, "saved network should start before recovery");
	station_status.state = WIFI_STATE_ASSOCIATED;
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_DISCONNECT_RESULT, wifi_iface, &failed_disconnect,
					sizeof(failed_disconnect));
	for (int i = 0; i < 1200 && disconnect_requests == 0; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(disconnect_requests, 1, "recovery must retry the uncertain disconnect");
	for (int i = 0; i < 1200 && connect_requests == 1; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(connect_requests, 2, "confirmed recovery must release the saved-network owner");

	struct wifi_status successful_connect = {.status = 0};
	struct in_addr address;

	station_status.state = WIFI_STATE_COMPLETED;
	zassert_ok(net_addr_pton(AF_INET, "192.0.2.2", &address));
	zassert_not_null(net_if_ipv4_addr_add(wifi_iface, &address, NET_ADDR_MANUAL, 0));
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_CONNECT_RESULT, wifi_iface, &successful_connect,
					sizeof(successful_connect));
	for (int i = 0; i < 100 && wireless_state() != 2; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(wireless_state(), 2, "saved connection should reach connected before link-loss test");

	unsigned int disconnects_before_link_loss = disconnect_requests;

	for (int i = 0; i < 17; ++i) {
		net_mgmt_event_notify_with_info(NET_EVENT_WIFI_CONNECT_RESULT, wifi_iface, &successful_connect,
						sizeof(successful_connect));
	}
	k_sleep(K_MSEC(20));
	net_mgmt_event_notify_with_info(NET_EVENT_IF_DOWN, wifi_iface, NULL, 0);
	for (int i = 0; i < 100 && disconnect_requests == disconnects_before_link_loss; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(disconnect_requests, disconnects_before_link_loss + 1,
		      "operational link loss must explicitly clear the AIROC driver's association");
	for (int i = 0; i < 1400 && connect_requests == 2; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(connect_requests, 3, "saved network should reconnect after driver cleanup");

	zf_lock();
	zassert_ok(zf_wifi_scan_start());
	zf_unlock();
	zf_wifi_poll();
	for (int i = 0; i < 100 && scan_requests == 0; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(scan_requests, 1);

	for (int i = 0; i < 2200 && zf_wifi_scan_state() == ZF_SCAN_RUNNING; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(zf_wifi_scan_state(), ZF_SCAN_FAILED);
	zassert_true(zf_wifi_scan_active(), "the timed-out driver operation remains quarantined");
	for (int i = 0; i < 20; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(connect_requests, 3, "automatic saved reconnect must wait for scan completion");
}

ZTEST_SUITE(wifi_recovery, NULL, NULL, NULL, NULL, NULL);

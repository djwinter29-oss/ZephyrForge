/* SPDX-License-Identifier: BSD-3-Clause */
#include "app/zephyrforge.h"
#include "app/protocol/hid_reports.h"
#include "platform/boot_config.h"
#include "platform/zf_platform.h"

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
static struct wifi_iface_status station_status;
static unsigned int disconnect_requests;
static unsigned int status_queries;
static int disconnect_state = WIFI_STATE_DISCONNECTED;
static int disconnect_result = -1;
static int status_error;

static void notify_connect_result(struct net_if *iface, int result)
{
	struct wifi_status status = {.status = result};

	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_CONNECT_RESULT, iface, &status, sizeof(status));
}

static void notify_disconnect_result(struct net_if *iface, int result)
{
	struct wifi_status status = {.status = result};

	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_DISCONNECT_RESULT, iface, &status, sizeof(status));
}

static uint8_t wireless_state(void)
{
	uint8_t report[ZF_WIRELESS_SIZE];

	zf_wireless_report(report);
	return report[1];
}

static struct net_if *wifi_iface;

static void iface_init(struct net_if *iface)
{
	net_if_flag_set(iface, NET_IF_NO_AUTO_START);
}

static const struct net_if_api iface_api = {.init = iface_init};
NET_DEVICE_INIT(wifi_test, "wifi_test", NULL, NULL, NULL, NULL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		&iface_api, DUMMY_L2, NET_L2_GET_CTX_TYPE(DUMMY_L2), 1500);

void zf_lock(void) { zassert_ok(k_mutex_lock(&app_mutex, K_FOREVER)); }
void zf_unlock(void) { zassert_ok(k_mutex_unlock(&app_mutex)); }
uint8_t zf_boot_config_wifi_ssid_bytes(uint8_t out[ZF_SSID_MAX_BYTES]) { ARG_UNUSED(out); return 0; }
void zf_boot_config_wifi_password(char out[ZF_WIFI_PASSWORD_SIZE]) { out[0] = 0; }
bool zf_boot_config_ble_key_configured(void) { return false; }
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
	station_status.state = WIFI_STATE_ASSOCIATING;
	return 0;
}

int net_mgmt_NET_REQUEST_WIFI_SCAN(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

int net_mgmt_NET_REQUEST_WIFI_IFACE_STATUS(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	zassert_equal(size, sizeof(station_status));
	zassert_not_null(data);
	++status_queries;
	if (status_error != 0) {
		return status_error;
	}
	memcpy(data, &station_status, sizeof(station_status));
	return 0;
}

int net_mgmt_NET_REQUEST_WIFI_DISCONNECT(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	++disconnect_requests;
	station_status.state = disconnect_state;
	notify_disconnect_result(iface, disconnect_result);
	return 0;
}

ZTEST(wifi_esp32_lifecycle, test_cancel_connecting_reconciles_esp32_disconnect_result)
{
	struct in_addr address;
	int admission = -EBUSY;

	wifi_iface = net_if_get_default();
	zassert_not_null(wifi_iface);
	zf_wifi_init(false);
	zf_lock();
	zassert_ok(zf_wifi_test_start((const uint8_t *)"candidate", 9, "password"));
	zf_unlock();
	zf_wifi_poll();

	disconnect_state = WIFI_STATE_ASSOCIATED;
	zf_lock();
	zf_wifi_test_cancel();
	zf_unlock();
	zf_wifi_poll();
	zassert_equal(disconnect_requests, 1);

	for (int i = 0; i < 100 && status_queries == 0; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zf_wifi_poll();
	zassert_true(status_queries > 0, "ambiguous disconnect must be reconciled using interface status");
	zf_lock();
	admission = zf_wifi_test_start((const uint8_t *)"replacement", 11, "new-password");
	zf_unlock();
	zassert_equal(admission, -EBUSY, "an associated driver must remain quarantined");

	disconnect_state = WIFI_STATE_DISCONNECTED;
	for (int i = 0; i < 1400 && admission == -EBUSY; ++i) {
		zf_wifi_poll();
		zf_lock();
		admission = zf_wifi_test_start((const uint8_t *)"replacement", 11, "new-password");
		zf_unlock();
		if (admission == -EBUSY) {
			k_sleep(K_MSEC(5));
		}
	}
	zassert_equal(admission, 0,
		      "confirmed disconnect admission=%d, queries=%u, disconnects=%u",
		      admission, status_queries, disconnect_requests);
	zassert_equal(disconnect_requests, 2, "still-associated state must trigger a later disconnect attempt");

	zf_wifi_poll();
	station_status.state = WIFI_STATE_ASSOCIATED;
	notify_disconnect_result(wifi_iface, -1);
	disconnect_state = WIFI_STATE_DISCONNECTED;
	disconnect_result = 0;
	for (int i = 0; i < 1200 && disconnect_requests < 3; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(disconnect_requests, 3, "recovery must retry the disconnect");
	for (int i = 0; i < 100 && zf_wifi_test_active(); ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_false(zf_wifi_test_active(), "a recovered disconnect must terminate the candidate");
	zassert_equal(wireless_state(), 6, "the failed candidate must leave a terminal result");

	zf_lock();
	zassert_ok(zf_wifi_test_start((const uint8_t *)"replacement", 11, "new-password"));
	zf_unlock();
	zf_wifi_poll();
	station_status.state = WIFI_STATE_COMPLETED;
	zassert_ok(net_addr_pton(AF_INET, "192.0.2.1", &address));
	zassert_not_null(net_if_ipv4_addr_add(wifi_iface, &address, NET_ADDR_MANUAL, 0));
	notify_connect_result(wifi_iface, 0);
	for (int i = 0; i < 100 && zf_wifi_test_active(); ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_false(zf_wifi_test_active(), "candidate should reach the connected state");

	unsigned int queries_before_overflow = status_queries;

	for (unsigned int i = 0; i < 17; ++i) {
		notify_connect_result(wifi_iface, 0);
	}
	k_sleep(K_MSEC(5));
	zf_wifi_poll();
	for (int i = 0; i < 100 && status_queries == queries_before_overflow; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_true(status_queries > queries_before_overflow,
		     "event queue overflow must request a state reconciliation");

	status_error = -ENOTCONN;
	queries_before_overflow = status_queries;
	for (unsigned int i = 0; i < 17; ++i) {
		notify_connect_result(wifi_iface, 0);
	}
	k_sleep(K_MSEC(5));
	zf_wifi_poll();
	for (int i = 0; i < 100 && status_queries == queries_before_overflow; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zf_wifi_poll();
	zassert_true(status_queries > queries_before_overflow, "query error should exercise established-link recovery");
	zassert_equal(wireless_state(), 5, "a status-query error must not report a proven disconnect");

	unsigned int failed_query_count = status_queries;

	status_error = 0;
	for (int i = 0; i < 400 && status_queries == failed_query_count; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_true(status_queries > failed_query_count, "an uncertain established link must be rechecked");
	zassert_equal(wireless_state(), 5, "a later connected observation must retain the joined state");
}

ZTEST_SUITE(wifi_esp32_lifecycle, NULL, NULL, NULL, NULL, NULL);

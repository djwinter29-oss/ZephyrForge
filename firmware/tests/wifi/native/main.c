/* SPDX-License-Identifier: BSD-3-Clause */
#include "app/zephyrforge.h"
#include "platform/boot_config.h"
#include "platform/zf_platform.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/ztest.h>

K_MUTEX_DEFINE(app_mutex);
K_SEM_DEFINE(entered, 0, 1);
K_SEM_DEFINE(release_request, 0, 1);
K_SEM_DEFINE(finished, 0, 1);
static int connect_error;
static unsigned int joined;
static unsigned int disconnected;

void zf_lock(void) { zassert_ok(k_mutex_lock(&app_mutex, K_FOREVER)); }
void zf_unlock(void) { zassert_ok(k_mutex_unlock(&app_mutex)); }
uint8_t zf_boot_config_wifi_ssid_bytes(uint8_t out[ZF_SSID_MAX_BYTES]) { ARG_UNUSED(out); return 0; }
void zf_boot_config_wifi_password(char out[ZF_WIFI_PASSWORD_SIZE]) { out[0] = 0; }
bool zf_boot_config_ble_key_configured(void) { return false; }
bool zf_ble_failed(void) { return false; }
uint8_t zf_ble_state(void) { return 0; }
void zf_control_test_joined(int64_t now) { ARG_UNUSED(now); ++joined; }
void zf_control_stage_status(uint8_t *flags, uint8_t *state) { *flags = 0; *state = 0; }

struct net_if *zf_test_wifi_iface(void) { return net_if_get_default(); }

static int blocked_request(void)
{
	k_sem_give(&entered);
	zassert_ok(k_sem_take(&release_request, K_SECONDS(2)));
	k_sem_give(&finished);
	return connect_error;
}

int net_mgmt_NET_REQUEST_WIFI_CONNECT(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	zassert_equal(size, sizeof(struct wifi_connect_req_params));
	zassert_not_null(data);
	return blocked_request();
}

int net_mgmt_NET_REQUEST_WIFI_SCAN(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	return blocked_request();
}

int net_mgmt_NET_REQUEST_WIFI_DISCONNECT(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	struct wifi_status status = {.status = 0};

	ARG_UNUSED(request);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	++disconnected;
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_DISCONNECT_RESULT, iface, &status, sizeof(status));
	return 0;
}

static uint8_t wifi_state(void)
{
	uint8_t report[ZF_WIRELESS_SIZE];

	zf_wireless_report(report);
	return report[1];
}

static void poll_for(uint8_t state)
{
	for (int i = 0; i < 100; ++i) {
		zf_wifi_poll();
		if (wifi_state() == state) {
			return;
		}
		k_sleep(K_MSEC(5));
	}
	zassert_equal(wifi_state(), state);
}

static void start_test(void)
{
	zf_lock();
	zassert_ok(zf_wifi_test_start((const uint8_t *)"candidate", 9, "password"));
	zf_unlock();
	zf_wifi_poll();
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
}

static void finish_request(void)
{
	k_sem_give(&release_request);
	zassert_ok(k_sem_take(&finished, K_SECONDS(1)));
	k_sleep(K_MSEC(20));
}

ZTEST(wifi_worker, test_blocking_requests_and_dhcp_complete_only_the_current_candidate)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_status success = {.status = 0};
	int64_t before;

	zassert_not_null(iface);
	zf_wifi_init(false);
	zf_lock();
	zassert_ok(zf_wifi_scan_start());
	zf_unlock();
	before = k_uptime_get();
	zf_wifi_poll();
	zassert_true(k_uptime_get() - before < 100, "scan blocked the management loop");
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(k_mutex_lock(&app_mutex, K_NO_WAIT), "driver retained the application lock");
	k_mutex_unlock(&app_mutex);
	net_mgmt_event_notify(NET_EVENT_WIFI_SCAN_DONE, iface);
	for (int i = 0; i < 100 && zf_wifi_scan_state() != ZF_SCAN_COMPLETE; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(zf_wifi_scan_state(), ZF_SCAN_COMPLETE);
	finish_request();

	start_test();
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_CONNECT_RESULT, iface, &success, sizeof(success));
	k_sleep(K_MSEC(20));
	zf_wifi_poll();
	zassert_equal(wifi_state(), 4, "association alone must not mark a join successful");
	zassert_equal(joined, 0);
	net_mgmt_event_notify(NET_EVENT_IPV4_DHCP_BOUND, iface);
	poll_for(5);
	zassert_equal(joined, 1);
	finish_request();
	zf_lock();
	zf_wifi_test_cancel();
	zf_unlock();
	for (int i = 0; i < 100 && disconnected == 0; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(disconnected, 1);
	zf_wifi_poll();

	connect_error = -ETIMEDOUT;
	start_test();
	zf_lock();
	zf_wifi_test_cancel();
	zassert_ok(zf_wifi_test_start((const uint8_t *)"replacement", 11, "new-password"));
	zf_unlock();
	finish_request();
	zassert_equal(wifi_state(), 4, "an old blocking request failed the replacement candidate");
	connect_error = 0;
	zf_wifi_poll();
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	net_mgmt_event_notify(NET_EVENT_IPV4_DHCP_BOUND, iface);
	poll_for(5);
	zassert_equal(joined, 2);
	finish_request();
}

ZTEST_SUITE(wifi_worker, NULL, NULL, NULL, NULL, NULL);

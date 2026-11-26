/* SPDX-License-Identifier: BSD-3-Clause */
#include "app/runtime.h"
#include "protocol/report_codec.h"
#include "settings/settings.h"
#include "transport/ble_gatt.h"
#include "wifi/wifi.h"
#include "management/wireless_status.h"

#include <errno.h>
#include <string.h>
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
static unsigned int disconnected;
static struct wifi_iface_status station_status;
static const struct in_addr station_ipv4 = {.s4_addr = {198, 51, 100, 1}};

static void iface_init(struct net_if *iface)
{
	net_if_flag_set(iface, NET_IF_NO_AUTO_START);
}

static const struct net_if_api iface_api = {.init = iface_init};
NET_DEVICE_INIT(wifi_test, "wifi_test", NULL, NULL, NULL, NULL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		&iface_api, DUMMY_L2, NET_L2_GET_CTX_TYPE(DUMMY_L2), 1500);

void zf_app_lock(void) { zassert_ok(k_mutex_lock(&app_mutex, K_FOREVER)); }
void zf_app_unlock(void) { zassert_ok(k_mutex_unlock(&app_mutex)); }
void zf_app_wakeup(void) {}
uint8_t zf_settings_wifi_ssid_bytes(uint8_t out[ZF_SSID_MAX_BYTES]) { ARG_UNUSED(out); return 0; }
void zf_settings_wifi_password(char out[ZF_WIFI_PASSWORD_SIZE]) { out[0] = 0; }
bool zf_settings_ble_key_configured(void) { return false; }
bool zf_ble_gatt_failed(void) { return false; }
uint8_t zf_ble_gatt_state(void) { return 0; }
void zf_control_staging_status(uint8_t *flags, uint8_t *state) { *flags = 0; *state = 0; }

struct net_if *zf_test_wifi_iface(void) { return net_if_get_default(); }

static int blocked_request(void)
{
	k_sem_give(&entered);
	zassert_ok(k_sem_take(&release_request, K_SECONDS(20)));
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

int net_mgmt_NET_REQUEST_WIFI_IFACE_STATUS(uint64_t request, struct net_if *iface, void *data, size_t size)
{
	ARG_UNUSED(request);
	ARG_UNUSED(iface);
	zassert_equal(size, sizeof(station_status));
	zassert_not_null(data);
	memcpy(data, &station_status, sizeof(station_status));
	return 0;
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
	station_status.state = WIFI_STATE_DISCONNECTED;
	station_status.ssid_len = 0;
	net_if_ipv4_addr_rm(iface, &station_ipv4);
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_DISCONNECT_RESULT, iface, &status, sizeof(status));
	return 0;
}

static uint8_t wifi_state(void)
{
	uint8_t report[ZF_WIRELESS_SIZE];

	zf_management_wireless_report(report);
	return report[1];
}

static zf_wireless_t scan_snapshot(void)
{
	zf_wireless_t status = {0};

	(void)zf_wifi_status(&status);
	return status;
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

static void poll_scan_for(uint8_t state)
{
	for (int i = 0; i < 100; ++i) {
		zf_wifi_poll();
		if (scan_snapshot().scan_state == state) {
			return;
		}
		k_sleep(K_MSEC(5));
	}
	zassert_equal(scan_snapshot().scan_state, state);
}

static void start_test_for(const char *ssid, uint8_t ssid_length, const char *password)
{
	int64_t before;

	zf_app_lock();
	zassert_ok(zf_wifi_test_start((const uint8_t *)ssid, ssid_length, password));
	zf_app_unlock();
	before = k_uptime_get();
	zf_wifi_poll();
	zassert_true(k_uptime_get() - before < 100, "join blocked the management loop");
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(k_mutex_lock(&app_mutex, K_NO_WAIT), "join retained the application lock");
	k_mutex_unlock(&app_mutex);
}

static void start_test(void)
{
	start_test_for("candidate", 9, "password");
}

static void finish_request(void)
{
	k_sem_give(&release_request);
	zassert_ok(k_sem_take(&finished, K_SECONDS(1)));
	k_sleep(K_MSEC(20));
}

static void set_station(const char *ssid, int state)
{
	memset(&station_status, 0, sizeof(station_status));
	station_status.state = state;
	station_status.ssid_len = strlen(ssid);
	memcpy(station_status.ssid, ssid, station_status.ssid_len);
	zassert_not_null(net_if_ipv4_addr_add(net_if_get_default(), &station_ipv4, NET_ADDR_MANUAL, 0));
}

ZTEST(wifi_worker, test_blocking_requests_and_dhcp_complete_only_the_current_candidate)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_status success = {.status = 0};
	struct wifi_scan_result scan_result = {.ssid = "partial", .ssid_length = 7, .rssi = -30};
	uint8_t entry_ssid[ZF_SSID_MAX_BYTES];
	uint8_t entry_length;
	int8_t entry_rssi;
	uint8_t entry_security;
	uint16_t scan_generation;
	int64_t before;

	zassert_not_null(iface);
	zf_wifi_init(false);
	zf_app_lock();
	zassert_ok(zf_wifi_scan_start());
	zf_app_unlock();
	scan_generation = scan_snapshot().scan_generation;
	zassert_equal(-EBUSY, zf_wifi_scan_entry(scan_generation, 0, entry_ssid, &entry_length, &entry_rssi,
						 &entry_security));
	before = k_uptime_get();
	zf_wifi_poll();
	zassert_true(k_uptime_get() - before < 100, "scan blocked the management loop");
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(k_mutex_lock(&app_mutex, K_NO_WAIT), "driver retained the application lock");
	k_mutex_unlock(&app_mutex);
	struct wifi_status failed_connect = {.status = -EIO};
	for (int i = 0; i < 17; ++i) {
		net_mgmt_event_notify_with_info(NET_EVENT_WIFI_CONNECT_RESULT, iface, &failed_connect,
						sizeof(failed_connect));
	}
	k_sleep(K_MSEC(20));
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_SCAN_DONE, iface, &success, sizeof(success));
	for (int i = 0; i < 100 && scan_snapshot().scan_state != ZF_SCAN_COMPLETE; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(scan_snapshot().scan_state, ZF_SCAN_COMPLETE);
	zassert_false(zf_wifi_scan_active(), "scan completion must release quarantine after event queue overflow");
	zassert_equal(-ESTALE, zf_wifi_scan_entry(scan_generation - 1, 0, entry_ssid, &entry_length, &entry_rssi,
						  &entry_security));
	zassert_equal(-EINVAL, zf_wifi_scan_entry(scan_generation, 0, entry_ssid, &entry_length, &entry_rssi,
						  &entry_security));
	finish_request();

	zf_app_lock();
	zassert_ok(zf_wifi_scan_start());
	zf_app_unlock();
	zf_wifi_poll();
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_SCAN_RESULT, iface, &scan_result, sizeof(scan_result));
	success.status = -EIO;
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_SCAN_DONE, iface, &success, sizeof(success));
	poll_scan_for(ZF_SCAN_FAILED);
	zassert_equal(scan_snapshot().scan_count, 0, "failed scans must discard partial results");
	finish_request();

	zf_app_lock();
	zassert_ok(zf_wifi_scan_start());
	zf_app_unlock();
	zf_wifi_poll();
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	while (scan_snapshot().scan_state == ZF_SCAN_RUNNING) {
		zf_wifi_poll();
		k_sleep(K_MSEC(10));
	}
	zassert_equal(scan_snapshot().scan_state, ZF_SCAN_FAILED);
	zf_app_lock();
	zassert_equal(zf_wifi_scan_start(), -EBUSY, "do not replace a scan whose driver is still active");
	zf_app_unlock();
	success.status = 0;
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_SCAN_DONE, iface, &success, sizeof(success));
	for (int i = 0; i < 100 && zf_wifi_scan_active(); ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(scan_snapshot().scan_state, ZF_SCAN_FAILED, "late completion must not publish the timed-out scan");
	finish_request();

	zf_app_lock();
	zassert_ok(zf_wifi_scan_start());
	zf_app_unlock();
	zf_wifi_poll();
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_SCAN_DONE, iface, &success, sizeof(success));
	poll_scan_for(ZF_SCAN_COMPLETE);
	finish_request();

	start_test();
	net_mgmt_event_notify_with_info(NET_EVENT_WIFI_CONNECT_RESULT, iface, &success, sizeof(success));
	k_sleep(K_MSEC(20));
	zf_wifi_poll();
	zassert_equal(wifi_state(), 4, "association alone must not mark a join successful");
	zassert_false(zf_wifi_test_joined());
	set_station("candidate", WIFI_STATE_COMPLETED);
	net_mgmt_event_notify(NET_EVENT_IPV4_DHCP_BOUND, iface);
	finish_request();
	poll_for(5);
	zassert_true(zf_wifi_test_joined());
	zf_app_lock();
	zf_wifi_test_cancel();
	zf_app_unlock();
	for (int i = 0; i < 100 && disconnected == 0; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_equal(disconnected, 1);
	zf_wifi_poll();

	connect_error = -ETIMEDOUT;
	start_test();
	zf_app_lock();
	zf_wifi_test_cancel();
	zassert_equal(zf_wifi_test_start((const uint8_t *)"replacement", 11, "new-password"), -EBUSY,
		      "a replacement must wait until the canceled driver connection is quiescent");
	zf_app_unlock();
	finish_request();
	connect_error = 0;
	for (int i = 0; i < 100 && disconnected < 2; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	int start_status = -EBUSY;
	for (int i = 0; i < 100 && start_status == -EBUSY; ++i) {
		zf_wifi_poll();
		zf_app_lock();
		start_status = zf_wifi_test_start((const uint8_t *)"replacement", 11, "new-password");
		zf_app_unlock();
		if (start_status == -EBUSY) {
			k_sleep(K_MSEC(5));
		}
	}
	zassert_ok(start_status, "start %d, scan active %d, scan state %u", start_status, zf_wifi_scan_active(),
		   scan_snapshot().scan_state);
	zf_wifi_poll();
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	net_mgmt_event_notify(NET_EVENT_IPV4_DHCP_BOUND, iface);
	for (int i = 0; i < 10; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	finish_request();
	for (int i = 0; i < 20; ++i) {
		zf_wifi_poll();
		k_sleep(K_MSEC(5));
	}
	zassert_false(zf_wifi_test_joined(), "a late DHCP event must not join an unassociated replacement");
	set_station("replacement", WIFI_STATE_COMPLETED);
	net_mgmt_event_notify(NET_EVENT_IPV4_DHCP_BOUND, iface);
	poll_for(5);
	zassert_true(zf_wifi_test_joined());
}

ZTEST_SUITE(wifi_worker, NULL, NULL, NULL, NULL, NULL);

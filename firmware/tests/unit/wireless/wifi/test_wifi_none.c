#include "unity.h"
#include "zephyrforge.h"
#include "report.h"
#include "ble.h"
#include "wifi.h"
#include "wireless.h"
#include "mock_settings.h"
#include "mock_control.h"

#include <errno.h>

TEST_SOURCE_FILE("wifi_none.c")

static uint8_t ble_state;
static bool ble_failed;
static bool locked;

void setUp(void)
{
	ble_state = ZF_BLE_OFF;
	ble_failed = false;
	locked = false;
}

void tearDown(void)
{
	TEST_ASSERT_FALSE(locked);
}

void zf_lock(void)
{
	TEST_ASSERT_FALSE(locked);
	locked = true;
}

void zf_unlock(void)
{
	TEST_ASSERT_TRUE(locked);
	locked = false;
}

uint8_t zf_ble_state(void)
{
	TEST_ASSERT_TRUE(locked);
	return ble_state;
}

bool zf_ble_failed(void)
{
	TEST_ASSERT_TRUE(locked);
	return ble_failed;
}

void test_disabled_wifi_refuses_driver_operations(void)
{
	uint8_t ssid[ZF_SSID_MAX_BYTES] = {0};
	uint8_t length = 0;
	int8_t rssi = 0;
	uint8_t security = 0;

	TEST_ASSERT_EQUAL_INT(-ENOTSUP, zf_wifi_test_start(ssid, 0, ""));
	TEST_ASSERT_EQUAL_INT(-ENOTSUP, zf_wifi_scan_start());
	TEST_ASSERT_EQUAL_INT(-ENOTSUP, zf_wifi_scan_entry(0, ssid, &length, &rssi, &security));
	TEST_ASSERT_FALSE(zf_wifi_test_active());
	TEST_ASSERT_FALSE(zf_wifi_scan_active());
}

void test_disabled_wifi_report_preserves_ble_and_staged_key_state(void)
{
	uint8_t out[ZF_WIRELESS_SIZE];
	uint8_t flags = 0x40u;
	uint8_t staged = 0x04u;
	uint8_t ssid[] = {'h', 'o', 'm', 'e'};

	ble_state = ZF_BLE_CONNECTED;
	zf_settings_wifi_ssid_bytes_ExpectAnyArgsAndReturn(sizeof(ssid));
	zf_settings_wifi_ssid_bytes_ReturnArrayThruPtr_ssid(ssid, sizeof(ssid));
	zf_control_stage_status_ExpectAnyArgs();
	zf_control_stage_status_ReturnThruPtr_flags(&flags);
	zf_control_stage_status_ReturnThruPtr_staged_state(&staged);
	zf_settings_ble_key_configured_ExpectAndReturn(true);

	zf_wireless_report(out);
	TEST_ASSERT_EQUAL_UINT8(0, out[1]);
	TEST_ASSERT_EQUAL_UINT8(0, out[2]);
	TEST_ASSERT_EQUAL_UINT8(ZF_BLE_CONNECTED, out[3]);
	TEST_ASSERT_EQUAL_HEX8(0x43u, out[4]);
	TEST_ASSERT_EACH_EQUAL_UINT8(0, out + 5, 4);
	TEST_ASSERT_EQUAL_UINT8(sizeof(ssid), out[9]);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(ssid, out + 10, sizeof(ssid));
	TEST_ASSERT_EACH_EQUAL_UINT8(0, out + 42, 5);
	TEST_ASSERT_EQUAL_UINT8(staged, out[47]);
}

void test_disabled_wifi_report_does_not_hide_ble_start_failure(void)
{
	uint8_t out[ZF_WIRELESS_SIZE];

	ble_failed = true;
	zf_settings_wifi_ssid_bytes_ExpectAnyArgsAndReturn(0);
	zf_control_stage_status_ExpectAnyArgs();
	zf_settings_ble_key_configured_ExpectAndReturn(false);

	zf_wireless_report(out);
	TEST_ASSERT_EQUAL_UINT8(0, out[1]);
	TEST_ASSERT_EQUAL_UINT8(1, out[2]);
	TEST_ASSERT_EQUAL_UINT8(ZF_BLE_OFF, out[3]);
	TEST_ASSERT_EQUAL_UINT8(0, out[4]);
}

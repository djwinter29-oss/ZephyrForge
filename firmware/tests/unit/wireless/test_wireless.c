#include "unity.h"
#include "zephyrforge.h"
#include "report.h"
#include "ble.h"
#include "wireless.h"
#include "mock_settings.h"
#include "mock_control.h"
#include "mock_wifi.h"

static bool locked;
static bool ble_failed;

void setUp(void)
{
	locked = false;
	ble_failed = false;
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
	return ZF_BLE_ADVERTISING;
}

bool zf_ble_failed(void)
{
	TEST_ASSERT_TRUE(locked);
	return ble_failed;
}

void test_wifi_status_overrides_detail_and_reports_saved_network(void)
{
	uint8_t out[ZF_WIRELESS_SIZE];
	zf_wireless_t wifi = {.wifi_state = 6, .detail = 5, .ble_state = ZF_BLE_ADVERTISING, .ssid_length = 1};

	wifi.ssid[0] = 'x';
	ble_failed = true;
	zf_wifi_status_ExpectAnyArgsAndReturn(true);
	zf_wifi_status_ReturnThruPtr_status(&wifi);
	zf_control_stage_status_ExpectAnyArgs();
	zf_settings_ble_key_configured_ExpectAndReturn(false);

	zf_wireless_report(out);
	TEST_ASSERT_EQUAL_UINT8(6, out[1]);
	TEST_ASSERT_EQUAL_UINT8(5, out[2]);
	TEST_ASSERT_EQUAL_UINT8(ZF_BLE_ADVERTISING, out[3]);
	TEST_ASSERT_EQUAL_HEX8(0x01u, out[4]);
	TEST_ASSERT_EQUAL_UINT8(1, out[9]);
	TEST_ASSERT_EQUAL_UINT8('x', out[10]);
}

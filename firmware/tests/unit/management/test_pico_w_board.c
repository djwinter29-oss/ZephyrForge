#include "unity.h"
#include "management/control.h"
#include "settings/settings.h"
#include "app/capabilities.h"
#include "protocol/report_codec.h"
#include "management/credential_stage.h"
#include "mock_benchmark.h"
#include "mock_board.h"
#include "mock_led.h"
#include "mock_settings_flash.h"
#include "mock_storage.h"
#include "mock_wifi.h"

#include <string.h>

static int write_calls;

static int flash_read(uint8_t *record, size_t size, int calls)
{
	(void)calls;
	memset(record, 0xFF, size);
	return 0;
}

static int flash_write(const uint8_t *record, size_t size, int calls)
{
	(void)calls;
	(void)record;
	(void)size;
	++write_calls;
	return 0;
}

void zf_app_lock(void)
{
}

void zf_app_unlock(void)
{
}

void zf_app_wakeup(void)
{
}

void setUp(void)
{
	zf_settings_flash_read_Stub(flash_read);
	zf_settings_flash_write_Stub(flash_write);
	write_calls = 0;
	zf_settings_load();
	zf_control_init();
	zf_wifi_test_active_IgnoreAndReturn(false);
	zf_wifi_scan_active_IgnoreAndReturn(false);
	zf_storage_busy_IgnoreAndReturn(false);
	zf_storage_result_read_Ignore();
}

void tearDown(void)
{
}

void test_w_board_advertises_wifi_without_ble_led_or_settings_save(void)
{
	TEST_ASSERT_EQUAL_UINT8(ZF_BOARD_PICO_W, ZF_BOARD_MODEL);
	TEST_ASSERT_EQUAL_HEX8(!ZF_ENABLE_STORAGE ? 0x82 :
			       ZF_ENABLE_STORAGE_BENCHMARK ? 0xB2 : 0x92, ZF_CAPABILITIES);
	TEST_ASSERT_EQUAL_HEX8(ZF_WIRELESS_WIFI, ZF_RADIO_MASK);
}

void test_w_board_admits_wifi_scans_but_rejects_key_staging_and_flash_save(void)
{
	uint8_t payload[ZF_CONTROL_SIZE] = {1, 1, ZF_OP_SCAN};
	uint8_t result[ZF_CONTROL_SIZE];

	zf_wifi_scan_start_ExpectAndReturn(0);
	TEST_ASSERT_TRUE(zf_control_usb_set(payload, sizeof(payload), 0));
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, result[3]);
	const uint8_t unsupported[] = {ZF_OP_STAGE_KEY, ZF_OP_SAVE, ZF_OP_LED};
	for (size_t i = 0; i < sizeof(unsupported); ++i) {
		payload[1]++;
		payload[2] = unsupported[i];
		TEST_ASSERT_TRUE(zf_control_usb_set(payload, sizeof(payload), 0));
		zf_control_usb_get(result);
		TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_UNSUPPORTED, result[3]);
		TEST_ASSERT_EQUAL_UINT8(ZF_ERROR_CAPABILITY, result[4]);
	}
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

#include "unity.h"
#include "control.h"
#include "boot_config.h"
#include "boot_flash.h"
#include "hid_reports.h"
#include "wireless_stage.h"
#include "mock_zf_platform.h"

#include <string.h>

static int write_calls;

int boot_flash_read(uint8_t *record, size_t size)
{
	memset(record, 0xFF, size);
	return 0;
}

int boot_flash_write(const uint8_t *record, size_t size)
{
	(void)record;
	(void)size;
	++write_calls;
	return 0;
}

void setUp(void)
{
	write_calls = 0;
	zf_boot_config_load();
	zf_control_init();
	zf_wifi_test_active_IgnoreAndReturn(false);
	zf_wifi_scan_active_IgnoreAndReturn(false);
	zf_platform_storage_busy_IgnoreAndReturn(false);
}

void tearDown(void)
{
}

void test_w_board_reports_wifi_without_ble_led_or_settings_save(void)
{
	const zf_identity_t identity = {0};
	const zf_config_t config = {.stored_radios = ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE};
	uint8_t identity_out[ZF_IDENTITY_SIZE];
	uint8_t config_out[ZF_CONFIG_SIZE];

	zf_report_identity(&identity, identity_out);
	TEST_ASSERT_EQUAL_UINT8(ZF_BOARD_PICO_W, identity_out[15]);
	TEST_ASSERT_EQUAL_HEX8(!ZF_ENABLE_STORAGE ? 0x82 :
			       ZF_ENABLE_STORAGE_BENCHMARK ? 0xB2 : 0x92, identity_out[17]);
	zf_report_config(&config, config_out);
	TEST_ASSERT_EQUAL_HEX8(0x0C, config_out[13]);
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

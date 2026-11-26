#include "unity.h"
#include "management/control.h"
#include "settings/settings.h"
#include "protocol/report_codec.h"
#include "management/credential_stage.h"
#include "mock_benchmark.h"
#include "mock_board.h"
#include "mock_led.h"
#include "mock_settings_flash.h"
#include "mock_storage.h"
#include "mock_wifi.h"

#include <string.h>

static uint8_t flash_record[ZF_SETTINGS_RECORD_SIZE];

/* Built with settings and the BLE-only radio override, ZF_RADIO_MASK=2u. The image
 * starts Bluetooth but never Wi-Fi, so Wi-Fi opcodes must be refused at capability admission, exactly
 * as the STAGE_KEY twin refuses BLE opcodes when the BLE bit is clear. */

static int flash_read(uint8_t *record, size_t size, int calls)
{
	(void)calls;
	memcpy(record, flash_record, size);
	return 0;
}

static int flash_write(const uint8_t *record, size_t size, int calls)
{
	(void)calls;
	memcpy(flash_record, record, size);
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

static int settings_write(const uint8_t *record, size_t size, zf_settings_done_t done, int calls)
{
	(void)calls;
	done(zf_settings_flash_write(record, size));
	return 0;
}

void setUp(void)
{
	zf_settings_flash_read_Stub(flash_read);
	zf_settings_flash_write_Stub(flash_write);
	memset(flash_record, 0xFF, sizeof(flash_record));
	zf_settings_load();
	zf_control_init();
	zf_settings_flash_write_async_Stub(settings_write);
	zf_storage_busy_IgnoreAndReturn(false);
	zf_storage_result_read_Ignore();
	zf_wifi_test_active_IgnoreAndReturn(false);
	zf_wifi_scan_active_IgnoreAndReturn(false);
}

void tearDown(void)
{
}

static const uint8_t *usb(uint8_t opcode, const uint8_t *args, size_t args_size)
{
	static uint8_t result[ZF_CONTROL_SIZE];
	static uint8_t transaction;
	uint8_t payload[ZF_CONTROL_SIZE] = {1, ++transaction, opcode, 0};

	if (args_size) {
		memcpy(payload + 4, args, args_size);
	}
	TEST_ASSERT_TRUE(zf_control_usb_set(payload, sizeof(payload), 0));
	zf_control_usb_get(result);
	return result;
}

void test_wifi_radio_opcodes_are_unsupported_on_ble_only_mask(void)
{
	static const uint8_t opcodes[] = {ZF_OP_STAGE_SSID, ZF_OP_STAGE_PASSWORD, ZF_OP_TEST_WIFI,
					  ZF_OP_CANCEL_TEST, ZF_OP_SCAN, ZF_OP_SCAN_ENTRY};
	uint8_t args[64] = {4, 'h', 'o', 'm', 'e'};

	for (size_t i = 0; i < sizeof(opcodes); ++i) {
		const uint8_t *result = usb(opcodes[i], args, sizeof(args));

		TEST_ASSERT_EQUAL_UINT8_MESSAGE(opcodes[i], result[2], "opcode echo");
		TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_UNSUPPORTED, result[3]);
		TEST_ASSERT_EQUAL_UINT8(ZF_ERROR_CAPABILITY, result[4]);
	}
}

void test_stage_key_remains_admissible_on_ble_only_mask(void)
{
	uint8_t key_args[64] = {32};
	const uint8_t *result;

	memset(key_args + 1, 0x5A, 32);
	result = usb(ZF_OP_STAGE_KEY, key_args, sizeof(key_args));
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, result[3]);
}

void test_save_rejects_enabling_a_radio_omitted_from_the_image(void)
{
	const uint8_t args[4] = {0, 0, 0, ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE};
	const uint8_t *result = usb(ZF_OP_SAVE, args, sizeof(args));

	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_INVALID, result[3]);
	TEST_ASSERT_EQUAL_UINT8(ZF_ERROR_ARGUMENT, result[4]);
}

void test_usb_save_changes_only_the_built_radio_policy(void)
{
	const uint8_t args[4] = {0};
	const uint8_t *result;

	zf_board_schedule_reboot_ExpectAndReturn(0);
	result = usb(ZF_OP_SAVE, args, sizeof(args));
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, result[3]);
	TEST_ASSERT_EQUAL_UINT8(0, result[8]);
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI, zf_settings_radios());
	zf_settings_load();
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI, zf_settings_radios());
}

void test_ble_save_accepts_reported_radios_and_preserves_omitted_radio_settings(void)
{
	const uint8_t request[ZF_BLE_CONTROL_SIZE] = {1, 1, ZF_OP_SAVE, 0, 0, 0, 0, ZF_WIRELESS_BLE, 0};
	uint8_t result[ZF_BLE_CONTROL_SIZE];

	zf_board_schedule_reboot_ExpectAndReturn(0);
	TEST_ASSERT_TRUE(zf_control_ble_execute(request, 0));
	zf_control_ble_get(result);
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, result[3]);
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, zf_settings_radios());
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_BLE, result[8]);
}

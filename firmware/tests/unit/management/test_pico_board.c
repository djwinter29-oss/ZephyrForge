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

#include <errno.h>
#include <string.h>

/* Built with the Raspberry Pi Pico board definitions: RP2040 Pico, no LED, settings or radios.
 * The monitor UART console facts ride the config struct sampled from the devicetree. */

static uint8_t flash[ZF_SETTINGS_RECORD_SIZE];
static int write_calls;

static int flash_read(uint8_t *record, size_t size, int calls)
{
	(void)calls;
	memcpy(record, flash, size);
	return 0;
}

static int flash_write(const uint8_t *record, size_t size, int calls)
{
	(void)calls;
	++write_calls;
	memcpy(flash, record, size);
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
	memset(flash, 0xFF, sizeof(flash));
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

void test_image_advertises_pico_without_led_settings_or_radios(void)
{
	TEST_ASSERT_EQUAL_UINT8(ZF_MCU_RP2040, ZF_MCU);
	TEST_ASSERT_EQUAL_UINT8(ZF_BOARD_PICO, ZF_BOARD_MODEL);
	TEST_ASSERT_EQUAL_HEX8(!ZF_ENABLE_STORAGE ? 0x02 :
			       ZF_ENABLE_STORAGE_BENCHMARK ? 0x32 : 0x12, ZF_CAPABILITIES);
	TEST_ASSERT_EQUAL_HEX8(0, ZF_RADIO_MASK);
}

void test_led_save_and_radio_opcodes_are_unsupported_without_touching_hardware(void)
{
	const uint8_t opcodes[] = {ZF_OP_LED, ZF_OP_SAVE, ZF_OP_STAGE_SSID, ZF_OP_STAGE_PASSWORD, ZF_OP_STAGE_KEY,
				   ZF_OP_TEST_WIFI, ZF_OP_CANCEL_TEST, ZF_OP_SCAN, ZF_OP_SCAN_ENTRY};

	for (size_t i = 0; i < sizeof(opcodes); i++) {
		uint8_t payload[ZF_CONTROL_SIZE] = {1, (uint8_t)(i + 1), opcodes[i]};
		uint8_t result[ZF_CONTROL_SIZE];

		TEST_ASSERT_TRUE(zf_control_usb_set(payload, sizeof(payload), 0));
		zf_control_usb_get(result);
		TEST_ASSERT_EQUAL_UINT8(opcodes[i], result[2]);
		TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_UNSUPPORTED, result[3]);
		TEST_ASSERT_EQUAL_UINT8(ZF_ERROR_CAPABILITY, result[4]);
	}
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

static void saved(int err)
{
	(void)err;
}

void test_settings_remain_readable_but_saving_is_not_built(void)
{
	uint32_t crc;

	memset(flash, 0, sizeof(flash));
	flash[0] = 0x45;
	flash[1] = 0x46;
	flash[2] = 0x43;
	flash[3] = 0x46;
	flash[4] = 1;
	flash[6] = ZF_SETTINGS_RECORD_SIZE;
	flash[8] = ZF_SYSCLK_80MHZ;
	crc = zf_settings_crc32(flash, 144);
	for (size_t i = 0; i < sizeof(crc); ++i) {
		flash[144 + i] = (uint8_t)(crc >> (8u * i));
	}
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_boot_sysclk());
	TEST_ASSERT_EQUAL_INT(-ENOTSUP, zf_settings_save_async(saved));
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

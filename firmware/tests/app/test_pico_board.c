#include "unity.h"
#include "control.h"
#include "boot_config.h"
#include "boot_flash.h"
#include "hid_reports.h"
#include "wireless_stage.h"
#include "mock_zf_platform.h"

#include <string.h>

/* Built with the Raspberry Pi Pico board definitions: RP2040 Pico, no LED, settings or radios.
 * The monitor UART console facts ride the config struct sampled from the devicetree. */

static uint8_t flash[ZF_BOOT_CONFIG_RECORD_SIZE];
static int write_calls;

int boot_flash_read(uint8_t *record, size_t size)
{
	memcpy(record, flash, size);
	return 0;
}

int boot_flash_write(const uint8_t *record, size_t size)
{
	++write_calls;
	memcpy(flash, record, size);
	return 0;
}

void zf_lock(void)
{
}

void zf_unlock(void)
{
}

void setUp(void)
{
	memset(flash, 0xFF, sizeof(flash));
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

void test_identity_and_config_advertise_pico_without_led_settings_or_radios(void)
{
	const zf_identity_t identity = {0};
	const zf_config_t config = {.stored_radios = ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE,
					    .monitor_uart_tx_gpio = 0, .monitor_uart_rx_gpio = 1};
	uint8_t identity_out[ZF_IDENTITY_SIZE];
	uint8_t config_out[ZF_CONFIG_SIZE];

	zf_report_identity(&identity, identity_out);
	TEST_ASSERT_EQUAL_UINT8(1, identity_out[14]);
	TEST_ASSERT_EQUAL_UINT8(2, identity_out[15]);
	TEST_ASSERT_EQUAL_HEX8(!ZF_ENABLE_STORAGE ? 0x02 :
			       ZF_ENABLE_STORAGE_BENCHMARK ? 0x32 : 0x12, identity_out[17]);
	zf_report_config(&config, config_out);
	TEST_ASSERT_EQUAL_UINT8(0, config_out[5]);
	TEST_ASSERT_EQUAL_UINT8(1, config_out[6]);
	TEST_ASSERT_EQUAL_HEX8(0x04, config_out[13]);
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

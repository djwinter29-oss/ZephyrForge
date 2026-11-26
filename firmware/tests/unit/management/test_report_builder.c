#include "unity.h"
#include "management/report_builder.h"
#include "protocol/report_codec.h"
#include "app/capabilities.h"
#include "app/version.h"
#include "mock_benchmark.h"
#include "mock_board.h"
#include "mock_led.h"
#include "mock_runtime.h"
#include "mock_settings.h"
#include "mock_storage.h"
#include "mock_telemetry.h"
#include "mock_usb_device.h"
#include "mock_wireless_status.h"


void test_report_builder_dispatches_shared_and_transport_independent_payloads(void)
{
	uint8_t out[ZF_CONTROL_SIZE];

	zf_board_identity_snapshot_ExpectAnyArgs();
	TEST_ASSERT_EQUAL_UINT16(ZF_IDENTITY_SIZE, zf_management_report_build(ZF_REPORT_IDENTITY, 0, out));
	zf_board_config_snapshot_ExpectAnyArgs();
	zf_app_lock_Expect();
	zf_settings_sysclk_ExpectAndReturn(ZF_SYSCLK_DEFAULT);
	zf_settings_radios_ExpectAndReturn(0);
	zf_app_unlock_Expect();
	TEST_ASSERT_EQUAL_UINT16(ZF_CONFIG_SIZE, zf_management_report_build(ZF_REPORT_CONFIG, 0, out));
	zf_board_memory_snapshot_ExpectAnyArgs();
	TEST_ASSERT_EQUAL_UINT16(ZF_MEMORY_SIZE, zf_management_report_build(ZF_REPORT_MEMORY, 0, out));

	zf_led_status_ExpectAnyArgs();
	zf_usb_device_configured_ExpectAndReturn(true);
	zf_telemetry_report_ExpectAnyArgs();
	TEST_ASSERT_EQUAL_UINT16(ZF_TELEMETRY_SIZE, zf_management_report_build(ZF_REPORT_TELEMETRY, 7, out));

	zf_storage_report_ExpectAnyArgs();
	TEST_ASSERT_EQUAL_UINT16(ZF_STORAGE_SIZE, zf_management_report_build(ZF_REPORT_STORAGE, 8, out));
#if ZF_ENABLE_STORAGE_BENCHMARK
	zf_benchmark_report_ExpectAnyArgs();
	TEST_ASSERT_EQUAL_UINT16(ZF_BENCHMARK_SIZE, zf_management_report_build(ZF_REPORT_BENCHMARK, 9, out));
#else
	TEST_ASSERT_EQUAL_UINT16(0, zf_management_report_build(ZF_REPORT_BENCHMARK, 9, out));
#endif
	zf_management_wireless_report_ExpectAnyArgs();
	TEST_ASSERT_EQUAL_UINT16(ZF_WIRELESS_SIZE, zf_management_report_build(ZF_REPORT_WIRELESS, 0, out));

	TEST_ASSERT_EQUAL_UINT16(0, zf_management_report_build(ZF_REPORT_CONTROL, 0, out));
	TEST_ASSERT_EQUAL_UINT16(0, zf_management_report_build(0xFF, 0, out));
}

void test_identity_assembly_preserves_hardware_facts_and_supplies_the_build_version(void)
{
	zf_identity_t hardware = {
		.uid = {1, 2, 3, 4, 5, 6, 7, 8},
		.firmware = {99, 99, 99},
		.ram_total_bytes = 524288,
		.ram_reserved_bytes = 123456,
	};
	uint8_t out[ZF_IDENTITY_SIZE];
	uint8_t expected[ZF_IDENTITY_SIZE];

	zf_board_identity_snapshot_ExpectAnyArgs();
	zf_board_identity_snapshot_ReturnThruPtr_out(&hardware);
	TEST_ASSERT_EQUAL_UINT16(sizeof(out), zf_management_report_build(ZF_REPORT_IDENTITY, 0, out));
	hardware.firmware[0] = ZEPHYRFORGE_VERSION_MAJOR;
	hardware.firmware[1] = ZEPHYRFORGE_VERSION_MINOR;
	hardware.firmware[2] = ZEPHYRFORGE_VERSION_PATCH;
	hardware.mcu = ZF_MCU;
	hardware.board_model = ZF_BOARD_MODEL;
	hardware.capabilities = ZF_CAPABILITIES;
	zf_report_encode_identity(&hardware, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, out, sizeof(out));
}

void test_configuration_assembly_merges_saved_policy_with_board_pins(void)
{
	zf_config_t hardware = {
		.monitor_uart_index = 1,
		.monitor_uart_tx_gpio = 17,
		.monitor_uart_rx_gpio = 18,
		.storage_gpios = {1, 2, 3, 4, 5, 6},
		.card_detect_available = true,
		.card_detect_gpio = 7,
		.msc_supported = true,
	};
	uint8_t out[ZF_CONFIG_SIZE];
	uint8_t expected[ZF_CONFIG_SIZE];

	zf_board_config_snapshot_ExpectAnyArgs();
	zf_board_config_snapshot_ReturnThruPtr_out(&hardware);
	zf_app_lock_Expect();
	zf_settings_sysclk_ExpectAndReturn(ZF_SYSCLK_160MHZ);
	zf_settings_radios_ExpectAndReturn(3);
	zf_app_unlock_Expect();
	TEST_ASSERT_EQUAL_UINT16(sizeof(out), zf_management_report_build(ZF_REPORT_CONFIG, 0, out));
	hardware.stored_clock_mhz = 160;
	hardware.stored_radios = 3 & ZF_RADIO_MASK;
	zf_report_encode_config(&hardware, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, out, sizeof(out));
}

void test_report_size_matches_supported_payloads_and_rejects_unknown_ids(void)
{
	TEST_ASSERT_EQUAL_UINT16(ZF_IDENTITY_SIZE, zf_management_report_size(ZF_REPORT_IDENTITY));
	TEST_ASSERT_EQUAL_UINT16(ZF_CONFIG_SIZE, zf_management_report_size(ZF_REPORT_CONFIG));
	TEST_ASSERT_EQUAL_UINT16(ZF_MEMORY_SIZE, zf_management_report_size(ZF_REPORT_MEMORY));
	TEST_ASSERT_EQUAL_UINT16(ZF_TELEMETRY_SIZE, zf_management_report_size(ZF_REPORT_TELEMETRY));
	TEST_ASSERT_EQUAL_UINT16(ZF_STORAGE_SIZE, zf_management_report_size(ZF_REPORT_STORAGE));
	TEST_ASSERT_EQUAL_UINT16(ZF_ENABLE_STORAGE_BENCHMARK ? ZF_BENCHMARK_SIZE : 0,
				 zf_management_report_size(ZF_REPORT_BENCHMARK));
	TEST_ASSERT_EQUAL_UINT16(ZF_WIRELESS_SIZE, zf_management_report_size(ZF_REPORT_WIRELESS));
	TEST_ASSERT_EQUAL_UINT16(ZF_CONTROL_SIZE, zf_management_report_size(ZF_REPORT_CONTROL));
	TEST_ASSERT_EQUAL_UINT16(0, zf_management_report_size(0xFF));
}

void test_identity_advertises_only_modules_built_into_the_image(void)
{
	uint8_t expected = 0x97;
	uint8_t out[ZF_IDENTITY_SIZE];

#if !ZF_ENABLE_STORAGE
	expected = 0x87;
#elif ZF_ENABLE_STORAGE_BENCHMARK
	expected = 0xB7;
#endif
#if ZF_ENABLE_STORAGE && ZF_STORAGE_SPI
	expected = (expected & ~ZF_CAP_SDMMC) | ZF_CAP_SPI;
#endif
	zf_board_identity_snapshot_ExpectAnyArgs();
	zf_management_report_build(ZF_REPORT_IDENTITY, 0, out);
	TEST_ASSERT_EQUAL_UINT8(ZF_MCU_ESP32S3, out[14]);
	TEST_ASSERT_EQUAL_UINT8(ZF_BOARD_DEVKITC, out[15]);
	TEST_ASSERT_EQUAL_HEX8(expected, out[17]);
}

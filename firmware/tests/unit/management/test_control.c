#include "unity.h"
#include "management/control.h"
#include "app/build_config.h"
#include "settings/settings.h"
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

static uint8_t flash[ZF_SETTINGS_RECORD_SIZE];
static int write_result;
static int write_calls;
static uint8_t txn;
static uint8_t queued[ZF_SETTINGS_RECORD_SIZE];
static zf_settings_done_t queued_done;
static bool defer_writes;
static bool locked;
static int settings_save_result;

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
	if (write_result == 0) {
		memcpy(flash, record, size);
	}
	return write_result;
}

void zf_app_lock(void)
{
	TEST_ASSERT_FALSE(locked);
	locked = true;
}

void zf_app_unlock(void)
{
	TEST_ASSERT_TRUE(locked);
	locked = false;
}

void zf_app_wakeup(void)
{
}

/* Run the queued settings work item: write without the application lock, then report completion. */
static void complete_write(void)
{
	zf_settings_done_t done = queued_done;

	TEST_ASSERT_NOT_NULL(done);
	TEST_ASSERT_FALSE(locked);
	queued_done = NULL;
	done(zf_settings_flash_write(queued, sizeof(queued)));
	TEST_ASSERT_FALSE(locked);
}

static int settings_write(const uint8_t *record, size_t size, zf_settings_done_t done, int calls)
{
	(void)calls;
	TEST_ASSERT_EQUAL_size_t(sizeof(queued), size);
	TEST_ASSERT_NULL(queued_done);
	memcpy(queued, record, size);
	queued_done = done;
	if (!defer_writes) {
		complete_write();
	}
	return 0;
}

static void settings_saved(int err)
{
	settings_save_result = err;
}

static int save_settings(void)
{
	settings_save_result = -EINPROGRESS;
	int err = zf_settings_save_async(settings_saved);

	return err != 0 ? err : settings_save_result;
}

void setUp(void)
{
	zf_settings_flash_read_Stub(flash_read);
	zf_settings_flash_write_Stub(flash_write);
	zf_settings_flash_write_async_Stub(settings_write);
	memset(flash, 0xFF, sizeof(flash));
	write_result = 0;
	write_calls = 0;
	txn = 0;
	queued_done = NULL;
	defer_writes = false;
	zf_settings_load();
	TEST_ASSERT_TRUE(zf_settings_set_wifi_ssid("home"));
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password("saved-password"));
	TEST_ASSERT_EQUAL_INT(0, save_settings());
	write_calls = 0;
	queued_done = NULL;
	defer_writes = false;
	zf_control_init();
	zf_wifi_test_active_IgnoreAndReturn(false);
	zf_wifi_scan_active_IgnoreAndReturn(false);
	zf_storage_busy_IgnoreAndReturn(false);
	zf_storage_result_read_Ignore();
}

void tearDown(void)
{
}

/* Send a USB Feature 20 request and return the result. */
static const uint8_t *usb(uint8_t opcode, uint8_t flags, const uint8_t *args, size_t args_size, int64_t now)
{
	static uint8_t result[ZF_CONTROL_SIZE];
	uint8_t payload[ZF_CONTROL_SIZE] = {1, ++txn ? txn : ++txn, opcode, flags};

	if (args_size) {
		memcpy(payload + 4, args, args_size);
	}
	TEST_ASSERT_TRUE(zf_control_usb_set(payload, sizeof(payload), now));
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(1, result[0]);
	TEST_ASSERT_EQUAL_UINT8(payload[1], result[1]);
	TEST_ASSERT_EQUAL_UINT8(opcode, result[2]);
	return result;
}

static const uint8_t *ble(uint8_t opcode, uint8_t flags, const uint8_t *args, int64_t now)
{
	static uint8_t result[ZF_BLE_CONTROL_SIZE];
	uint8_t request[ZF_BLE_CONTROL_SIZE] = {1, ++txn ? txn : ++txn, opcode, flags};

	if (args) {
		memcpy(request + 4, args, 5);
	}
	TEST_ASSERT_TRUE(zf_control_ble_envelope(request, sizeof(request)));
	TEST_ASSERT_TRUE(zf_control_ble_execute(request, now));
	zf_control_ble_get(result);
	TEST_ASSERT_EQUAL_UINT8(request[1], result[1]);
	return result;
}

static void assert_result(const uint8_t *result, uint8_t status, uint8_t error, uint8_t detail)
{
	TEST_ASSERT_EQUAL_UINT8_MESSAGE(status, result[3], "status");
	TEST_ASSERT_EQUAL_UINT8_MESSAGE(error, result[4], "error");
	TEST_ASSERT_EQUAL_UINT8_MESSAGE(detail, result[5], "detail");
}

static const uint8_t *stage(uint8_t opcode, const char *text, int64_t now)
{
	uint8_t args[64] = {(uint8_t)strlen(text)};

	memcpy(args + 1, text, args[0]);
	return usb(opcode, 0, args, sizeof(args), now);
}

void test_envelope_rejections(void)
{
	uint8_t payload[ZF_CONTROL_SIZE] = {1, 0, ZF_OP_REBOOT, 1};

	TEST_ASSERT_FALSE(zf_control_usb_set(payload, sizeof(payload), 0));
	payload[1] = 1;
	payload[0] = 2;
	TEST_ASSERT_FALSE(zf_control_usb_set(payload, sizeof(payload), 0));
	payload[0] = 1;
	TEST_ASSERT_FALSE(zf_control_usb_set(payload, sizeof(payload) - 1, 0));
	TEST_ASSERT_FALSE(zf_control_ble_envelope(payload, 8));
	TEST_ASSERT_FALSE(zf_control_ble_envelope((const uint8_t[9]){1, 0}, 9));
}

void test_initial_result_is_idle(void)
{
	uint8_t result[ZF_CONTROL_SIZE];

	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(1, result[0]);
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_IDLE, result[3]);
}

void test_unassigned_and_unsupported_opcodes(void)
{
	assert_result(usb(0, 0, NULL, 0, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_OPCODE, 0);
	assert_result(usb(14, 0, NULL, 0, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_OPCODE, 0);
	assert_result(usb(ZF_OP_SCAN_ENTRY + 1, 0, NULL, 0, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_OPCODE, 0);
#if !ZF_ENABLE_STORAGE_BENCHMARK
	assert_result(usb(ZF_OP_BENCHMARK, 0, NULL, 0, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
#endif
}

void test_msc_is_unsupported_when_storage_or_usb_export_is_not_built(void)
{
#if !ZF_ENABLE_STORAGE || !ZF_ENABLE_USB_MSC
	const uint8_t enable[1] = {1};

	assert_result(usb(ZF_OP_MSC, 0, enable, sizeof(enable), 0),
		      ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
#else
	TEST_IGNORE_MESSAGE("MSC is built for this configuration");
#endif
}

void test_led_admission_and_invalid_arguments(void)
{
	const uint8_t blue = 4;
	const uint8_t invalid = 8;
	const uint8_t tail[] = {1, 1};

	zf_led_request_ExpectAndReturn(blue, 0);
	assert_result(usb(ZF_OP_LED, 0, &blue, 1, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_led_request_ExpectAndReturn(blue, -EBUSY);
	assert_result(usb(ZF_OP_LED, 0, &blue, 1, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, 0);
	zf_led_request_ExpectAndReturn(blue, -ENODEV);
	assert_result(usb(ZF_OP_LED, 0, &blue, 1, 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	assert_result(usb(ZF_OP_LED, 0, &invalid, 1, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	assert_result(usb(ZF_OP_LED, 1, &blue, 1, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(usb(ZF_OP_LED, 0, tail, sizeof(tail), 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
}

void test_benchmark_admission_size_and_transport_rules(void)
{
#if ZF_ENABLE_STORAGE_BENCHMARK
	const uint8_t start[5] = {1, 32, 0, 0, 0};
	const uint8_t invalid[5] = {1, 1, 4, 0, 0};

	zf_benchmark_request_ExpectAndReturn(true, 32, 0);
	assert_result(usb(ZF_OP_BENCHMARK, 0, start, sizeof(start), 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_benchmark_request_ExpectAndReturn(false, 0, 0);
	assert_result(usb(ZF_OP_BENCHMARK, 0, NULL, 0, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	assert_result(usb(ZF_OP_BENCHMARK, 0, invalid, sizeof(invalid), 0),
		      ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 3);
	assert_result(usb(ZF_OP_BENCHMARK, 1, start, sizeof(start), 0),
		      ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	zf_benchmark_request_ExpectAndReturn(true, 32, 1);
	assert_result(usb(ZF_OP_BENCHMARK, 0, start, sizeof(start), 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, 1);
	zf_benchmark_request_ExpectAndReturn(true, 32, 2);
	assert_result(usb(ZF_OP_BENCHMARK, 0, start, sizeof(start), 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, 2);
	zf_benchmark_request_ExpectAndReturn(true, 32, 4);
	assert_result(usb(ZF_OP_BENCHMARK, 0, start, sizeof(start), 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 4);
#endif
	assert_result(ble(ZF_OP_BENCHMARK, 0, NULL, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
}

void test_msc_requires_confirmed_return_and_rejects_malformed_or_unsafe_actions(void)
{
#if ZF_ENABLE_STORAGE && ZF_ENABLE_USB_MSC
	const uint8_t enable[1] = {1};
	const uint8_t invalid[1] = {2};

	assert_result(usb(ZF_OP_MSC, 0, NULL, 0, 0), ZF_STATUS_INVALID, ZF_ERROR_CONFIRMATION, 0);
	assert_result(usb(ZF_OP_MSC, 1, enable, 1, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(usb(ZF_OP_MSC, 1, invalid, 1, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	zf_storage_request_ExpectAnyArgsAndReturn(0);
	assert_result(usb(ZF_OP_MSC, 0, enable, 1, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_storage_request_ExpectAnyArgsAndReturn(-EBUSY);
	assert_result(usb(ZF_OP_MSC, 1, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, 0);
	zf_storage_request_ExpectAnyArgsAndReturn(-ENOMEDIUM);
	assert_result(usb(ZF_OP_MSC, 0, enable, 1, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	zf_storage_request_ExpectAnyArgsAndReturn(-EIO);
	assert_result(usb(ZF_OP_MSC, 0, enable, 1, 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	assert_result(ble(ZF_OP_MSC, 0, enable, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
#else
	TEST_IGNORE_MESSAGE("MSC is not built for this configuration");
#endif
}

void test_reboot_and_save_do_not_run_while_storage_is_busy_or_usb_owned(void)
{
	const uint8_t settings[4] = {240, 0, 0, 3};

	zf_storage_busy_StopIgnore();
	zf_storage_busy_ExpectAndReturn(true);
	zf_storage_busy_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, 1);
	assert_result(usb(ZF_OP_SAVE, 0, settings, sizeof(settings), 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, 1);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

void test_reboot_requires_confirmation_and_clean_args(void)
{
	const uint8_t junk[1] = {1};

	assert_result(usb(ZF_OP_REBOOT, 0, NULL, 0, 0), ZF_STATUS_INVALID, ZF_ERROR_CONFIRMATION, 0);
	assert_result(usb(ZF_OP_REBOOT, 3, NULL, 0, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(usb(ZF_OP_REBOOT, 1, junk, 1, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	zf_board_schedule_reboot_ExpectAndReturn(-EIO);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 10), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

void test_scheduled_usb_reboot_reserves_storage_across_transports_and_keeps_reads_available(void)
{
	const uint8_t save_args[5] = {80, 0, 0, 3, 0};
	uint8_t result[ZF_CONTROL_SIZE];

	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, result[3]);
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, result[3]);
#if ZF_ENABLE_STORAGE && ZF_ENABLE_USB_MSC
	const uint8_t export[1] = {1};

	assert_result(usb(ZF_OP_MSC, 0, export, sizeof(export), 10),
		      ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
#endif
#if ZF_ENABLE_STORAGE_BENCHMARK
	const uint8_t start[5] = {1, 32, 0, 0, 0};

	assert_result(usb(ZF_OP_BENCHMARK, 0, start, sizeof(start), 10),
		      ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
#endif
	assert_result(ble(ZF_OP_REBOOT, 1, NULL, 10), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
	assert_result(ble(ZF_OP_SAVE, 0, save_args, 10), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
	assert_result(usb(ZF_OP_REBOOT, 0, NULL, 0, 10), ZF_STATUS_INVALID, ZF_ERROR_CONFIRMATION, 0);
	assert_result(usb(ZF_OP_SAVE, 1, NULL, 0, 10), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
	zf_wifi_scan_state_ExpectAndReturn(ZF_SCAN_IDLE);
	assert_result(usb(ZF_OP_SCAN_ENTRY, 0, NULL, 0, 10),
		      ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_SCAN_STALE);
}

void test_scheduled_ble_save_reserves_usb_storage_and_preserves_its_result(void)
{
	const uint8_t args[5] = {80, 0, 0, 3, 0};
	uint8_t result[ZF_BLE_CONTROL_SIZE];

	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(ble(ZF_OP_SAVE, 0, args, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
#if ZF_ENABLE_STORAGE && ZF_ENABLE_USB_MSC
	const uint8_t export[1] = {1};

	assert_result(usb(ZF_OP_MSC, 0, export, sizeof(export), 10),
		      ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
#endif
#if ZF_ENABLE_STORAGE_BENCHMARK
	const uint8_t start[5] = {1, 32, 0, 0, 0};

	assert_result(usb(ZF_OP_BENCHMARK, 0, start, sizeof(start), 10),
		      ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
#endif
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 10), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
	assert_result(usb(ZF_OP_SAVE, 0, args, 4, 10), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
	TEST_ASSERT_EQUAL_INT(1, write_calls);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_sysclk());
	zf_control_ble_get(result);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
}

void test_save_settings_without_commit(void)
{
	const uint8_t args[4] = {160, 0, 0, ZF_WIRELESS_BLE};
	const uint8_t *result;

	zf_board_schedule_reboot_ExpectAndReturn(0);
	result = usb(ZF_OP_SAVE, 0, args, sizeof(args), 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 160);
	TEST_ASSERT_EQUAL_UINT8(0, result[6]);
	TEST_ASSERT_EQUAL_UINT8(0, result[7]);
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_BLE, result[8]);
	TEST_ASSERT_EQUAL_UINT8(0, result[9]);
	TEST_ASSERT_EQUAL_INT(1, write_calls);
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_160MHZ, zf_settings_sysclk());
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_BLE, zf_settings_radios());
}

void test_save_rejects_bad_arguments_and_forced_flag(void)
{
	const uint8_t bad_clock[4] = {100, 0, 0, 3};
	const uint8_t sd_profile[4] = {0, 0, 1, 3};
	const uint8_t switch_bits[4] = {0, 0, 0, 7};
	uint8_t trailing[5] = {0, 0, 0, 3, 1};

	assert_result(usb(ZF_OP_SAVE, 1, NULL, 0, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
	assert_result(usb(ZF_OP_SAVE, 4, NULL, 0, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(usb(ZF_OP_SAVE, 0, trailing, sizeof(trailing), 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(usb(ZF_OP_SAVE, 0, bad_clock, 4, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	assert_result(usb(ZF_OP_SAVE, 0, sd_profile, 4, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	assert_result(usb(ZF_OP_SAVE, 0, switch_bits, 4, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

void test_save_flash_failure_discards(void)
{
	const uint8_t args[4] = {80, 0, 0, 3};

	write_result = -EIO;
	assert_result(usb(ZF_OP_SAVE, 0, args, 4, 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 10), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

void test_save_reports_persisted_settings_when_reboot_scheduling_fails(void)
{
	const uint8_t args[4] = {80, 0, 0, 3};

	zf_board_schedule_reboot_ExpectAndReturn(-EIO);
	assert_result(usb(ZF_OP_SAVE, 0, args, sizeof(args), 0),
		      ZF_STATUS_FAILED, ZF_ERROR_FAILED, ZF_DETAIL_SAVED_REBOOT_FAILED);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_sysclk());
}

void test_usb_save_is_pending_without_the_lock_and_blocks_reset_class_commands(void)
{
	const uint8_t args[5] = {80, 0, 0, 3, 0};
	uint8_t result[ZF_CONTROL_SIZE];

	defer_writes = true;
	assert_result(usb(ZF_OP_SAVE, 0, args, 4, 0), ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
	assert_result(ble(ZF_OP_SAVE, 0, args, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SAVE_PENDING);
	assert_result(ble(ZF_OP_REBOOT, 1, NULL, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SAVE_PENDING);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	complete_write();
	TEST_ASSERT_EQUAL_INT(1, write_calls);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_sysclk());
	zf_control_usb_get(result);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
	TEST_ASSERT_EQUAL_UINT8(3, result[8]);
}

void test_ble_save_stays_pending_until_the_write_fails_and_discards(void)
{
	const uint8_t args[5] = {80, 0, 0, 3, 0};
	uint8_t result[ZF_BLE_CONTROL_SIZE];

	defer_writes = true;
	write_result = -EIO;
	assert_result(ble(ZF_OP_SAVE, 0, args, 0), ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	complete_write();
	zf_control_ble_get(result);
	assert_result(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 10), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

void test_usb_receives_busy_while_ble_save_is_pending(void)
{
	const uint8_t args[5] = {80, 0, 0, 3, 0};
	const uint8_t request[ZF_CONTROL_SIZE] = {1, 0x61, ZF_OP_REBOOT, 1};
	uint8_t usb_result[ZF_CONTROL_SIZE];
	uint8_t ble_result[ZF_BLE_CONTROL_SIZE];

	defer_writes = true;
	assert_result(ble(ZF_OP_SAVE, 0, args, 0), ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	TEST_ASSERT_TRUE(zf_control_usb_set(request, sizeof(request), 0));
	zf_control_usb_get(usb_result);
	assert_result(usb_result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SAVE_PENDING);
	zf_control_ble_get(ble_result);
	assert_result(ble_result, ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	complete_write();
}

void test_completed_save_result_survives_later_usb_commands(void)
{
	const uint8_t args[4] = {80, 0, 0, 3};
	uint8_t result[ZF_CONTROL_SIZE];
	uint8_t payload[ZF_CONTROL_SIZE] = {1, 0x7Fu, ZF_OP_REBOOT, 1};

	defer_writes = true;
	assert_result(usb(ZF_OP_SAVE, 0, args, 4, 0), ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	TEST_ASSERT_FALSE(zf_control_usb_set(payload, sizeof(payload), 0));
	zf_board_schedule_reboot_ExpectAndReturn(0);
	complete_write();
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(ZF_OP_SAVE, result[2]);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(ZF_OP_SAVE, result[2]);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_sysclk());
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
}

void test_usb_save_result_is_not_replaced_by_a_ble_save(void)
{
	const uint8_t ble_request[ZF_BLE_CONTROL_SIZE] = {1, 0x52, ZF_OP_SAVE, 0, 160, 0, 0, 3, 0};
	uint8_t payload[ZF_CONTROL_SIZE] = {1, 0x51, ZF_OP_SAVE, 0, 80, 0, 0, 3};
	uint8_t result[ZF_CONTROL_SIZE];

	defer_writes = true;
	write_result = -EIO;
	TEST_ASSERT_TRUE(zf_control_usb_set(payload, sizeof(payload), 0));
	complete_write();

	defer_writes = false;
	write_result = 0;
	zf_board_schedule_reboot_ExpectAndReturn(0);
	TEST_ASSERT_TRUE(zf_control_ble_execute(ble_request, 1));
	zf_control_usb_get(result);
	TEST_ASSERT_EQUAL_UINT8(0x51, result[1]);
	assert_result(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
}

void test_unchanged_ble_save_completes_without_a_flash_write(void)
{
	const uint8_t request[ZF_BLE_CONTROL_SIZE] = {1, 0x53, ZF_OP_SAVE, 0, 0, 0, 0,
							ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, 0};
	uint8_t result[ZF_BLE_CONTROL_SIZE];

	TEST_ASSERT_EQUAL_INT(0, write_calls);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	TEST_ASSERT_TRUE(zf_control_ble_execute(request, 1));
	zf_control_ble_get(result);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

void test_staging_is_blocked_while_save_is_in_flight(void)
{
	const uint8_t args[4] = {80, 0, 0, 3};
	uint8_t result[ZF_CONTROL_SIZE];
	uint8_t payload[ZF_CONTROL_SIZE] = {1, 0x7Fu, ZF_OP_STAGE_SSID, 0};

	defer_writes = true;
	assert_result(usb(ZF_OP_SAVE, 0, args, sizeof(args), 0), ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	TEST_ASSERT_FALSE(zf_control_usb_set(payload, sizeof(payload), 0));
	zf_board_schedule_reboot_ExpectAndReturn(0);
	complete_write();
	zf_control_usb_get(result);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
	assert_result(usb(ZF_OP_STAGE_SSID, 0, (const uint8_t[64]){0}, 64, 0),
		      ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_REBOOT_PENDING);
}

void test_save_queue_failure_discards_without_a_write(void)
{
	const uint8_t args[4] = {80, 0, 0, 3};

	zf_settings_flash_write_async_Stub(NULL);
	zf_settings_flash_write_async_ExpectAnyArgsAndReturn(-EIO);
	assert_result(usb(ZF_OP_SAVE, 0, args, 4, 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 10), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

void test_staged_values_require_commit_flag(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};

	assert_result(stage(ZF_OP_STAGE_PASSWORD, "password", 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	assert_result(usb(ZF_OP_SAVE, 0, args, 4, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_COMMIT_FLAG);
}

void test_commit_new_network_and_key(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};
	uint8_t key_args[64] = {32};
	uint8_t key[ZF_BLE_KEY_SIZE];
	char password[ZF_WIFI_PASSWORD_SIZE];
	char ssid[ZF_SSID_BUFFER_SIZE];
	const uint8_t *result;

	memset(key_args + 1, 0x5A, 32);
	stage(ZF_OP_STAGE_SSID, "lab", 0);
	assert_result(usb(ZF_OP_SAVE, 2, args, 4, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_PAIR_INCOMPLETE);
	stage(ZF_OP_STAGE_PASSWORD, "lab-password", 0);
	assert_result(usb(ZF_OP_STAGE_KEY, 0, key_args, sizeof(key_args), 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	result = usb(ZF_OP_SAVE, 2, args, 4, 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	TEST_ASSERT_EQUAL_HEX8(ZF_COMMIT_SSID | ZF_COMMIT_PASSWORD | ZF_COMMIT_KEY, result[9]);
	zf_settings_load();
	zf_settings_wifi_ssid(ssid);
	zf_settings_wifi_password(password);
	zf_settings_ble_key(key);
	TEST_ASSERT_EQUAL_STRING("lab", ssid);
	TEST_ASSERT_EQUAL_STRING("lab-password", password);
	TEST_ASSERT_EACH_EQUAL_HEX8(0x5A, key, sizeof(key));
}

void test_commit_without_staged_values_and_after_expiry(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};
	uint8_t flags;
	uint8_t staged_state;

	assert_result(usb(ZF_OP_SAVE, 2, args, 4, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_NOTHING_STAGED);
	stage(ZF_OP_STAGE_PASSWORD, "password", 0);
	zf_wifi_test_cancel_Expect();
	zf_control_tick(ZF_CREDENTIAL_STAGE_LIFETIME_MS);
	zf_control_staging_status(&flags, &staged_state);
	TEST_ASSERT_EQUAL_HEX8(ZF_STAGED_EXPIRED, staged_state);
	assert_result(usb(ZF_OP_SAVE, 2, args, 4, ZF_CREDENTIAL_STAGE_LIFETIME_MS), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_EXPIRED);
}

void test_stage_validation(void)
{
	uint8_t args[64] = {33};
	uint8_t trailing[64] = {2, 'a', 'b', 'c'};

	assert_result(usb(ZF_OP_STAGE_SSID, 0, args, sizeof(args), 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_SSID_LENGTH);
	assert_result(usb(ZF_OP_STAGE_SSID, 0, trailing, sizeof(trailing), 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED,
		      0);
	assert_result(usb(ZF_OP_STAGE_SSID, 1, NULL, 0, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(stage(ZF_OP_STAGE_PASSWORD, "short", 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_PASSWORD);
	args[0] = 16;
	assert_result(usb(ZF_OP_STAGE_KEY, 0, args, sizeof(args), 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_KEY_LENGTH);
}

void test_stage_busy_during_test(void)
{
	zf_wifi_test_active_StopIgnore();
	zf_wifi_test_active_ExpectAndReturn(true);
	assert_result(stage(ZF_OP_STAGE_SSID, "lab", 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
}

void test_wifi_test_starts_candidate_and_marks_tested(void)
{
	uint8_t flags;
	uint8_t staged_state;

	assert_result(usb(ZF_OP_TEST_WIFI, 0, NULL, 0, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_NOTHING_STAGED);
	stage(ZF_OP_STAGE_PASSWORD, "password", 0);
	zf_wifi_test_start_ExpectAndReturn((const uint8_t *)"home", 4, "password", 0);
	zf_wifi_test_start_IgnoreArg_ssid();
	assert_result(usb(ZF_OP_TEST_WIFI, 0, NULL, 0, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_wifi_test_joined_ExpectAndReturn(true);
	zf_control_tick(5);
	zf_control_staging_status(&flags, &staged_state);
	TEST_ASSERT_EQUAL_HEX8(ZF_CREDENTIAL_STAGE_FLAG_PASSWORD | ZF_CREDENTIAL_STAGE_FLAG_TESTED, flags);
	zf_wifi_test_start_ExpectAndReturn(NULL, 4, "password", -EIO);
	zf_wifi_test_start_IgnoreArg_ssid();
	assert_result(usb(ZF_OP_TEST_WIFI, 0, NULL, 0, 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	zf_wifi_test_start_ExpectAndReturn(NULL, 4, "password", -EBUSY);
	zf_wifi_test_start_IgnoreArg_ssid();
	assert_result(usb(ZF_OP_TEST_WIFI, 0, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
}

void test_wifi_test_and_scan_busy(void)
{
	zf_wifi_test_active_StopIgnore();
	zf_wifi_scan_active_StopIgnore();
	zf_wifi_test_active_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_TEST_WIFI, 0, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
	zf_wifi_test_active_ExpectAndReturn(false);
	zf_wifi_scan_active_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_TEST_WIFI, 0, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
	zf_wifi_test_active_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_SCAN, 0, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
	zf_wifi_test_active_ExpectAndReturn(false);
	zf_wifi_scan_active_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_SCAN, 0, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
}

void test_commit_busy_while_test_active(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};

	stage(ZF_OP_STAGE_PASSWORD, "password", 0);
	zf_wifi_test_active_StopIgnore();
	zf_wifi_test_active_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_SAVE, 2, args, 4, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
}

void test_cancel_test_discards_stage(void)
{
	uint8_t flags;
	uint8_t staged_state;

	stage(ZF_OP_STAGE_PASSWORD, "password", 0);
	zf_wifi_test_cancel_Expect();
	assert_result(usb(ZF_OP_CANCEL_TEST, 0, NULL, 0, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_control_staging_status(&flags, &staged_state);
	TEST_ASSERT_EQUAL_HEX8(0, flags);
	TEST_ASSERT_EQUAL_HEX8(0, staged_state);
}

void test_scan_start_and_entry(void)
{
	const uint8_t stale[3] = {0, 8, 0};
	const uint8_t out_of_range[3] = {2, 7, 0};
	const uint8_t entry[3] = {1, 7, 0};
	uint8_t ssid[ZF_SSID_MAX_BYTES] = {'c', 'a', 'f', 'e'};
	uint8_t length = 4;
	int8_t rssi = -61;
	uint8_t security = 3;
	const uint8_t *result;

	zf_wifi_scan_start_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_SCAN, 0, NULL, 0, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_wifi_scan_state_IgnoreAndReturn(ZF_SCAN_COMPLETE);
	zf_wifi_scan_generation_IgnoreAndReturn(7);
	zf_wifi_scan_count_IgnoreAndReturn(2);
	assert_result(usb(ZF_OP_SCAN_ENTRY, 0, stale, 3, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_SCAN_STALE);
	assert_result(usb(ZF_OP_SCAN_ENTRY, 0, out_of_range, 3, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_SCAN_INDEX);
	zf_wifi_scan_entry_ExpectAnyArgsAndReturn(0);
	zf_wifi_scan_entry_ReturnArrayThruPtr_ssid(ssid, 32);
	zf_wifi_scan_entry_ReturnThruPtr_ssid_length(&length);
	zf_wifi_scan_entry_ReturnThruPtr_rssi(&rssi);
	zf_wifi_scan_entry_ReturnThruPtr_security(&security);
	result = usb(ZF_OP_SCAN_ENTRY, 0, entry, 3, 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 7);
	TEST_ASSERT_EQUAL_UINT8(0, result[6]);
	TEST_ASSERT_EQUAL_UINT8(1, result[7]);
	TEST_ASSERT_EQUAL_UINT8(4, result[8]);
	TEST_ASSERT_EQUAL_MEMORY("cafe", result + 9, 4);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)-61, result[41]);
	TEST_ASSERT_EQUAL_UINT8(3, result[42]);
	zf_wifi_test_active_ExpectAndReturn(false);
	zf_wifi_scan_active_ExpectAndReturn(false);
	zf_wifi_scan_start_ExpectAndReturn(-EBUSY);
	assert_result(usb(ZF_OP_SCAN, 0, NULL, 0, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
}

void test_ble_rejects_usb_only_opcodes(void)
{
	assert_result(ble(ZF_OP_STAGE_SSID, 0, NULL, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
	assert_result(ble(ZF_OP_SCAN, 0, NULL, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
	zf_led_request_ExpectAndReturn(0, 0);
	assert_result(ble(ZF_OP_LED, 0, NULL, 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	assert_result(ble(14, 0, NULL, 0), ZF_STATUS_UNSUPPORTED, ZF_ERROR_OPCODE, 0);
}

void test_ble_save_requires_saved_radio_mask(void)
{
	const uint8_t changed[5] = {80, 0, 0, ZF_WIRELESS_WIFI, 0};
	const uint8_t same[5] = {80, 0, 0, ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, 0};
	const uint8_t *result;

	assert_result(ble(ZF_OP_SAVE, 2, same, 0), ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	assert_result(ble(ZF_OP_SAVE, 0, changed, 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	result = ble(ZF_OP_SAVE, 0, same, 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 80);
	TEST_ASSERT_EQUAL_UINT8(0, result[6]);
	TEST_ASSERT_EQUAL_UINT8(0, result[7]);
	TEST_ASSERT_EQUAL_UINT8(3, result[8]);
}

void test_ble_busy_while_usb_staged_and_slots_are_separate(void)
{
	uint8_t usb_result[ZF_CONTROL_SIZE];

	stage(ZF_OP_STAGE_PASSWORD, "password", 0);
	assert_result(ble(ZF_OP_REBOOT, 1, NULL, 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_USB_STAGED);
	zf_control_usb_get(usb_result);
	TEST_ASSERT_EQUAL_UINT8(ZF_OP_STAGE_PASSWORD, usb_result[2]);
	TEST_ASSERT_EQUAL_UINT8(ZF_STATUS_COMPLETED, usb_result[3]);
}

void test_save_reports_failed_but_keeps_committed_settings_when_reboot_fails(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};
	char password[ZF_WIFI_PASSWORD_SIZE];

	assert_result(stage(ZF_OP_STAGE_PASSWORD, "password", 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_board_schedule_reboot_ExpectAndReturn(-EIO);
	assert_result(usb(ZF_OP_SAVE, 2, args, sizeof(args), 0), ZF_STATUS_FAILED, ZF_ERROR_FAILED,
		      ZF_DETAIL_SAVED_REBOOT_FAILED);
	/* The record is already persisted; only the reboot scheduling failed. */
	TEST_ASSERT_EQUAL_INT(1, write_calls);
	zf_settings_load();
	zf_settings_wifi_password(password);
	TEST_ASSERT_EQUAL_STRING("password", password);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	assert_result(usb(ZF_OP_REBOOT, 1, NULL, 0, 10), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

void test_commit_busy_while_scan_active(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};

	stage(ZF_OP_STAGE_PASSWORD, "password", 0);
	zf_wifi_scan_active_StopIgnore();
	zf_wifi_scan_active_ExpectAndReturn(true);
	assert_result(usb(ZF_OP_SAVE, 2, args, sizeof(args), 0), ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

void test_scan_entry_clamps_driver_reported_oversize_ssid(void)
{
	const uint8_t entry[3] = {1, 7, 0};
	uint8_t ssid[ZF_SSID_MAX_BYTES];
	uint8_t length = 255;
	int8_t rssi = -50;
	uint8_t security = 4;
	const uint8_t *result;

	memset(ssid, 's', sizeof(ssid));
	zf_wifi_scan_state_ExpectAndReturn(ZF_SCAN_COMPLETE);
	zf_wifi_scan_generation_ExpectAndReturn(7);
	zf_wifi_scan_count_ExpectAndReturn(2);
	zf_wifi_scan_entry_ExpectAnyArgsAndReturn(0);
	zf_wifi_scan_entry_ReturnArrayThruPtr_ssid(ssid, sizeof(ssid));
	zf_wifi_scan_entry_ReturnThruPtr_ssid_length(&length);
	zf_wifi_scan_entry_ReturnThruPtr_rssi(&rssi);
	zf_wifi_scan_entry_ReturnThruPtr_security(&security);
	result = usb(ZF_OP_SCAN_ENTRY, 0, entry, sizeof(entry), 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 7);
	/* The reported length is clamped to the wire field: the copy cannot reach rssi or security. */
	TEST_ASSERT_EQUAL_UINT8(ZF_SSID_MAX_BYTES, result[8]);
	TEST_ASSERT_EACH_EQUAL_UINT8('s', result + 9, ZF_SSID_MAX_BYTES);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)-50, result[41]);
	TEST_ASSERT_EQUAL_UINT8(4, result[42]);
}

void test_scan_entry_rejects_stale_state_and_failed_reads(void)
{
	const uint8_t entry[3] = {1, 7, 0};

	zf_wifi_scan_state_ExpectAndReturn(ZF_SCAN_RUNNING);
	assert_result(usb(ZF_OP_SCAN_ENTRY, 0, entry, sizeof(entry), 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_SCAN_STALE);
	zf_wifi_scan_state_ExpectAndReturn(ZF_SCAN_COMPLETE);
	zf_wifi_scan_generation_ExpectAndReturn(7);
	zf_wifi_scan_count_ExpectAndReturn(2);
	zf_wifi_scan_entry_ExpectAnyArgsAndReturn(-EINVAL);
	assert_result(usb(ZF_OP_SCAN_ENTRY, 0, entry, sizeof(entry), 0), ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT,
		      ZF_DETAIL_SCAN_INDEX);
}

void test_commit_zero_length_key_clears_stored_key(void)
{
	const uint8_t args[4] = {0, 0, 0, 3};
	uint8_t clear[64] = {0};
	const uint8_t *result;

	assert_result(usb(ZF_OP_STAGE_KEY, 0, clear, sizeof(clear), 0), ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_board_schedule_reboot_ExpectAndReturn(0);
	result = usb(ZF_OP_SAVE, 2, args, sizeof(args), 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	TEST_ASSERT_EQUAL_HEX8(ZF_COMMIT_KEY, result[9]);
	/* Clearing must not install the staged (zero-filled) key as a configured one. */
	zf_settings_load();
	TEST_ASSERT_FALSE(zf_settings_ble_key_configured());
}

void test_stage_accepts_maximum_length_ssid_and_password(void)
{
	uint8_t args[64] = {ZF_SSID_MAX_BYTES};
	const uint8_t *result;

	memset(args + 1, 'a', ZF_SSID_MAX_BYTES);
	result = usb(ZF_OP_STAGE_SSID, 0, args, sizeof(args), 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	args[0] = ZF_CREDENTIAL_STAGE_PASSWORD_MAX;
	memset(args + 1, 'p', ZF_CREDENTIAL_STAGE_PASSWORD_MAX);
	result = usb(ZF_OP_STAGE_PASSWORD, 0, args, sizeof(args), 0);
	assert_result(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

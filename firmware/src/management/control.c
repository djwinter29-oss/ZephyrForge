#include "management/control.h"
#include "platform/board.h"
#include "app/capabilities.h"
#include "platform/led.h"
#include "management/credential_stage.h"
#include "settings/settings.h"
#include "benchmark/benchmark.h"
#include "storage/storage.h"
#include "wifi/wifi.h"
#include "app/runtime.h"

#include <errno.h>
#include <string.h>

/* Flag bits are opcode-specific: reboot and MSC use bit 0 as confirm; save uses bits 0 and 1. */
#define FLAG_CONFIRM 0x01u
#define SAVE_FLAG_FORCED 0x01u
#define SAVE_FLAG_COMMIT 0x02u
#define ARGS_SIZE 64u

/** Feature 20 result slot: the decoded result as replayed by GET_REPORT. */
typedef struct {
	uint8_t transaction_id; /**< Echoed request transaction. */
	uint8_t opcode; /**< Echoed request opcode. */
	uint8_t status; /**< ZF_STATUS_* outcome. */
	uint8_t error; /**< ZF_ERROR_* reason. */
	uint8_t data[63]; /**< Outcome detail and payload; data[0] is the wire detail byte. */
} result_t;

/** Normalized control request from either transport before execution. */
typedef struct {
	bool ble; /**< True for the BLE transport: envelope-restricted opcodes and a zero-flags save. */
	uint8_t transaction_id; /**< Nonzero request transaction echoed into the result. */
	uint8_t opcode; /**< Wire opcode, already range-checked by the dispatcher. */
	uint8_t flags; /**< Wire flags byte: confirm, forced-save and commit bits. */
	uint8_t args[64]; /**< Fixed 64-byte argument field: opcode-specific layout. */
} request_t;

static result_t usb_result;
static result_t ble_result;
static zf_credential_stage_t stage;
/** Reset-class work that blocks later state-changing commands. */
typedef enum {
	PENDING_NONE, /**< No save or reboot outstanding. */
	PENDING_SAVE_USB, /**< USB-requested settings write in flight; its result goes to the USB slot. */
	PENDING_SAVE_BLE, /**< BLE-requested settings write in flight; its result goes to the BLE slot. */
	PENDING_REBOOT, /**< Reboot scheduled; only reads remain useful. */
} pending_t;

/* A started candidate test whose join result has not yet marked the staged pair. */
static bool test_pending;
static pending_t pending;
static bool usb_save_result_unread;
static uint8_t save_commit_mask;

static void led(const request_t *request, result_t *result, int64_t now_ms);
static void reboot(const request_t *request, result_t *result, int64_t now_ms);
static void msc(const request_t *request, result_t *result, int64_t now_ms);
static void benchmark(const request_t *request, result_t *result, int64_t now_ms);
static void save(const request_t *request, result_t *result, int64_t now_ms);
static void stage_value(const request_t *request, result_t *result, int64_t now_ms);
static void test_wifi(const request_t *request, result_t *result, int64_t now_ms);
static void cancel_wifi_test(const request_t *request, result_t *result, int64_t now_ms);
static void start_wifi_scan(const request_t *request, result_t *result, int64_t now_ms);
static void scan_entry(const request_t *request, result_t *result, int64_t now_ms);

/** Per-opcode admission and framing; the handler validates only opcode-specific argument values. */
typedef struct {
	void (*handler)(const request_t *request, result_t *result, int64_t now_ms); /**< Opcode executor. */
	uint8_t capability; /**< ZF_CAP_* bit that must be built; zero marks an unassigned opcode. */
	uint8_t radio; /**< ZF_WIRELESS_* radio that must be built, or zero. */
	uint8_t flags; /**< Flag bits accepted before the handler runs; others are Malformed. */
	uint8_t args_used; /**< Leading argument bytes the handler reads; the rest must be zero. */
	bool ble_allowed; /**< Opcode is accepted over BLE. */
	bool proof_required; /**< BLE requests need a Control-key proof. */
} opcode_policy_t;

/* Save and staging frame their own fields so their established error precedence is preserved. */
static const opcode_policy_t opcode_policy[ZF_OP_SCAN_ENTRY + 1] = {
	[ZF_OP_LED] = {led, ZF_CAP_LED, 0, 0, 1, true, false},
	[ZF_OP_REBOOT] = {reboot, ZF_CAP_REBOOT, 0, FLAG_CONFIRM, 0, true, true},
	[ZF_OP_MSC] = {msc, ZF_CAP_MSC, 0, FLAG_CONFIRM, 1, false, true},
	[ZF_OP_BENCHMARK] = {benchmark, ZF_CAP_BENCHMARK, 0, 0, 5, false, true},
	[ZF_OP_SAVE] = {save, ZF_CAP_SETTINGS, 0, 0xFFu, ARGS_SIZE, true, true},
	[ZF_OP_STAGE_SSID] = {stage_value, ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, 0, ARGS_SIZE, false, true},
	[ZF_OP_STAGE_PASSWORD] = {stage_value, ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, 0, ARGS_SIZE, false, true},
	[ZF_OP_STAGE_KEY] = {stage_value, ZF_CAP_WIRELESS, ZF_WIRELESS_BLE, 0, ARGS_SIZE, false, true},
	[ZF_OP_TEST_WIFI] = {test_wifi, ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, 0, 0, false, true},
	[ZF_OP_CANCEL_TEST] = {cancel_wifi_test, ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, 0, 0, false, true},
	[ZF_OP_SCAN] = {start_wifi_scan, ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, 0, 0, false, true},
	[ZF_OP_SCAN_ENTRY] = {scan_entry, ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, 0, 3, false, true},
};

void zf_control_init(void)
{
	memset(&usb_result, 0, sizeof(usb_result));
	memset(&ble_result, 0, sizeof(ble_result));
	zf_credential_stage_reset(&stage);
	test_pending = false;
	pending = PENDING_NONE;
	usb_save_result_unread = false;
	save_commit_mask = 0;
}

static bool zero(const uint8_t *data, size_t size)
{
	while (size--) {
		if (*data++) {
			return false;
		}
	}
	return true;
}

static void finish(result_t *result, uint8_t status, uint8_t error, uint8_t detail)
{
	result->status = status;
	result->error = error;
	result->data[0] = detail;
}

/* A scheduled reset or an in-flight settings write blocks state-changing commands. */
static bool reject_if_reset_blocked(result_t *result)
{
	if (pending == PENDING_NONE) {
		return false;
	}
	finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY,
	       pending == PENDING_REBOOT ? ZF_DETAIL_REBOOT_PENDING : ZF_DETAIL_SAVE_PENDING);
	return true;
}

static bool reject_if_wifi_busy(result_t *result, bool scan_conflicts)
{
	if (zf_wifi_test_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
		return true;
	}
	if (scan_conflicts && zf_wifi_scan_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
		return true;
	}
	return false;
}

/** Keep transport and feature admission in one descriptor table. */
static bool capable(const request_t *request)
{
	const opcode_policy_t *policy = &opcode_policy[request->opcode];

	return policy->capability != 0 && (ZF_CAPABILITIES & policy->capability) != 0 &&
	       (policy->radio == 0 || (ZF_RADIO_MASK & policy->radio) != 0) &&
	       (!request->ble || policy->ble_allowed);
}

/** Map a platform admission result: zero Completed, -EBUSY Busy, any other error Failed. */
static void finish_errno(result_t *result, int err)
{
	if (err == 0) {
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	} else if (err == -EBUSY) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, 0);
	} else {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	}
}

static void put_saved_network(zf_saved_network_t *saved, uint8_t ssid[ZF_SSID_MAX_BYTES],
			      char password[ZF_WIFI_PASSWORD_SIZE])
{
	saved->ssid_length = zf_settings_wifi_ssid_bytes(ssid);
	zf_settings_wifi_password(password);
	saved->ssid = ssid;
	saved->password = password;
}

static void reboot(const request_t *request, result_t *result, int64_t now_ms)
{
	(void)now_ms;
	if (!(request->flags & FLAG_CONFIRM)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_CONFIRMATION, 0);
	} else if (reject_if_reset_blocked(result)) {
		return;
	} else if (zf_storage_in_use()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_CONFLICT);
	} else if (request->ble && zf_credential_stage_active(&stage)) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_USB_STAGED);
	} else if (zf_board_schedule_reboot() != 0) {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	} else {
		pending = PENDING_REBOOT;
		zf_credential_stage_reset(&stage);
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	}
}

static bool apply_commit(void)
{
	if (stage.ssid_staged && !zf_settings_set_wifi_ssid_bytes(stage.ssid, stage.ssid_length)) {
		return false;
	}
	if (stage.password_staged && !zf_settings_set_wifi_password(stage.password)) {
		return false;
	}
	return !stage.key_staged || zf_settings_set_ble_key(stage.key_clear ? NULL : stage.key);
}

static void save_complete(result_t *result, int err, uint8_t commit_mask)
{
	uint16_t mhz;

	if (err) {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
		return;
	}
	zf_credential_stage_reset(&stage);
	if (zf_board_schedule_reboot() != 0) {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, ZF_DETAIL_SAVED_REBOOT_FAILED);
		return;
	}
	pending = PENDING_REBOOT;
	finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	mhz = zf_sysclk_profile_mhz(zf_settings_sysclk());
	result->data[0] = (uint8_t)mhz;
	result->data[1] = (uint8_t)(mhz >> 8);
	result->data[2] = 0;
	result->data[3] = zf_settings_radios() & ZF_RADIO_MASK;
	result->data[4] = commit_mask;
}

static void save_written(int err)
{
	bool usb;

	zf_app_lock();
	usb = pending == PENDING_SAVE_USB;
	pending = PENDING_NONE;
	save_complete(usb ? &usb_result : &ble_result, err, save_commit_mask);
	usb_save_result_unread = usb_save_result_unread || usb;
	zf_app_unlock();
}

static void save(const request_t *request, result_t *result, int64_t now_ms)
{
	uint16_t mhz = (uint16_t)(request->args[0] | request->args[1] << 8);
	uint8_t radios = request->args[3];
	bool commit = (request->flags & SAVE_FLAG_COMMIT) != 0;
	zf_sysclk_profile_t profile = zf_sysclk_profile_from_mhz(mhz);
	uint8_t commit_mask = 0;
	uint8_t detail;
	int err;

	(void)now_ms;
	if (!request->ble && (request->flags & SAVE_FLAG_FORCED)) {
		finish(result, ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
		return;
	}
	if ((request->flags & ~(request->ble ? 0u : SAVE_FLAG_COMMIT)) || !zero(request->args + 4, ARGS_SIZE - 4)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	if ((unsigned)profile > ZF_SYSCLK_MAX_PROFILE || request->args[2] != 0 ||
	    (radios & (uint8_t)~ZF_RADIO_MASK)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
		return;
	}
	if (reject_if_reset_blocked(result)) {
		return;
	}
	if (zf_storage_in_use()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_CONFLICT);
		return;
	}
	if (request->ble && zf_credential_stage_active(&stage)) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_USB_STAGED);
		return;
	}
	if (commit && reject_if_wifi_busy(result, true)) {
		return;
	}
	if (request->ble && radios != (zf_settings_radios() & ZF_RADIO_MASK)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
		return;
	}
	if (commit) {
		uint8_t ssid[ZF_SSID_MAX_BYTES];
		char password[ZF_WIFI_PASSWORD_SIZE];
		zf_saved_network_t saved;

		put_saved_network(&saved, ssid, password);
		detail = zf_credential_stage_check_commit(&stage, &saved);
		memset(password, 0, sizeof(password));
		if (detail) {
			finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, detail);
			return;
		}
		commit_mask = zf_credential_stage_commit_mask(&stage);
	} else if (zf_credential_stage_active(&stage)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_COMMIT_FLAG);
		return;
	}

	/* Preserve omitted radios' saved policy for a later image that includes them. */
	if (!zf_settings_set_sysclk(profile) ||
	    !zf_settings_set_radios((zf_settings_radios() & (uint8_t)~ZF_RADIO_MASK) | radios) ||
	    (commit && !apply_commit())) {
		zf_settings_discard();
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
		return;
	}
	finish(result, ZF_STATUS_PENDING, ZF_ERROR_NONE, 0);
	pending = request->ble ? PENDING_SAVE_BLE : PENDING_SAVE_USB;
	save_commit_mask = commit_mask;
	err = zf_settings_save_async(save_written);
	if (err == ZF_SETTINGS_SAVE_UNCHANGED) {
		pending = PENDING_NONE;
		save_complete(result, 0, commit_mask);
		usb_save_result_unread = usb_save_result_unread || !request->ble;
	} else if (err != 0) {
		pending = PENDING_NONE;
		save_complete(result, err, commit_mask);
	}
}

static void stage_value(const request_t *request, result_t *result, int64_t now_ms)
{
	static uint8_t (*const check[])(const uint8_t *) = {
		zf_credential_stage_check_ssid, zf_credential_stage_check_password, zf_credential_stage_check_key,
	};
	unsigned index = request->opcode - ZF_OP_STAGE_SSID;
	uint8_t detail;

	detail = check[index](request->args);
	if (detail) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, detail);
		return;
	}
	if (!zero(request->args + 1 + request->args[0], ARGS_SIZE - 1u - request->args[0])) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	if (reject_if_reset_blocked(result)) {
		return;
	}
	if (reject_if_wifi_busy(result, false)) {
		return;
	}
	if (request->opcode == ZF_OP_STAGE_SSID) {
		zf_credential_stage_ssid(&stage, request->args, now_ms);
	} else if (request->opcode == ZF_OP_STAGE_PASSWORD) {
		zf_credential_stage_password(&stage, request->args, now_ms);
	} else {
		zf_credential_stage_key(&stage, request->args, now_ms);
	}
	finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
}

static void test_wifi(const request_t *request, result_t *result, int64_t now_ms)
{
	uint8_t ssid[ZF_SSID_MAX_BYTES];
	char password[ZF_WIFI_PASSWORD_SIZE];
	zf_saved_network_t saved;
	zf_candidate_t candidate;
	uint8_t detail;

	(void)request;
	(void)now_ms;
	if (reject_if_reset_blocked(result) || reject_if_wifi_busy(result, true)) {
		return;
	}
	put_saved_network(&saved, ssid, password);
	detail = zf_credential_stage_test_candidate(&stage, &saved, &candidate);
	if (detail) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, detail);
	} else {
		int err = zf_wifi_test_start(candidate.ssid, candidate.ssid_length, candidate.password);

		if (err == -EBUSY) {
			finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
		} else if (err != 0) {
			finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
		} else {
			stage.pair_tested = false;
			test_pending = true;
			finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
		}
	}
	memset(password, 0, sizeof(password));
}

static void scan_entry(const request_t *request, result_t *result, int64_t now_ms)
{
	uint16_t generation = (uint16_t)(request->args[1] | request->args[2] << 8);
	uint8_t ssid[ZF_SSID_MAX_BYTES] = {0};
	uint8_t length = 0;
	int8_t rssi = 0;
	uint8_t security = 0;
	int err;

	(void)now_ms;
	err = zf_wifi_scan_entry(generation, request->args[0], ssid, &length, &rssi, &security);
	if (err == -EBUSY) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
	} else if (err == -ESTALE) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_SCAN_STALE);
	} else if (err != 0) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_SCAN_INDEX);
	} else {
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
		zf_report_encode_scan_entry_result(result->data, generation, request->args[0], ssid, length, rssi,
						   security);
	}
}

static void cancel_wifi_test(const request_t *request, result_t *result, int64_t now_ms)
{
	(void)request;
	(void)now_ms;
	if (!reject_if_reset_blocked(result)) {
		zf_credential_stage_reset(&stage);
		zf_wifi_test_cancel();
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	}
}

static void start_wifi_scan(const request_t *request, result_t *result, int64_t now_ms)
{
	(void)request;
	(void)now_ms;
	if (reject_if_reset_blocked(result) || reject_if_wifi_busy(result, true)) {
		return;
	}
	int err = zf_wifi_scan_start();

	if (err == -EBUSY) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
	} else if (err != 0) {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	} else {
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	}
}

static void led(const request_t *request, result_t *result, int64_t now_ms)
{
	(void)now_ms;
	if (request->args[0] > 7) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	} else if (reject_if_reset_blocked(result)) {
		return;
	} else {
		finish_errno(result, zf_led_request(request->args[0]));
	}
}

static void benchmark(const request_t *request, result_t *result, int64_t now_ms)
{
	(void)now_ms;
	if (request->args[0] > 1) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	uint32_t size = (uint32_t)request->args[1] | (uint32_t)request->args[2] << 8 |
			(uint32_t)request->args[3] << 16 | (uint32_t)request->args[4] << 24;
	bool start = request->args[0] == 1;
	int detail;

	if (reject_if_reset_blocked(result)) {
		return;
	}
	detail = zf_benchmark_request(start, size);
	if (detail == ZF_BENCHMARK_ERROR_NONE) {
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	} else if (detail == ZF_BENCHMARK_ERROR_SIZE) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, (uint8_t)detail);
	} else if (detail == ZF_BENCHMARK_ERROR_BUSY || detail == ZF_BENCHMARK_ERROR_USB) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, (uint8_t)detail);
	} else {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, (uint8_t)detail);
	}
}

/** Return requires the user's OS-eject confirmation; the native class cannot verify host cache state. */
static void msc(const request_t *request, result_t *result, int64_t now_ms)
{
	uint8_t action = request->args[0];
	int err;

	(void)now_ms;
	if (action == 1 && request->flags) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	} else if (action > 1) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	} else if (action == 0 && !(request->flags & FLAG_CONFIRM)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_CONFIRMATION, 0);
	} else if (reject_if_reset_blocked(result)) {
		return;
	} else {
		err = zf_storage_request(action == 1, request->transaction_id);
		if (err == -ENOMEDIUM) {
			/* No initialized card with valid geometry is a state precondition, not an execution failure. */
			finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
		} else {
			finish_errno(result, err);
		}
	}
}

static void execute(const request_t *request, result_t *result, int64_t now_ms)
{
	const opcode_policy_t *policy;

	memset(result, 0, sizeof(*result));
	result->transaction_id = request->transaction_id;
	result->opcode = request->opcode;

	if (request->opcode == 0 || request->opcode > ZF_OP_SCAN_ENTRY) {
		finish(result, ZF_STATUS_UNSUPPORTED, ZF_ERROR_OPCODE, 0);
		return;
	}
	if (!capable(request)) {
		finish(result, ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
		return;
	}
	policy = &opcode_policy[request->opcode];
	if ((request->flags & ~policy->flags) ||
	    !zero(request->args + policy->args_used, ARGS_SIZE - policy->args_used)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	policy->handler(request, result, now_ms);
}

bool zf_control_usb_set(const uint8_t *payload, size_t size, int64_t now_ms)
{
	request_t request = {0};

	if (size != ZF_CONTROL_SIZE || payload[0] != 1 || payload[1] == 0) {
		return false;
	}
	if (pending == PENDING_SAVE_USB || usb_save_result_unread) {
		return false;
	}
	zf_control_tick(now_ms);
	request.transaction_id = payload[1];
	request.opcode = payload[2];
	request.flags = payload[3];
	memcpy(request.args, payload + 4, ARGS_SIZE);
	execute(&request, &usb_result, now_ms);
	memset(&request, 0, sizeof(request));
	zf_app_wakeup();
	return true;
}

static void encode(const result_t *result, uint8_t *out, size_t data_size)
{
	out[0] = 1;
	out[1] = result->transaction_id;
	out[2] = result->opcode;
	out[3] = result->status;
	out[4] = result->error;
	memcpy(out + 5, result->data, data_size);
}

void zf_control_usb_get(uint8_t out[ZF_CONTROL_SIZE])
{
	encode(&usb_result, out, sizeof(usb_result.data));
	usb_save_result_unread = false;
}

bool zf_control_ble_envelope(const uint8_t *request, size_t size)
{
	return size == ZF_BLE_CONTROL_SIZE && request[0] == 1 && request[1] != 0;
}

bool zf_control_ble_execute(const uint8_t request[ZF_BLE_CONTROL_SIZE], int64_t now_ms)
{
	request_t normalized = {
		.ble = true,
	};

	if (pending == PENDING_SAVE_BLE) {
		return false;
	}
	zf_control_tick(now_ms);
	normalized.transaction_id = request[1];
	normalized.opcode = request[2];
	normalized.flags = request[3];
	memcpy(normalized.args, request + 4, ZF_BLE_CONTROL_SIZE - 4);
	execute(&normalized, &ble_result, now_ms);
	/* The BLE result carries only four data bytes. */
	memset(ble_result.data + 4, 0, sizeof(ble_result.data) - 4);
	zf_app_wakeup();
	return true;
}

void zf_control_ble_get(uint8_t out[ZF_BLE_CONTROL_SIZE])
{
	encode(&ble_result, out, ZF_BLE_CONTROL_SIZE - 5);
}

bool zf_control_ble_proof_required(uint8_t opcode)
{
	return opcode > ZF_OP_SCAN_ENTRY || opcode_policy[opcode].capability == 0 ||
	       !opcode_policy[opcode].ble_allowed || opcode_policy[opcode].proof_required;
}

bool zf_control_ble_busy(void)
{
	return pending == PENDING_SAVE_BLE;
}

void zf_control_tick(int64_t now_ms)
{
	if (test_pending && !zf_wifi_test_active()) {
		test_pending = false;
		if (zf_wifi_test_joined()) {
			zf_credential_stage_mark_tested(&stage, now_ms);
		}
	}
	if (zf_credential_stage_tick(&stage, now_ms)) {
		zf_wifi_test_cancel();
	}
}

int64_t zf_control_next_deadline(void)
{
	return zf_credential_stage_active(&stage) ? stage.deadline_ms : INT64_MAX;
}

void zf_control_staging_status(uint8_t *flags, uint8_t *staged_state)
{
	zf_credential_stage_encode(&stage, flags, staged_state);
}

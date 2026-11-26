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

#define FLAG_CONFIRM 0x01u
#define FLAG_FORCED_SAVE 0x01u
#define FLAG_COMMIT 0x02u
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
/* A started candidate test whose join result has not yet marked the staged pair. */
static bool test_pending;
static bool reboot_pending;
static bool save_pending;
static bool save_ble;
static bool usb_save_result_unread;
static uint8_t save_commit_mask;

typedef struct {
	uint8_t capability;
	uint8_t radio;
	bool ble_allowed;
	bool usb_msc;
} opcode_policy_t;

static const opcode_policy_t opcode_policy[ZF_OP_SCAN_ENTRY + 1] = {
	[ZF_OP_LED] = {ZF_CAP_LED, 0, true, false},
	[ZF_OP_REBOOT] = {ZF_CAP_REBOOT, 0, true, false},
	[ZF_OP_MSC] = {ZF_CAP_STORAGE, 0, false, true},
	[ZF_OP_BENCHMARK] = {ZF_CAP_BENCHMARK, 0, false, false},
	[ZF_OP_SAVE] = {ZF_CAP_SETTINGS, 0, true, false},
	[ZF_OP_STAGE_SSID] = {ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, false, false},
	[ZF_OP_STAGE_PASSWORD] = {ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, false, false},
	[ZF_OP_STAGE_KEY] = {ZF_CAP_WIRELESS, ZF_WIRELESS_BLE, false, false},
	[ZF_OP_TEST_WIFI] = {ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, false, false},
	[ZF_OP_CANCEL_TEST] = {ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, false, false},
	[ZF_OP_SCAN] = {ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, false, false},
	[ZF_OP_SCAN_ENTRY] = {ZF_CAP_WIRELESS, ZF_WIRELESS_WIFI, false, false},
};

void zf_control_init(void)
{
	memset(&usb_result, 0, sizeof(usb_result));
	memset(&ble_result, 0, sizeof(ble_result));
	zf_credential_stage_reset(&stage);
	test_pending = false;
	reboot_pending = false;
	save_pending = false;
	save_ble = false;
	usb_save_result_unread = false;
}

/* A scheduled reset or an in-flight settings write blocks state-changing commands. */
static bool reset_blocked(void)
{
	return reboot_pending || save_pending;
}

static uint8_t reset_block_detail(void)
{
	return save_pending ? ZF_DETAIL_SAVE_PENDING : ZF_DETAIL_REBOOT_PENDING;
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

static bool reject_if_reset_blocked(result_t *result)
{
	if (!reset_blocked()) {
		return false;
	}
	finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, reset_block_detail());
	return true;
}

/** Keep transport and feature admission in one descriptor table. */
static bool capable(const request_t *request)
{
	if (request->opcode > ZF_OP_SCAN_ENTRY) {
		return false;
	}
	const opcode_policy_t *policy = &opcode_policy[request->opcode];

	return policy->capability != 0 && (ZF_CAPABILITIES & policy->capability) != 0 &&
	       (policy->radio == 0 || (ZF_RADIO_MASK & policy->radio) != 0) &&
	       (!policy->usb_msc || ZF_ENABLE_USB_MSC) && (!request->ble || policy->ble_allowed);
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

static void reboot(const request_t *request, result_t *result)
{
	if ((request->flags & ~FLAG_CONFIRM) || !zero(request->args, ARGS_SIZE)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	} else if (!(request->flags & FLAG_CONFIRM)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_CONFIRMATION, 0);
	} else if (reject_if_reset_blocked(result)) {
		return;
	} else if (zf_storage_busy()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_CONFLICT);
	} else if (request->ble && zf_credential_stage_active(&stage)) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_USB_STAGED);
	} else if (zf_board_schedule_reboot() != 0) {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
	} else {
		reboot_pending = true;
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
	if ((stage.ssid_staged || stage.password_staged) &&
	    !zf_settings_set_wifi_auth_state(ZF_WIFI_AUTH_TRY_WPA2)) {
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
	reboot_pending = true;
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
	result_t *result;

	zf_app_lock();
	result = save_ble ? &ble_result : &usb_result;
	save_complete(result, err, save_commit_mask);
	save_pending = false;
	if (!save_ble) {
		usb_save_result_unread = true;
	}
	zf_app_unlock();
}

static void save(const request_t *request, result_t *result)
{
	uint16_t mhz = (uint16_t)(request->args[0] | request->args[1] << 8);
	uint8_t radios = request->args[3];
	bool commit = (request->flags & FLAG_COMMIT) != 0;
	zf_sysclk_profile_t profile = zf_sysclk_profile_from_mhz(mhz);
	uint8_t commit_mask = 0;
	uint8_t detail;
	int err;

	if (!request->ble && (request->flags & FLAG_FORCED_SAVE)) {
		finish(result, ZF_STATUS_UNSUPPORTED, ZF_ERROR_CAPABILITY, 0);
		return;
	}
	if ((request->flags & ~(request->ble ? 0u : FLAG_COMMIT)) || !zero(request->args + 4, ARGS_SIZE - 4)) {
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
	if (zf_storage_busy()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_CONFLICT);
		return;
	}
	if (request->ble && zf_credential_stage_active(&stage)) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_USB_STAGED);
		return;
	}
	if (commit && zf_wifi_test_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
		return;
	}
	if (commit && zf_wifi_scan_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
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
	save_pending = true;
	save_ble = request->ble;
	save_commit_mask = commit_mask;
	err = zf_settings_save_async(save_written);
	if (err == ZF_SETTINGS_SAVE_UNCHANGED) {
		save_pending = false;
		save_complete(result, 0, commit_mask);
		if (!request->ble) {
			usb_save_result_unread = true;
		}
	} else if (err != 0) {
		save_pending = false;
		save_complete(result, err, commit_mask);
	}
}

static void stage_value(const request_t *request, result_t *result, int64_t now_ms)
{
	static uint8_t (*const check[])(const uint8_t *) = {
		zf_credential_stage_check_ssid, zf_credential_stage_check_password, zf_credential_stage_check_key,
	};
	static const uint8_t capacity[] = {ZF_SSID_MAX_BYTES, ZF_CREDENTIAL_STAGE_PASSWORD_MAX, ZF_CREDENTIAL_STAGE_KEY_SIZE};
	unsigned index = request->opcode - ZF_OP_STAGE_SSID;
	uint8_t detail;

	if (request->flags) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	detail = check[index](request->args);
	if (detail) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, detail);
		return;
	}
	if (!zero(request->args + 1 + request->args[0], ARGS_SIZE - 1u - request->args[0]) ||
	    request->args[0] > capacity[index]) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	if (reject_if_reset_blocked(result)) {
		return;
	}
	if (zf_wifi_test_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
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

static void test_wifi(result_t *result)
{
	uint8_t ssid[ZF_SSID_MAX_BYTES];
	char password[ZF_WIFI_PASSWORD_SIZE];
	zf_saved_network_t saved;
	zf_candidate_t candidate;
	uint8_t detail;

	if (reject_if_reset_blocked(result)) {
		return;
	}
	if (zf_wifi_test_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
		return;
	}
	if (zf_wifi_scan_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
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

static void scan_entry(const request_t *request, result_t *result)
{
	uint16_t generation = (uint16_t)(request->args[1] | request->args[2] << 8);
	uint8_t ssid[ZF_SSID_MAX_BYTES] = {0};
	uint8_t length = 0;
	int8_t rssi = 0;
	uint8_t security = 0;

	if (zf_wifi_scan_active()) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
		return;
	}
	if (zf_wifi_scan_state() != ZF_SCAN_COMPLETE || generation != zf_wifi_scan_generation()) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_SCAN_STALE);
		return;
	}
	if (request->args[0] >= zf_wifi_scan_count() ||
	    zf_wifi_scan_entry(request->args[0], ssid, &length, &rssi, &security) != 0) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_DETAIL_SCAN_INDEX);
		return;
	}
	finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	zf_report_encode_scan_entry_result(result->data, generation, request->args[0], ssid, length, rssi, security);
}

static bool args_clear(const request_t *request, size_t from)
{
	return request->flags == 0 && zero(request->args + from, ARGS_SIZE - from);
}

static void led(const request_t *request, result_t *result)
{
	if (!args_clear(request, 1)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
	} else if (request->args[0] > 7) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, 0);
	} else if (reject_if_reset_blocked(result)) {
		return;
	} else {
		finish_errno(result, zf_led_request(request->args[0]));
	}
}

static void benchmark(const request_t *request, result_t *result)
{
	if (!args_clear(request, 5) || request->args[0] > 1) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		return;
	}
	uint32_t size = (uint32_t)request->args[1] | (uint32_t)request->args[2] << 8 |
			(uint32_t)request->args[3] << 16 | (uint32_t)request->args[4] << 24;
	bool start = request->args[0] == 1;
	int detail;

	if ((start && (size == 0 || size > ZF_BENCHMARK_MAX_MIB)) || (!start && size != 0)) {
		finish(result, ZF_STATUS_INVALID, ZF_ERROR_ARGUMENT, ZF_BENCHMARK_ERROR_SIZE);
		return;
	}
	if (reject_if_reset_blocked(result)) {
		return;
	}
	detail = zf_benchmark_request(start, size);
	if (detail == ZF_BENCHMARK_ERROR_NONE) {
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
	} else if (detail == ZF_BENCHMARK_ERROR_BUSY || detail == ZF_BENCHMARK_ERROR_USB) {
		finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, (uint8_t)detail);
	} else {
		finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, (uint8_t)detail);
	}
}

/** Return requires the user's OS-eject confirmation; the native class cannot verify host cache state. */
static void msc(const request_t *request, result_t *result)
{
	uint8_t action = request->args[0];
	int err;

	if ((request->flags & ~FLAG_CONFIRM) || !zero(request->args + 1, ARGS_SIZE - 1) ||
	    (action == 1 && request->flags)) {
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
	switch (request->opcode) {
	case ZF_OP_LED:
		led(request, result);
		break;
	case ZF_OP_BENCHMARK:
		benchmark(request, result);
		break;
	case ZF_OP_MSC:
		msc(request, result);
		break;
	case ZF_OP_REBOOT:
		reboot(request, result);
		break;
	case ZF_OP_SAVE:
		save(request, result);
		break;
	case ZF_OP_STAGE_SSID:
	case ZF_OP_STAGE_PASSWORD:
	case ZF_OP_STAGE_KEY:
		stage_value(request, result, now_ms);
		break;
	case ZF_OP_TEST_WIFI:
		if (!args_clear(request, 0)) {
			finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		} else {
			test_wifi(result);
		}
		break;
	case ZF_OP_CANCEL_TEST:
		if (!args_clear(request, 0)) {
			finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
			break;
		}
		if (reject_if_reset_blocked(result)) {
			break;
		}
		zf_credential_stage_reset(&stage);
		zf_wifi_test_cancel();
		finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
		break;
	case ZF_OP_SCAN:
		if (!args_clear(request, 0)) {
			finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		} else if (reject_if_reset_blocked(result)) {
			break;
		} else if (zf_wifi_test_active()) {
			finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_TEST_ACTIVE);
		} else if (zf_wifi_scan_active()) {
			finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
		} else {
			int err = zf_wifi_scan_start();

			if (err == -EBUSY) {
				finish(result, ZF_STATUS_BUSY, ZF_ERROR_BUSY, ZF_DETAIL_SCAN_ACTIVE);
			} else if (err != 0) {
				finish(result, ZF_STATUS_FAILED, ZF_ERROR_FAILED, 0);
			} else {
				finish(result, ZF_STATUS_COMPLETED, ZF_ERROR_NONE, 0);
			}
		}
		break;
	case ZF_OP_SCAN_ENTRY:
		if (!args_clear(request, 3)) {
			finish(result, ZF_STATUS_INVALID, ZF_ERROR_MALFORMED, 0);
		} else {
			scan_entry(request, result);
		}
		break;
	default:
		finish(result, ZF_STATUS_UNSUPPORTED, ZF_ERROR_OPCODE, 0);
		break;
	}
}

bool zf_control_usb_set(const uint8_t *payload, size_t size, int64_t now_ms)
{
	request_t request = {0};

	if (size != ZF_CONTROL_SIZE || payload[0] != 1 || payload[1] == 0) {
		return false;
	}
	if ((save_pending && !save_ble) || usb_save_result_unread) {
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
	if (usb_save_result_unread) {
		usb_save_result_unread = false;
	}
	zf_storage_result_read(usb_result.transaction_id);
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

	if (save_pending && save_ble) {
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

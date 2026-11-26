#include "transport/ble_auth.h"
#include "transport/sha256.h"
#include "management/control.h"

#include <string.h>

static const uint8_t domain[] = {'P', 'F', 'B', 'L', 'E', 'C', 'T', 'L', '1'};
static const uint8_t session_request[] = {'S', 'E', 'S', 'S', 'I', 'O', 'N', '0', '1'};

static uint8_t control_key[ZF_BLE_AUTH_KEY_SIZE];
static uint8_t board_uid[ZF_BLE_AUTH_UID_SIZE];
static bool key_loaded;
static bool delay_active;
static int64_t retry_after_ms;

void zf_ble_auth_proof(const uint8_t key[ZF_BLE_AUTH_KEY_SIZE], const uint8_t nonce[ZF_BLE_AUTH_NONCE_SIZE],
		   const uint8_t uid[ZF_BLE_AUTH_UID_SIZE], const uint8_t *request, size_t request_size,
		   uint8_t proof[ZF_BLE_AUTH_PROOF_SIZE])
{
	const zf_sha256_hmac_part_t parts[] = {
		{nonce, ZF_BLE_AUTH_NONCE_SIZE},
		{uid, ZF_BLE_AUTH_UID_SIZE},
		{domain, sizeof(domain)},
		{request, request_size},
	};
	uint8_t mac[ZF_SHA256_SIZE];

	zf_sha256_hmac(key, ZF_BLE_AUTH_KEY_SIZE, parts, sizeof(parts) / sizeof(parts[0]), mac);
	memcpy(proof, mac, ZF_BLE_AUTH_PROOF_SIZE);
	memset(mac, 0, sizeof(mac));
}

void zf_ble_auth_init(const uint8_t key[ZF_BLE_AUTH_KEY_SIZE], const uint8_t uid[ZF_BLE_AUTH_UID_SIZE])
{
	key_loaded = key != NULL;
	if (key) {
		memcpy(control_key, key, sizeof(control_key));
	} else {
		memset(control_key, 0, sizeof(control_key));
	}
	memcpy(board_uid, uid, sizeof(board_uid));
	delay_active = false;
	retry_after_ms = 0;
}

void zf_ble_auth_session_reset(zf_ble_auth_session_t *session)
{
	memset(session, 0, sizeof(*session));
}

void zf_ble_auth_set_nonce(zf_ble_auth_session_t *session, const uint8_t nonce[ZF_BLE_AUTH_NONCE_SIZE])
{
	memcpy(session->nonce, nonce, ZF_BLE_AUTH_NONCE_SIZE);
	session->nonce_valid = true;
	session->proof_staged = false;
	memset(session->proof, 0, sizeof(session->proof));
}

static bool rate_limited(int64_t now_ms)
{
	if (delay_active && now_ms < retry_after_ms) {
		return true;
	}
	delay_active = false;
	return false;
}

static bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t size)
{
	uint8_t difference = 0;

	for (size_t i = 0; i < size; ++i) {
		difference |= (uint8_t)(a[i] ^ b[i]);
	}
	return difference == 0;
}

/* Checks the staged proof for one request, consuming the challenge either way. */
static bool consume_and_verify(zf_ble_auth_session_t *session, const uint8_t *request, size_t size, int64_t now_ms)
{
	uint8_t expected[ZF_BLE_AUTH_PROOF_SIZE];
	bool match;

	zf_ble_auth_proof(control_key, session->nonce, board_uid, request, size, expected);
	match = constant_time_equal(expected, session->proof, sizeof(expected));
	memset(expected, 0, sizeof(expected));
	session->nonce_valid = false;
	session->proof_staged = false;
	memset(session->nonce, 0, sizeof(session->nonce));
	memset(session->proof, 0, sizeof(session->proof));
	if (!match) {
		delay_active = true;
		retry_after_ms = now_ms + ZF_BLE_AUTH_RETRY_DELAY_MS;
	}
	return match;
}

uint8_t zf_ble_auth_write_proof(zf_ble_auth_session_t *session, const uint8_t *data, size_t size, int64_t now_ms)
{
	if (size != ZF_BLE_AUTH_PROOF_SIZE) {
		return ZF_ATT_ERR_INVALID_LENGTH;
	}
	if (rate_limited(now_ms)) {
		return ZF_ATT_ERR_RATE_LIMITED;
	}
	if (!key_loaded || !session->nonce_valid) {
		return ZF_ATT_ERR_UNAVAILABLE;
	}
	memcpy(session->proof, data, ZF_BLE_AUTH_PROOF_SIZE);
	session->proof_staged = true;
	if (session->unlocked) {
		return 0;
	}
	if (!consume_and_verify(session, session_request, sizeof(session_request), now_ms)) {
		return ZF_ATT_ERR_REJECTED;
	}
	session->unlocked = true;
	return 0;
}

uint8_t zf_ble_auth_authorize(zf_ble_auth_session_t *session, const uint8_t *request, size_t size, int64_t now_ms)
{
	if (!session->unlocked) {
		return ZF_ATT_ERR_REJECTED;
	}
	if (rate_limited(now_ms)) {
		return ZF_ATT_ERR_RATE_LIMITED;
	}
	if (!key_loaded || !session->nonce_valid) {
		return ZF_ATT_ERR_UNAVAILABLE;
	}
	if (!session->proof_staged) {
		return ZF_ATT_ERR_REJECTED;
	}
	return consume_and_verify(session, request, size, now_ms) ? 0 : ZF_ATT_ERR_REJECTED;
}

uint8_t zf_ble_auth_control_write(zf_ble_auth_session_t *session, const uint8_t *request, size_t size,
				  int64_t now_ms)
{
	uint8_t err;

	if (!session->unlocked) {
		return ZF_ATT_ERR_REJECTED;
	}
	if (!zf_control_ble_envelope(request, size)) {
		return ZF_ATT_ERR_ENVELOPE;
	}
	if (zf_control_ble_busy()) {
		return ZF_ATT_ERR_UNLIKELY;
	}
	err = zf_control_ble_proof_required(request[2]) ? zf_ble_auth_authorize(session, request, size, now_ms) : 0;
	if (err != 0) {
		return err;
	}
	return zf_control_ble_execute(request, now_ms) ? 0 : ZF_ATT_ERR_UNLIKELY;
}

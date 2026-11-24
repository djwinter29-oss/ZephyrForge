/**
 * @file wireless_auth.h
 * @brief Bluetooth LE session unlock and per-command proofs.
 *
 * Portable state machine: the Zephyr glue supplies time, random challenges and the saved Control key. Results are
 * ATT error codes (0 when accepted). The retry delay is global across connections; the session is per connection.
 */
#ifndef ZEPHYRFORGE_WIRELESS_AUTH_H
#define ZEPHYRFORGE_WIRELESS_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Saved Control key size in bytes. */
#define ZF_AUTH_KEY_SIZE 32u
/** Connection challenge size in bytes. */
#define ZF_AUTH_NONCE_SIZE 16u
/** Truncated HMAC proof size in bytes. */
#define ZF_AUTH_PROOF_SIZE 16u
/** Board UID length bound into every proof. */
#define ZF_AUTH_UID_SIZE 8u
/** Global delay every proof attempt must wait after a failed proof, across connections. */
#define ZF_AUTH_RETRY_DELAY_MS 2000

/** ATT error returns used by the control characteristics (BT_GATT_ERR payload values). */
#define ZF_ATT_ERR_INVALID_OFFSET 0x07u /**< Standard ATT invalid offset. */
#define ZF_ATT_ERR_INVALID_LENGTH 0x0Du /**< Standard ATT invalid attribute length. */
#define ZF_ATT_ERR_REJECTED 0x80u /**< Application rejection: locked session or unavailable state. */
#define ZF_ATT_ERR_ENVELOPE 0x81u /**< Control request envelope failed (length, version or transaction). */
#define ZF_ATT_ERR_RATE_LIMITED 0x82u /**< Retry delay active after a failed proof. */
#define ZF_ATT_ERR_UNAVAILABLE 0x83u /**< No key was saved; proofs can never be accepted. */

/** Per-connection BLE unlock state: one challenge, one staged proof, consumed on use. */
typedef struct {
	bool unlocked; /**< The session proof matched; commands may be authorized. */
	bool nonce_valid; /**< A challenge is installed; reading again replaces and invalidates staged proofs. */
	bool proof_staged; /**< A command proof was written for the nonce and is pending one authorization. */
	uint8_t nonce[ZF_AUTH_NONCE_SIZE]; /**< Current challenge; caller-supplied via the nonce characteristic. */
	uint8_t proof[ZF_AUTH_PROOF_SIZE]; /**< Staged command proof; consumed by every verify, success or failure. */
} zf_auth_session_t;

/** First 16 bytes of HMAC-SHA256(key, nonce || uid || "PFBLECTL1" || request). */
void zf_auth_proof(const uint8_t key[ZF_AUTH_KEY_SIZE], const uint8_t nonce[ZF_AUTH_NONCE_SIZE],
		   const uint8_t uid[ZF_AUTH_UID_SIZE], const uint8_t *request, size_t request_size,
		   uint8_t proof[ZF_AUTH_PROOF_SIZE]);

/** Load the Control key and board UID; a NULL key leaves proofs unavailable. Resets the retry delay. */
void zf_auth_init(const uint8_t key[ZF_AUTH_KEY_SIZE], const uint8_t uid[ZF_AUTH_UID_SIZE]);

/** Clear a connection's unlock, challenge and proof (connect and disconnect). */
void zf_auth_session_reset(zf_auth_session_t *session);

/** Install a fresh challenge read from characteristic 0100; invalidates any staged proof. */
void zf_auth_set_nonce(zf_auth_session_t *session, const uint8_t nonce[ZF_AUTH_NONCE_SIZE]);

/** Handle a 0101 Write Request. Unlocks a locked session when the session proof matches. */
uint8_t zf_auth_write_proof(zf_auth_session_t *session, const uint8_t *data, size_t size, int64_t now_ms);

/** Authorize one valid-envelope, non-LED control request on an unlocked session; consumes the proof. */
uint8_t zf_auth_authorize(zf_auth_session_t *session, const uint8_t *request, size_t size, int64_t now_ms);

#endif

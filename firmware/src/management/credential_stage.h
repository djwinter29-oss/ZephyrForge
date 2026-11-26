/**
 * @file credential_stage.h
 * @brief RAM-only staged Wi-Fi/Control-key candidates for USB Feature 20 opcodes 5-10.
 *
 * Handles staging, expiry and test eligibility. Portable: callers pass the monotonic time and the saved network.
 */
#ifndef ZEPHYRFORGE_CREDENTIAL_STAGE_H
#define ZEPHYRFORGE_CREDENTIAL_STAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/report_codec.h"

/** Unrefreshed staged candidates expire after this many milliseconds. */
#define ZF_CREDENTIAL_STAGE_LIFETIME_MS 600000
/** Maximum staged password length in bytes (NUL excluded). */
#define ZF_CREDENTIAL_STAGE_PASSWORD_MAX 63u
/** Staged Control key size in bytes. */
#define ZF_CREDENTIAL_STAGE_KEY_SIZE 32u

/** Feature 20 wireless detail values owned by staging (dispatchers own 1-15 and 22-28). */
#define ZF_DETAIL_SSID_LENGTH 16u /**< Staged SSID exceeds ZF_SSID_MAX_BYTES. */
#define ZF_DETAIL_PASSWORD 17u /**< Staged password fails the length or printable-byte rules. */
#define ZF_DETAIL_KEY_LENGTH 18u /**< Staged key length is neither 0 nor 32. */
#define ZF_DETAIL_PAIR_INCOMPLETE 19u /**< A new SSID lacks a staged password pair. */
#define ZF_DETAIL_EXPIRED 20u /**< The staging window expired before the action. */
#define ZF_DETAIL_NOTHING_STAGED 21u /**< No staged value exists to test or commit. */

/** Feature 12 flag bits owned by staging (bits 0-3 belong to Wi-Fi). */
#define ZF_CREDENTIAL_STAGE_FLAG_SSID 0x10u /**< An SSID is staged. */
#define ZF_CREDENTIAL_STAGE_FLAG_PASSWORD 0x20u /**< A password is staged. */
#define ZF_CREDENTIAL_STAGE_FLAG_KEY 0x40u /**< A Control key is staged. */
#define ZF_CREDENTIAL_STAGE_FLAG_TESTED 0x80u /**< The current SSID/password pair joined successfully. */

/** Feature 12 staged_state bits owned by staging. */
#define ZF_STAGED_EMPTY_SSID 0x01u /**< The staged SSID is empty (forget). */
#define ZF_STAGED_EMPTY_PASSWORD 0x02u /**< The staged password is empty (open network). */
#define ZF_STAGED_EMPTY_KEY 0x04u /**< The staged key clears the stored Control key. */
#define ZF_STAGED_EXPIRED 0x08u /**< Staged values expired; nothing remains staged. */

/** Successful opcode 5 commit-mask bits naming the fields written to the saved record. */
#define ZF_COMMIT_SSID 0x01u /**< The SSID was committed. */
#define ZF_COMMIT_PASSWORD 0x02u /**< The password was committed. */
#define ZF_COMMIT_KEY 0x04u /**< The Control key was committed or cleared. */

/** RAM-only staging slot for SSID, password and Control-key candidates with a shared expiry. */
typedef struct {
	bool ssid_staged; /**< An SSID candidate is present. */
	bool password_staged; /**< A password candidate is present. */
	bool key_staged; /**< A Control-key candidate is present. */
	bool pair_tested; /**< The current SSID/password pair joined successfully since the last restage. */
	bool expired; /**< The deadline passed; the slot reports EMPTY states until restaged. */
	uint8_t ssid_length; /**< Staged SSID length, 0..ZF_SSID_MAX_BYTES. */
	uint8_t ssid[ZF_SSID_MAX_BYTES]; /**< Raw SSID bytes including embedded NULs; length-bound. */
	uint8_t password_length; /**< Staged password length, 0..ZF_CREDENTIAL_STAGE_PASSWORD_MAX. */
	char password[ZF_CREDENTIAL_STAGE_PASSWORD_MAX + 1]; /**< NUL-terminated staged password; credentials are caller-owned. */
	bool key_clear; /**< A zero-length key stage requests clearing the stored key. */
	uint8_t key[ZF_CREDENTIAL_STAGE_KEY_SIZE]; /**< Staged Control key; meaningless when key_clear is set. */
	int64_t deadline_ms; /**< Monotonic time when the whole slot expires. */
} zf_credential_stage_t;

/** The saved network that staged values are compared against. */
typedef struct {
	uint8_t ssid_length; /**< Saved SSID length, 0..ZF_SSID_MAX_BYTES. */
	const uint8_t *ssid; /**< Borrowed caller buffer, valid for the call; never retained. */
	const char *password; /**< Borrowed NUL-terminated saved password, valid for the call; never retained. */
} zf_saved_network_t;

/** A resolved credential pair to test or commit; pointers refer to staged or saved storage. */
typedef struct {
	uint8_t ssid_length; /**< Candidate SSID length, 0..ZF_SSID_MAX_BYTES. */
	const uint8_t *ssid; /**< Borrowed staged or saved SSID bytes; lifetime belongs to the inputs. */
	const char *password; /**< Borrowed staged or saved NUL-terminated password; may be "" for open networks. */
} zf_candidate_t;

/** Clear the staging slot completely (successful commit, cancel or expiry restage). */
void zf_credential_stage_reset(zf_credential_stage_t *stage);
/** True while an SSID, password or key candidate is staged (expiry does not count). */
bool zf_credential_stage_active(const zf_credential_stage_t *stage);

/** Validate a staged SSID argument; returns 0 or ZF_DETAIL_SSID_LENGTH. */
uint8_t zf_credential_stage_check_ssid(const uint8_t *args);
/** Validate a staged password argument; returns 0 or ZF_DETAIL_PASSWORD. */
uint8_t zf_credential_stage_check_password(const uint8_t *args);
/** Validate a staged key argument; returns 0 or ZF_DETAIL_KEY_LENGTH. */
uint8_t zf_credential_stage_check_key(const uint8_t *args);
/** Stage a validated SSID argument and refresh the window; length 0 also stages an empty password. */
void zf_credential_stage_ssid(zf_credential_stage_t *stage, const uint8_t *args, int64_t now_ms);
/** Stage a validated password argument and refresh the window. */
void zf_credential_stage_password(zf_credential_stage_t *stage, const uint8_t *args, int64_t now_ms);
/** Stage a validated key argument (length 0 requests clearing) and refresh the window. */
void zf_credential_stage_key(zf_credential_stage_t *stage, const uint8_t *args, int64_t now_ms);

/** Expire unrefreshed values. Returns true when values expired during this call. */
bool zf_credential_stage_tick(zf_credential_stage_t *stage, int64_t now_ms);

/** Resolve the test pair; returns 0 or detail 19-21. */
uint8_t zf_credential_stage_test_candidate(const zf_credential_stage_t *stage, const zf_saved_network_t *saved,
					zf_candidate_t *candidate);

/** Validate an opcode 5 wireless commit; returns 0 or detail 19-21. */
uint8_t zf_credential_stage_check_commit(const zf_credential_stage_t *stage, const zf_saved_network_t *saved);
/** Which committed fields the successful opcode 5 wrote; one of the ZF_COMMIT_* bits or their union. */
uint8_t zf_credential_stage_commit_mask(const zf_credential_stage_t *stage);

/** Record a successful test of the current pair and restart the save window. */
void zf_credential_stage_mark_tested(zf_credential_stage_t *stage, int64_t now_ms);

/** Encode Feature 12 flag bits 4-7 and the staged_state byte from the slot. */
void zf_credential_stage_encode(const zf_credential_stage_t *stage, uint8_t *flags, uint8_t *staged_state);

#endif

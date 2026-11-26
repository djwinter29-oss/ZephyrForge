/**
 * @file control.h
 * @brief USB Feature 20 and Bluetooth control dispatcher with separate per-transport result slots.
 *
 * Not thread-safe: callers serialize every call with one lock.
 */
#ifndef ZEPHYRFORGE_CONTROL_H
#define ZEPHYRFORGE_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/management_protocol.h"
#include "protocol/report_codec.h"

/** Initialize result slots, staging and reboot reservation before transports start. */
void zf_control_init(void);

/** Handle a USB Feature 20 SET payload (report ID removed). Returns false for a rejected envelope. */
bool zf_control_usb_set(const uint8_t *payload, size_t size, int64_t now_ms);
void zf_control_usb_get(uint8_t out[ZF_CONTROL_SIZE]);

/** True when a 9-byte BLE request has a valid envelope (length, version, nonzero transaction ID). */
bool zf_control_ble_envelope(const uint8_t *request, size_t size);
/** True unless the opcode is a BLE-allowed command that does not require a proof. */
bool zf_control_ble_proof_required(uint8_t opcode);

/**
 * Execute an authorized BLE request with a valid envelope; replaces the BLE result slot.
 * @return False when another BLE settings save is still pending.
 */
bool zf_control_ble_execute(const uint8_t request[ZF_BLE_CONTROL_SIZE], int64_t now_ms);
void zf_control_ble_get(uint8_t out[ZF_BLE_CONTROL_SIZE]);

/** Mark a finished candidate test that joined and expire staged values; cancels a test when they expire. */
void zf_control_tick(int64_t now_ms);
/** Return the next staging-expiry deadline, or INT64_MAX when no timed control state exists. */
int64_t zf_control_next_deadline(void);

/** Feature 12 staging flags (bits 4-7) and staged_state. */
void zf_control_staging_status(uint8_t *flags, uint8_t *staged_state);

#endif

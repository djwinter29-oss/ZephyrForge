/**
 * @file storage_policy.h
 * @brief Portable SD lifecycle policy and the media operations the storage worker provides to it.
 */
#ifndef ZEPHYRFORGE_STORAGE_POLICY_H
#define ZEPHYRFORGE_STORAGE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/report_codec.h"

/** Presence debounce period; a raw absence already prevents USB disk I/O. */
#define ZF_STORAGE_DEBOUNCE_MS 100
/** Pace failed initialization and mount attempts. */
#define ZF_STORAGE_RETRY_MS 2000
/** Ownership action consumed by the serialized storage worker. */
typedef enum {
	ZF_STORAGE_CANCEL_REQUEST = -2, /**< Card changed after an ownership request was admitted. */
	ZF_STORAGE_NO_REQUEST = -1, /**< No ownership operation is due. */
	ZF_STORAGE_FIRMWARE_OWNER = 0, /**< Return the medium to firmware. */
	ZF_STORAGE_USB_OWNER = 1, /**< Give the medium to USB MSC. */
} zf_storage_request_t;

/** Initialize policy before transports start; subsequent calls belong to the storage worker. */
void zf_storage_policy_init(bool configured, bool present, int64_t now_ms);
/**
 * @brief Debounce presence, retry mounting and execute one request (0 firmware, 1 USB, or a sentinel).
 * @param present Zero when absent, positive when inserted, or a negative card-detect error.
 * @param now_ms Monotonic time in milliseconds.
 * @param request One ownership request or a sentinel.
 */
void zf_storage_policy_poll(int present, int64_t now_ms, zf_storage_request_t request);

/**
 * @brief Publish a coherent snapshot without holding the application lock or exposing live mutable state.
 * @param storage Wire-visible storage state.
 * @param busy True while the worker is about to manipulate, or is manipulating, the medium.
 */
void zf_storage_service_publish(const zf_storage_t *storage, bool busy);
/**
 * @brief Reset the previous medium and open it for exactly one owner.
 * @param usb True initializes raw blocks; false mounts the existing FAT volume without formatting.
 * @param storage Receives genuine hardware metadata even if filesystem mounting fails.
 * @return Zero on success, otherwise a platform error. Initialized hardware may have no mounted filesystem.
 * Failed cleanup prevents a new owner; callers must discard metadata after a failed USB handoff.
 */
int zf_storage_media_open(bool usb, zf_storage_t *storage);
/** Monotonic time after blocking media work, used to pace retries from completion. */
int64_t zf_storage_media_time(void);
/** Unmount and deinitialize the medium; failures must be logged and returned. */
int zf_storage_media_close(void);

#endif

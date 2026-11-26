/**
 * @file storage.h
 * @brief SD storage service: card ownership between firmware and USB, and the Feature 10 snapshot.
 */
#ifndef ZEPHYRFORGE_STORAGE_H
#define ZEPHYRFORGE_STORAGE_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include "protocol/report_codec.h"

/** Disk-access name of the SD medium owned by the storage service. */
#define ZF_STORAGE_DISK_NAME "SD"

/** Storage-owned job run by the sole media worker while it owns the card. */
typedef void (*zf_storage_exclusive_job_t)(void *context);

#if IS_ENABLED(CONFIG_ZF_STORAGE)
/** Register the storage service and start policy polling with the boot-sampled card state. */
void zf_storage_init(void);
/** Allow card initialization and mounting after management transports are initialized. */
void zf_storage_start(void);
/** Encode Feature 10: the coherent storage snapshot with the given Input sequence. */
void zf_storage_report(uint8_t sequence, uint8_t *out);
/** Copy the current storage snapshot without encoding it. */
void zf_storage_snapshot(zf_storage_t *out);
/**
 * @brief Let the storage worker proceed after this Feature 20 result has been read.
 *
 * The worker initially delays an ownership handoff so the host can read the command result. Reading the
 * correlated transaction shortens that delay.
 */
void zf_storage_result_read(uint8_t transaction);
/** True while the storage worker is busy or USB exclusively owns the card. */
bool zf_storage_busy(void);
/**
 * @brief Admit one USB ownership request without doing disk I/O; correlate its result before USB refresh.
 * @param usb True requests USB ownership; false returns the medium to firmware.
 * @param transaction Feature 20 transaction whose result read shortens the handoff delay.
 * @return Zero when admitted or the owner is already active, -EBUSY while storage is busy, -ENOMEDIUM when
 *         USB is requested without an initialized card with valid geometry, -ENOTSUP when USB export is
 *         not built, or another negative errno.
 */
int zf_storage_request(bool usb, uint8_t transaction);
/**
 * @brief Queue exclusive media work on the storage worker.
 * @param job Work to run while the worker holds the media lock.
 * @param context Borrowed context retained until the job starts.
 * @return Zero when queued, -EBUSY when another operation owns the medium, -EACCES while USB owns it,
 *         -ENOMEDIUM when the card is unavailable, -ENODEV when it is not mounted, or -EINVAL for bad input.
 */
int zf_storage_run_exclusive(zf_storage_exclusive_job_t job, void *context);
/** Check card presence during an exclusive job and latch removal until the next job is admitted. */
bool zf_storage_media_present(void);
#else
static inline void zf_storage_init(void)
{
}

static inline void zf_storage_start(void)
{
}

static inline void zf_storage_report(uint8_t sequence, uint8_t *out)
{
	zf_storage_t storage = {.sequence = sequence};

	zf_report_encode_storage(&storage, out);
}

static inline void zf_storage_snapshot(zf_storage_t *out)
{
	*out = (zf_storage_t){0};
}

static inline void zf_storage_result_read(uint8_t transaction)
{
	(void)transaction;
}

static inline bool zf_storage_busy(void)
{
	return false;
}

static inline int zf_storage_request(bool usb, uint8_t transaction)
{
	(void)usb;
	(void)transaction;
	return -ENOTSUP;
}

static inline int zf_storage_run_exclusive(zf_storage_exclusive_job_t job, void *context)
{
	(void)job;
	(void)context;
	return -ENOTSUP;
}
#endif

#endif

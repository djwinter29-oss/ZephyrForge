/**
 * @file benchmark.h
 * @brief Serialized temporary-file verification and throughput measurement.
 */
#ifndef ZEPHYRFORGE_BENCHMARK_H
#define ZEPHYRFORGE_BENCHMARK_H

#include <stdbool.h>
#include <stdint.h>

#include "report/report.h"

/** Maximum whole-MiB benchmark request. */
#define ZF_BENCHMARK_MAX_MIB 1024u
/** Aligned transfer size; each whole-MiB target contains complete chunks. */
#define ZF_BENCHMARK_CHUNK_SIZE 32768u

/**
 * @brief Admit or cancel a serialized benchmark without disk I/O.
 * @param start True starts a run; false cancels any active run.
 * @param size_mib Run size in MiB when starting; zero when cancelling.
 * @return ZF_BENCHMARK_ERROR_NONE when admitted, an zf_benchmark_error_t admission detail, or -ENOTSUP
 *         when the benchmark is not built in.
 */
int zf_benchmark_request(bool start, uint32_t size_mib);
/** Encode Feature 11: retained benchmark progress, read without acquiring the media lock. */
void zf_benchmark_report(uint8_t sequence, uint8_t *out);

/** @brief Run on the sole storage worker; only this run's exclusively created file may be removed.
 * @param size_mib Validated whole-MiB target.
 */
void zf_benchmark_run(uint32_t size_mib);

/* Media operations the storage worker provides to the benchmark engine. */

/** @brief Check media, existing path and free space without writing.
 * @param size_mib Whole-MiB target.
 * @return Benchmark error detail, or zero.
 */
int zf_benchmark_media_preflight(uint32_t size_mib);
/** @brief Open the reserved path, exclusively creating it for writes.
 * @param writing Create for writing when true, otherwise reopen for reading.
 * @return Zero or negative errno.
 */
int zf_benchmark_media_open(bool writing);
/** @brief Transfer one chunk without replaying short or failed I/O.
 * @param writing Write when true, otherwise read.
 * @param data Aligned chunk buffer.
 * @param size Requested byte count.
 * @param transferred Receives actual byte count.
 * @return Zero or negative errno.
 */
int zf_benchmark_media_transfer(bool writing, uint8_t *data, uint32_t size, uint32_t *transferred);
/** @return Zero or negative errno; write close includes filesystem sync. */
int zf_benchmark_media_close(void);
/** @return Zero or negative errno; never remove a file on replacement media. */
int zf_benchmark_media_remove(void);
/** @return Monotonic milliseconds, modulo 2^32. */
uint32_t zf_benchmark_media_time(void);
/** @brief Publish progress and observe media presence.
 * @param status Current run snapshot.
 * @return False requests cancellation; cleanup is still attempted.
 */
bool zf_benchmark_publish(const zf_benchmark_t *status);

#endif

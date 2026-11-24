/**
 * @file benchmark.h
 * @brief Serialized temporary-file verification and throughput measurement.
 */
#ifndef ZEPHYRFORGE_BENCHMARK_H
#define ZEPHYRFORGE_BENCHMARK_H

#include <stdbool.h>
#include <stdint.h>
#include "app/storage/benchmark_types.h"

#ifndef ZF_ENABLE_STORAGE_BENCHMARK
/** Optional firmware feature; enabled explicitly by the build. */
#define ZF_ENABLE_STORAGE_BENCHMARK 0
#endif

/** Maximum whole-MiB benchmark request. */
#define ZF_BENCHMARK_MAX_MIB 1024u
/** Aligned transfer size; each whole-MiB target contains complete chunks. */
#define ZF_BENCHMARK_CHUNK_SIZE 32768u

/** @brief Run on the sole storage worker; only this run's exclusively created file may be removed.
 * @param size_mib Validated whole-MiB target.
 */
void zf_benchmark_run(uint32_t size_mib);

/** @brief Check media, existing path and free space without writing.
 * @param size_mib Whole-MiB target.
 * @return Benchmark error detail, or zero.
 */
int zf_platform_benchmark_preflight(uint32_t size_mib);
/** @brief Open the reserved path, exclusively creating it for writes.
 * @param writing Create for writing when true, otherwise reopen for reading.
 * @return Zero or negative errno.
 */
int zf_platform_benchmark_open(bool writing);
/** @brief Transfer one chunk without replaying short or failed I/O.
 * @param writing Write when true, otherwise read.
 * @param data Aligned chunk buffer.
 * @param size Requested byte count.
 * @param transferred Receives actual byte count.
 * @return Zero or negative errno.
 */
int zf_platform_benchmark_transfer(bool writing, uint8_t *data, uint32_t size, uint32_t *transferred);
/** @return Zero or negative errno; write close includes filesystem sync. */
int zf_platform_benchmark_close(void);
/** @return Zero or negative errno; never remove a file on replacement media. */
int zf_platform_benchmark_remove(void);
/** @return Monotonic milliseconds, modulo 2^32. */
uint32_t zf_platform_benchmark_time(void);
/** @brief Publish progress and observe media presence.
 * @param status Current run snapshot.
 * @return False requests cancellation; cleanup is still attempted.
 */
bool zf_platform_benchmark_update(const zf_benchmark_t *status);

#endif

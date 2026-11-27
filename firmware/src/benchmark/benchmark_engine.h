/**
 * @file benchmark_engine.h
 * @brief Portable verification engine and its exclusive-media adapter contract.
 */
#ifndef ZEPHYRFORGE_BENCHMARK_ENGINE_H
#define ZEPHYRFORGE_BENCHMARK_ENGINE_H

#include "benchmark/benchmark.h"

/** Aligned transfer size; each whole-MiB target contains complete chunks. */
#ifndef ZF_BENCHMARK_CHUNK_SIZE
#define ZF_BENCHMARK_CHUNK_SIZE 32768u
#endif

/** @brief Run while the caller exclusively owns the mounted medium.
 * @param size_mib Validated whole-MiB target.
 */
void zf_benchmark_engine_run(uint32_t size_mib);
/** Reset adapter error state before an admitted run starts. */
void zf_benchmark_media_reset(void);
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
/** @return The first FatFs result observed during this run, or zero. */
uint8_t zf_benchmark_media_result(void);
/** @brief Publish progress and observe media presence.
 * @param status Current run snapshot.
 * @return False requests cancellation; cleanup is still attempted.
 */
bool zf_benchmark_service_publish(const zf_benchmark_t *status);

#endif

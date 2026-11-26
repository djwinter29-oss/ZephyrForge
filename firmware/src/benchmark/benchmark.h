/**
 * @file benchmark.h
 * @brief Admission and retained status of storage benchmark runs.
 */
#ifndef ZEPHYRFORGE_BENCHMARK_H
#define ZEPHYRFORGE_BENCHMARK_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "app/build_config.h"
#include "protocol/report_codec.h"

/** Maximum whole-MiB benchmark request. */
#define ZF_BENCHMARK_MAX_MIB 1024u

#if ZF_ENABLE_STORAGE_BENCHMARK
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
#else
static inline int zf_benchmark_request(bool start, uint32_t size_mib)
{
	(void)start;
	(void)size_mib;
	return -ENOTSUP;
}

static inline void zf_benchmark_report(uint8_t sequence, uint8_t *out)
{
	zf_benchmark_t status = {.sequence = sequence, .state = ZF_BENCHMARK_IDLE};

	zf_report_encode_benchmark(&status, out);
}
#endif

#endif

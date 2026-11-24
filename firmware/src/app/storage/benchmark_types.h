/**
 * @file benchmark_types.h
 * @brief Benchmark lifecycle and snapshot types shared with report encoders.
 */
#ifndef ZEPHYRFORGE_BENCHMARK_TYPES_H
#define ZEPHYRFORGE_BENCHMARK_TYPES_H

#include <stdint.h>

#ifndef ZF_ENABLE_STORAGE_BENCHMARK
/** Optional firmware feature; enabled explicitly by the build. */
#define ZF_ENABLE_STORAGE_BENCHMARK 0
#endif

/** Retained benchmark lifecycle, encoded in Report 11. */
typedef enum {
	ZF_BENCHMARK_IDLE = 0, /**< No admitted run. */
	ZF_BENCHMARK_QUEUED = 1, /**< Storage reserved for execution. */
	ZF_BENCHMARK_PREPARING = 2, /**< Checking the mounted filesystem. */
	ZF_BENCHMARK_WRITING = 3, /**< Writing the generated pattern. */
	ZF_BENCHMARK_READING = 4, /**< Reading and verifying the pattern. */
	ZF_BENCHMARK_CLEANUP = 5, /**< Closing and removing this run's file. */
	ZF_BENCHMARK_PASSED = 6, /**< Verification and cleanup succeeded. */
	ZF_BENCHMARK_FAILED = 7, /**< Execution or cleanup failed. */
	ZF_BENCHMARK_CANCELLED = 8, /**< Cancellation and cleanup completed. */
} zf_benchmark_state_t;

/** Admission and execution details; zero means no error. */
typedef enum {
	ZF_BENCHMARK_ERROR_NONE = 0, /**< No error. */
	ZF_BENCHMARK_ERROR_BUSY = 1, /**< Another operation owns storage. */
	ZF_BENCHMARK_ERROR_USB = 2, /**< USB owns the card. */
	ZF_BENCHMARK_ERROR_SIZE = 3, /**< Size outside 1..1024 MiB. */
	ZF_BENCHMARK_ERROR_CARD = 4, /**< Medium absent or changed. */
	ZF_BENCHMARK_ERROR_SPACE = 5, /**< Insufficient filesystem space. */
	ZF_BENCHMARK_ERROR_EXISTS = 6, /**< Reserved file already exists. */
	ZF_BENCHMARK_ERROR_IO = 7, /**< Filesystem failure or short transfer. */
	ZF_BENCHMARK_ERROR_VERIFY = 8, /**< Pattern mismatch. */
	ZF_BENCHMARK_ERROR_CLEANUP = 9, /**< File close or removal failed. */
	ZF_BENCHMARK_ERROR_MOUNT = 10, /**< No firmware-owned filesystem mount. */
} zf_benchmark_error_t;

/** This run created a file that could not be removed. */
#define ZF_BENCHMARK_FILE_REMAINS 1u
/** The firmware filesystem remains mounted. */
#define ZF_BENCHMARK_MOUNTED 2u

/** Coherent progress, retained until a subsequent admitted run or reboot. */
typedef struct {
	uint8_t sequence; /**< Input publication sequence. */
	zf_benchmark_state_t state; /**< Current or terminal lifecycle. */
	uint8_t flags; /**< FILE_REMAINS and MOUNTED bits. */
	uint32_t target_bytes; /**< Requested file length. */
	uint32_t written_bytes; /**< Bytes accepted by writes. */
	uint32_t read_bytes; /**< Bytes compared with the pattern. */
	uint32_t write_elapsed_ms; /**< Pattern generation, writes and final close/sync. */
	uint32_t read_elapsed_ms; /**< Reads, verification and final close. */
	uint32_t mismatch_count; /**< Differing bytes in the first mismatching chunk. */
	zf_benchmark_error_t error; /**< Execution failure, independent of admission errors. */
	uint8_t filesystem_result; /**< FatFs result when an I/O or cleanup failure supplies one. */
} zf_benchmark_t;

#endif

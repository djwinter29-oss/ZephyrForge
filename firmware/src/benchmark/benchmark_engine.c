#include "benchmark/benchmark_engine.h"
#include "protocol/report_codec.h"

#include <errno.h>
#include <string.h>

static uint32_t buffer[ZF_BENCHMARK_CHUNK_SIZE / sizeof(uint32_t)];

_Static_assert(ZF_BENCHMARK_CHUNK_SIZE >= 4096u && ZF_BENCHMARK_CHUNK_SIZE <= 32768u &&
	       ZF_BENCHMARK_CHUNK_SIZE % sizeof(uint32_t) == 0,
	       "benchmark buffer size must be 4-byte aligned and between 4 and 32 KiB");

static uint32_t pattern(uint32_t offset)
{
	return (offset / 4u) ^ 0xa5c391f7u;
}

static void fill(uint32_t offset)
{
	for (uint32_t i = 0; i < sizeof(buffer) / sizeof(buffer[0]); ++i) {
		buffer[i] = pattern(offset + i * 4u);
	}
}

static uint32_t mismatches(uint32_t offset)
{
	uint32_t count = 0;

	for (uint32_t i = 0; i < sizeof(buffer) / sizeof(buffer[0]); ++i) {
		for (uint32_t difference = buffer[i] ^ pattern(offset + i * 4u); difference; difference >>= 8) {
			count += (difference & 0xffu) != 0;
		}
	}
	return count;
}

static void io_error(zf_benchmark_t *status, int error)
{
	if (error != 0 && status->error == ZF_BENCHMARK_ERROR_NONE) {
		status->error = error == -ENOMEDIUM ? ZF_BENCHMARK_ERROR_CARD :
				error == -EEXIST ? ZF_BENCHMARK_ERROR_EXISTS : ZF_BENCHMARK_ERROR_IO;
	}
}

void zf_benchmark_engine_run(uint32_t size_mib)
{
	zf_benchmark_t status = {.state = ZF_BENCHMARK_PREPARING};
	bool created = false;
	bool opened = false;
	bool cancelled = false;
	bool close_failed = false;
	uint32_t start;
	int err;

	if (size_mib == 0 || size_mib > ZF_BENCHMARK_MAX_MIB) {
		status.error = ZF_BENCHMARK_ERROR_SIZE;
		goto cleanup;
	}
	status.target_bytes = size_mib * 1024u * 1024u;
	if (!zf_benchmark_service_publish(&status)) {
		cancelled = true;
		goto cleanup;
	}
	status.error = zf_benchmark_media_preflight(size_mib);
	if (status.error) {
		goto cleanup;
	}
	err = zf_benchmark_media_open(true);
	if (err != 0) {
		io_error(&status, err);
		goto cleanup;
	}
	created = opened = true;
	status.state = ZF_BENCHMARK_WRITING;
	status.write_elapsed_ms = 0;
	while (status.written_bytes < status.target_bytes) {
		uint32_t count = 0;

		if (!zf_benchmark_service_publish(&status)) {
			cancelled = true;
			break;
		}
		fill(status.written_bytes);
		start = zf_benchmark_media_time();
		err = zf_benchmark_media_transfer(true, (uint8_t *)buffer, sizeof(buffer), &count);
		status.write_elapsed_ms += zf_benchmark_media_time() - start;
		if (count > sizeof(buffer)) {
			count = 0;
			err = -EIO;
		}
		status.written_bytes += count;
		if (err != 0 || count != sizeof(buffer)) {
			io_error(&status, err != 0 ? err : -EIO);
			break;
		}
	}
	err = zf_benchmark_media_close();
	opened = false;
	close_failed = err != 0;
	io_error(&status, err);
	if (status.error || cancelled) {
		goto cleanup;
	}
	err = zf_benchmark_media_open(false);
	if (err != 0) {
		io_error(&status, err);
		goto cleanup;
	}
	opened = true;
	status.state = ZF_BENCHMARK_READING;
	status.read_elapsed_ms = 0;
	while (status.read_bytes < status.target_bytes) {
		uint32_t count = 0;

		if (!zf_benchmark_service_publish(&status)) {
			cancelled = true;
			break;
		}
		start = zf_benchmark_media_time();
		err = zf_benchmark_media_transfer(false, (uint8_t *)buffer, sizeof(buffer), &count);
		status.read_elapsed_ms += zf_benchmark_media_time() - start;
		if (err != 0 || count != sizeof(buffer)) {
			io_error(&status, err != 0 ? err : -EIO);
			break;
		}
		status.mismatch_count += mismatches(status.read_bytes);
		status.read_bytes += count;
		if (status.mismatch_count) {
			status.error = ZF_BENCHMARK_ERROR_VERIFY;
			break;
		}
	}
	err = zf_benchmark_media_close();
	opened = false;
	close_failed = err != 0;
	io_error(&status, err);

cleanup:
	status.state = ZF_BENCHMARK_CLEANUP;
	(void)zf_benchmark_service_publish(&status);
	if (opened) {
		err = zf_benchmark_media_close();
		close_failed = err != 0;
		io_error(&status, err);
	}
	if (created && (close_failed || zf_benchmark_media_remove() != 0)) {
		status.flags |= ZF_BENCHMARK_FILE_REMAINS;
		if (!status.error) {
			status.error = ZF_BENCHMARK_ERROR_CLEANUP;
		}
	}
	status.state = status.error ? ZF_BENCHMARK_FAILED : cancelled ? ZF_BENCHMARK_CANCELLED : ZF_BENCHMARK_PASSED;
	(void)zf_benchmark_service_publish(&status);
}

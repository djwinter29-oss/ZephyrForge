#include "unity.h"
#include "benchmark/benchmark.h"

#include <errno.h>
#include <string.h>

static uint8_t file[1024u * 1024u];
static uint32_t position;
static uint32_t ticks;
static bool exists;
static bool corrupt;
static bool short_transfer;
static bool short_read;
static bool oversize;
static bool cancel;
static bool cancel_during_read;
static bool close_failure;
static bool removed;
static bool remove_during_write;
static bool unlink_failure;
static bool writing;
static unsigned opens;
static unsigned deletes;
static zf_benchmark_t last;

void setUp(void)
{
	position = ticks = opens = deletes = 0;
	exists = corrupt = short_transfer = short_read = oversize = cancel = close_failure = removed = false;
	cancel_during_read = writing = remove_during_write = unlink_failure = false;
	memset(&last, 0, sizeof(last));
}

void tearDown(void) {}

int zf_benchmark_media_preflight(uint32_t size_mib)
{
	TEST_ASSERT_EQUAL_UINT32(1, size_mib);
	return exists ? ZF_BENCHMARK_ERROR_EXISTS : 0;
}

int zf_benchmark_media_open(bool write)
{
	++opens;
	writing = write;
	position = 0;
	if (write) {
		if (exists) {
			return -EEXIST;
		}
		exists = true;
	}
	return 0;
}

int zf_benchmark_media_transfer(bool write, uint8_t *data, uint32_t size, uint32_t *count)
{
	if (removed) {
		*count = 0;
		return -ENOMEDIUM;
	}
	if (oversize && write) {
		/* A driver reporting more bytes than requested must not be trusted or counted. */
		*count = size + 16u;
		return 0;
	}
	*count = (short_transfer || (short_read && !write)) ? size / 2 : size;
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(file), position + *count);
	if (write) {
		memcpy(file + position, data, *count);
	} else {
		memcpy(data, file + position, *count);
		if (corrupt) {
			data[7] ^= 0x80;
		}
	}
	position += *count;
	return 0;
}

int zf_benchmark_media_close(void)
{
	return removed ? -ENOMEDIUM : close_failure ? -EIO : 0;
}

int zf_benchmark_media_remove(void)
{
	++deletes;
	TEST_ASSERT_TRUE(exists);
	if (unlink_failure) {
		return -EIO;
	}
	exists = false;
	return 0;
}

uint32_t zf_benchmark_media_time(void)
{
	ticks += 5;
	return ticks;
}

bool zf_benchmark_publish(const zf_benchmark_t *status)
{
	last = *status;
	if (remove_during_write && position >= ZF_BENCHMARK_CHUNK_SIZE) {
		removed = true;
	}
	if (cancel_during_read && !writing && position >= ZF_BENCHMARK_CHUNK_SIZE &&
	    status->state == ZF_BENCHMARK_READING) {
		return false;
	}
	return !(cancel && writing && position >= ZF_BENCHMARK_CHUNK_SIZE);
}

void test_benchmark_writes_verifies_and_removes_only_its_own_file(void)
{
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_PASSED, last.state);
	TEST_ASSERT_EQUAL_UINT32(sizeof(file), last.target_bytes);
	TEST_ASSERT_EQUAL_UINT32(sizeof(file), last.written_bytes);
	TEST_ASSERT_EQUAL_UINT32(sizeof(file), last.read_bytes);
	TEST_ASSERT_EQUAL_UINT32(0, last.mismatch_count);
	TEST_ASSERT_GREATER_THAN_UINT32(0, last.write_elapsed_ms);
	TEST_ASSERT_GREATER_THAN_UINT32(0, last.read_elapsed_ms);
	TEST_ASSERT_EQUAL_UINT(2, opens);
	TEST_ASSERT_EQUAL_UINT(1, deletes);
	TEST_ASSERT_FALSE(exists);
}

void test_existing_file_is_neither_opened_nor_deleted(void)
{
	exists = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_EXISTS, last.error);
	TEST_ASSERT_EQUAL_UINT(0, opens);
	TEST_ASSERT_EQUAL_UINT(0, deletes);
	TEST_ASSERT_TRUE(exists);
}

void test_short_write_is_not_replayed_and_cleanup_is_attempted(void)
{
	short_transfer = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_IO, last.error);
	TEST_ASSERT_EQUAL_UINT32(ZF_BENCHMARK_CHUNK_SIZE / 2, last.written_bytes);
	TEST_ASSERT_EQUAL_UINT32(0, last.read_bytes);
	TEST_ASSERT_EQUAL_UINT(1, opens);
	TEST_ASSERT_FALSE(exists);
}

void test_corruption_counts_differing_bytes_and_fails_verification(void)
{
	corrupt = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_VERIFY, last.error);
	TEST_ASSERT_EQUAL_UINT32(1, last.mismatch_count);
	TEST_ASSERT_EQUAL_UINT32(ZF_BENCHMARK_CHUNK_SIZE, last.read_bytes);
	TEST_ASSERT_FALSE(exists);
}

void test_cancellation_retains_partial_counts_and_removes_the_created_file(void)
{
	cancel = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_CANCELLED, last.state);
	TEST_ASSERT_EQUAL_UINT32(ZF_BENCHMARK_CHUNK_SIZE, last.written_bytes);
	TEST_ASSERT_EQUAL_UINT32(0, last.read_bytes);
	TEST_ASSERT_FALSE(exists);
}

void test_close_failure_does_not_unlink_a_still_open_file(void)
{
	close_failure = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_IO, last.error);
	TEST_ASSERT_EQUAL_UINT(ZF_BENCHMARK_FILE_REMAINS, last.flags);
	TEST_ASSERT_EQUAL_UINT(0, deletes);
	TEST_ASSERT_TRUE(exists);
}

void test_removal_stops_io_without_reopening_or_unlinking_replacement_media(void)
{
	remove_during_write = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_CARD, last.error);
	TEST_ASSERT_EQUAL_UINT32(ZF_BENCHMARK_CHUNK_SIZE, last.written_bytes);
	TEST_ASSERT_EQUAL_UINT(1, opens);
	TEST_ASSERT_EQUAL_UINT(0, deletes);
	TEST_ASSERT_TRUE(last.flags & ZF_BENCHMARK_FILE_REMAINS);
}

void test_cleanup_failure_is_not_reported_as_passed(void)
{
	unlink_failure = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_CLEANUP, last.error);
	TEST_ASSERT_EQUAL_UINT32(sizeof(file), last.read_bytes);
	TEST_ASSERT_TRUE(last.flags & ZF_BENCHMARK_FILE_REMAINS);
}

void test_size_guard_rejects_zero_and_oversize_without_platform_io(void)
{
	zf_benchmark_run(0);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_SIZE, last.error);
	TEST_ASSERT_EQUAL_UINT32(0, last.target_bytes);
	TEST_ASSERT_EQUAL_UINT(0, opens);
	TEST_ASSERT_EQUAL_UINT(0, deletes);
	TEST_ASSERT_FALSE(exists);

	zf_benchmark_run(ZF_BENCHMARK_MAX_MIB + 1u);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_SIZE, last.error);
	TEST_ASSERT_EQUAL_UINT32(0, last.target_bytes);
	TEST_ASSERT_EQUAL_UINT(0, opens);
	TEST_ASSERT_EQUAL_UINT(0, deletes);
	TEST_ASSERT_FALSE(exists);
}

void test_oversize_transfer_report_is_not_trusted(void)
{
	oversize = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_IO, last.error);
	TEST_ASSERT_EQUAL_UINT32(0, last.written_bytes);
	TEST_ASSERT_EQUAL_UINT32(0, last.read_bytes);
	TEST_ASSERT_EQUAL_UINT(1, opens);
	TEST_ASSERT_FALSE(exists);
}

void test_short_read_fails_io_without_reaching_verification(void)
{
	short_read = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_FAILED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_IO, last.error);
	TEST_ASSERT_EQUAL_UINT32(0, last.read_bytes);
	TEST_ASSERT_EQUAL_UINT32(sizeof(file), last.written_bytes);
	TEST_ASSERT_EQUAL_UINT(2, opens);
	TEST_ASSERT_EQUAL_UINT(1, deletes);
	TEST_ASSERT_FALSE(exists);
}

void test_cancellation_during_read_stops_before_completing_verification(void)
{
	cancel_during_read = true;
	zf_benchmark_run(1);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_CANCELLED, last.state);
	TEST_ASSERT_EQUAL(ZF_BENCHMARK_ERROR_NONE, last.error);
	TEST_ASSERT_EQUAL_UINT32(sizeof(file), last.written_bytes);
	TEST_ASSERT_EQUAL_UINT32(ZF_BENCHMARK_CHUNK_SIZE, last.read_bytes);
	TEST_ASSERT_EQUAL_UINT(2, opens);
	TEST_ASSERT_EQUAL_UINT(1, deletes);
	TEST_ASSERT_FALSE(exists);
}

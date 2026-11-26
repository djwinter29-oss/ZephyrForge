#include "benchmark/benchmark_engine.h"
#include "storage/storage.h"

#include <errno.h>
#include <ff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zf_benchmark_fatfs, LOG_LEVEL_INF);

#define BENCHMARK_PATH ZF_STORAGE_DISK_NAME ":/EFTEST.BIN"

/* Accessed only by the storage worker's exclusive benchmark job. */
static FIL benchmark_file;
static uint8_t filesystem_result;

void zf_benchmark_media_reset(void)
{
	filesystem_result = 0;
}

uint8_t zf_benchmark_media_result(void)
{
	return filesystem_result;
}

static int benchmark_result(FRESULT result)
{
	if (result == FR_OK) {
		return 0;
	}
	LOG_ERR("Benchmark filesystem failure: %d", result);
	if (filesystem_result == 0) {
		filesystem_result = result;
	}
	return result == FR_EXIST ? -EEXIST : result == FR_NOT_READY ? -ENOMEDIUM : -EIO;
}

int zf_benchmark_media_preflight(uint32_t size_mib)
{
	FILINFO info;
	FATFS *filesystem;
	DWORD free_clusters;
	FRESULT result;

	if (!zf_storage_media_present()) {
		return ZF_BENCHMARK_ERROR_CARD;
	}
	result = f_stat(BENCHMARK_PATH, &info);
	if (result == FR_OK) {
		return ZF_BENCHMARK_ERROR_EXISTS;
	}
	if (result != FR_NO_FILE) {
		(void)benchmark_result(result);
		return ZF_BENCHMARK_ERROR_IO;
	}
	result = f_getfree(ZF_STORAGE_DISK_NAME ":", &free_clusters, &filesystem);
	if (result != FR_OK) {
		(void)benchmark_result(result);
		return ZF_BENCHMARK_ERROR_IO;
	}
	uint64_t free_bytes = (uint64_t)free_clusters * filesystem->csize * 512u;

	return free_bytes < (uint64_t)size_mib * 1024u * 1024u + ZF_BENCHMARK_CHUNK_SIZE
		? ZF_BENCHMARK_ERROR_SPACE : 0;
}

int zf_benchmark_media_open(bool writing)
{
	return zf_storage_media_present()
		? benchmark_result(f_open(&benchmark_file, BENCHMARK_PATH,
					 writing ? FA_WRITE | FA_CREATE_NEW : FA_READ)) : -ENOMEDIUM;
}

int zf_benchmark_media_transfer(bool writing, uint8_t *data, uint32_t size, uint32_t *transferred)
{
	UINT count = 0;
	FRESULT result;

	*transferred = 0;
	if (!zf_storage_media_present()) {
		return -ENOMEDIUM;
	}
	result = writing ? f_write(&benchmark_file, data, size, &count) : f_read(&benchmark_file, data, size, &count);
	*transferred = count;
	return benchmark_result(result);
}

int zf_benchmark_media_close(void)
{
	return zf_storage_media_present() ? benchmark_result(f_close(&benchmark_file)) : -ENOMEDIUM;
}

int zf_benchmark_media_remove(void)
{
	return zf_storage_media_present() ? benchmark_result(f_unlink(BENCHMARK_PATH)) : -ENOMEDIUM;
}

uint32_t zf_benchmark_media_time(void)
{
	return (uint32_t)k_uptime_get();
}

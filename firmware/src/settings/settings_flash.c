#include "settings/settings_flash.h"
#include "settings/settings.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(zf_settings_flash, LOG_LEVEL_INF);

#define SETTINGS_PARTITION storage_partition
#define SETTINGS_SECTOR_SIZE 4096u
#define VERIFY_CHUNK_SIZE 32u

int zf_settings_flash_read(uint8_t *record, size_t size)
{
	const struct flash_area *area;
	int err = flash_area_open(FIXED_PARTITION_ID(SETTINGS_PARTITION), &area);

	if (err) {
		return err;
	}
	err = flash_area_read(area, 0, record, size);
	flash_area_close(area);
	return err;
}

int zf_settings_flash_write(const uint8_t *record, size_t size)
{
	uint8_t verify[VERIFY_CHUNK_SIZE];
	const struct flash_area *area;
	int64_t started;
	int err;

	if (record == NULL || size == 0 || size > ZF_SETTINGS_RECORD_SIZE) {
		return -EINVAL;
	}
	err = flash_area_open(FIXED_PARTITION_ID(SETTINGS_PARTITION), &area);
	if (err) {
		return err;
	}
	started = k_uptime_get();
	err = flash_area_erase(area, 0, SETTINGS_SECTOR_SIZE);
	if (!err) {
		err = flash_area_write(area, 0, record, size);
	}
	for (size_t offset = 0; !err && offset < size; offset += sizeof(verify)) {
		size_t length = MIN(sizeof(verify), size - offset);

		err = flash_area_read(area, offset, verify, length);
		if (!err && memcmp(verify, record + offset, length) != 0) {
			err = -EIO;
		}
	}
	memset(verify, 0, sizeof(verify));
	flash_area_close(area);
	LOG_DBG("Settings flash operation: %lld ms, result %d", (long long)(k_uptime_get() - started), err);
	return err;
}

static const uint8_t *queued_record;
static size_t queued_size;
static zf_settings_done_t queued_done;
static atomic_t write_pending;
static atomic_t queue_started;
static struct k_work_q settings_work_queue;
K_THREAD_STACK_DEFINE(settings_work_stack, 3072);

static void settings_write_handler(struct k_work *work)
{
	zf_settings_done_t done = queued_done;
	int err = zf_settings_flash_write(queued_record, queued_size);

	ARG_UNUSED(work);
	queued_record = NULL;
	queued_size = 0;
	queued_done = NULL;
	atomic_clear(&write_pending);
	done(err);
}

static K_WORK_DEFINE(settings_write_work, settings_write_handler);

int zf_settings_flash_write_async(const uint8_t *record, size_t size, zf_settings_done_t done)
{
	if (record == NULL || done == NULL || size == 0 || size > ZF_SETTINGS_RECORD_SIZE) {
		return -EINVAL;
	}
	if (!atomic_cas(&write_pending, 0, 1)) {
		return -EBUSY;
	}
	if (atomic_cas(&queue_started, 0, 1)) {
		k_work_queue_start(&settings_work_queue, settings_work_stack, K_THREAD_STACK_SIZEOF(settings_work_stack),
				   5, NULL);
	}
	queued_record = record;
	queued_size = size;
	queued_done = done;
	if (k_work_submit_to_queue(&settings_work_queue, &settings_write_work) < 0) {
		queued_record = NULL;
		queued_size = 0;
		queued_done = NULL;
		atomic_clear(&write_pending);
		return -EIO;
	}
	return 0;
}

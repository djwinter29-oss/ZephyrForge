#include "platform/boot_flash.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(zf_boot_flash, LOG_LEVEL_INF);

#define SETTINGS_PARTITION storage_partition
#define SETTINGS_SECTOR_SIZE 4096u
#define SETTINGS_PAGE_SIZE 256u

int boot_flash_read(uint8_t *record, size_t size)
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

int boot_flash_write(const uint8_t *record, size_t size)
{
	static uint8_t page[SETTINGS_PAGE_SIZE];
	const struct flash_area *area;
	int64_t started;
	int err;

	if (size > sizeof(page)) {
		return -EINVAL;
	}
	err = flash_area_open(FIXED_PARTITION_ID(SETTINGS_PARTITION), &area);
	if (err) {
		return err;
	}
	memset(page, 0xFF, sizeof(page));
	memcpy(page, record, size);
	started = k_uptime_get();
	err = flash_area_erase(area, 0, SETTINGS_SECTOR_SIZE);
	if (!err) {
		err = flash_area_write(area, 0, page, sizeof(page));
	}
	if (!err) {
		err = flash_area_read(area, 0, page, sizeof(page));
	}
	if (!err && memcmp(page, record, size) != 0) {
		err = -EIO;
	}
	memset(page, 0, sizeof(page));
	flash_area_close(area);
	LOG_INF("Settings flash operation: %lld ms, result %d", (long long)(k_uptime_get() - started), err);
	return err;
}

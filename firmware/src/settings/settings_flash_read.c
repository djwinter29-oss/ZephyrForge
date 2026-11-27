#include "settings/settings_flash.h"

#include <zephyr/storage/flash_map.h>

#define SETTINGS_PARTITION storage_partition

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

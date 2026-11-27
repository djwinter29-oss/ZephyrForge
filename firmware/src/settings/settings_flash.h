/**
 * @file settings_flash.h
 * @brief Settings record backend: the first page of the `storage_partition` flash area.
 */

#ifndef ZEPHYRFORGE_SETTINGS_FLASH_H
#define ZEPHYRFORGE_SETTINGS_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include "settings/settings.h"

/**
 * @brief Read the start of the settings sector.
 * @return 0 on success, otherwise a negative errno.
 */
int zf_settings_flash_read(uint8_t *record, size_t size);

/**
 * @brief Erase the settings sector, program the record and verify it by readback.
 * @return 0 when readback matched, otherwise a negative errno.
 */
int zf_settings_flash_write(const uint8_t *record, size_t size);

/**
 * @brief Erase, program and verify a settings record on a work item, outside the application lock.
 *
 * The backend admits one write at a time and returns -EBUSY while that write is queued or running. The caller
 * must keep the record unchanged and available until @p done runs.
 * @param done Called once from the work item, without the application lock.
 * @return 0 when queued, otherwise a negative errno and @p done is not called.
 */
int zf_settings_flash_write_async(const uint8_t *record, size_t size, zf_settings_save_done_t done);

#endif

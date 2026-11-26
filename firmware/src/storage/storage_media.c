#include "storage/storage.h"
#include "app/build_config.h"
#include "transport/usb_msc.h"
#include "storage/storage_policy.h"

#include <errno.h>
#include <string.h>
#include <ff.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

LOG_MODULE_DECLARE(zf_storage, LOG_LEVEL_INF);

#define DISK_NAME ZF_STORAGE_DISK_NAME
#if ZF_STORAGE_SPI
#define BUS_CLOCK_HZ DT_PROP(DT_ALIAS(zephyrforge_sd), spi_max_frequency)
#else
#define BUS_CLOCK_HZ DT_PROP(DT_ALIAS(zephyrforge_sd), max_bus_freq)
#endif

/* Accessed only by the storage worker while it owns the media lock. */
static FATFS fatfs;
static struct fs_mount_t mount = {
	.type = FS_FATFS,
	.fs_data = &fatfs,
	.mnt_point = "/" DISK_NAME ":",
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static bool mounted;
static bool media_started;

int zf_storage_media_close(void)
{
	int err;

	if (mounted) {
		err = fs_unmount(&mount);
		if (err != 0) {
			LOG_ERR("SD unmount failed: %d", err);
			return err;
		}
		mounted = false;
	}
	if (media_started) {
		/* This adapter is the sole initializer; reset all references, including a failed FatFs mount. */
		bool force = true;

		err = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_DEINIT, &force);
		if (err != 0) {
			LOG_ERR("SD deinitialization failed: %d", err);
			return err;
		}
		media_started = false;
	}
	return 0;
}

int64_t zf_storage_media_time(void)
{
	return k_uptime_get();
}

int zf_storage_media_open(bool usb, zf_storage_t *storage)
{
	uint32_t sector_size = 0;
	int err = zf_storage_media_close();

	storage->initialized = false;
	storage->capacity_valid = false;
	storage->cid_valid = false;
	memset(storage->cid, 0, sizeof(storage->cid));
	storage->block_count = 0;
	storage->bus_clock_hz = 0;
	storage->bus_mode = 0;
	storage->filesystem_format = 0;
	if (err != 0) {
		return err;
	}
	media_started = true;
	err = disk_access_init(DISK_NAME);
	if (err == 0) {
		err = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &storage->block_count);
	}
	if (err == 0) {
		err = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	}
	if (err == 0 && (storage->block_count == 0 || sector_size != 512)) {
		err = -EINVAL;
	}
	if (err != 0) {
		LOG_ERR("SD hardware initialization failed: %d", err);
		storage->block_count = 0;
		(void)zf_storage_media_close();
		return err;
	}
	storage->initialized = true;
	storage->capacity_valid = true;
	storage->bus_clock_hz = BUS_CLOCK_HZ;
	storage->bus_mode = ZF_STORAGE_SPI ? ZF_STORAGE_BUS_SPI : ZF_STORAGE_BUS_SD4;
	err = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SD_CID, storage->cid);
	if (err == 0) {
		storage->cid_valid = true;
	} else {
		memset(storage->cid, 0, sizeof(storage->cid));
		LOG_ERR("SD CID unavailable: %d", err);
	}
	if (!usb) {
		err = fs_mount(&mount);
		if (err != 0) {
			LOG_ERR("SD mount failed: %d", err);
			/* FatFs may retain its registration even when Zephyr did not register the mount. */
			FRESULT result = f_mount(NULL, DISK_NAME ":", 0);

			if (result != FR_OK) {
				LOG_ERR("SD failed-mount cleanup failed: %d", result);
			}
			return err;
		}
		mounted = true;
		storage->filesystem_format = fatfs.fs_type <= 4 ? fatfs.fs_type : 0;
	} else {
#if ZF_ENABLE_USB_MSC
		err = zf_usb_msc_reconnect();
		if (err != 0) {
			return err;
		}
#else
		return -ENOTSUP;
#endif
	}
	LOG_INF("SD %s: %u sectors", usb ? "export ready" : "mounted", storage->block_count);
	return 0;
}

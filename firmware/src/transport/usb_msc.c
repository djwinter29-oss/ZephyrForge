#include "transport/usb_msc.h"
#include "transport/usb_device.h"
#include "storage/storage_export.h"
#include "storage/storage.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/usb/class/usbd_msc.h>

LOG_MODULE_DECLARE(zf_storage);

#define USB_DISK_NAME "USB_SD"
/* Long enough for hosts to observe the detach before the device reattaches. */
#define DETACH_MS 250
#define RETRY_MS 1000

/* SCSI INQUIRY limits the vendor to eight bytes, so use the short project name. */
USBD_DEFINE_MSC_LUN(zephyrforge_sd, USB_DISK_NAME, "ZForge", "SD card", "1.00");

static int usb_status(struct disk_info *disk)
{
	ARG_UNUSED(disk);
	return zf_storage_export_ready(NULL) ? DISK_STATUS_OK : DISK_STATUS_NOMEDIA;
}

static int usb_init(struct disk_info *disk)
{
	ARG_UNUSED(disk);
	return zf_storage_export_ready(NULL) ? 0 : -ENOMEDIUM;
}

static int usb_read(struct disk_info *disk, uint8_t *buffer, uint32_t sector, uint32_t count)
{
	int err;
	uint32_t blocks;

	ARG_UNUSED(disk);
	if (!zf_storage_export_lock(&blocks)) {
		return -ENOMEDIUM;
	}
	if (buffer == NULL || count == 0 || sector >= blocks || count > blocks - sector) {
		err = -EINVAL;
	} else {
		err = disk_access_read(ZF_STORAGE_DISK_NAME, buffer, sector, count);
	}
	if (err != 0) {
		LOG_ERR("USB SD read failed: %d", err);
	}
	zf_storage_export_unlock();
	return err;
}

static int usb_write(struct disk_info *disk, const uint8_t *buffer, uint32_t sector, uint32_t count)
{
	int err;
	uint32_t blocks;

	ARG_UNUSED(disk);
	if (!zf_storage_export_lock(&blocks)) {
		return -ENOMEDIUM;
	}
	if (buffer == NULL || count == 0 || sector >= blocks || count > blocks - sector) {
		err = -EINVAL;
	} else {
		err = disk_access_write(ZF_STORAGE_DISK_NAME, buffer, sector, count);
	}
	if (err != 0) {
		LOG_ERR("USB SD write failed: %d", err);
	}
	zf_storage_export_unlock();
	return err;
}

static int usb_ioctl(struct disk_info *disk, uint8_t command, void *buffer)
{
	int err;

	ARG_UNUSED(disk);
	if (command == DISK_IOCTL_CTRL_DEINIT) {
		/* Releasing proxy references must not deinitialize the storage worker's medium. */
		return 0;
	}
	if (!zf_storage_export_lock(NULL)) {
		return -ENOMEDIUM;
	}
	if (command == DISK_IOCTL_CTRL_INIT) {
		err = 0;
	} else if (command == DISK_IOCTL_CTRL_SYNC) {
		err = disk_access_ioctl(ZF_STORAGE_DISK_NAME, command, buffer);
	} else if (command == DISK_IOCTL_GET_SECTOR_COUNT || command == DISK_IOCTL_GET_SECTOR_SIZE ||
		   command == DISK_IOCTL_GET_ERASE_BLOCK_SZ) {
		err = buffer == NULL ? -EINVAL : disk_access_ioctl(ZF_STORAGE_DISK_NAME, command, buffer);
	} else {
		err = -ENOTSUP;
	}
	if (err != 0 && err != -ENOMEDIUM) {
		LOG_ERR("USB SD ioctl %u failed: %d", command, err);
	}
	zf_storage_export_unlock();
	return err;
}

static const struct disk_operations usb_ops = {
	.init = usb_init,
	.status = usb_status,
	.read = usb_read,
	.write = usb_write,
	.ioctl = usb_ioctl,
};
static struct disk_info usb_disk = {.name = USB_DISK_NAME, .ops = &usb_ops};

/* Card owner the host saw at the last successful enumeration. */
static atomic_t enumerated_usb;
static atomic_t reconnect_pending;
/* Detached and waiting to reattach; touched only by the reconnect work item. */
static bool detached;

/* Two phases on the system work queue: detach, then reattach after DETACH_MS without sleeping. */
static void reconnect_handler(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	int err;
	bool usb;

	if (!detached) {
		err = zf_usb_device_disable();
		if (err == 0) {
			detached = true;
			(void)k_work_reschedule(delayable, K_MSEC(DETACH_MS));
			return;
		}
	} else {
		err = zf_usb_device_enable();
		if (err == 0) {
			detached = false;
			if (zf_storage_export_owner(&usb)) {
				atomic_set(&enumerated_usb, usb);
			}
			atomic_clear(&reconnect_pending);
			return;
		}
	}
	LOG_ERR("USB storage refresh failed: %d", err);
	if (k_work_reschedule(delayable, K_MSEC(RETRY_MS)) < 0) {
		atomic_clear(&reconnect_pending);
	}
}

static K_WORK_DELAYABLE_DEFINE(reconnect_work, reconnect_handler);

void zf_usb_msc_poll(void)
{
	bool usb;

	if (!zf_storage_export_owner(&usb) || usb == (atomic_get(&enumerated_usb) != 0)) {
		return;
	}
	if (atomic_cas(&reconnect_pending, 0, 1) && k_work_reschedule(&reconnect_work, K_NO_WAIT) < 0) {
		atomic_clear(&reconnect_pending);
		LOG_ERR("Unable to queue USB storage refresh");
	}
}

int zf_usb_msc_init(void)
{
	bool usb;
	int err = disk_access_register(&usb_disk);

	if (err != 0) {
		LOG_ERR("USB mass-storage disk proxy failed: %d; continuing with HID only", err);
		return err;
	}
	(void)zf_storage_export_owner(&usb);
	atomic_set(&enumerated_usb, usb);
	zf_storage_export_attach();
	return 0;
}

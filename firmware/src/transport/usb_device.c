/*
 * Vendor HID management interface on the native USB OTG port.
 */
#include "transport/usb_device.h"
#include "platform/board.h"
#include <zephyr/sys/util.h>
#include "management/control.h"
#include "protocol/report_codec.h"
#include "management/report_builder.h"
#include "app/runtime.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(zf_usb, LOG_LEVEL_INF);

#define ZF_USB_VID 0xCAFE
#define ZF_USB_PID 0x4021
#define TELEMETRY_PERIOD_MS 250
#define STORAGE_PERIOD_MS 500
#define REPORT_KEEPALIVE_MS 1000
#if IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK)
#define USB_INPUT_REPORT_SIZE MAX(ZF_STORAGE_SIZE, MAX(ZF_TELEMETRY_SIZE, ZF_BENCHMARK_SIZE))
#else
#define USB_INPUT_REPORT_SIZE MAX(ZF_STORAGE_SIZE, ZF_TELEMETRY_SIZE)
#endif

#define FEATURE(id, size) 0x85, (id), 0x95, (size), 0x09, (id), 0xB1, 0x02
#define INPUT(id, size) 0x85, (id), 0x95, (size), 0x09, (id), 0x81, 0x02

static const uint8_t report_descriptor[] = {
	0x06, 0x00, 0xFF,
	0x09, 0x01,
	0xA1, 0x01,
	0x15, 0x00,
	0x26, 0xFF, 0x00,
	0x75, 0x08,
	FEATURE(ZF_REPORT_IDENTITY, ZF_IDENTITY_SIZE),
	FEATURE(ZF_REPORT_CONFIG, ZF_CONFIG_SIZE),
	FEATURE(ZF_REPORT_MEMORY, ZF_MEMORY_SIZE),
	INPUT(ZF_REPORT_TELEMETRY, ZF_TELEMETRY_SIZE),
	INPUT(ZF_REPORT_STORAGE, ZF_STORAGE_SIZE),
#if IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK)
	INPUT(ZF_REPORT_BENCHMARK, ZF_BENCHMARK_SIZE),
#endif
	FEATURE(ZF_REPORT_CONTROL, ZF_CONTROL_SIZE),
	FEATURE(ZF_REPORT_WIRELESS, ZF_WIRELESS_SIZE),
	0xC0,
};

USBD_DEVICE_DEFINE(zephyrforge_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), ZF_USB_VID, ZF_USB_PID);
USBD_DESC_LANG_DEFINE(zephyrforge_lang);
USBD_DESC_MANUFACTURER_DEFINE(zephyrforge_manufacturer, "djwinter29");
USBD_DESC_PRODUCT_DEFINE(zephyrforge_product, "ZephyrForge");
#if IS_ENABLED(CONFIG_ZF_USB_MSC)
USBD_DESC_CONFIG_DEFINE(zephyrforge_config_name, "HID + SD storage");
#else
USBD_DESC_CONFIG_DEFINE(zephyrforge_config_name, "HID management");
#endif
USBD_CONFIGURATION_DEFINE(zephyrforge_config, 0, 100, &zephyrforge_config_name);

/* The serial is Report 1's 8-byte UID; Zephyr's hwinfo serial would carry only the 6-byte MAC. */
static uint8_t serial_ascii[2 * ZF_UID_SIZE + 1];
static struct usbd_desc_node zephyrforge_serial = {
	.str = {
		.utype = USBD_DUT_STRING_SERIAL_NUMBER,
		.ascii7 = true,
	},
	.ptr = serial_ascii,
	.bLength = USB_STRING_DESCRIPTOR_LENGTH("0000000000000000"),
	.bDescriptorType = USB_DESC_STRING,
};

static const struct device *const hid = DEVICE_DT_GET(DT_NODELABEL(zephyrforge_hid));

static atomic_t ready;
static atomic_t in_flight;
static atomic_t cache_reset_pending;
static uint8_t input[1 + USB_INPUT_REPORT_SIZE] __aligned(4);
static uint8_t telemetry_sequence;
static uint8_t storage_sequence;
static uint8_t benchmark_sequence;
static uint8_t last_storage[ZF_STORAGE_SIZE];
static uint8_t last_benchmark[ZF_BENCHMARK_SIZE];
static bool storage_cache_valid;
static bool benchmark_cache_valid;
static int64_t telemetry_at;
static int64_t storage_check_at;
static int64_t benchmark_check_at;
static int64_t storage_sent_at;
static int64_t benchmark_sent_at;

static void iface_ready(const struct device *dev, const bool is_ready)
{
	ARG_UNUSED(dev);
	atomic_set(&ready, is_ready);
	atomic_clear(&in_flight);
	if (is_ready) {
		atomic_set(&cache_reset_pending, 1);
	}
	zf_app_wakeup();
	LOG_INF("HID interface %s", is_ready ? "ready" : "not ready");
}

static int get_report(const struct device *dev, const uint8_t type, const uint8_t id, const uint16_t len,
		      uint8_t *const buf)
{
	uint16_t size = zf_management_report_size(id);
	zf_management_report_type_t report_type = zf_management_report_type(id);

	ARG_UNUSED(dev);
	bool valid_type = (type == HID_REPORT_TYPE_FEATURE && report_type == ZF_MANAGEMENT_REPORT_FEATURE) ||
			  (type == HID_REPORT_TYPE_INPUT && report_type == ZF_MANAGEMENT_REPORT_INPUT);

	if (!valid_type || size == 0 || len < size + 1) {
		return -ENOTSUP;
	}

	buf[0] = id;
	if (id == ZF_REPORT_CONTROL) {
		zf_app_lock();
		zf_control_usb_get(buf + 1);
		zf_app_unlock();
	} else if (zf_management_report_build(id, 0, buf + 1) != size) {
		return -ENOTSUP;
	}
	return size + 1;
}

static int set_report(const struct device *dev, const uint8_t type, const uint8_t id, const uint16_t len,
		      const uint8_t *const buf)
{
	bool accepted;

	ARG_UNUSED(dev);
	if (type != HID_REPORT_TYPE_FEATURE || id != ZF_REPORT_CONTROL || len != ZF_CONTROL_SIZE + 1 ||
	    buf[0] != ZF_REPORT_CONTROL) {
		return -ENOTSUP;
	}
	zf_app_lock();
	accepted = zf_control_usb_set(buf + 1, ZF_CONTROL_SIZE, k_uptime_get());
	zf_app_unlock();
	/* A rejected envelope stalls the status stage and leaves the previous result readable. */
	return accepted ? 0 : -EINVAL;
}

static void input_report_done(const struct device *dev, const uint8_t *const report)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(report);
	atomic_clear(&in_flight);
	zf_app_wakeup();
}

static const struct hid_device_ops hid_ops = {
	.iface_ready = iface_ready,
	.get_report = get_report,
	.set_report = set_report,
	.input_report_done = input_report_done,
};

static bool submit(uint8_t id)
{
	uint8_t *sequence;
	uint8_t *cache = NULL;
	bool *cache_valid = NULL;
	int64_t *sent_at = NULL;
	uint8_t next_sequence;
	bool tracked = false;
	uint16_t size;

	if (!atomic_cas(&in_flight, 0, 1)) {
		return false;
	}
	input[0] = id;
	if (id == ZF_REPORT_TELEMETRY) {
		sequence = &telemetry_sequence;
		size = ZF_TELEMETRY_SIZE;
	} else if (id == ZF_REPORT_STORAGE) {
		sequence = &storage_sequence;
		cache = last_storage;
		cache_valid = &storage_cache_valid;
		sent_at = &storage_sent_at;
		tracked = true;
		size = ZF_STORAGE_SIZE;
	} else {
		sequence = &benchmark_sequence;
		cache = last_benchmark;
		cache_valid = &benchmark_cache_valid;
		sent_at = &benchmark_sent_at;
		tracked = true;
		size = ZF_BENCHMARK_SIZE;
	}
	next_sequence = (uint8_t)(*sequence + 1u);
	(void)zf_management_report_build(id, next_sequence, input + 1);
	if (tracked && *cache_valid && memcmp(input + 2, cache + 1, size - 1) == 0 &&
	    k_uptime_get() - *sent_at < REPORT_KEEPALIVE_MS) {
		atomic_clear(&in_flight);
		return false;
	}
	if (hid_device_submit_report(hid, size + 1, input) != 0) {
		atomic_clear(&in_flight);
		return false;
	}
	*sequence = next_sequence;
	if (tracked) {
		memcpy(cache, input + 1, size);
		*cache_valid = true;
		*sent_at = k_uptime_get();
	}
	return true;
}

void zf_usb_device_poll(void)
{
	int64_t now = k_uptime_get();

	if (!atomic_get(&ready)) {
		return;
	}
	if (atomic_cas(&cache_reset_pending, 1, 0)) {
		storage_cache_valid = false;
		benchmark_cache_valid = false;
		storage_check_at = 0;
		benchmark_check_at = 0;
	}
	if (now - telemetry_at >= TELEMETRY_PERIOD_MS && submit(ZF_REPORT_TELEMETRY)) {
		telemetry_at = now;
	} else if (IS_ENABLED(CONFIG_ZF_STORAGE) && now - storage_check_at >= STORAGE_PERIOD_MS) {
		storage_check_at = now;
		(void)submit(ZF_REPORT_STORAGE);
	} else if (IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK) && now - benchmark_check_at >= STORAGE_PERIOD_MS) {
		benchmark_check_at = now;
		(void)submit(ZF_REPORT_BENCHMARK);
	}
}

int64_t zf_usb_device_next_deadline(void)
{
	if (!atomic_get(&ready) || atomic_get(&in_flight)) {
		return INT64_MAX;
	}
	int64_t deadline = telemetry_at + TELEMETRY_PERIOD_MS;

	if (IS_ENABLED(CONFIG_ZF_STORAGE) && storage_check_at + STORAGE_PERIOD_MS < deadline) {
		deadline = storage_check_at + STORAGE_PERIOD_MS;
	}
	if (IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK) && benchmark_check_at + STORAGE_PERIOD_MS < deadline) {
		deadline = benchmark_check_at + STORAGE_PERIOD_MS;
	}
	return deadline;
}

bool zf_usb_device_configured(void)
{
	return atomic_get(&ready) != 0;
}

#if IS_ENABLED(CONFIG_ZF_USB_MSC)
int zf_usb_device_reconnect(void)
{
	int err = usbd_disable(&zephyrforge_usbd);

	if (err == 0) {
		k_sleep(K_MSEC(250));
		err = usbd_enable(&zephyrforge_usbd);
	}
	if (err != 0) {
		LOG_ERR("USB storage refresh failed: %d", err);
	}
	return err;
}
#endif

int zf_usb_device_init(void)
{
	int err;

	if (!device_is_ready(hid)) {
		LOG_ERR("HID device not ready");
		return -ENODEV;
	}
	err = hid_device_register(hid, report_descriptor, sizeof(report_descriptor), &hid_ops);
	if (err) {
		return err;
	}
	memcpy(serial_ascii, zf_board_serial(), sizeof(serial_ascii));

	err = usbd_add_descriptor(&zephyrforge_usbd, &zephyrforge_lang);
	err = err ? err : usbd_add_descriptor(&zephyrforge_usbd, &zephyrforge_manufacturer);
	err = err ? err : usbd_add_descriptor(&zephyrforge_usbd, &zephyrforge_product);
	err = err ? err : usbd_add_descriptor(&zephyrforge_usbd, &zephyrforge_serial);
	err = err ? err : usbd_add_configuration(&zephyrforge_usbd, USBD_SPEED_FS, &zephyrforge_config);
	err = err ? err : usbd_register_all_classes(&zephyrforge_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("USB device setup failed: %d", err);
		return err;
	}
	usbd_device_set_code_triple(&zephyrforge_usbd, USBD_SPEED_FS, 0, 0, 0);
	err = usbd_init(&zephyrforge_usbd);
	err = err ? err : usbd_enable(&zephyrforge_usbd);
	if (err) {
		LOG_ERR("USB enable failed: %d", err);
	}
	return err;
}

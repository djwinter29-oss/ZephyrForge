#include "platform/board.h"
#include "app/capabilities.h"
#include "platform/led.h"
#include "platform/sysclk.h"
#include "management/control.h"
#include "settings/settings.h"
#include "storage/storage.h"
#include "platform/telemetry.h"
#include "transport/usb_device.h"
#include "transport/ble_gatt.h"
#include "wifi/wifi.h"
#include "app/version.h"
#include "app/runtime.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define SERVICE_MAX_IDLE_MS 1000

int main(void)
{
	uint8_t radios;
	int64_t settings_started;
	int err;

	zf_board_init();
	zf_app_lock();
	settings_started = k_uptime_get();
	zf_settings_load();
	zf_settings_source_t settings_source = zf_settings_source();
	zf_control_init();
	radios = zf_settings_boot_radios() & ZF_RADIO_MASK;
	zf_app_unlock();
	printk("ZephyrForge %s (%s)\n", ZEPHYRFORGE_VERSION, zf_board_serial());
	printk("Boot settings load: %lld ms (source %u)\n",
	       (long long)(k_uptime_get() - settings_started), settings_source);
	if (settings_source == ZF_SETTINGS_READ_ERROR) {
		printk("Boot settings read failed; defaults are active\n");
	}

	err = zf_sysclk_apply(zf_sysclk_profile_mhz(zf_settings_boot_sysclk()));
	if (err) {
		printk("CPU clock change failed: %d\n", err);
	}
	zf_telemetry_init();
	zf_led_init();
	zf_storage_init();
	zf_wifi_init((radios & ZF_WIRELESS_WIFI) != 0);
	zf_ble_gatt_init((radios & ZF_WIRELESS_BLE) != 0);
	if (zf_usb_device_init() != 0) {
		printk("USB HID unavailable\n");
	}
	zf_storage_start();

	while (true) {
		int64_t deadline;
		int64_t telemetry_deadline;
		int64_t wifi_deadline;
		int64_t usb_deadline;

		zf_app_lock();
		zf_control_tick(k_uptime_get());
		deadline = zf_control_next_deadline();
		zf_app_unlock();
		zf_wifi_poll();
		zf_telemetry_poll();
		zf_usb_device_poll();
		int64_t now = k_uptime_get();
		telemetry_deadline = zf_telemetry_next_deadline();
		wifi_deadline = zf_wifi_next_deadline();
		usb_deadline = zf_usb_device_next_deadline();
		if (telemetry_deadline < deadline) {
			deadline = telemetry_deadline;
		}
		if (wifi_deadline < deadline) {
			deadline = wifi_deadline;
		}
		if (usb_deadline < deadline) {
			deadline = usb_deadline;
		}
		int64_t fallback = now + SERVICE_MAX_IDLE_MS;

		if (deadline == INT64_MAX || deadline > fallback) {
			deadline = fallback;
		}
		zf_app_wait(MAX(0, deadline - now));
	}
	return 0;
}

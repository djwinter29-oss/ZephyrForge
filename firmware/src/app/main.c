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
		zf_app_lock();
		zf_control_tick(k_uptime_get());
		int64_t control_deadline = zf_control_next_deadline();
		zf_app_unlock();
		zf_telemetry_set_active(zf_usb_device_configured() || zf_ble_gatt_state() == ZF_BLE_CONNECTED);
		zf_wifi_poll();
		zf_telemetry_poll();
		zf_usb_device_poll();
		int64_t now = k_uptime_get();
		const int64_t deadlines[] = {
			control_deadline,
			zf_telemetry_next_deadline(),
			zf_wifi_next_deadline(),
			zf_usb_device_next_deadline(),
		};
		int64_t deadline = INT64_MAX;

		for (size_t i = 0; i < ARRAY_SIZE(deadlines); ++i) {
			if (deadlines[i] < deadline) {
				deadline = deadlines[i];
			}
		}
		zf_app_wait(deadline == INT64_MAX ? INT64_MAX : MAX(0, deadline - now));
	}
	return 0;
}

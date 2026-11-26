#include "board/board.h"
#include "led/led.h"
#include "sysclk/sysclk.h"
#include "control/control.h"
#include "report/report.h"
#include "settings/settings.h"
#include "storage/storage.h"
#include "telemetry/telemetry.h"
#include "usb/usb.h"
#include "wireless/ble/ble.h"
#include "wireless/wifi/wifi.h"
#include "zephyrforge.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define POLL_PERIOD_MS 20

K_MUTEX_DEFINE(app_lock);

void zf_lock(void)
{
	(void)k_mutex_lock(&app_lock, K_FOREVER);
}

void zf_unlock(void)
{
	(void)k_mutex_unlock(&app_lock);
}

int main(void)
{
	uint8_t radios;
	int64_t settings_started;
	int err;

	zf_board_init();
	zf_lock();
	settings_started = k_uptime_get();
	zf_settings_load();
	zf_settings_source_t settings_source = zf_settings_source();
	zf_control_init();
	radios = zf_settings_boot_radios() & ZF_RADIO_MASK;
	zf_unlock();
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
	zf_ble_init((radios & ZF_WIRELESS_BLE) != 0);
#if CONFIG_ZF_USB_HID
	if (zf_usb_init() != 0) {
		printk("USB HID unavailable\n");
	}
#endif

	while (true) {
		zf_lock();
		zf_control_tick(k_uptime_get());
		zf_unlock();
		zf_wifi_poll();
		zf_telemetry_poll();
#if CONFIG_ZF_USB_HID
		zf_usb_poll();
#endif
		k_sleep(K_MSEC(POLL_PERIOD_MS));
	}
	return 0;
}

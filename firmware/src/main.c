#include "app/zephyrforge.h"
#include "app/protocol/hid_reports.h"
#include "platform/boot_config.h"
#include "app/control/control.h"
#include "platform/zf_platform.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define POLL_PERIOD_MS 20

int main(void)
{
	uint8_t radios;
	int64_t settings_started;
	int err;

	zf_board_init();
	zf_lock();
	settings_started = k_uptime_get();
	zf_boot_config_load();
	zf_boot_config_source_t settings_source = zf_boot_config_source();
	zf_control_init();
	radios = zf_boot_config_boot_radios() & ZF_RADIO_MASK;
	zf_unlock();
	printk("ZephyrForge %s (%s)\n", ZEPHYRFORGE_VERSION, zf_board_serial());
	printk("Boot settings load: %lld ms (source %u)\n",
	       (long long)(k_uptime_get() - settings_started), settings_source);
	if (settings_source == ZF_BOOT_CONFIG_READ_ERROR) {
		printk("Boot settings read failed; defaults are active\n");
	}

	err = zf_system_clock_apply(zf_sysclk_profile_mhz(zf_boot_config_boot_sysclk()));
	if (err) {
		printk("CPU clock change failed: %d\n", err);
	}
	zf_telemetry_init();
	zf_platform_led_init();
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

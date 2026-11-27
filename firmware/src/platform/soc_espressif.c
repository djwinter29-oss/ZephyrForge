/* Espressif SoC hooks: CPU clock and flash/PSRAM inventory. */

#include "platform/board.h"
#include "platform/sysclk.h"

#include <errno.h>
#include <esp_flash.h>
#if CONFIG_ESP_SPIRAM
#include <esp_psram.h>
#endif
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/esp32_clock_control.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zf_board, LOG_LEVEL_INF);

#if CONFIG_ESP_SPIRAM
extern char _ext_ram_start[];
extern char _ext_ram_end[];
#endif

int zf_sysclk_apply(uint16_t mhz)
{
	const struct device *clock = DEVICE_DT_GET(DT_NODELABEL(clock));
	struct esp32_clock_config config = {
		.cpu = {
			.clk_src = DT_PROP(DT_NODELABEL(cpu0), clock_source),
			.cpu_freq = mhz,
			.xtal_freq = DT_PROP(DT_NODELABEL(cpu0), xtal_freq),
		},
	};

	if (mhz == 0 || mhz * 1000000u == DT_PROP(DT_NODELABEL(cpu0), clock_frequency)) {
		return 0;
	}
	if (!device_is_ready(clock)) {
		return -ENODEV;
	}
	return clock_control_configure(clock, (clock_control_subsys_t)ESP32_CLOCK_CONTROL_SUBSYS_CPU, &config);
}

uint32_t zf_sysclk_hz(void)
{
	const struct device *clock = DEVICE_DT_GET(DT_NODELABEL(clock));
	uint32_t rate = 0;

	if (!device_is_ready(clock) ||
	    clock_control_get_rate(clock, (clock_control_subsys_t)ESP32_CLOCK_CONTROL_SUBSYS_CPU, &rate) != 0) {
		return DT_PROP(DT_NODELABEL(cpu0), clock_frequency);
	}
	return rate;
}

void zf_soc_memory_inventory(zf_memory_inventory_t *out)
{
#if CONFIG_ESP_SPIRAM
	out->psram_available = esp_psram_is_initialized();
	if (out->psram_available) {
		out->psram_total_bytes = esp_psram_get_size();
		out->psram_reserved_bytes = (uintptr_t)_ext_ram_end - (uintptr_t)_ext_ram_start;
	} else {
		LOG_ERR("PSRAM inventory unavailable: external RAM is not initialized");
	}
#endif
	int err = esp_flash_get_physical_size(esp_flash_default_chip, &out->flash_total_bytes);

	out->flash_available = err == ESP_OK;
	if (err != ESP_OK) {
		out->flash_total_bytes = 0;
		LOG_ERR("Module flash capacity read failed: %d", err);
	}
}

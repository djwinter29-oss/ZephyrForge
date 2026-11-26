#include "platform/telemetry.h"
#include "platform/sysclk.h"
#include "protocol/report_codec.h"
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#define LOAD_WINDOW_MS 250
#define TEMPERATURE_PERIOD_MS 1000

static const struct device *const temperature_sensor = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(coretemp));

static bool temperature_valid;
static int16_t temperature_centi_c;
static int64_t temperature_at;
static uint16_t core0_load = ZF_LOAD_UNAVAILABLE;
static bool sampling_active;
#if IS_ENABLED(CONFIG_ZF_CPU_LOAD)
static uint64_t window_busy;
static uint64_t window_total;
static int64_t window_at;
#endif
K_MUTEX_DEFINE(telemetry_lock);

#if IS_ENABLED(CONFIG_ZF_CPU_LOAD)
static bool cpu_cycles(uint64_t *busy, uint64_t *total)
{
	k_thread_runtime_stats_t stats;

	if (k_thread_runtime_stats_cpu_get(0, &stats) != 0) {
		return false;
	}
	*busy = stats.total_cycles;
	*total = stats.execution_cycles;
	return true;
}
#endif

static void read_temperature(void)
{
	struct sensor_value value;
	bool valid = false;
	int16_t centi = 0;

	if (temperature_sensor != NULL && device_is_ready(temperature_sensor) &&
	    sensor_sample_fetch(temperature_sensor) == 0 &&
	    sensor_channel_get(temperature_sensor, SENSOR_CHAN_DIE_TEMP, &value) == 0) {
		int64_t micro = sensor_value_to_micro(&value);

		centi = (int16_t)CLAMP(micro / 10000, INT16_MIN, INT16_MAX);
		valid = true;
	}
	k_mutex_lock(&telemetry_lock, K_FOREVER);
	temperature_valid = valid;
	temperature_centi_c = centi;
	k_mutex_unlock(&telemetry_lock);
}

void zf_telemetry_init(void)
{
	sampling_active = false;
	temperature_valid = false;
	temperature_centi_c = 0;
	core0_load = ZF_LOAD_UNAVAILABLE;
#if IS_ENABLED(CONFIG_ZF_CPU_LOAD)
	window_at = k_uptime_get();
	(void)cpu_cycles(&window_busy, &window_total);
#endif
	temperature_at = k_uptime_get();
}

void zf_telemetry_set_active(bool active)
{
	if (sampling_active == active) {
		return;
	}
	sampling_active = active;
	if (!active) {
		return;
	}

	int64_t now = k_uptime_get();

#if IS_ENABLED(CONFIG_ZF_CPU_LOAD)
	window_at = now;
	(void)cpu_cycles(&window_busy, &window_total);
#endif
	temperature_at = now - TEMPERATURE_PERIOD_MS;
	k_mutex_lock(&telemetry_lock, K_FOREVER);
	temperature_valid = false;
	core0_load = ZF_LOAD_UNAVAILABLE;
	k_mutex_unlock(&telemetry_lock);
}

void zf_telemetry_poll(void)
{
	if (!sampling_active) {
		return;
	}
	int64_t now = k_uptime_get();
#if IS_ENABLED(CONFIG_ZF_CPU_LOAD)
	uint64_t busy;
	uint64_t total;

	if (now - window_at >= LOAD_WINDOW_MS && cpu_cycles(&busy, &total)) {
		uint16_t load = zf_report_load_basis_points(busy - window_busy, total - window_total);

		k_mutex_lock(&telemetry_lock, K_FOREVER);
		core0_load = load;
		k_mutex_unlock(&telemetry_lock);
		window_busy = busy;
		window_total = total;
		window_at = now;
	}
#endif
	if (now - temperature_at >= TEMPERATURE_PERIOD_MS) {
		read_temperature();
		temperature_at = now;
	}
}

int64_t zf_telemetry_next_deadline(void)
{
	if (!sampling_active) {
		return INT64_MAX;
	}
	int64_t deadline = temperature_at + TEMPERATURE_PERIOD_MS;

#if IS_ENABLED(CONFIG_ZF_CPU_LOAD)
	if (window_at + LOAD_WINDOW_MS < deadline) {
		deadline = window_at + LOAD_WINDOW_MS;
	}
#endif
	return deadline;
}

void zf_telemetry_report(uint8_t sequence, bool usb_configured, bool led_available, uint8_t led_colour,
			 uint8_t *out)
{
	zf_telemetry_t telemetry = {
		.uptime_ms = (uint64_t)k_uptime_get(),
		.system_clock_hz = zf_sysclk_hz(),
		.sequence = sequence,
		.usb_configured = usb_configured,
		.led_available = led_available,
		.led_colour = led_colour,
	};

	k_mutex_lock(&telemetry_lock, K_FOREVER);
	telemetry.temperature_valid = temperature_valid;
	telemetry.temperature_centi_c = temperature_centi_c;
	telemetry.core0_load = core0_load;
	k_mutex_unlock(&telemetry_lock);
	zf_report_encode_telemetry(&telemetry, out);
}

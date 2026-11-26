#include "management/report_builder.h"

#include "platform/board.h"
#include "app/capabilities.h"
#include "protocol/report_codec.h"
#include "management/control.h"
#include "management/telemetry.h"
#include "transport/ble_gatt.h"
#include "transport/usb_device.h"
#include "platform/led.h"
#include "storage/storage.h"
#include "benchmark/benchmark.h"
#include "app/runtime.h"
#include "app/version.h"
#include "settings/settings.h"
#include "platform/sysclk.h"
#include "wifi/wifi.h"

#include <stddef.h>

/** Report ID metadata shared by management sizing and USB report-type validation. */
typedef struct {
	uint8_t id; /**< ZF_REPORT_* identifier. */
	uint16_t size; /**< Payload size excluding the transport report ID. */
	zf_management_report_type_t type; /**< HID feature or input category. */
} report_descriptor_t;

static const report_descriptor_t reports[] = {
	{ZF_REPORT_IDENTITY, ZF_IDENTITY_SIZE, ZF_MANAGEMENT_REPORT_FEATURE},
	{ZF_REPORT_CONFIG, ZF_CONFIG_SIZE, ZF_MANAGEMENT_REPORT_FEATURE},
	{ZF_REPORT_MEMORY, ZF_MEMORY_SIZE, ZF_MANAGEMENT_REPORT_FEATURE},
	{ZF_REPORT_TELEMETRY, ZF_TELEMETRY_SIZE, ZF_MANAGEMENT_REPORT_INPUT},
	{ZF_REPORT_STORAGE, ZF_STORAGE_SIZE, ZF_MANAGEMENT_REPORT_INPUT},
#if IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK)
	{ZF_REPORT_BENCHMARK, ZF_BENCHMARK_SIZE, ZF_MANAGEMENT_REPORT_INPUT},
#endif
	{ZF_REPORT_WIRELESS, ZF_WIRELESS_SIZE, ZF_MANAGEMENT_REPORT_FEATURE},
	{ZF_REPORT_CONTROL, ZF_CONTROL_SIZE, ZF_MANAGEMENT_REPORT_FEATURE},
};

uint16_t zf_management_report_size(uint8_t id)
{
	for (size_t i = 0; i < sizeof(reports) / sizeof(reports[0]); ++i) {
		if (reports[i].id == id) {
			return reports[i].size;
		}
	}
	return 0;
}

zf_management_report_type_t zf_management_report_type(uint8_t id)
{
	for (size_t i = 0; i < sizeof(reports) / sizeof(reports[0]); ++i) {
		if (reports[i].id == id) {
			return reports[i].type;
		}
	}
	return ZF_MANAGEMENT_REPORT_UNSUPPORTED;
}

static void wireless_report(uint8_t *out)
{
	zf_wireless_t status = {0};
	uint8_t stage_flags = 0;
	bool saved_network;

	zf_app_lock();
	status.ble_state = zf_ble_gatt_state();
	/* Detail 1 reports a terminal radio start failure unless Wi-Fi has a more specific detail. */
	status.detail = zf_ble_gatt_failed() ? ZF_WIRELESS_DETAIL_RADIO : ZF_WIRELESS_DETAIL_NONE;
	saved_network = zf_wifi_status(&status);
	zf_control_staging_status(&stage_flags, &status.staged_state);
	status.flags = (uint8_t)((saved_network ? 0x01u : 0u) | (zf_settings_ble_key_configured() ? 0x02u : 0u) |
				 (stage_flags & 0xF0u));
	zf_app_unlock();
	zf_report_encode_wireless(&status, out);
}

uint16_t zf_management_report_build(uint8_t id, uint8_t sequence, uint8_t *out)
{
	switch (id) {
	case ZF_REPORT_IDENTITY: {
		zf_identity_t identity = {0};

		zf_board_identity_snapshot(&identity);
		identity.firmware[0] = ZEPHYRFORGE_VERSION_MAJOR;
		identity.firmware[1] = ZEPHYRFORGE_VERSION_MINOR;
		identity.firmware[2] = ZEPHYRFORGE_VERSION_PATCH;
		identity.mcu = ZF_MCU;
		identity.board_model = ZF_BOARD_MODEL;
		identity.capabilities = ZF_CAPABILITIES;
		zf_report_encode_identity(&identity, out);
		return ZF_IDENTITY_SIZE;
	}
	case ZF_REPORT_CONFIG: {
		zf_config_t config = {0};

		zf_board_config_snapshot(&config);
		zf_app_lock();
		config.stored_clock_mhz = zf_sysclk_profile_mhz(zf_settings_sysclk());
		/* Boards without radios still load the default switches; they are never advertised. */
		config.stored_radios = zf_settings_radios() & ZF_RADIO_MASK;
		zf_app_unlock();
		zf_report_encode_config(&config, out);
		return ZF_CONFIG_SIZE;
	}
	case ZF_REPORT_MEMORY: {
		zf_memory_inventory_t memory = {0};

		zf_board_memory_snapshot(&memory);
		zf_report_encode_memory(&memory, out);
		return ZF_MEMORY_SIZE;
	}
	case ZF_REPORT_TELEMETRY: {
		uint8_t colour;
		bool available;

		zf_led_status(&colour, &available);
		zf_telemetry_report(sequence, zf_usb_device_configured(), available, colour, out);
		return ZF_TELEMETRY_SIZE;
	}
	case ZF_REPORT_STORAGE:
		zf_storage_report(sequence, out);
		return ZF_STORAGE_SIZE;
#if IS_ENABLED(CONFIG_ZF_STORAGE_BENCHMARK)
	case ZF_REPORT_BENCHMARK:
		zf_benchmark_report(sequence, out);
		return ZF_BENCHMARK_SIZE;
#endif
	case ZF_REPORT_WIRELESS:
		wireless_report(out);
		return ZF_WIRELESS_SIZE;
	default:
		return 0;
	}
}

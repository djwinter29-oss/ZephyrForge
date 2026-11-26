#include "app/zephyrforge.h"
#include "app/protocol/hid_reports.h"
#include "app/control/control.h"
#include "platform/boot_config.h"
#include "platform/zf_platform.h"

#include <errno.h>

void zf_wifi_init(bool enabled)
{
	(void)enabled;
}

void zf_wifi_poll(void)
{
}

bool zf_wifi_test_active(void)
{
	return false;
}

bool zf_wifi_scan_active(void)
{
	return false;
}

int zf_wifi_test_start(const uint8_t *ssid, uint8_t ssid_length, const char *password)
{
	(void)ssid;
	(void)ssid_length;
	(void)password;
	return -ENOTSUP;
}

void zf_wifi_test_cancel(void)
{
}

int zf_wifi_scan_start(void)
{
	return -ENOTSUP;
}

uint8_t zf_wifi_scan_state(void)
{
	return ZF_SCAN_IDLE;
}

uint16_t zf_wifi_scan_generation(void)
{
	return 0;
}

uint8_t zf_wifi_scan_count(void)
{
	return 0;
}

int zf_wifi_scan_entry(uint8_t index, uint8_t *ssid, uint8_t *ssid_length, int8_t *rssi, uint8_t *security)
{
	(void)index;
	(void)ssid;
	(void)ssid_length;
	(void)rssi;
	(void)security;
	return -ENOTSUP;
}

void zf_wireless_report(uint8_t *out)
{
	zf_wireless_t status = {0};
	uint8_t stage_flags = 0;

	zf_lock();
	status.ble_state = zf_ble_state();
	status.detail = zf_ble_failed() ? 1u : 0u;
	status.ssid_length = zf_boot_config_wifi_ssid_bytes(status.ssid);
	zf_control_stage_status(&stage_flags, &status.staged_state);
	status.flags = (uint8_t)((status.ssid_length ? 0x01u : 0u) |
				 (zf_boot_config_ble_key_configured() ? 0x02u : 0u) | (stage_flags & 0xF0u));
	zf_unlock();
	zf_report_wireless(&status, out);
}

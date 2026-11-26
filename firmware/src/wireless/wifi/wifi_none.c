#include "report/report.h"
#include "settings/settings.h"
#include "wireless/wifi/wifi.h"

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

bool zf_wifi_test_joined(void)
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

bool zf_wifi_status(zf_wireless_t *status)
{
	status->ssid_length = zf_settings_wifi_ssid_bytes(status->ssid);
	return status->ssid_length != 0;
}

/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ji Dong
 */

/*
 * Raspberry Pi Pico family board services. The boards have no addressable RGB LED and keep their devicetree clock.
 * Wi-Fi services are compiled separately on W boards; Bluetooth has no supported shared-bus HCI transport.
 */

#include "app/zephyrforge.h"
#include "app/usb/hid_reports.h"
#include "platform/zf_platform.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define CLK_SYS_HZ DT_PROP(DT_NODELABEL(clk_sys), clock_frequency)

int zf_system_clock_apply(uint16_t mhz)
{
	return mhz == 0 || mhz * 1000000u == CLK_SYS_HZ ? 0 : -ENOTSUP;
}

uint32_t zf_system_clock_hz(void)
{
	return CLK_SYS_HZ;
}

void zf_platform_led_init(void)
{
}

int zf_platform_led_request(uint8_t colour)
{
	ARG_UNUSED(colour);
	return -ENOTSUP;
}

void zf_platform_led_status(uint8_t *colour, bool *available)
{
	*colour = 0;
	*available = false;
}

#ifndef CONFIG_WIFI
void zf_wifi_init(bool enabled)
{
	ARG_UNUSED(enabled);
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
	ARG_UNUSED(ssid);
	ARG_UNUSED(ssid_length);
	ARG_UNUSED(password);
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
	ARG_UNUSED(index);
	ARG_UNUSED(ssid);
	ARG_UNUSED(ssid_length);
	ARG_UNUSED(rssi);
	ARG_UNUSED(security);
	return -ENOTSUP;
}
#endif

void zf_ble_init(bool enabled)
{
	ARG_UNUSED(enabled);
}

uint8_t zf_ble_state(void)
{
	return ZF_BLE_OFF;
}

bool zf_ble_failed(void)
{
	return false;
}

#ifndef CONFIG_WIFI
void zf_wireless_report(uint8_t *out)
{
	const zf_wireless_t off = {0};

	zf_report_wireless(&off, out);
}
#endif

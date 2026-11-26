/**
 * @file wifi_status.h
 * @brief Wi-Fi contribution to the Feature 12 wireless status shared by Wi-Fi and Wi-Fi-less images.
 */
#ifndef ZEPHYRFORGE_WIFI_STATUS_H
#define ZEPHYRFORGE_WIFI_STATUS_H

#include <stdbool.h>

#include "app/protocol/hid_reports.h"

/**
 * @brief Fill the Wi-Fi fields of a Feature 12 snapshot; called with the application lock held.
 * @param status Snapshot whose ble_state and Bluetooth-failure detail are already set. The Wi-Fi image may
 *        replace detail with a Wi-Fi join detail; it fills wifi_state, SSID, IPv4 and scan fields.
 * @return True when a saved network is configured (Feature 12 flags bit 0).
 */
bool zf_wifi_status(zf_wireless_t *status);

#endif

/**
 * @file wireless_status.h
 * @brief Feature 12 wireless status, built once from the Wi-Fi, Bluetooth and staging contributions.
 */
#ifndef ZEPHYRFORGE_MANAGEMENT_WIRELESS_STATUS_H
#define ZEPHYRFORGE_MANAGEMENT_WIRELESS_STATUS_H

#include <stdint.h>

/** Encode Feature 12: Wi-Fi, BLE and staging state. */
void zf_management_wireless_report(uint8_t *out);

#endif

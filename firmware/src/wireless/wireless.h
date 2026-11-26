/**
 * @file wireless.h
 * @brief Feature 12 wireless status, built once from the Wi-Fi, Bluetooth and staging contributions.
 */
#ifndef ZEPHYRFORGE_WIRELESS_H
#define ZEPHYRFORGE_WIRELESS_H

#include <stdint.h>

/** Encode Feature 12: Wi-Fi, BLE and staging state. */
void zf_wireless_report(uint8_t *out);

#endif

/**
 * @file wireless_types.h
 * @brief Shared wireless setting and report field values.
 */
#ifndef ZEPHYRFORGE_WIRELESS_TYPES_H
#define ZEPHYRFORGE_WIRELESS_TYPES_H

/** Wi-Fi radio master-switch bit. */
#define ZF_WIRELESS_WIFI 1u
/** BLE radio master-switch bit. */
#define ZF_WIRELESS_BLE 2u
/** Maximum Wi-Fi SSID length in bytes, excluding a string terminator. */
#define ZF_SSID_MAX_BYTES 32u
/** Buffer size for a NUL-terminated Wi-Fi SSID. */
#define ZF_SSID_BUFFER_SIZE (ZF_SSID_MAX_BYTES + 1u)

#endif

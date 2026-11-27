/**
 * @file ble_gatt.h
 * @brief Bluetooth LE management transport; a Bluetooth-less image compiles inline stubs that stay off.
 */
#ifndef ZEPHYRFORGE_BLE_GATT_H
#define ZEPHYRFORGE_BLE_GATT_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include "protocol/management_protocol.h"

#if IS_ENABLED(CONFIG_ZF_BLUETOOTH)
/** Start Bluetooth when the saved switch is on and a Control key is saved. */
void zf_ble_gatt_init(bool enabled);
/** Current Report 12 ble_state value. */
uint8_t zf_ble_gatt_state(void);
/** True when the Bluetooth start failed terminally (no retry until reboot). */
bool zf_ble_gatt_failed(void);
#else
static inline void zf_ble_gatt_init(bool enabled)
{
	(void)enabled;
}

static inline uint8_t zf_ble_gatt_state(void)
{
	return ZF_BLE_OFF;
}

static inline bool zf_ble_gatt_failed(void)
{
	return false;
}
#endif

#endif

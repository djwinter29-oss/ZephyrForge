#include "app/zephyrforge.h"

void zf_ble_init(bool enabled)
{
	(void)enabled;
}

uint8_t zf_ble_state(void)
{
	return ZF_BLE_OFF;
}

bool zf_ble_failed(void)
{
	return false;
}

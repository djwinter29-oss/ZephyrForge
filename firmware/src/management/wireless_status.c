#include "management/wireless_status.h"
#include "management/control.h"
#include "protocol/report_codec.h"
#include "settings/settings.h"
#include "transport/ble_gatt.h"
#include "wifi/wifi.h"
#include "app/runtime.h"

void zf_management_wireless_report(uint8_t *out)
{
	zf_wireless_t status = {0};
	uint8_t stage_flags = 0;
	bool saved_network;

	zf_app_lock();
	status.ble_state = zf_ble_gatt_state();
	/* Detail 1 reports a terminal radio start failure unless Wi-Fi has a more specific detail. */
	status.detail = zf_ble_gatt_failed() ? 1u : 0u;
	saved_network = zf_wifi_status(&status);
	zf_control_staging_status(&stage_flags, &status.staged_state);
	status.flags = (uint8_t)((saved_network ? 0x01u : 0u) | (zf_settings_ble_key_configured() ? 0x02u : 0u) |
				 (stage_flags & 0xF0u));
	zf_app_unlock();
	zf_report_encode_wireless(&status, out);
}

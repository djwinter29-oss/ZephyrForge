#include "app/wireless/wifi/wifi_status.h"
#include "app/zephyrforge.h"
#include "app/control/control.h"
#include "platform/boot_config.h"

void zf_wireless_report(uint8_t *out)
{
	zf_wireless_t status = {0};
	uint8_t stage_flags = 0;
	bool saved_network;

	zf_lock();
	status.ble_state = zf_ble_state();
	/* Detail 1 reports a terminal radio start failure unless Wi-Fi has a more specific detail. */
	status.detail = zf_ble_failed() ? 1u : 0u;
	saved_network = zf_wifi_status(&status);
	zf_control_stage_status(&stage_flags, &status.staged_state);
	status.flags = (uint8_t)((saved_network ? 0x01u : 0u) | (zf_boot_config_ble_key_configured() ? 0x02u : 0u) |
				 (stage_flags & 0xF0u));
	zf_unlock();
	zf_report_wireless(&status, out);
}

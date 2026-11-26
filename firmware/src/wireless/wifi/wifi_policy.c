/* SPDX-License-Identifier: BSD-3-Clause */
#include "wireless/wifi/wifi_policy.h"

zf_wifi_policy_down_action_t zf_wifi_policy_link_down(zf_wifi_owner_t owner, zf_wifi_command_t command)
{
	if (owner == OWNER_TEST) {
		if (command == COMMAND_TESTING) {
			return ZF_WIFI_POLICY_FAIL_TEST;
		}
		if (command == COMMAND_JOINED) {
			return ZF_WIFI_POLICY_TEST_LINK_LOST;
		}
	}
	if (owner == OWNER_SAVED) {
		return ZF_WIFI_POLICY_RETRY_SAVED;
	}
	return ZF_WIFI_POLICY_RELEASE;
}

bool zf_wifi_policy_driver_operation_available(bool scan_active, bool connect_quarantined)
{
	return !scan_active && !connect_quarantined;
}

/* SPDX-License-Identifier: BSD-3-Clause */
#include "wifi/wifi_policy.h"

zf_wifi_policy_down_action_t zf_wifi_policy_link_down(zf_wifi_owner_t owner, zf_wifi_command_t command)
{
	if (owner == ZF_WIFI_OWNER_TEST) {
		if (command == ZF_WIFI_COMMAND_TESTING) {
			return ZF_WIFI_POLICY_FAIL_TEST;
		}
		if (command == ZF_WIFI_COMMAND_JOINED) {
			return ZF_WIFI_POLICY_TEST_LINK_LOST;
		}
	}
	if (owner == ZF_WIFI_OWNER_SAVED) {
		return ZF_WIFI_POLICY_RETRY_SAVED;
	}
	return ZF_WIFI_POLICY_RELEASE;
}

bool zf_wifi_policy_test_in_progress(zf_wifi_command_t command)
{
	return command == ZF_WIFI_COMMAND_QUEUED || command == ZF_WIFI_COMMAND_TESTING;
}

zf_wifi_link_action_t zf_wifi_policy_link_work_action(zf_wifi_link_work_t work)
{
	switch (work) {
	case ZF_WIFI_LINK_WORK_LEAVE:
		return ZF_WIFI_LINK_ACTION_LEAVE;
	case ZF_WIFI_LINK_WORK_VERIFY_UP:
		return ZF_WIFI_LINK_ACTION_VERIFY_UP;
	case ZF_WIFI_LINK_WORK_VERIFY_DOWN:
		return ZF_WIFI_LINK_ACTION_VERIFY_DOWN;
	case ZF_WIFI_LINK_WORK_IDLE:
	default:
		return ZF_WIFI_LINK_ACTION_NONE;
	}
}

bool zf_wifi_policy_driver_operation_available(bool scan_busy, bool connect_quarantined)
{
	return !scan_busy && !connect_quarantined;
}

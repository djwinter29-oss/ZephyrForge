#include "wifi/wifi_policy.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_confirmed_down_selects_owner_specific_terminal_action(void)
{
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_FAIL_TEST,
			  zf_wifi_policy_link_down(ZF_WIFI_OWNER_TEST, ZF_WIFI_COMMAND_TESTING));
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_TEST_LINK_LOST,
			  zf_wifi_policy_link_down(ZF_WIFI_OWNER_TEST, ZF_WIFI_COMMAND_JOINED));
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_RETRY_SAVED,
			  zf_wifi_policy_link_down(ZF_WIFI_OWNER_SAVED, ZF_WIFI_COMMAND_IDLE));
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_RELEASE,
			  zf_wifi_policy_link_down(ZF_WIFI_OWNER_NONE, ZF_WIFI_COMMAND_IDLE));
}

void test_candidate_command_is_in_progress_while_queued_or_joining(void)
{
	TEST_ASSERT_TRUE(zf_wifi_policy_test_in_progress(ZF_WIFI_COMMAND_QUEUED));
	TEST_ASSERT_TRUE(zf_wifi_policy_test_in_progress(ZF_WIFI_COMMAND_TESTING));
	TEST_ASSERT_FALSE(zf_wifi_policy_test_in_progress(ZF_WIFI_COMMAND_IDLE));
	TEST_ASSERT_FALSE(zf_wifi_policy_test_in_progress(ZF_WIFI_COMMAND_JOINED));
	TEST_ASSERT_FALSE(zf_wifi_policy_test_in_progress(ZF_WIFI_COMMAND_FAILED));
}

void test_link_work_phase_selects_one_exclusive_operation(void)
{
	TEST_ASSERT_EQUAL(ZF_WIFI_LINK_ACTION_NONE,
			  zf_wifi_policy_link_work_action(ZF_WIFI_LINK_WORK_IDLE));
	TEST_ASSERT_EQUAL(ZF_WIFI_LINK_ACTION_LEAVE,
			  zf_wifi_policy_link_work_action(ZF_WIFI_LINK_WORK_LEAVE));
	TEST_ASSERT_EQUAL(ZF_WIFI_LINK_ACTION_VERIFY_UP,
			  zf_wifi_policy_link_work_action(ZF_WIFI_LINK_WORK_VERIFY_UP));
	TEST_ASSERT_EQUAL(ZF_WIFI_LINK_ACTION_VERIFY_DOWN,
			  zf_wifi_policy_link_work_action(ZF_WIFI_LINK_WORK_VERIFY_DOWN));
}

void test_driver_operation_is_blocked_by_pending_scan_or_connection_quarantine(void)
{
	TEST_ASSERT_TRUE(zf_wifi_policy_driver_operation_available(false, false));
	TEST_ASSERT_FALSE(zf_wifi_policy_driver_operation_available(true, false));
	TEST_ASSERT_FALSE(zf_wifi_policy_driver_operation_available(false, true));
}

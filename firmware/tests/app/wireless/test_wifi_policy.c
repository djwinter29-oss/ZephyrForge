#include "app/wireless/wifi/wifi_policy.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_confirmed_down_selects_owner_specific_terminal_action(void)
{
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_FAIL_TEST,
			  zf_wifi_policy_link_down(OWNER_TEST, COMMAND_TESTING));
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_TEST_LINK_LOST,
			  zf_wifi_policy_link_down(OWNER_TEST, COMMAND_JOINED));
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_RETRY_SAVED,
			  zf_wifi_policy_link_down(OWNER_SAVED, COMMAND_IDLE));
	TEST_ASSERT_EQUAL(ZF_WIFI_POLICY_RELEASE,
			  zf_wifi_policy_link_down(OWNER_NONE, COMMAND_IDLE));
}

void test_driver_operation_requires_both_quarantines_to_be_clear(void)
{
	TEST_ASSERT_TRUE(zf_wifi_policy_driver_operation_available(false, false));
	TEST_ASSERT_FALSE(zf_wifi_policy_driver_operation_available(true, false));
	TEST_ASSERT_FALSE(zf_wifi_policy_driver_operation_available(false, true));
}

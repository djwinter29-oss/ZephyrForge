#include "unity.h"
#include "scan_table.h"

#include <stdio.h>
#include <string.h>

static zf_scan_table_t table;

void setUp(void)
{
	memset(&table, 0, sizeof(table));
}

void tearDown(void)
{
}

static void add(const char *ssid, int rssi)
{
	zf_scan_table_add(&table, (const uint8_t *)ssid, (uint8_t)strlen(ssid), rssi, 0x05u);
}

void test_duplicates_keep_strongest_and_empty_ssid_is_ignored(void)
{
	add("", -10);
	add("home", -70);
	add("home", -80);
	add("home", -40);
	TEST_ASSERT_EQUAL_UINT8(1, table.count);
	TEST_ASSERT_EQUAL_INT8(-40, table.entries[0].rssi);
	TEST_ASSERT_EQUAL_UINT8(0x05u, table.entries[0].security);
}

void test_full_table_evicts_weakest_only_for_stronger_result(void)
{
	char ssid[4];

	for (uint8_t i = 0; i < ZF_SCAN_TABLE_MAX; ++i) {
		snprintf(ssid, sizeof(ssid), "n%u", i);
		add(ssid, -50 - i);
	}
	add("weak", -90);
	TEST_ASSERT_EQUAL_UINT8(ZF_SCAN_TABLE_MAX, table.count);
	for (uint8_t i = 0; i < table.count; ++i) {
		TEST_ASSERT_NOT_EQUAL(0, memcmp(table.entries[i].ssid, "weak", 4));
	}
	add("strong", -20);
	TEST_ASSERT_EQUAL_UINT8(ZF_SCAN_TABLE_MAX, table.count);
	TEST_ASSERT_EQUAL_MEMORY("strong", table.entries[ZF_SCAN_TABLE_MAX - 1].ssid, 6);
}

void test_long_ssid_is_truncated_and_rssi_clamped(void)
{
	uint8_t ssid[ZF_SSID_MAX_BYTES + 4];

	memset(ssid, 'a', sizeof(ssid));
	zf_scan_table_add(&table, ssid, sizeof(ssid), -300, 0);
	TEST_ASSERT_EQUAL_UINT8(ZF_SSID_MAX_BYTES, table.entries[0].ssid_length);
	TEST_ASSERT_EQUAL_INT8(INT8_MIN, table.entries[0].rssi);
}

void test_sort_is_descending_and_stable(void)
{
	add("a", -60);
	add("b", -30);
	add("c", -60);
	zf_scan_table_sort(&table);
	TEST_ASSERT_EQUAL_UINT8('b', table.entries[0].ssid[0]);
	TEST_ASSERT_EQUAL_UINT8('a', table.entries[1].ssid[0]);
	TEST_ASSERT_EQUAL_UINT8('c', table.entries[2].ssid[0]);
}

/**
 * @file wifi_scan_table.h
 * @brief Bounded Wi-Fi scan table keeping the strongest result per SSID, independent of Zephyr.
 */
#ifndef ZEPHYRFORGE_WIFI_SCAN_TABLE_H
#define ZEPHYRFORGE_WIFI_SCAN_TABLE_H

#include <stdint.h>

#include "report/report.h"

/** Maximum distinct SSIDs retained from one scan. */
#define ZF_WIFI_SCAN_TABLE_MAX 18u

/** One collected scan result. */
typedef struct {
	uint8_t ssid[ZF_SSID_MAX_BYTES]; /**< Raw SSID bytes, zero-padded. */
	uint8_t ssid_length; /**< SSID length, 1..ZF_SSID_MAX_BYTES. */
	int8_t rssi; /**< Signal strength used for eviction and ordering. */
	uint8_t security; /**< Feature 12 security bitmask. */
} zf_wifi_scan_entry_t;

/** Scan results collected for one scan generation. */
typedef struct {
	zf_wifi_scan_entry_t entries[ZF_WIFI_SCAN_TABLE_MAX]; /**< Valid entries, strongest first after sorting. */
	uint8_t count; /**< Valid entry count. */
} zf_wifi_scan_table_t;

/**
 * @brief Record one driver result; ignores empty SSIDs and weaker duplicates.
 * @param table Table being collected.
 * @param ssid Raw SSID bytes; lengths above ZF_SSID_MAX_BYTES are truncated.
 * @param ssid_length Driver-reported SSID length.
 * @param rssi Driver RSSI, clamped to int8_t.
 * @param security Feature 12 security bitmask.
 *
 * When the table is full, the result replaces the weakest entry only if it is stronger.
 */
void zf_wifi_scan_table_add(zf_wifi_scan_table_t *table, const uint8_t *ssid, uint8_t ssid_length, int rssi,
		       uint8_t security);
/** Order entries by descending RSSI; entries with equal RSSI keep their collection order. */
void zf_wifi_scan_table_sort(zf_wifi_scan_table_t *table);

#endif

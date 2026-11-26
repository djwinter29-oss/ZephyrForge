#include "wifi/wifi_scan_table.h"
#include "protocol/report_codec.h"

#include <string.h>

void zf_wifi_scan_table_add(zf_wifi_scan_table_t *table, const uint8_t *ssid, uint8_t ssid_length, int rssi,
		       uint8_t security)
{
	uint8_t length = ssid_length < ZF_SSID_MAX_BYTES ? ssid_length : ZF_SSID_MAX_BYTES;
	zf_wifi_scan_entry_t *slot = NULL;

	if (length == 0) {
		return;
	}
	for (uint8_t i = 0; i < table->count; ++i) {
		if (table->entries[i].ssid_length == length && memcmp(table->entries[i].ssid, ssid, length) == 0) {
			if (rssi <= table->entries[i].rssi) {
				return;
			}
			slot = &table->entries[i];
			break;
		}
	}
	if (slot == NULL && table->count < ZF_WIFI_SCAN_TABLE_MAX) {
		slot = &table->entries[table->count++];
	}
	if (slot == NULL) {
		slot = &table->entries[0];
		for (uint8_t i = 1; i < table->count; ++i) {
			if (table->entries[i].rssi < slot->rssi) {
				slot = &table->entries[i];
			}
		}
		if (rssi <= slot->rssi) {
			return;
		}
	}
	memset(slot, 0, sizeof(*slot));
	memcpy(slot->ssid, ssid, length);
	slot->ssid_length = length;
	slot->rssi = (int8_t)(rssi < INT8_MIN ? INT8_MIN : rssi > INT8_MAX ? INT8_MAX : rssi);
	slot->security = security;
}

void zf_wifi_scan_table_sort(zf_wifi_scan_table_t *table)
{
	for (uint8_t i = 1; i < table->count; ++i) {
		zf_wifi_scan_entry_t entry = table->entries[i];
		uint8_t j = i;

		for (; j > 0 && table->entries[j - 1].rssi < entry.rssi; --j) {
			table->entries[j] = table->entries[j - 1];
		}
		table->entries[j] = entry;
	}
}

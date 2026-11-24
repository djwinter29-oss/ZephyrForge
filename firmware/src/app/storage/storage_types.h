/**
 * @file storage_types.h
 * @brief Storage service snapshot shared with report encoders.
 */
#ifndef ZEPHYRFORGE_STORAGE_TYPES_H
#define ZEPHYRFORGE_STORAGE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/** Coherent storage snapshot; invalid samples carry no live SD fields on the wire. */
typedef struct {
	uint8_t sequence; /**< Published Input sequence. */
	bool configured; /**< Card detection and storage service are configured. */
	bool presence_known; /**< Debounced presence is known. */
	bool present; /**< Debounced card-present state. */
	bool sample_valid; /**< Firmware-owned, non-busy SD sample is available. */
	bool initialized; /**< Hardware initialization succeeded, independently of filesystem mounting. */
	bool capacity_valid; /**< Sector count belongs to the current medium. */
	bool cid_valid; /**< CID belongs to the initialized current medium. */
	uint8_t cid[16]; /**< Raw SD CID in wire byte order. */
	uint32_t bus_clock_hz; /**< Configured bus ceiling, not a measured clock. */
	uint32_t block_count; /**< Whole-card capacity in 512-byte sectors. */
	uint8_t filesystem_format; /**< Mounted FAT format, or zero when unknown. */
	bool msc_enabled; /**< USB exclusively owns the raw medium. */
	bool msc_transition_pending; /**< An admitted ownership handoff is pending. */
	bool msc_request_rejected; /**< Last admitted handoff failed or was cancelled. */
	bool busy; /**< Storage worker is manipulating the medium; not encoded separately. */
} zf_storage_t;

#endif

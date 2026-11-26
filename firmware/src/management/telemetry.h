/**
 * @file telemetry.h
 * @brief Temperature, CPU load and clock telemetry for Feature 3.
 */
#ifndef ZEPHYRFORGE_MANAGEMENT_TELEMETRY_H
#define ZEPHYRFORGE_MANAGEMENT_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

/** Initialize cached telemetry without sampling until a management transport is active. */
void zf_telemetry_init(void);
/** Enable or pause periodic sampling based on active USB/BLE management clients. */
void zf_telemetry_set_active(bool active);
/** Refresh the cached temperature and load window; call from the main loop. */
void zf_telemetry_poll(void);
/** Return the next CPU-load or temperature sampling deadline. */
int64_t zf_telemetry_next_deadline(void);
/** Encode Feature 3 from cached telemetry and transport/LED state sampled by the caller. */
void zf_telemetry_report(uint8_t sequence, bool usb_configured, bool led_available, uint8_t led_colour,
			 uint8_t *out);

#endif

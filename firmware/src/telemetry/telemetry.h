/**
 * @file telemetry.h
 * @brief Temperature, CPU load and clock telemetry for Feature 3.
 */
#ifndef ZEPHYRFORGE_TELEMETRY_H
#define ZEPHYRFORGE_TELEMETRY_H

#include <stdint.h>

/** Start the temperature/load sampling windows. */
void zf_telemetry_init(void);
/** Refresh the cached temperature and load window; call from the main loop. */
void zf_telemetry_poll(void);
/** Encode Feature 3: temperature, load, clock and last-LED telemetry with the given Input sequence. */
void zf_telemetry_report(uint8_t sequence, uint8_t *out);

#endif

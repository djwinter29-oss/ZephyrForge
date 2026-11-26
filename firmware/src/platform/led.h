/**
 * @file led.h
 * @brief Onboard addressable RGB LED; boards without one report it unavailable.
 */
#ifndef ZEPHYRFORGE_LED_H
#define ZEPHYRFORGE_LED_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZF_LED)
/** Initialize the onboard addressable LED to Off before management starts. */
void zf_led_init(void);
/** Queue an RGB bitmask (red=1, green=2, blue=4); returns zero or negative errno without SPI I/O. */
int zf_led_request(uint8_t colour);
/** Copy the last successfully transmitted colour and driver availability. */
void zf_led_status(uint8_t *colour, bool *available);
#else
static inline void zf_led_init(void)
{
}

static inline int zf_led_request(uint8_t colour)
{
	(void)colour;
	return -ENOTSUP;
}

static inline void zf_led_status(uint8_t *colour, bool *available)
{
	*colour = 0;
	*available = false;
}
#endif

#endif

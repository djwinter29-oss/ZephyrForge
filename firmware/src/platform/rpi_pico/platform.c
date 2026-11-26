/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ji Dong
 */

/*
 * Raspberry Pi Pico family clock and LED hooks. The boards have no addressable RGB LED and keep their
 * devicetree clock.
 */

#include "app/zephyrforge.h"
#include "platform/zf_platform.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define CLK_SYS_HZ DT_PROP(DT_NODELABEL(clk_sys), clock_frequency)

int zf_system_clock_apply(uint16_t mhz)
{
	return mhz == 0 || mhz * 1000000u == CLK_SYS_HZ ? 0 : -ENOTSUP;
}

uint32_t zf_system_clock_hz(void)
{
	return CLK_SYS_HZ;
}

void zf_platform_led_init(void)
{
}

int zf_platform_led_request(uint8_t colour)
{
	ARG_UNUSED(colour);
	return -ENOTSUP;
}

void zf_platform_led_status(uint8_t *colour, bool *available)
{
	*colour = 0;
	*available = false;
}

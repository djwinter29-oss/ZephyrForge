/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ji Dong
 */

/* Raspberry Pi Pico family clock hooks: the boards keep their devicetree clock. */

#include "platform/sysclk.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define CLK_SYS_HZ DT_PROP(DT_NODELABEL(clk_sys), clock_frequency)

int zf_sysclk_apply(uint16_t mhz)
{
	return mhz == 0 || mhz * 1000000u == CLK_SYS_HZ ? 0 : -ENOTSUP;
}

uint32_t zf_sysclk_hz(void)
{
	return CLK_SYS_HZ;
}

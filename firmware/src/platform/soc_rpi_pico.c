/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ji Dong
 */

/* Raspberry Pi Pico family SoC hooks: the boards keep their devicetree clock and report QSPI flash capacity. */

#include <hardware/flash.h>

#include "platform/board.h"
#include "platform/sysclk.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zf_board, LOG_LEVEL_INF);

#define CLK_SYS_HZ DT_PROP(DT_NODELABEL(clk_sys), clock_frequency)

int zf_sysclk_apply(uint16_t mhz)
{
	return mhz == 0 || mhz * 1000000u == CLK_SYS_HZ ? 0 : -ENOTSUP;
}

uint32_t zf_sysclk_hz(void)
{
	return CLK_SYS_HZ;
}

void zf_soc_memory_inventory(zf_memory_inventory_t *out)
{
	/* JEDEC Read ID returns the manufacturer, memory type and log2 of the capacity in bytes. */
	const uint8_t command[4] = {0x9Fu};
	uint8_t id[4] = {0};
	/* The command suspends XIP; no interrupt handler may execute from flash until it returns. */
	unsigned int key = irq_lock();

	flash_do_cmd(command, id, sizeof(id));
	irq_unlock(key);
	out->flash_available = id[1] != 0x00u && id[1] != 0xFFu && id[3] >= 16u && id[3] <= 31u;
	if (out->flash_available) {
		out->flash_total_bytes = 1u << id[3];
	} else {
		LOG_ERR("Flash capacity read failed: JEDEC ID %02x %02x %02x", id[1], id[2], id[3]);
	}
}

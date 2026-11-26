/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ji Dong
 */

#include <pico/rand.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

/* Network identifiers use the SDK PRNG; this is not a source for cryptographic nonces. */
void z_impl_sys_rand_get(void *dst, size_t len)
{
	uint8_t *out = dst;

	while (len != 0) {
		uint64_t value = get_rand_64();
		size_t count = MIN(len, sizeof(value));

		memcpy(out, &value, count);
		out += count;
		len -= count;
	}
}

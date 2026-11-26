/* Host stand-in for Zephyr's software CRC-32 (IEEE, reflected) used by native tests. */
#ifndef ZEPHYR_INCLUDE_SYS_CRC_H_
#define ZEPHYR_INCLUDE_SYS_CRC_H_

#include <stddef.h>
#include <stdint.h>

static inline uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;

	while (len--) {
		crc ^= *data++;
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

#endif

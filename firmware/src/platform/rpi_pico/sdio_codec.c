// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

#include "platform/rpi_pico/sdio_codec.h"

#include <string.h>

/* Divider limits in 1/256 system cycles. The command program spends two cycles per bus period, so 2.0 is the floor. */
#define DIVIDER_MIN_TICKS 512u
#define DIVIDER_MAX_TICKS 0xffffffu
/* The shortened High Speed receive loop rearms after three cycles; a longer CLK low phase samples a nibble twice. */
#define HIGH_SPEED_DIVIDER_MAX_TICKS 768u

/*
 * Four data lines carry four independent CRC16s. They are kept bit-interleaved in one 64-bit register: bit
 * 4 * n + lane holds CRC bit n of that lane, so a wire nibble advances all four at once and the register's big-endian
 * bytes are the 16 CRC nibbles in wire order. The step is linear, so four payload bytes fold at once through
 * slicing tables; each table is split into 32-bit halves to avoid 64-bit shifts on Cortex-M. The 8 KiB of tables
 * are built on first use.
 */
static uint32_t slice_hi[4][256];
static uint32_t slice_lo[4][256];
static bool slices_ready;

/** @brief Advance the four interleaved lane CRCs by one wire nibble. */
static uint64_t crc16_lanes_nibble(uint64_t reg, uint8_t nibble)
{
	uint64_t feedback = ((reg >> 60u) ^ nibble) & 0x0fu;

	return (reg << 4u) ^ (feedback << 48u) ^ (feedback << 20u) ^ feedback;
}

static void build_slices(void)
{
	for (uint32_t slice = 0u; slice < 4u; slice++) {
		for (uint32_t index = 0u; index < 256u; index++) {
			uint64_t reg = (uint64_t)index << (32u + 8u * slice);

			for (uint32_t step = 0u; step < 8u; step++) {
				reg = crc16_lanes_nibble(reg, 0u);
			}
			slice_hi[slice][index] = (uint32_t)(reg >> 32u);
			slice_lo[slice][index] = (uint32_t)reg;
		}
	}
	slices_ready = true;
}

/** @brief Fold four wire-order bytes, the first in the most significant position. */
static uint64_t crc16_lanes_word(uint64_t reg, uint32_t word)
{
	uint32_t x = (uint32_t)(reg >> 32u) ^ word;
	uint32_t b3 = x >> 24u;
	uint32_t b2 = (x >> 16u) & 0xffu;
	uint32_t b1 = (x >> 8u) & 0xffu;
	uint32_t b0 = x & 0xffu;
	uint32_t hi = (uint32_t)reg ^ slice_hi[3][b3] ^ slice_hi[2][b2] ^ slice_hi[1][b1] ^ slice_hi[0][b0];
	uint32_t lo = slice_lo[3][b3] ^ slice_lo[2][b2] ^ slice_lo[1][b1] ^ slice_lo[0][b0];

	return ((uint64_t)hi << 32u) | lo;
}

static uint64_t crc16_lanes(const uint8_t *payload, size_t size)
{
	uint64_t reg = 0u;

	if (!slices_ready) {
		build_slices();
	}
	for (size_t index = 0u; index < size; index += 4u) {
		reg = crc16_lanes_word(reg, ((uint32_t)payload[index] << 24u) | ((uint32_t)payload[index + 1u] << 16u) |
						    ((uint32_t)payload[index + 2u] << 8u) | payload[index + 3u]);
	}
	return reg;
}

static uint16_t crc16_update(uint16_t crc, uint8_t value)
{
	crc = (uint16_t)((crc >> 8u) | (crc << 8u));
	crc ^= value;
	crc ^= (uint16_t)((crc & 0xffu) >> 4u);
	crc ^= (uint16_t)(crc << 12u);
	crc ^= (uint16_t)((crc & 0xffu) << 5u);
	return crc;
}

/** @brief Spread one payload byte over four one-bit image bytes, DAT1-DAT3 high. */
static void encode_one_bit_byte(uint8_t value, uint8_t *image)
{
	for (size_t index = 0u; index < 4u; index++) {
		image[index] = (uint8_t)(0xeeu | ((value >> 3u) & 0x10u) | ((value >> 6u) & 0x01u));
		value = (uint8_t)(value << 2u);
	}
}

/** @brief Collect DAT0 from four one-bit image bytes. */
static uint8_t decode_one_bit_byte(const uint8_t *image)
{
	uint8_t value = 0u;

	for (size_t index = 0u; index < 4u; index++) {
		value = (uint8_t)((value << 2u) | ((image[index] >> 3u) & 0x02u) | (image[index] & 0x01u));
	}
	return value;
}

uint8_t zf_sdio_crc7(const uint8_t *data, size_t size)
{
	uint8_t crc = 0u;

	if (data == NULL) {
		return 0u;
	}
	for (size_t index = 0u; index < size; index++) {
		uint8_t value = data[index];

		for (uint8_t bit = 0u; bit < 8u; bit++) {
			bool feedback = ((crc & 0x40u) != 0u) != ((value & 0x80u) != 0u);

			crc = (uint8_t)((crc << 1u) & 0x7fu);
			if (feedback) {
				crc ^= 0x09u;
			}
			value = (uint8_t)(value << 1u);
		}
	}
	return crc;
}

uint16_t zf_sdio_crc16(const uint8_t *data, size_t size)
{
	uint16_t crc = 0u;

	if (data == NULL) {
		return 0u;
	}
	for (size_t index = 0u; index < size; index++) {
		crc = crc16_update(crc, data[index]);
	}
	return crc;
}

void zf_sdio_build_command(uint8_t opcode, uint32_t argument, uint8_t frame[ZF_SDIO_COMMAND_SIZE])
{
	frame[0] = (uint8_t)(0x40u | (opcode & 0x3fu));
	frame[1] = (uint8_t)(argument >> 24u);
	frame[2] = (uint8_t)(argument >> 16u);
	frame[3] = (uint8_t)(argument >> 8u);
	frame[4] = (uint8_t)argument;
	frame[5] = (uint8_t)((zf_sdio_crc7(frame, 5u) << 1u) | 1u);
}

static uint32_t load_be32(const uint8_t *data)
{
	return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) | ((uint32_t)data[2] << 8u) | data[3];
}

bool zf_sdio_parse_response(zf_sdio_response_t type, uint8_t opcode, const uint8_t *frame, uint32_t response[4])
{
	switch (type) {
	case ZF_SDIO_RESPONSE_NONE:
		return true;
	case ZF_SDIO_RESPONSE_SHORT:
		if ((frame[0] & 0xc0u) != 0u || (frame[0] & 0x3fu) != (opcode & 0x3fu) || (frame[5] & 1u) == 0u ||
		    (frame[5] >> 1u) != zf_sdio_crc7(frame, 5u)) {
			return false;
		}
		response[0] = load_be32(frame + 1u);
		return true;
	case ZF_SDIO_RESPONSE_OCR:
		if (frame[0] != 0x3fu || frame[5] != 0xffu) {
			return false;
		}
		response[0] = load_be32(frame + 1u);
		return true;
	case ZF_SDIO_RESPONSE_LONG:
		if (frame[0] != 0x3fu || (frame[16] & 1u) == 0u || (frame[16] >> 1u) != zf_sdio_crc7(frame + 1u, 15u)) {
			return false;
		}
		for (size_t word = 0u; word < 4u; word++) {
			response[3u - word] = load_be32(frame + 1u + 4u * word);
		}
		return true;
	default:
		return false;
	}
}

size_t zf_sdio_image_size(size_t block_size, uint8_t bus_width)
{
	if (block_size == 0u || block_size > 512u) {
		return 0u;
	}
	if (bus_width == 4u) {
		return (block_size % 4u) == 0u ? block_size + 8u : 0u;
	}
	return bus_width == 1u ? 4u * block_size + 8u : 0u;
}

bool zf_sdio_encode(const uint8_t *block, size_t block_size, uint8_t bus_width, uint8_t *image)
{
	if (block == NULL || image == NULL || zf_sdio_image_size(block_size, bus_width) == 0u) {
		return false;
	}
	if (bus_width == 4u) {
		uint64_t reg = crc16_lanes(block, block_size);

		memcpy(image, block, block_size);
		for (size_t index = 0u; index < 8u; index++) {
			image[block_size + index] = (uint8_t)(reg >> (56u - 8u * index));
		}
		return true;
	}

	uint16_t crc = zf_sdio_crc16(block, block_size);

	for (size_t index = 0u; index < block_size; index++) {
		encode_one_bit_byte(block[index], image + 4u * index);
	}
	encode_one_bit_byte((uint8_t)(crc >> 8u), image + 4u * block_size);
	encode_one_bit_byte((uint8_t)crc, image + 4u * block_size + 4u);
	return true;
}

bool zf_sdio_decode(const uint8_t *image, uint8_t *block, size_t block_size, uint8_t bus_width)
{
	if (image == NULL || zf_sdio_image_size(block_size, bus_width) == 0u) {
		return false;
	}
	if (bus_width == 4u) {
		uint64_t reg = crc16_lanes(image, block_size);
		uint64_t received = 0u;

		for (size_t index = 0u; index < 8u; index++) {
			received = (received << 8u) | image[block_size + index];
		}
		if (block != NULL) {
			memcpy(block, image, block_size);
		}
		return received == reg;
	}

	uint16_t crc = 0u;

	for (size_t index = 0u; index < block_size; index++) {
		uint8_t value = decode_one_bit_byte(image + 4u * index);

		crc = crc16_update(crc, value);
		if (block != NULL) {
			block[index] = value;
		}
	}
	return crc == (uint16_t)((decode_one_bit_byte(image + 4u * block_size) << 8u) |
				 decode_one_bit_byte(image + 4u * block_size + 4u));
}

bool zf_sdio_plan_clock(uint32_t system_hz, uint32_t clock_hz, bool high_speed, uint32_t *ticks,
			uint32_t *actual_hz)
{
	uint64_t divider;

	if (ticks == NULL || actual_hz == NULL || system_hz == 0u || clock_hz == 0u ||
	    clock_hz > (high_speed ? ZF_SDIO_MAX_CLOCK_HZ : ZF_SDIO_DEFAULT_SPEED_MAX_HZ)) {
		return false;
	}
	/* High Speed halves before rounding: the transmit program runs at half this divider and must stay even. */
	divider = high_speed ? 2u * (((uint64_t)system_hz * 64u + clock_hz - 1u) / clock_hz)
			     : (((uint64_t)system_hz + 2u * clock_hz - 1u) / (2u * clock_hz)) * 256u;
	if (divider < DIVIDER_MIN_TICKS) {
		divider = DIVIDER_MIN_TICKS;
	}
	if (divider > DIVIDER_MAX_TICKS || (high_speed && divider > HIGH_SPEED_DIVIDER_MAX_TICKS)) {
		return false;
	}
	*ticks = (uint32_t)divider;
	*actual_hz = (uint32_t)(((uint64_t)system_hz * 128u) / divider);
	return true;
}

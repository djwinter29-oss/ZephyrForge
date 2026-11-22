// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file sdio_codec.h
 * @brief Native SD bus framing for the RP2040/RP2350 PIO host.
 *
 * Covers command frames, response checks, data-block wire images and the PIO clock-divider plan. A data-block image
 * is the exact nibble stream shifted by the PIO data programs, packed two nibbles per byte with the first nibble in
 * the high half: the payload followed by the CRC16 of every active data line. Four-bit images carry the payload bytes
 * unchanged. One-bit images spend one nibble per payload bit on DAT0 and keep DAT1-DAT3 high.
 *
 * The four-bit CRC tables are built on first use, so callers must serialize image encoding and decoding.
 */

#ifndef ZEPHYRFORGE_SDIO_CODEC_H
#define ZEPHYRFORGE_SDIO_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Command frame size in bytes: start, direction, index, argument, CRC7 and end bit. */
#define ZF_SDIO_COMMAND_SIZE 6u
/** 48-bit response frame size in bytes. */
#define ZF_SDIO_SHORT_RESPONSE_SIZE 6u
/** 136-bit R2 response frame size in bytes. */
#define ZF_SDIO_LONG_RESPONSE_SIZE 17u
/** Fastest bus clock the PIO programs can time (High Speed). */
#define ZF_SDIO_MAX_CLOCK_HZ 50000000u
/** Fastest Default Speed bus clock. */
#define ZF_SDIO_DEFAULT_SPEED_MAX_HZ 25000000u

/** Response frame checks applied by zf_sdio_parse_response(). */
typedef enum {
	ZF_SDIO_RESPONSE_NONE = 0,  /**< No response is expected. */
	ZF_SDIO_RESPONSE_SHORT = 1, /**< 48-bit R1, R1b, R6 or R7: command index and CRC7 are checked. */
	ZF_SDIO_RESPONSE_OCR = 2,   /**< 48-bit R3: reserved index and all-ones CRC field, no CRC check. */
	ZF_SDIO_RESPONSE_LONG = 3,  /**< 136-bit R2: CRC7 over the CID or CSD register. */
} zf_sdio_response_t;

/**
 * @brief SD CRC7 (polynomial 0x09) used by command and response frames.
 * @return The seven-bit CRC, or 0 when @p data is NULL.
 */
uint8_t zf_sdio_crc7(const uint8_t *data, size_t size);

/**
 * @brief SD data CRC16 (CRC-16/XMODEM) for one data line.
 * @return The CRC, or 0 when @p data is NULL.
 */
uint16_t zf_sdio_crc16(const uint8_t *data, size_t size);

/**
 * @brief Build a host-to-card command frame.
 * @param opcode Command index; only the low six bits are used.
 * @param argument Command argument.
 * @param frame Receives ZF_SDIO_COMMAND_SIZE bytes.
 */
void zf_sdio_build_command(uint8_t opcode, uint32_t argument, uint8_t frame[ZF_SDIO_COMMAND_SIZE]);

/**
 * @brief Validate a received response frame and extract its payload.
 *
 * The frame starts with the start bit as received by the PIO command program. Short responses fill
 * @p response[0] with the 32-bit card status or register. R2 fills @p response[3] down to @p response[0] with bits
 * 127 to 0 of the register, including the CRC7 and end bit in the lowest byte.
 *
 * @param type Expected response format; ZF_SDIO_RESPONSE_NONE always succeeds.
 * @param opcode Command index the short response must echo.
 * @param frame ZF_SDIO_SHORT_RESPONSE_SIZE or ZF_SDIO_LONG_RESPONSE_SIZE bytes.
 * @param response Receives the decoded payload words.
 * @return false on a malformed frame, wrong index or CRC mismatch.
 */
bool zf_sdio_parse_response(zf_sdio_response_t type, uint8_t opcode, const uint8_t *frame, uint32_t response[4]);

/**
 * @brief Wire-image size of one data block.
 * @param block_size Payload bytes, 1 to 512; four-bit images need a multiple of four.
 * @param bus_width 1 or 4 data lines.
 * @return Image bytes, which are always a whole number of 32-bit words, or 0 when the shape is unsupported.
 */
size_t zf_sdio_image_size(size_t block_size, uint8_t bus_width);

/**
 * @brief Encode one payload block and its data CRC into a wire image.
 * @param image Receives zf_sdio_image_size() bytes.
 * @return false when the block shape is unsupported.
 */
bool zf_sdio_encode(const uint8_t *block, size_t block_size, uint8_t bus_width, uint8_t *image);

/**
 * @brief Verify the data CRC of a received wire image and extract its payload.
 * @param block Receives @p block_size bytes, or NULL to verify only. It is written even when the CRC fails.
 * @return false when the block shape is unsupported or any data line CRC mismatches.
 */
bool zf_sdio_decode(const uint8_t *image, uint8_t *block, size_t block_size, uint8_t bus_width);

/**
 * @brief Plan the PIO clock divider for a bus clock.
 *
 * The divider is in 1/256 system-clock cycles and drives a two-cycle bus period. Default Speed uses whole-cycle
 * phases for a symmetric duty cycle. High Speed keeps an even divider so the cycle-locked transmit program, which
 * runs at half this divider, stays aligned with CLK. The plan never exceeds the request; requests faster than the
 * system clock allows run at system clock / 4.
 *
 * @param system_hz PIO input clock.
 * @param clock_hz Requested bus clock; at most ZF_SDIO_MAX_CLOCK_HZ, and at most ZF_SDIO_DEFAULT_SPEED_MAX_HZ for
 *                 Default Speed.
 * @param high_speed True when the card uses High Speed output timing.
 * @param ticks Receives the divider.
 * @param actual_hz Receives the resulting bus clock.
 * @return false when the request cannot be timed safely; outputs are then unchanged.
 */
bool zf_sdio_plan_clock(uint32_t system_hz, uint32_t clock_hz, bool high_speed, uint32_t *ticks,
			uint32_t *actual_hz);

#endif

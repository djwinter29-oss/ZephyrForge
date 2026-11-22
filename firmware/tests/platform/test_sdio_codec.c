#include "unity.h"
#include "sdio_codec.h"

#include <string.h>

static uint8_t block[512];
static uint8_t decoded[512];
static uint8_t image[2056];

void setUp(void)
{
	uint32_t seed = 0x12345678u;

	for (size_t index = 0u; index < sizeof(block); index++) {
		seed = seed * 1103515245u + 12345u;
		block[index] = (uint8_t)(seed >> 16u);
	}
	memset(decoded, 0, sizeof(decoded));
	memset(image, 0, sizeof(image));
}

void tearDown(void)
{
}

/* Bit-serial CRC16 reference, MSB first. */
static uint16_t crc16_bits(const uint8_t *bits, size_t count)
{
	uint16_t crc = 0u;

	for (size_t index = 0u; index < count; index++) {
		bool feedback = ((crc >> 15u) & 1u) != bits[index];

		crc = (uint16_t)(crc << 1u);
		if (feedback) {
			crc ^= 0x1021u;
		}
	}
	return crc;
}

static uint8_t image_nibble(size_t position)
{
	uint8_t value = image[position / 2u];

	return (position % 2u) == 0u ? (uint8_t)(value >> 4u) : (uint8_t)(value & 0x0fu);
}

void test_command_frames_use_crc7(void)
{
	static const uint8_t cmd8[] = {0x48, 0x00, 0x00, 0x01, 0xaa};
	uint8_t frame[ZF_SDIO_COMMAND_SIZE];

	TEST_ASSERT_EQUAL_HEX8(0x43, zf_sdio_crc7(cmd8, sizeof(cmd8)));
	zf_sdio_build_command(0u, 0u, frame);
	TEST_ASSERT_EQUAL_HEX8(0x40, frame[0]);
	TEST_ASSERT_EQUAL_HEX8(0x95, frame[5]);
	zf_sdio_build_command(8u, 0x1aau, frame);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(cmd8, frame, sizeof(cmd8));
	TEST_ASSERT_EQUAL_HEX8(0x87, frame[5]);
}

void test_crc16_matches_xmodem(void)
{
	uint8_t ones[512];

	memset(ones, 0xff, sizeof(ones));
	TEST_ASSERT_EQUAL_HEX16(0x31c3, zf_sdio_crc16((const uint8_t *)"123456789", 9u));
	TEST_ASSERT_EQUAL_HEX16(0x7fa1, zf_sdio_crc16(ones, sizeof(ones)));
}

void test_short_response_checks_index_crc_and_end_bit(void)
{
	uint8_t frame[ZF_SDIO_SHORT_RESPONSE_SIZE] = {0x11, 0x00, 0x00, 0x09, 0x00};
	uint32_t response[4] = {0};

	frame[5] = (uint8_t)((zf_sdio_crc7(frame, 5u) << 1u) | 1u);
	TEST_ASSERT_TRUE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_SHORT, 17u, frame, response));
	TEST_ASSERT_EQUAL_HEX32(0x00000900u, response[0]);
	TEST_ASSERT_FALSE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_SHORT, 18u, frame, response));
	frame[5] &= 0xfeu;
	TEST_ASSERT_FALSE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_SHORT, 17u, frame, response));
	frame[5] |= 1u;
	frame[3] ^= 0x10u;
	TEST_ASSERT_FALSE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_SHORT, 17u, frame, response));
}

void test_ocr_response_skips_crc_but_checks_reserved_fields(void)
{
	uint8_t frame[ZF_SDIO_SHORT_RESPONSE_SIZE] = {0x3f, 0xc0, 0xff, 0x80, 0x00, 0xff};
	uint32_t response[4] = {0};

	TEST_ASSERT_TRUE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_OCR, 41u, frame, response));
	TEST_ASSERT_EQUAL_HEX32(0xc0ff8000u, response[0]);
	frame[5] = 0xfeu;
	TEST_ASSERT_FALSE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_OCR, 41u, frame, response));
}

void test_long_response_fills_words_most_significant_last(void)
{
	uint8_t frame[ZF_SDIO_LONG_RESPONSE_SIZE] = {0x3f};
	uint32_t response[4] = {0};

	for (uint8_t index = 1u; index < 16u; index++) {
		frame[index] = (uint8_t)(index * 17u);
	}
	frame[16] = (uint8_t)((zf_sdio_crc7(frame + 1u, 15u) << 1u) | 1u);
	TEST_ASSERT_TRUE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_LONG, 9u, frame, response));
	TEST_ASSERT_EQUAL_HEX32(0x11223344u, response[3]);
	TEST_ASSERT_EQUAL_HEX32(0x55667788u, response[2]);
	TEST_ASSERT_EQUAL_HEX32(0x99aabbccu, response[1]);
	TEST_ASSERT_EQUAL_HEX32(((uint32_t)0xddeeffu << 8u) | frame[16], response[0]);
	frame[8] ^= 1u;
	TEST_ASSERT_FALSE(zf_sdio_parse_response(ZF_SDIO_RESPONSE_LONG, 9u, frame, response));
}

void test_image_sizes_cover_supported_shapes(void)
{
	TEST_ASSERT_EQUAL_UINT(520u, zf_sdio_image_size(512u, 4u));
	TEST_ASSERT_EQUAL_UINT(16u, zf_sdio_image_size(8u, 4u));
	TEST_ASSERT_EQUAL_UINT(2056u, zf_sdio_image_size(512u, 1u));
	TEST_ASSERT_EQUAL_UINT(40u, zf_sdio_image_size(8u, 1u));
	TEST_ASSERT_EQUAL_UINT(0u, zf_sdio_image_size(6u, 4u));
	TEST_ASSERT_EQUAL_UINT(0u, zf_sdio_image_size(0u, 1u));
	TEST_ASSERT_EQUAL_UINT(0u, zf_sdio_image_size(516u, 4u));
	TEST_ASSERT_EQUAL_UINT(0u, zf_sdio_image_size(512u, 8u));
}

void test_four_bit_image_carries_independent_lane_crcs(void)
{
	static uint8_t lanes[4][1024];
	uint16_t crc[4];

	TEST_ASSERT_TRUE(zf_sdio_encode(block, 512u, 4u, image));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(block, image, 512u);
	for (size_t position = 0u; position < 1024u; position++) {
		for (uint8_t lane = 0u; lane < 4u; lane++) {
			lanes[lane][position] = (uint8_t)((image_nibble(position) >> lane) & 1u);
		}
	}
	for (uint8_t lane = 0u; lane < 4u; lane++) {
		crc[lane] = crc16_bits(lanes[lane], 1024u);
	}
	for (size_t bit = 0u; bit < 16u; bit++) {
		uint8_t expected = 0u;

		for (uint8_t lane = 0u; lane < 4u; lane++) {
			expected |= (uint8_t)(((crc[lane] >> (15u - bit)) & 1u) << lane);
		}
		TEST_ASSERT_EQUAL_HEX8(expected, image_nibble(1024u + bit));
	}
	TEST_ASSERT_TRUE(zf_sdio_decode(image, decoded, 512u, 4u));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(block, decoded, 512u);
}

void test_four_bit_decode_rejects_payload_and_crc_corruption(void)
{
	TEST_ASSERT_TRUE(zf_sdio_encode(block, 512u, 4u, image));
	image[100] ^= 0x04u;
	TEST_ASSERT_FALSE(zf_sdio_decode(image, decoded, 512u, 4u));
	image[100] ^= 0x04u;
	image[519] ^= 0x80u;
	TEST_ASSERT_FALSE(zf_sdio_decode(image, NULL, 512u, 4u));
	image[519] ^= 0x80u;
	TEST_ASSERT_TRUE(zf_sdio_decode(image, NULL, 512u, 4u));
}

void test_one_bit_image_drives_dat0_only(void)
{
	static uint8_t bits[4096];
	uint16_t crc;

	TEST_ASSERT_TRUE(zf_sdio_encode(block, 512u, 1u, image));
	for (size_t position = 0u; position < 4112u; position++) {
		TEST_ASSERT_EQUAL_HEX8(0x0e, image_nibble(position) & 0x0eu);
	}
	for (size_t position = 0u; position < 4096u; position++) {
		bits[position] = (uint8_t)((block[position / 8u] >> (7u - position % 8u)) & 1u);
		TEST_ASSERT_EQUAL_UINT8(bits[position], image_nibble(position) & 1u);
	}
	crc = crc16_bits(bits, 4096u);
	TEST_ASSERT_EQUAL_HEX16(crc, zf_sdio_crc16(block, 512u));
	for (size_t bit = 0u; bit < 16u; bit++) {
		TEST_ASSERT_EQUAL_UINT8((crc >> (15u - bit)) & 1u, image_nibble(4096u + bit) & 1u);
	}
	TEST_ASSERT_TRUE(zf_sdio_decode(image, decoded, 512u, 1u));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(block, decoded, 512u);
}

void test_one_bit_decode_ignores_unused_lines_and_rejects_dat0_errors(void)
{
	TEST_ASSERT_TRUE(zf_sdio_encode(block, 8u, 1u, image));
	image[3] &= 0x11u;
	TEST_ASSERT_TRUE(zf_sdio_decode(image, decoded, 8u, 1u));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(block, decoded, 8u);
	image[3] ^= 0x10u;
	TEST_ASSERT_FALSE(zf_sdio_decode(image, decoded, 8u, 1u));
}

void test_clock_plan_matches_rp2040_and_rp2350_limits(void)
{
	uint32_t ticks = 0u;
	uint32_t actual = 0u;

	TEST_ASSERT_TRUE(zf_sdio_plan_clock(125000000u, 400000u, false, &ticks, &actual));
	TEST_ASSERT_EQUAL_UINT32(40192u, ticks);
	TEST_ASSERT_EQUAL_UINT32(398089u, actual);
	TEST_ASSERT_TRUE(zf_sdio_plan_clock(125000000u, 25000000u, false, &ticks, &actual));
	TEST_ASSERT_EQUAL_UINT32(768u, ticks);
	TEST_ASSERT_EQUAL_UINT32(20833333u, actual);
	TEST_ASSERT_TRUE(zf_sdio_plan_clock(125000000u, 50000000u, true, &ticks, &actual));
	TEST_ASSERT_EQUAL_UINT32(512u, ticks);
	TEST_ASSERT_EQUAL_UINT32(31250000u, actual);
	TEST_ASSERT_TRUE(zf_sdio_plan_clock(150000000u, 50000000u, true, &ticks, &actual));
	TEST_ASSERT_EQUAL_UINT32(37500000u, actual);
	TEST_ASSERT_TRUE(zf_sdio_plan_clock(150000000u, 25000000u, true, &ticks, &actual));
	TEST_ASSERT_EQUAL_UINT32(768u, ticks);
	TEST_ASSERT_EQUAL_UINT32(25000000u, actual);
}

void test_clock_plan_rejects_untimeable_requests(void)
{
	uint32_t ticks = 7u;
	uint32_t actual = 9u;

	TEST_ASSERT_FALSE(zf_sdio_plan_clock(150000000u, 20000000u, true, &ticks, &actual));
	TEST_ASSERT_FALSE(zf_sdio_plan_clock(150000000u, 26000000u, false, &ticks, &actual));
	TEST_ASSERT_FALSE(zf_sdio_plan_clock(150000000u, 51000000u, true, &ticks, &actual));
	TEST_ASSERT_FALSE(zf_sdio_plan_clock(0u, 400000u, false, &ticks, &actual));
	TEST_ASSERT_FALSE(zf_sdio_plan_clock(150000000u, 0u, false, &ticks, &actual));
	TEST_ASSERT_EQUAL_UINT32(7u, ticks);
	TEST_ASSERT_EQUAL_UINT32(9u, actual);
}

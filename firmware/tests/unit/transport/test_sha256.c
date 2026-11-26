#include "unity.h"
#include "transport/sha256.h"

#include <string.h>

static void assert_hex(const char *hex, const uint8_t *bytes, size_t size)
{
	char text[2 * ZF_SHA256_SIZE + 1];

	for (size_t i = 0; i < size; ++i) {
		static const char digits[] = "0123456789abcdef";
		text[2 * i] = digits[bytes[i] >> 4];
		text[2 * i + 1] = digits[bytes[i] & 0x0F];
	}
	text[2 * size] = '\0';
	TEST_ASSERT_EQUAL_STRING(hex, text);
}

void test_sha256_abc(void)
{
	zf_sha256_t ctx;
	uint8_t digest[ZF_SHA256_SIZE];

	zf_sha256_init(&ctx);
	zf_sha256_update(&ctx, (const uint8_t *)"abc", 3);
	zf_sha256_final(&ctx, digest);
	assert_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", digest, sizeof(digest));
}

void test_sha256_multi_block_split_updates(void)
{
	uint8_t data[1000];
	zf_sha256_t ctx;
	uint8_t digest[ZF_SHA256_SIZE];

	memset(data, 'a', sizeof(data));
	zf_sha256_init(&ctx);
	zf_sha256_update(&ctx, data, 7);
	zf_sha256_update(&ctx, data + 7, 600);
	zf_sha256_update(&ctx, data + 607, 393);
	zf_sha256_final(&ctx, digest);
	assert_hex("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", digest, sizeof(digest));
}

void test_hmac_hashes_long_key_and_joins_parts(void)
{
	uint8_t key[100];
	uint8_t mac[ZF_SHA256_SIZE];
	const zf_sha256_hmac_part_t parts[] = {{(const uint8_t *)"m", 1}, {(const uint8_t *)"sg", 2}};

	memset(key, 'k', sizeof(key));
	zf_sha256_hmac(key, sizeof(key), parts, 2, mac);
	assert_hex("bd56a1782c2830e8abc6ed866a57a1230661e650b84c62f7ee3accc5fa5af491", mac, sizeof(mac));
}

void test_sha256_padding_boundaries_and_empty_message(void)
{
	static const struct {
		size_t length;
		const char *hex;
	} cases[] = {
		/* Lengths 56..63 push the 64-bit bit count into a second padding block. */
		{0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
		{55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
		{56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
		{63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
		{64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
	};
	uint8_t data[64];
	uint8_t digest[ZF_SHA256_SIZE];

	memset(data, 'a', sizeof(data));
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		zf_sha256_t ctx;

		zf_sha256_init(&ctx);
		zf_sha256_update(&ctx, data, cases[i].length);
		zf_sha256_final(&ctx, digest);
		assert_hex(cases[i].hex, digest, sizeof(digest));
	}
}

void test_hmac_short_and_empty_keys(void)
{
	uint8_t mac[ZF_SHA256_SIZE];
	const uint8_t rfc_key[20] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
				     0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
	const zf_sha256_hmac_part_t rfc_parts[] = {{(const uint8_t *)"Hi There", 8}};
	const zf_sha256_hmac_part_t fox[] = {{(const uint8_t *)"The quick brown fox jumps over the lazy dog", 43}};
	const zf_sha256_hmac_part_t abc[] = {{(const uint8_t *)"abc", 3}};

	/* Keys up to the block size are padded, not hashed; an empty key pads all-zero. */
	zf_sha256_hmac((const uint8_t *)"key", 3, fox, 1, mac);
	assert_hex("f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8", mac, sizeof(mac));
	zf_sha256_hmac(rfc_key, sizeof(rfc_key), rfc_parts, 1, mac);
	assert_hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", mac, sizeof(mac));
	zf_sha256_hmac(NULL, 0, abc, 1, mac);
	assert_hex("fd7adb152c05ef80dccf50a1fa4c05d5a3ec6da95575fc312ae7c5d091836351", mac, sizeof(mac));
}

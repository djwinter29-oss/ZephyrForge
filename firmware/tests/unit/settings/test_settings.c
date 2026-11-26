#include "unity.h"
#include "settings/settings.h"
#include "settings/settings_flash.h"

#include <errno.h>
#include <string.h>

static uint8_t flash[ZF_SETTINGS_RECORD_SIZE];
static int read_result;
static int write_result;
static int write_calls;
static bool defer_write;
static const uint8_t *queued_record;
static size_t queued_size;
static zf_settings_done_t queued_done;
static int save_result;

static bool set_ssid(const char *ssid)
{
	return zf_settings_set_wifi_ssid_bytes((const uint8_t *)ssid, (uint8_t)strlen(ssid));
}

void zf_app_lock(void)
{
}

void zf_app_unlock(void)
{
}

int zf_settings_flash_read(uint8_t *record, size_t size)
{
	TEST_ASSERT_EQUAL_UINT(sizeof(flash), size);
	memcpy(record, flash, size);
	return read_result;
}

int zf_settings_flash_write(const uint8_t *record, size_t size)
{
	TEST_ASSERT_EQUAL_UINT(sizeof(flash), size);
	++write_calls;
	if (write_result == 0) {
		memcpy(flash, record, size);
	}
	return write_result;
}

int zf_settings_flash_write_async(const uint8_t *record, size_t size, zf_settings_done_t done)
{
	if (defer_write) {
		queued_record = record;
		queued_size = size;
		queued_done = done;
		return 0;
	}
	int err = zf_settings_flash_write(record, size);

	done(err);
	return 0;
}

static void save_done(int err)
{
	save_result = err;
}

static int save_settings(void)
{
	save_result = -EINPROGRESS;
	int err = zf_settings_save_async(save_done);

	return err == ZF_SETTINGS_SAVE_UNCHANGED ? 0 : err != 0 ? err : save_result;
}

static void complete_write(void)
{
	zf_settings_done_t done = queued_done;

	TEST_ASSERT_NOT_NULL(done);
	queued_done = NULL;
	done(zf_settings_flash_write(queued_record, queued_size));
	queued_record = NULL;
	queued_size = 0;
}

static void erase_flash(void)
{
	memset(flash, 0xFF, sizeof(flash));
}

static void reseal(void)
{
	uint32_t crc = zf_settings_crc32(flash, 144);

	flash[144] = (uint8_t)crc;
	flash[145] = (uint8_t)(crc >> 8);
	flash[146] = (uint8_t)(crc >> 16);
	flash[147] = (uint8_t)(crc >> 24);
}

static void save_valid_record(void)
{
	erase_flash();
	zf_settings_load();
	TEST_ASSERT_TRUE(set_ssid("lab"));
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password("password"));
	TEST_ASSERT_EQUAL_INT(0, save_settings());
}

static void assert_defaults(void)
{
	uint8_t ssid[ZF_SSID_MAX_BYTES];

	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, zf_settings_radios());
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
	TEST_ASSERT_FALSE(zf_settings_wifi_password_configured());
	TEST_ASSERT_FALSE(zf_settings_ble_key_configured());
	TEST_ASSERT_EQUAL_UINT8(0, zf_settings_wifi_ssid_bytes(ssid));
}

void setUp(void)
{
	read_result = 0;
	write_result = 0;
	write_calls = 0;
	defer_write = false;
	queued_record = NULL;
	queued_size = 0;
	queued_done = NULL;
	erase_flash();
	zf_settings_load();
	write_calls = 0;
}

void tearDown(void)
{
}

void test_crc_matches_ieee_check_value(void)
{
	TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, zf_settings_crc32((const uint8_t *)"123456789", 9));
}

void test_sysclk_profiles_convert_to_and_from_mhz(void)
{
	TEST_ASSERT_EQUAL_UINT16(0, zf_sysclk_profile_mhz(ZF_SYSCLK_DEFAULT));
	TEST_ASSERT_EQUAL_UINT16(80, zf_sysclk_profile_mhz(ZF_SYSCLK_80MHZ));
	TEST_ASSERT_EQUAL_UINT16(160, zf_sysclk_profile_mhz(ZF_SYSCLK_160MHZ));
	TEST_ASSERT_EQUAL_UINT16(240, zf_sysclk_profile_mhz(ZF_SYSCLK_240MHZ));
	TEST_ASSERT_EQUAL_UINT16(0, zf_sysclk_profile_mhz((zf_sysclk_profile_t)4));
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_160MHZ, zf_sysclk_profile_from_mhz(160));
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_sysclk_profile_from_mhz(0));
	TEST_ASSERT_FALSE(zf_settings_set_sysclk(zf_sysclk_profile_from_mhz(200)));
}

void test_erased_flash_loads_defaults_with_both_radios(void)
{
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_DEFAULTS, zf_settings_source());
	assert_defaults();
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, zf_settings_boot_radios());
}

void test_read_error_loads_defaults(void)
{
	save_valid_record();
	read_result = -EIO;
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_READ_ERROR, zf_settings_source());
	assert_defaults();
}

void test_record_layout_matches_documented_offsets(void)
{
	uint8_t key[ZF_BLE_KEY_SIZE];

	memset(key, 0xA5, sizeof(key));
	TEST_ASSERT_TRUE(zf_settings_set_sysclk(ZF_SYSCLK_160MHZ));
	TEST_ASSERT_TRUE(zf_settings_set_radios(ZF_WIRELESS_BLE));
	TEST_ASSERT_TRUE(set_ssid("lab"));
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password("password"));
	TEST_ASSERT_TRUE(zf_settings_set_ble_key(key));
	TEST_ASSERT_EQUAL_INT(0, save_settings());
	TEST_ASSERT_EQUAL_INT(1, write_calls);

	TEST_ASSERT_EQUAL_MEMORY("EFCF", flash, 4);
	TEST_ASSERT_EQUAL_UINT8(1, flash[4]);
	TEST_ASSERT_EQUAL_UINT8(0, flash[5]);
	TEST_ASSERT_EQUAL_UINT8(148, flash[6]);
	TEST_ASSERT_EQUAL_UINT8(0, flash[7]);
	TEST_ASSERT_EQUAL_UINT8(ZF_SYSCLK_160MHZ, flash[8]);
	TEST_ASSERT_EQUAL_UINT8(0, flash[9]);
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI, flash[10]);
	TEST_ASSERT_EQUAL_UINT8(0, flash[11]);
	TEST_ASSERT_EQUAL_UINT8(3, flash[12]);
	TEST_ASSERT_EQUAL_MEMORY("lab", flash + 13, 3);
	TEST_ASSERT_EQUAL_STRING("password", (const char *)flash + 45);
	TEST_ASSERT_EQUAL_MEMORY(key, flash + 109, sizeof(key));
	TEST_ASSERT_EQUAL_UINT8(1, flash[141]);
	TEST_ASSERT_EQUAL_UINT8(0, flash[142]);
	TEST_ASSERT_EQUAL_UINT8(0, flash[143]);
	uint32_t crc = zf_settings_crc32(flash, 144);
	TEST_ASSERT_EQUAL_HEX32(crc, flash[144] | flash[145] << 8 | flash[146] << 16 | (uint32_t)flash[147] << 24);
}

void test_saved_record_round_trips_and_applies_at_next_boot(void)
{
	uint8_t ssid[ZF_SSID_MAX_BYTES];
	const uint8_t opaque[] = {'a', 0, 'b'};
	char password[ZF_WIFI_PASSWORD_SIZE];

	TEST_ASSERT_TRUE(zf_settings_set_sysclk(ZF_SYSCLK_80MHZ));
	TEST_ASSERT_TRUE(zf_settings_set_radios(ZF_WIRELESS_WIFI));
	TEST_ASSERT_TRUE(zf_settings_set_wifi_ssid_bytes(opaque, sizeof(opaque)));
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password("12345678"));
	TEST_ASSERT_EQUAL_INT(0, save_settings());

	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, zf_settings_boot_radios());
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_boot_sysclk());
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_SAVED, zf_settings_source());
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI, zf_settings_boot_radios());
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_boot_sysclk());
	TEST_ASSERT_EQUAL_UINT8(3, zf_settings_wifi_ssid_bytes(ssid));
	TEST_ASSERT_EQUAL_MEMORY(opaque, ssid, sizeof(opaque));
	zf_settings_wifi_password(password);
	TEST_ASSERT_EQUAL_STRING("12345678", password);
}

void test_legacy_auth_state_is_accepted_but_new_records_reserve_the_byte(void)
{
	save_valid_record();
	flash[11] = 3;
	reseal();
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_SAVED, zf_settings_source());
	TEST_ASSERT_TRUE(set_ssid("new-network"));
	TEST_ASSERT_EQUAL_INT(0, save_settings());
	TEST_ASSERT_EQUAL_UINT8(0, flash[11]);
}

void test_save_maps_positive_flash_error_to_eio(void)
{
	write_result = 5;
	TEST_ASSERT_TRUE(zf_settings_set_sysclk(ZF_SYSCLK_80MHZ));
	TEST_ASSERT_EQUAL_INT(-EIO, save_settings());
	/* A positive driver return is a failure: staged values must be discarded, not kept as saved. */
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
}

void test_decode_rejects_unterminated_password_field(void)
{
	save_valid_record();
	memset(flash + 45, 'q', ZF_WIFI_PASSWORD_SIZE);
	reseal();
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_DEFAULTS, zf_settings_source());
	assert_defaults();
}

void test_unchanged_save_does_not_write_flash(void)
{
	save_result = -EINPROGRESS;
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_SAVE_UNCHANGED, zf_settings_save_async(save_done));
	TEST_ASSERT_EQUAL_INT(-EINPROGRESS, save_result);
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

void test_async_save_keeps_confirmed_values_visible_while_writing(void)
{
	char password[ZF_WIFI_PASSWORD_SIZE];

	save_valid_record();
	TEST_ASSERT_TRUE(zf_settings_set_sysclk(ZF_SYSCLK_80MHZ));
	TEST_ASSERT_TRUE(set_ssid("next-network"));
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password("next-password"));
	defer_write = true;
	TEST_ASSERT_EQUAL_INT(0, zf_settings_save_async(save_done));
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
	TEST_ASSERT_FALSE(set_ssid("late-change"));
	TEST_ASSERT_FALSE(zf_settings_set_wifi_password("late-password"));
	TEST_ASSERT_FALSE(zf_settings_set_ble_key(NULL));
	zf_settings_wifi_password(password);
	TEST_ASSERT_EQUAL_STRING("password", password);
	complete_write();
	TEST_ASSERT_EQUAL_INT(0, save_result);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_80MHZ, zf_settings_sysclk());
	TEST_ASSERT_TRUE(set_ssid("after-save"));
	memset(password, 0, sizeof(password));
}

void test_failed_save_discards_staged_values(void)
{
	write_result = -EIO;
	TEST_ASSERT_TRUE(zf_settings_set_radios(0));
	TEST_ASSERT_EQUAL_INT(-EIO, save_settings());
	TEST_ASSERT_EQUAL_UINT8(ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE, zf_settings_radios());
}

void test_discard_restores_last_saved_values(void)
{
	save_valid_record();
	TEST_ASSERT_TRUE(set_ssid(""));
	TEST_ASSERT_TRUE(zf_settings_set_sysclk(ZF_SYSCLK_80MHZ));
	zf_settings_discard();
	uint8_t ssid[ZF_SSID_MAX_BYTES];
	TEST_ASSERT_EQUAL_UINT8(3, zf_settings_wifi_ssid_bytes(ssid));
	TEST_ASSERT_EQUAL_MEMORY("lab", ssid, 3);
	TEST_ASSERT_EQUAL_INT(ZF_SYSCLK_DEFAULT, zf_settings_sysclk());
}

void test_setters_reject_invalid_values_without_change(void)
{
	uint8_t long_ssid[33];
	char long_password[65];

	memset(long_ssid, 'a', 33);
	memset(long_password, 'p', 64);
	long_password[64] = '\0';
	TEST_ASSERT_FALSE(zf_settings_set_sysclk((zf_sysclk_profile_t)4));
	TEST_ASSERT_FALSE(zf_settings_set_radios(4));
	TEST_ASSERT_FALSE(zf_settings_set_wifi_ssid_bytes(NULL, 1));
	TEST_ASSERT_FALSE(zf_settings_set_wifi_ssid_bytes(long_ssid, 33));
	TEST_ASSERT_FALSE(zf_settings_set_wifi_password("short"));
	TEST_ASSERT_FALSE(zf_settings_set_wifi_password(long_password));
	TEST_ASSERT_FALSE(zf_settings_set_wifi_password(NULL));
	assert_defaults();
	TEST_ASSERT_EQUAL_INT(0, save_settings());
	TEST_ASSERT_EQUAL_INT(0, write_calls);
}

void test_password_accepts_boundary_lengths_and_empty(void)
{
	char password[64];

	memset(password, '~', 63);
	password[63] = '\0';
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password(password));
	TEST_ASSERT_TRUE(zf_settings_wifi_password_configured());
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password(""));
	TEST_ASSERT_FALSE(zf_settings_wifi_password_configured());
	/* The hosts send any non-NUL UTF-8 bytes; the record keeps that contract. */
	TEST_ASSERT_TRUE(zf_settings_set_wifi_password("p\xC3\xA4sswort"));
	TEST_ASSERT_TRUE(zf_settings_wifi_password_configured());
}

void test_all_zero_key_is_present_until_cleared(void)
{
	uint8_t key[ZF_BLE_KEY_SIZE] = {0};
	uint8_t copy[ZF_BLE_KEY_SIZE];

	TEST_ASSERT_TRUE(zf_settings_set_ble_key(key));
	TEST_ASSERT_TRUE(zf_settings_ble_key_configured());
	TEST_ASSERT_EQUAL_INT(0, save_settings());
	zf_settings_load();
	TEST_ASSERT_TRUE(zf_settings_ble_key_configured());
	zf_settings_ble_key(copy);
	TEST_ASSERT_EQUAL_MEMORY(key, copy, sizeof(key));
	TEST_ASSERT_TRUE(zf_settings_set_ble_key(NULL));
	TEST_ASSERT_FALSE(zf_settings_ble_key_configured());
}

void test_corrupted_crc_loads_defaults(void)
{
	save_valid_record();
	flash[144] ^= 1u;
	zf_settings_load();
	TEST_ASSERT_EQUAL_INT(ZF_SETTINGS_DEFAULTS, zf_settings_source());
	assert_defaults();
}

void test_invalid_fields_with_valid_crc_load_defaults(void)
{
	static const struct {
		uint8_t offset;
		uint8_t value;
	} cases[] = {
		{0, 'P'},    /* magic */
		{4, 2},      /* version */
		{5, 1},      /* version high byte */
		{6, 147},    /* size */
		{7, 1},      /* size high byte */
		{8, 4},      /* clock profile */
		{9, 1},      /* reserved SD speed byte */
		{10, 4},     /* unknown service bit */
		{11, 4},     /* auth state */
		{12, 33},    /* SSID length */
		{20, 'x'},   /* SSID byte beyond length */
		{48, '\0'},  /* passphrase shorter than eight characters */
		{60, 'x'},   /* byte after the passphrase terminator */
		{110, 1},    /* key bytes without the present flag */
		{141, 2},    /* unknown credential flag */
		{142, 1},    /* reserved */
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		save_valid_record();
		flash[cases[i].offset] = cases[i].value;
		reseal();
		zf_settings_load();
		TEST_ASSERT_EQUAL_INT_MESSAGE(ZF_SETTINGS_DEFAULTS, zf_settings_source(), "invalid field accepted");
		assert_defaults();
	}
}

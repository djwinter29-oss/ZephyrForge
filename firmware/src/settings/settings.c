#include "settings/settings.h"
#include "settings/settings_flash.h"
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include "app/runtime.h"

#include <errno.h>
#include <string.h>

#define RECORD_MAGIC 0x46434645u /* "EFCF" little-endian */
#define RECORD_VERSION 1u
#define CRC_OFFSET 144u
#define SERVICE_BITS (ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE)
#define KEY_PRESENT 1u

struct settings {
	uint8_t sysclk;
	uint8_t disabled;
	uint8_t ssid_length;
	uint8_t ssid[ZF_SSID_MAX_BYTES];
	char password[ZF_WIFI_PASSWORD_SIZE];
	uint8_t key[ZF_BLE_KEY_SIZE];
	uint8_t flags;
};

struct boot_settings {
	uint8_t sysclk;
	uint8_t disabled;
};

/* Zero is the default for every field: all radios allowed, build clock, no credentials. */
static struct settings staged;
static struct settings saved;
static struct boot_settings boot;
static bool saving;
static zf_settings_source_t source;
#if IS_ENABLED(CONFIG_ZF_SETTINGS_SAVE)
static uint8_t save_record[ZF_SETTINGS_RECORD_SIZE];
static zf_settings_save_done_t save_done;

static int save_prepare(uint8_t record[ZF_SETTINGS_RECORD_SIZE]);
static int save_finish(int err);

static void save_written(int err)
{
	zf_settings_save_done_t done;

	zf_app_lock();
	done = save_done;
	err = save_finish(err);
	memset(save_record, 0, sizeof(save_record));
	save_done = NULL;
	zf_app_unlock();
	done(err);
}
#endif

static const struct settings *visible(void)
{
	return saving ? &saved : &staged;
}

#if IS_ENABLED(CONFIG_ZF_SETTINGS_SAVE)
static void put_u16(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)value;
	out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value)
{
	put_u16(out, (uint16_t)value);
	put_u16(out + 2, (uint16_t)(value >> 16));
}
#endif

static uint32_t get_u32(const uint8_t *in)
{
	return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

static bool zero(const uint8_t *data, size_t size)
{
	while (size--) {
		if (*data++) {
			return false;
		}
	}
	return true;
}

static bool password_valid(const char *password, size_t capacity)
{
	size_t length = 0;

	while (length < capacity && password[length]) {
		++length;
	}
	return length < capacity && (length == 0 || (length >= 8 && length <= 63));
}

#if IS_ENABLED(CONFIG_ZF_SETTINGS_SAVE)
static void encode(const struct settings *config, uint8_t record[ZF_SETTINGS_RECORD_SIZE])
{
	memset(record, 0, ZF_SETTINGS_RECORD_SIZE);
	put_u32(record, RECORD_MAGIC);
	put_u16(record + 4, RECORD_VERSION);
	put_u16(record + 6, ZF_SETTINGS_RECORD_SIZE);
	record[8] = config->sysclk;
	record[10] = config->disabled;
	record[11] = 0; /* Legacy Wi-Fi auth mode is reserved; the station currently selects WPA2 by password. */
	record[12] = config->ssid_length;
	memcpy(record + 13, config->ssid, sizeof(config->ssid));
	memcpy(record + 45, config->password, sizeof(config->password));
	memcpy(record + 109, config->key, sizeof(config->key));
	record[141] = config->flags;
	put_u32(record + CRC_OFFSET, crc32_ieee(record, CRC_OFFSET));
}
#endif

static bool decode(const uint8_t record[ZF_SETTINGS_RECORD_SIZE], struct settings *config)
{
	const char *password = (const char *)record + 45;
	size_t password_length;

	if (get_u32(record) != RECORD_MAGIC || record[4] != RECORD_VERSION || record[5] != 0 ||
	    record[6] != ZF_SETTINGS_RECORD_SIZE || record[7] != 0 ||
	    get_u32(record + CRC_OFFSET) != crc32_ieee(record, CRC_OFFSET)) {
		return false;
	}
	if (record[8] > ZF_SYSCLK_MAX_PROFILE || record[9] != 0 || (record[10] & ~SERVICE_BITS) ||
	    record[11] > 3u || record[12] > ZF_SSID_MAX_BYTES ||
	    !zero(record + 13 + record[12], ZF_SSID_MAX_BYTES - record[12]) ||
	    !password_valid(password, ZF_WIFI_PASSWORD_SIZE) || (record[141] & ~KEY_PRESENT) ||
	    (!(record[141] & KEY_PRESENT) && !zero(record + 109, ZF_BLE_KEY_SIZE)) || record[142] || record[143]) {
		return false;
	}
	password_length = strlen(password);
	if (!zero(record + 45 + password_length, ZF_WIFI_PASSWORD_SIZE - password_length)) {
		return false;
	}
	memset(config, 0, sizeof(*config));
	config->sysclk = record[8];
	config->disabled = record[10];
	config->ssid_length = record[12];
	memcpy(config->ssid, record + 13, sizeof(config->ssid));
	memcpy(config->password, password, sizeof(config->password));
	memcpy(config->key, record + 109, sizeof(config->key));
	config->flags = record[141];
	return true;
}

void zf_settings_load(void)
{
	uint8_t record[ZF_SETTINGS_RECORD_SIZE];
	struct settings loaded = {0};

	if (zf_settings_flash_read(record, sizeof(record)) != 0) {
		source = ZF_SETTINGS_READ_ERROR;
	} else if (decode(record, &loaded)) {
		source = ZF_SETTINGS_SAVED;
	} else {
		memset(&loaded, 0, sizeof(loaded));
		source = ZF_SETTINGS_DEFAULTS;
	}
	saved = loaded;
	staged = loaded;
	boot.sysclk = loaded.sysclk;
	boot.disabled = loaded.disabled;
	saving = false;
#if IS_ENABLED(CONFIG_ZF_SETTINGS_SAVE)
	save_done = NULL;
	memset(save_record, 0, sizeof(save_record));
#endif
	memset(record, 0, sizeof(record));
	memset(&loaded, 0, sizeof(loaded));
}

zf_settings_source_t zf_settings_source(void)
{
	return source;
}

zf_sysclk_profile_t zf_settings_sysclk(void)
{
	return (zf_sysclk_profile_t)visible()->sysclk;
}

bool zf_settings_set_sysclk(zf_sysclk_profile_t profile)
{
	if (saving || (unsigned)profile > ZF_SYSCLK_MAX_PROFILE) {
		return false;
	}
	staged.sysclk = (uint8_t)profile;
	return true;
}

zf_sysclk_profile_t zf_settings_boot_sysclk(void)
{
	return (zf_sysclk_profile_t)boot.sysclk;
}

uint8_t zf_settings_radios(void)
{
	return (uint8_t)(~visible()->disabled & SERVICE_BITS);
}

bool zf_settings_set_radios(uint8_t radios)
{
	if (saving || (radios & ~SERVICE_BITS)) {
		return false;
	}
	staged.disabled = (uint8_t)(~radios & SERVICE_BITS);
	return true;
}

uint8_t zf_settings_boot_radios(void)
{
	return (uint8_t)(~boot.disabled & SERVICE_BITS);
}

uint8_t zf_settings_wifi_ssid_bytes(uint8_t ssid[ZF_SSID_MAX_BYTES])
{
	const struct settings *current = visible();

	memcpy(ssid, current->ssid, sizeof(current->ssid));
	return current->ssid_length;
}

bool zf_settings_set_wifi_ssid_bytes(const uint8_t *ssid, uint8_t length)
{
	if (saving || length > sizeof(staged.ssid) || (!ssid && length)) {
		return false;
	}
	memset(staged.ssid, 0, sizeof(staged.ssid));
	if (length) {
		memcpy(staged.ssid, ssid, length);
	}
	staged.ssid_length = length;
	return true;
}

void zf_settings_wifi_password(char password[ZF_WIFI_PASSWORD_SIZE])
{
	memcpy(password, visible()->password, sizeof(staged.password));
}

bool zf_settings_set_wifi_password(const char *password)
{
	if (saving || !password || !password_valid(password, ZF_WIFI_PASSWORD_SIZE)) {
		return false;
	}
	memset(staged.password, 0, sizeof(staged.password));
	memcpy(staged.password, password, strlen(password));
	return true;
}

bool zf_settings_wifi_password_configured(void)
{
	return visible()->password[0] != '\0';
}

void zf_settings_ble_key(uint8_t key[ZF_BLE_KEY_SIZE])
{
	memcpy(key, visible()->key, sizeof(staged.key));
}

bool zf_settings_set_ble_key(const uint8_t key[ZF_BLE_KEY_SIZE])
{
	if (saving) {
		return false;
	}
	if (key) {
		memcpy(staged.key, key, sizeof(staged.key));
		staged.flags |= KEY_PRESENT;
	} else {
		memset(staged.key, 0, sizeof(staged.key));
		staged.flags &= (uint8_t)~KEY_PRESENT;
	}
	return true;
}

bool zf_settings_ble_key_configured(void)
{
	return (visible()->flags & KEY_PRESENT) != 0;
}

#if IS_ENABLED(CONFIG_ZF_SETTINGS_SAVE)
static int save_prepare(uint8_t record[ZF_SETTINGS_RECORD_SIZE])
{
	if (saving) {
		return -EBUSY;
	}
	if (memcmp(&staged, &saved, sizeof(staged)) == 0) {
		return 0;
	}
	encode(&staged, record);
	saving = true;
	return 1;
}

static int save_finish(int err)
{
	if (!saving) {
		return -EINVAL;
	}
	if (err) {
		staged = saved;
		err = err < 0 ? err : -EIO;
	} else {
		saved = staged;
		source = ZF_SETTINGS_SAVED;
	}
	saving = false;
	return err;
}

int zf_settings_save_async(zf_settings_save_done_t done)
{
	int err;

	if (done == NULL || save_done != NULL) {
		return done == NULL ? -EINVAL : -EBUSY;
	}
	err = save_prepare(save_record);
	if (err < 0) {
		memset(save_record, 0, sizeof(save_record));
		return err;
	}
	save_done = done;
	if (err == 0) {
		save_done = NULL;
		return ZF_SETTINGS_SAVE_UNCHANGED;
	}
	err = zf_settings_flash_write_async(save_record, sizeof(save_record), save_written);
	if (err != 0) {
		(void)save_finish(err);
		memset(save_record, 0, sizeof(save_record));
		save_done = NULL;
	}
	return err;
}
#else
int zf_settings_save_async(zf_settings_save_done_t done)
{
	(void)done;
	return -ENOTSUP;
}
#endif

void zf_settings_discard(void)
{
	if (saving) {
		return;
	}
	staged = saved;
}

/**
 * @file settings.h
 * @brief ZephyrForge boot settings shared by supported firmware targets.
 *
 * One CRC-protected record in the `storage_partition` holds the
 * system clock profile, radio switches and credentials. Setters stage values in RAM; zf_settings_save_async()
 * commits them as one unit. Saved values apply at the next boot only.
 *
 * This API is not thread-safe. Call zf_settings_load() once before other threads use settings; afterwards all
 * access must be serialized with the application lock (zf_lock()/zf_unlock()). Management callbacks can stage
 * and save settings; ownership is not limited to the main thread.
 */

#ifndef ZEPHYRFORGE_SETTINGS_H
#define ZEPHYRFORGE_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sysclk/sysclk.h"
#include "wireless/wireless_types.h"
/** NUL-terminated passphrase storage size, including its terminator. */
#define ZF_WIFI_PASSWORD_SIZE 64u
/** Control key size in bytes. */
#define ZF_BLE_KEY_SIZE 32u
/** Serialized record size in bytes. */
#define ZF_SETTINGS_RECORD_SIZE 148u

/** Stored Wi-Fi authentication policy, including the last confirmed mode when available. */
typedef enum {
	ZF_WIFI_AUTH_TRY_WPA2 = 0, /**< Mode unknown; try WPA2 first. */
	ZF_WIFI_AUTH_TRY_SAE = 1,  /**< Mode unknown; try WPA3-SAE. */
	ZF_WIFI_AUTH_WPA2 = 2,     /**< WPA2 connection was confirmed. */
	ZF_WIFI_AUTH_SAE = 3,      /**< WPA3-SAE connection was confirmed. */
} zf_wifi_auth_state_t;

/** Where the RAM copy came from at the last load. */
typedef enum {
	ZF_SETTINGS_NOT_LOADED = 0, /**< zf_settings_load() has not run; defaults are in use. */
	ZF_SETTINGS_DEFAULTS = 1,   /**< The record is erased or invalid; defaults are in use. */
	ZF_SETTINGS_SAVED = 2,      /**< A valid saved record was loaded. */
	ZF_SETTINGS_READ_ERROR = 3, /**< Flash could not be read; defaults are in use. */
} zf_settings_source_t;

/**
 * @brief Read and validate the saved record, replacing the RAM copy and the boot snapshot.
 *
 * An erased, invalid or unreadable record loads complete defaults: every radio allowed, the build clock, and no
 * credentials. Boot never depends on flash succeeding.
 */
void zf_settings_load(void);
/** @brief Report where the current RAM copy came from. */
zf_settings_source_t zf_settings_source(void);

/** @brief Return the staged CPU clock profile, or the last confirmed value while a save is in progress. */
zf_sysclk_profile_t zf_settings_sysclk(void);
/**
 * @brief Stage a CPU clock profile; persist with zf_settings_save_async(), applied at the next boot.
 * @return false (configuration unchanged) for an unknown profile.
 */
bool zf_settings_set_sysclk(zf_sysclk_profile_t profile);
/** @brief Return the CPU clock profile loaded at boot, which is the one the running firmware applied. */
zf_sysclk_profile_t zf_settings_boot_sysclk(void);

/** @brief Return the staged ZF_WIRELESS_* radios, or the last confirmed value during a save. */
uint8_t zf_settings_radios(void);
/**
 * @brief Stage the ZF_WIRELESS_* radio switches; persist with zf_settings_save_async(), applied at the next boot.
 * @return false (configuration unchanged) for unknown bits.
 */
bool zf_settings_set_radios(uint8_t radios);
/** @brief Return the radios loaded at boot, which are the ones the running firmware started. */
uint8_t zf_settings_boot_radios(void);

/**
 * @brief Copy the staged SSID, or the last confirmed value while a save is in progress, into a NUL-terminated buffer.
 *
 * Opaque SSIDs may contain embedded zeros; use the byte getter for network identity.
 */
void zf_settings_wifi_ssid(char ssid[ZF_SSID_BUFFER_SIZE]);
/**
 * @brief Stage an SSID string; an empty string clears it.
 * @return false for null or a string longer than ZF_SSID_MAX_BYTES.
 */
bool zf_settings_set_wifi_ssid(const char *ssid);
/**
 * @brief Copy the staged opaque SSID, or the last confirmed value during a save, zero padded to
 *        ZF_SSID_MAX_BYTES.
 * @return Saved byte length, including embedded zero bytes.
 */
uint8_t zf_settings_wifi_ssid_bytes(uint8_t ssid[ZF_SSID_MAX_BYTES]);
/**
 * @brief Stage an opaque SSID without text conversion.
 * @param ssid Bytes to copy; may be null only when length is zero.
 * @param length Byte length, zero through ZF_SSID_MAX_BYTES.
 */
bool zf_settings_set_wifi_ssid_bytes(const uint8_t *ssid, uint8_t length);
/** @brief Copy the staged (or last confirmed during a save) passphrase; never expose it in a host report. */
void zf_settings_wifi_password(char password[ZF_WIFI_PASSWORD_SIZE]);
/**
 * @brief Stage a passphrase; empty clears it, otherwise it must be 8-63 non-NUL bytes (UTF-8 is accepted).
 * @return false for null or an invalid passphrase.
 */
bool zf_settings_set_wifi_password(const char *password);
/** @brief Return whether a Wi-Fi passphrase is staged, or last confirmed during a save. */
bool zf_settings_wifi_password_configured(void);
/** @brief Return the staged Wi-Fi authentication state, or last confirmed during a save. */
zf_wifi_auth_state_t zf_settings_wifi_auth_state(void);
/** @brief Stage the Wi-Fi authentication state; false for an unknown state. */
bool zf_settings_set_wifi_auth_state(zf_wifi_auth_state_t state);

/** @brief Copy the staged (or last confirmed during a save) private BLE Control key. */
void zf_settings_ble_key(uint8_t key[ZF_BLE_KEY_SIZE]);
/** @brief Stage a raw Control key, including all-zero bytes; null clears it. Returns false during a save. */
bool zf_settings_set_ble_key(const uint8_t key[ZF_BLE_KEY_SIZE]);
/** @brief Return whether a Control key is staged, or last confirmed during a save. */
bool zf_settings_ble_key_configured(void);

/** @brief Report whether saved wireless settings differ from the values loaded at boot. */
bool zf_settings_wireless_restart_required(void);
/** @brief Report whether any saved setting differs from the values loaded at boot. */
bool zf_settings_restart_required(void);

/** Settings save completion; called once after the settings record is committed or discarded. */
typedef void (*zf_settings_save_done_t)(int err);

/**
 * @brief Persist staged settings as one record outside the caller's lock.
 *
 * The settings module owns record preparation, flash submission and finalization. The callback may run before
 * this function returns when there is nothing to write; otherwise it runs once after write/readback completes.
 * The single-copy flash write is not power-loss atomic.
 * @return 0 when accepted, otherwise a negative errno and no callback.
 */
int zf_settings_save_async(zf_settings_save_done_t done);
/** @brief Drop all staged, unsaved values, restoring the last loaded or saved configuration. */
void zf_settings_discard(void);

/** @brief Return the IEEE CRC-32 (reflected) used by the record. */
uint32_t zf_settings_crc32(const uint8_t *data, size_t size);

#endif

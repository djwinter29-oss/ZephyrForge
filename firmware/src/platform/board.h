/**
 * @file board.h
 * @brief Hardware identity, board facts, memory inventory and delayed reboot.
 */
#ifndef ZEPHYRFORGE_BOARD_H
#define ZEPHYRFORGE_BOARD_H

#include <stdint.h>
#include "protocol/report_codec.h"

/** Grace period between an accepted reboot or save result and the reset. */
#define ZF_REBOOT_DELAY_MS 1000

/** Sample identity, memory and reset cause before any transport starts. */
void zf_board_init(void);
/** Boot-sampled eight-byte unique identity; zeros if the hardware source failed. */
const uint8_t *zf_board_uid(void);
/** Uppercase hex USB serial derived from the UID, NUL-terminated. */
const char *zf_board_serial(void);

/** Copy hardware UID and SRAM accounting; firmware version is supplied by management. */
void zf_board_identity_snapshot(zf_identity_t *out);
/** Copy the immutable boot-sampled flash and PSRAM inventory. */
void zf_board_memory_snapshot(zf_memory_inventory_t *out);
/** Copy reset cause and configured pins; saved clock and radio policy are supplied by management. */
void zf_board_config_snapshot(zf_config_t *out);

/** Queue a reset after ZF_REBOOT_DELAY_MS; returns zero or a negative errno. */
int zf_board_schedule_reboot(void);

/**
 * @brief Probe flash and external RAM capacity; implemented once per SoC family and called by zf_board_init().
 * @param out Zero-initialized inventory to fill; unavailable parts keep their availability flag clear.
 */
void zf_soc_memory_inventory(zf_memory_inventory_t *out);

#endif

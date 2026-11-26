/**
 * @file board.h
 * @brief Boot-sampled board facts, their Feature 1, 2 and 13 reports, and delayed reboot.
 */
#ifndef ZEPHYRFORGE_BOARD_H
#define ZEPHYRFORGE_BOARD_H

#include <stdint.h>

/** Grace period between an accepted reboot or save result and the reset. */
#define ZF_REBOOT_DELAY_MS 1000

/** Sample identity, memory and reset cause before any transport starts. */
void zf_board_init(void);
/** Boot-sampled eight-byte unique identity; zeros if the hardware source failed. */
const uint8_t *zf_board_uid(void);
/** Uppercase hex USB serial derived from the UID, NUL-terminated. */
const char *zf_board_serial(void);

/** Encode Feature 1: identity, firmware version, MCU, board model and capabilities. */
void zf_board_identity_report(uint8_t *out);
/** Encode Feature 13: the immutable boot-sampled module memory inventory. */
void zf_board_memory_report(uint8_t *out);
/** Encode Feature 2: saved configuration and hardware pin allocation. */
void zf_board_config_report(uint8_t *out);

/** Queue a reset after ZF_REBOOT_DELAY_MS; returns zero or a negative errno. */
int zf_board_schedule_reboot(void);

#endif

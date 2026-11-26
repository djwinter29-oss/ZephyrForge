/**
 * @file storage.h
 * @brief SD storage service: card ownership between firmware and USB, and the Feature 10 snapshot.
 */
#ifndef ZEPHYRFORGE_STORAGE_H
#define ZEPHYRFORGE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/** Register the storage service and start policy polling with the boot-sampled card state. */
void zf_storage_init(void);
/** Encode Feature 10: the coherent storage snapshot with the given Input sequence. */
void zf_storage_report(uint8_t sequence, uint8_t *out);
/** Let the storage worker change owners after this Feature 20 result has left the control path. */
void zf_storage_result_read(uint8_t transaction);
/** True while the storage worker is busy or USB exclusively owns the card. */
bool zf_storage_busy(void);
/**
 * @brief Admit one USB ownership request without doing disk I/O; correlate its result before USB refresh.
 * @param usb True requests USB ownership; false returns the medium to firmware.
 * @param transaction Feature 20 transaction whose result read shortens the handoff delay.
 * @return Zero when admitted or the owner is already active, -EBUSY while storage is busy, -ENOMEDIUM when
 *         USB is requested without an initialized card with valid geometry, -ENOTSUP when USB export is
 *         not built, or another negative errno.
 */
int zf_storage_request(bool usb, uint8_t transaction);

#endif

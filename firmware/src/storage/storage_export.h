/**
 * @file storage_export.h
 * @brief Storage-owned card export lock used by the USB mass-storage disk proxy.
 */
#ifndef ZEPHYRFORGE_STORAGE_EXPORT_H
#define ZEPHYRFORGE_STORAGE_EXPORT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Check that USB owns a present, initialized card.
 * @param blocks Receives the exported block count when not NULL.
 * @return True while the card is exported and no storage work is pending.
 */
bool zf_storage_export_ready(uint32_t *blocks);
/**
 * @brief Lock the exported card for one MSC batch and check ownership again under the lock.
 *
 * Fails instead of waiting while the storage worker owns the card. An MSC command parked behind a benchmark or
 * remount makes the host time out and reset the device, which can eventually leave it unable to enumerate.
 *
 * @param blocks Receives the exported block count when not NULL.
 * @return True with the media lock held, or false without it.
 */
bool zf_storage_export_lock(uint32_t *blocks);
/** Release the media lock taken by a successful zf_storage_export_lock(). */
void zf_storage_export_unlock(void);
/** Allow USB ownership requests once the USB disk proxy has registered; until then they fail with -ENOTSUP. */
void zf_storage_export_attach(void);
/**
 * @brief Read the card owner the USB host should see after re-enumeration.
 * @param usb Receives true while USB owns the card.
 * @return False while an ownership handoff or other media work is still in progress.
 */
bool zf_storage_export_owner(bool *usb);

#endif

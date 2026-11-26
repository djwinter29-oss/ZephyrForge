/**
 * @file runtime.h
 * @brief Shared application synchronization and service-loop wakeups.
 */
#ifndef ZEPHYRFORGE_APP_RUNTIME_H
#define ZEPHYRFORGE_APP_RUNTIME_H

#include <stdint.h>

/** Acquire the recursive mutex for control, settings, staging and BLE session state. */
void zf_app_lock(void);
/** Release the application mutex acquired by zf_app_lock(). */
void zf_app_unlock(void);
/** Wake the service loop after an event; multiple pending wakeups coalesce. */
void zf_app_wakeup(void);
/**
 * @brief Wait for an event or a bounded service-loop timeout.
 * @param timeout_ms Nonnegative wait in milliseconds; zero never blocks.
 */
void zf_app_wait(int64_t timeout_ms);

#endif

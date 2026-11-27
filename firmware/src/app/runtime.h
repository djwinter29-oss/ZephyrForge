/**
 * @file runtime.h
 * @brief Shared application synchronization and service-loop wakeups.
 */
#ifndef ZEPHYRFORGE_APP_RUNTIME_H
#define ZEPHYRFORGE_APP_RUNTIME_H

#include <stdint.h>

/**
 * @brief Acquire the recursive mutex for control, settings, staging and BLE session state.
 *
 * When a call needs both this lock and a service lock, acquire this lock first. Release service locks before
 * acquiring it; do not invert the order.
 */
void zf_app_lock(void);
/** Release the application mutex acquired by zf_app_lock(). */
void zf_app_unlock(void);
/** Wake the service loop after an event; multiple pending wakeups coalesce. */
void zf_app_wakeup(void);
/**
 * @brief Wait for an event or a service-loop timeout.
 * @param timeout_ms Nonnegative wait in milliseconds; zero never blocks and INT64_MAX waits indefinitely.
 */
void zf_app_wait(int64_t timeout_ms);

#endif

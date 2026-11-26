/**
 * @file zephyrforge.h
 * @brief Firmware version and the application lock shared by every module.
 */
#ifndef ZEPHYRFORGE_H
#define ZEPHYRFORGE_H

#ifndef ZEPHYRFORGE_VERSION
/** Semantic version stamped by the build (ZEPHYRFORGE_VERSION environment) or the development default. */
#define ZEPHYRFORGE_VERSION "0.1.0"
#endif

/** Recursive lock for shared control, settings, staging and BLE session state. */
void zf_lock(void);
/** Release the recursive application lock taken by zf_lock(). */
void zf_unlock(void);
/** Wake the main service loop after a queued module event. */
void zf_poll_wakeup(void);
#endif

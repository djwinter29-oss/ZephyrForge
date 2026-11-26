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

/** One recursive lock serializes the control dispatcher, staging and wireless state across threads. */
void zf_lock(void);
/** Release the recursive application lock taken by zf_lock(). */
void zf_unlock(void);

#endif

/**
 * @file sysclk.h
 * @brief CPU clock profiles; applying a profile is implemented once per SoC family.
 */
#ifndef ZEPHYRFORGE_SYSCLK_H
#define ZEPHYRFORGE_SYSCLK_H

#include <stdint.h>

/** User-selected CPU clock profile; flash stores its ID, while host protocols carry MHz. */
typedef enum {
	ZF_SYSCLK_DEFAULT = 0, /**< Keep the board build clock (240 MHz on S3, 160 MHz on C6). */
	ZF_SYSCLK_80MHZ = 1,   /**< 80 MHz. */
	ZF_SYSCLK_160MHZ = 2,  /**< 160 MHz. */
	ZF_SYSCLK_240MHZ = 3,  /**< 240 MHz. */
} zf_sysclk_profile_t;

/** Highest clock-profile ID accepted by this build; C6 builds cap it at 160 MHz. */
#ifndef ZF_SYSCLK_MAX_PROFILE
#define ZF_SYSCLK_MAX_PROFILE 3u
#endif

/**
 * @brief Convert a CPU clock profile to MHz.
 * @return 0 (keep the build clock) for ZF_SYSCLK_DEFAULT or an unknown profile, otherwise 80, 160 or 240.
 */
static inline uint16_t zf_sysclk_profile_mhz(zf_sysclk_profile_t profile)
{
	static const uint16_t mhz[] = {0u, 80u, 160u, 240u};

	return (unsigned)profile < sizeof(mhz) / sizeof(mhz[0]) ? mhz[profile] : 0u;
}

/**
 * @brief Convert a protocol MHz value to a CPU clock profile.
 * @return The matching profile, or an out-of-range profile rejected by zf_settings_set_sysclk().
 */
static inline zf_sysclk_profile_t zf_sysclk_profile_from_mhz(uint16_t mhz)
{
	unsigned profile = ZF_SYSCLK_DEFAULT;

	while (profile <= ZF_SYSCLK_MAX_PROFILE && zf_sysclk_profile_mhz((zf_sysclk_profile_t)profile) != mhz) {
		++profile;
	}
	return (zf_sysclk_profile_t)profile;
}

/**
 * @brief Apply the saved CPU clock profile before transports start.
 * @param mhz Saved clock profile; zero or the devicetree default leaves the clock unchanged.
 * @return Zero on success or no change, otherwise a negative errno.
 */
int zf_sysclk_apply(uint16_t mhz);
/** Running CPU clock in Hz, or the devicetree default when the clock driver cannot report it. */
uint32_t zf_sysclk_hz(void);

#endif

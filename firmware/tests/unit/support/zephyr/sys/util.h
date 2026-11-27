#ifndef ZEPHYRFORGE_TEST_ZEPHYR_SYS_UTIL_H
#define ZEPHYRFORGE_TEST_ZEPHYR_SYS_UTIL_H

#ifndef CONFIG_ZEPHYR
#ifndef CONFIG_ZF_LED
#define CONFIG_ZF_LED 1
#endif
#ifndef CONFIG_ZF_WIFI
#define CONFIG_ZF_WIFI 1
#endif
#ifndef CONFIG_ZF_BLUETOOTH
#define CONFIG_ZF_BLUETOOTH 1
#endif
#ifndef CONFIG_ZF_CPU_LOAD
#define CONFIG_ZF_CPU_LOAD 1
#endif
#ifndef CONFIG_ZF_STORAGE
#define CONFIG_ZF_STORAGE 1
#endif
#ifndef CONFIG_ZF_USB_HID
#define CONFIG_ZF_USB_HID 1
#endif
#ifndef CONFIG_ZF_USB_MSC
#define CONFIG_ZF_USB_MSC 1
#endif
#ifndef CONFIG_ZF_STORAGE_BENCHMARK
#define CONFIG_ZF_STORAGE_BENCHMARK 1
#endif
#ifndef CONFIG_ZF_SETTINGS_SAVE
#define CONFIG_ZF_SETTINGS_SAVE 1
#endif
#ifndef CONFIG_SPI_SDHC
#define CONFIG_SPI_SDHC 0
#endif
#endif

#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)

#endif

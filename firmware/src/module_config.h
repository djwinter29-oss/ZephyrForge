/**
 * @file module_config.h
 * @brief Build-selected module switches shared by firmware and portable tests.
 */
#ifndef ZEPHYRFORGE_MODULE_CONFIG_H
#define ZEPHYRFORGE_MODULE_CONFIG_H

#ifndef ZF_ENABLE_STORAGE
#define ZF_ENABLE_STORAGE 1
#endif
#ifndef ZF_ENABLE_USB_HID
#define ZF_ENABLE_USB_HID 1
#endif
#ifndef ZF_ENABLE_USB_MSC
#define ZF_ENABLE_USB_MSC (ZF_ENABLE_STORAGE && ZF_ENABLE_USB_HID)
#endif
#ifndef ZF_ENABLE_STORAGE_BENCHMARK
#define ZF_ENABLE_STORAGE_BENCHMARK (ZF_ENABLE_STORAGE && ZF_ENABLE_USB_HID)
#endif
#ifndef ZF_STORAGE_SPI
#define ZF_STORAGE_SPI 0
#endif

#endif

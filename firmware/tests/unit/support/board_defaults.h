/* Native tests model the ESP32-S3 DevKitC unless a test's define set selects another board. */
#ifndef ZF_MCU
#define ZF_MCU ZF_MCU_ESP32S3
#endif
#ifndef ZF_BOARD_MODEL
#define ZF_BOARD_MODEL ZF_BOARD_DEVKITC
#endif

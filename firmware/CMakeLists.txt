cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyrforge)
if(NOT CONFIG_ZF_STORAGE AND
   (CONFIG_USBD_MSC_CLASS OR CONFIG_DISK_ACCESS OR CONFIG_DISK_DRIVER_SDMMC OR CONFIG_FILE_SYSTEM OR
    CONFIG_FAT_FILESYSTEM_ELM OR CONFIG_FS_FATFS_EXFAT OR CONFIG_SDHC OR CONFIG_SPI_SDHC OR
    CONFIG_ZF_SDHC_RPI_PICO_PIO))
  message(FATAL_ERROR
    "SD storage subsystems are owned by CONFIG_ZF_STORAGE; partial storage configuration is not supported")
endif()
if(CONFIG_FS_FATFS_MOUNT_MKFS OR CONFIG_FS_FATFS_MKFS)
  message(FATAL_ERROR "ZephyrForge never formats SD cards; disable FAT mkfs support")
endif()
if(NOT CONFIG_ZF_USB_HID AND NOT CONFIG_ZF_BLUETOOTH)
  message(FATAL_ERROR "Firmware needs a management link: enable CONFIG_ZF_USB_HID or CONFIG_ZF_BLUETOOTH")
endif()
# Module switches are Kconfig symbols; these definitions let native unit tests select the same variants.
target_compile_definitions(app PRIVATE ZF_STORAGE_SPI=$<BOOL:${CONFIG_SPI_SDHC}>
  ZF_ENABLE_USB_HID=$<BOOL:${CONFIG_ZF_USB_HID}>
  ZF_ENABLE_STORAGE=$<BOOL:${CONFIG_ZF_STORAGE}>
  ZF_ENABLE_USB_MSC=$<BOOL:${CONFIG_ZF_USB_MSC}>
  ZF_ENABLE_STORAGE_BENCHMARK=$<BOOL:${CONFIG_ZF_STORAGE_BENCHMARK}>)
if(CONFIG_SOC_FAMILY_RPI_PICO)
  # Report 1 identity: MCU 1 RP2040 or 2 RP2350. Report 2 monitor UART pins are read from the
  # console devicetree pinctrl in board.c.
  if(CONFIG_SOC_RP2040)
    set(zf_mcu ZF_MCU_RP2040)
  else()
    set(zf_mcu ZF_MCU_RP2350)
  endif()
  if(CONFIG_BOARD_RPI_PICO_RP2040_W)
    set(zf_board ZF_BOARD_PICO_W)
  elseif(CONFIG_BOARD_RPI_PICO2_RP2350A_M33_W)
    set(zf_board ZF_BOARD_PICO2_W)
  elseif(CONFIG_BOARD_RPI_PICO2)
    set(zf_board ZF_BOARD_PICO2)
  else()
    set(zf_board ZF_BOARD_PICO)
  endif()
  if(CONFIG_ZF_WIFI)
    set(zf_pico_rand ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/pico_rand)
    target_sources(app PRIVATE src/drivers/random/random_rpi_pico.c ${zf_pico_rand}/rand.c)
    target_include_directories(app PRIVATE ${zf_pico_rand}/include
      ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/common/pico_time/include)
    target_compile_definitions(app PRIVATE PICO_RAND_SEED_ENTROPY_SRC_BOARD_ID=0
      PICO_RAND_SEED_ENTROPY_SRC_RAM_HASH=0)
    # Map SDK state to Zephyr RAM and use timer waits for microsecond ROSC sampling, without SDK alarm pools.
    set_source_files_properties(${zf_pico_rand}/rand.c TARGET_DIRECTORY app PROPERTIES
      COMPILE_OPTIONS "-D__uninitialized_ram(name)=__attribute__((section(\".noinit\"))) name;-Dsleep_until=busy_wait_until")
    zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/random/random.h)
  endif()
  set(zf_board_caps 0)
  target_sources(app PRIVATE src/sysclk/sysclk_rpi_pico.c)
  target_sources_ifdef(CONFIG_ZF_SDHC_RPI_PICO_PIO app PRIVATE src/drivers/sdhc/sdhc_rpi_pico_pio.c
    src/drivers/sdhc/sdio_codec.c)
else()
  if(CONFIG_SOC_ESP32S3)
    set(zf_mcu ZF_MCU_ESP32S3)
    set(zf_board ZF_BOARD_DEVKITC)
    set(zf_board_caps "ZF_CAP_LED|ZF_CAP_SETTINGS")
  elseif(CONFIG_SOC_ESP32C6)
    set(zf_mcu ZF_MCU_ESP32C6)
    set(zf_board ZF_BOARD_ESP32C6_DEVKITC)
    set(zf_board_caps ZF_CAP_SETTINGS)
    set(zf_sysclk_max_profile 2u)
  else()
    message(FATAL_ERROR "Unsupported Espressif SoC")
  endif()
  target_sources(app PRIVATE src/sysclk/sysclk_espressif.c)
  if(DEFINED zf_sysclk_max_profile)
    target_compile_definitions(app PRIVATE ZF_SYSCLK_MAX_PROFILE=${zf_sysclk_max_profile})
  endif()
endif()

set(zf_radio_mask 0)
if(CONFIG_ZF_WIFI)
  math(EXPR zf_radio_mask "${zf_radio_mask} | 1")
  target_sources(app PRIVATE src/wireless/wifi/wifi.c src/wireless/wifi/wifi_driver.c
    src/wireless/wifi/wifi_policy.c src/wireless/wifi/wifi_scan_table.c)
else()
  target_sources(app PRIVATE src/wireless/wifi/wifi_none.c)
endif()
if(CONFIG_ZF_BLUETOOTH)
  math(EXPR zf_radio_mask "${zf_radio_mask} | 2")
  target_sources(app PRIVATE src/wireless/ble/ble.c src/wireless/ble/ble_auth.c src/lib/sha256.c)
else()
  target_sources(app PRIVATE src/wireless/ble/ble_none.c)
endif()
if(zf_radio_mask)
  string(APPEND zf_board_caps "|ZF_CAP_WIRELESS")
endif()
target_compile_definitions(app PRIVATE ZF_MCU=${zf_mcu} ZF_BOARD_MODEL=${zf_board}
  "ZF_BOARD_CAPS=(${zf_board_caps})" ZF_RADIO_MASK=${zf_radio_mask})

if(CONFIG_ZF_STORAGE)
  target_sources(app PRIVATE src/storage/storage.c src/storage/storage_policy.c)
  target_sources_ifdef(CONFIG_ZF_USB_MSC app PRIVATE src/storage/storage_msc.c)
else()
  target_sources(app PRIVATE src/storage/storage_none.c)
endif()
if(CONFIG_ZF_STORAGE AND CONFIG_ZF_STORAGE_BENCHMARK)
  target_sources(app PRIVATE src/benchmark/benchmark.c)
elseif(NOT CONFIG_ZF_STORAGE)
  target_sources(app PRIVATE src/benchmark/benchmark_none.c)
endif()
target_sources(app PRIVATE
  src/main.c
  src/board/board.c
  src/control/control.c
  src/control/control_stage.c
  src/led/led.c
  src/report/report.c
  src/settings/settings.c
  src/settings/settings_flash.c
  src/telemetry/telemetry.c
  src/wireless/wireless.c)
if(CONFIG_ZF_USB_HID)
  target_sources(app PRIVATE src/usb/usb.c)
else()
  target_sources(app PRIVATE src/usb/usb_none.c)
endif()
target_include_directories(app PRIVATE src)
# The pinned ESP32-S3 HAL leaves the PHY library's weak sensor-power hooks linked. They power the die-temperature
# sensor off during Wi-Fi modem sleep, and the next telemetry read then spins with interrupts locked. The HAL
# override reference-counts that power instead.
if(CONFIG_ESP32_TEMP AND CONFIG_SOC_ESP32S3)
  target_sources(app PRIVATE ${ZEPHYR_HAL_ESPRESSIF_MODULE_DIR}/components/esp_phy/src/phy_override.c)
endif()
if(NOT DEFINED ZEPHYRFORGE_VERSION)
  set(ZEPHYRFORGE_VERSION "$ENV{ZEPHYRFORGE_VERSION}")
endif()
if(NOT "${ZEPHYRFORGE_VERSION}" STREQUAL "")
  set(zf_version_component "(0|[1-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])")
  if(NOT "${ZEPHYRFORGE_VERSION}" MATCHES
     "^${zf_version_component}\\.${zf_version_component}\\.${zf_version_component}$")
    message(FATAL_ERROR
      "ZEPHYRFORGE_VERSION must be MAJOR.MINOR.PATCH with components from 0 to 255 and no leading zeros")
  endif()
  target_compile_definitions(app PRIVATE "ZEPHYRFORGE_VERSION=\"${ZEPHYRFORGE_VERSION}\"")
endif()

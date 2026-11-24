cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyrforge)
option(ZF_ENABLE_STORAGE "Include removable SD-card and filesystem support" ON)
option(ZF_ENABLE_STORAGE_BENCHMARK "Include temporary-file verification and throughput measurement" OFF)
if(NOT ZF_ENABLE_STORAGE AND
   (CONFIG_USBD_MSC_CLASS OR CONFIG_DISK_ACCESS OR CONFIG_DISK_DRIVER_SDMMC OR CONFIG_FILE_SYSTEM OR
    CONFIG_FAT_FILESYSTEM_ELM OR CONFIG_FS_FATFS_EXFAT OR CONFIG_SDHC OR CONFIG_SPI_SDHC OR
    CONFIG_ZF_SDHC_RPI_PICO_PIO))
  message(FATAL_ERROR
    "ZF_ENABLE_STORAGE=OFF must not include profiles/storage.conf, profiles/usb-msc.conf or SD devicetree overlays")
endif()
if(ZF_ENABLE_STORAGE_BENCHMARK AND NOT ZF_ENABLE_STORAGE)
  message(FATAL_ERROR "ZF_ENABLE_STORAGE_BENCHMARK requires ZF_ENABLE_STORAGE")
endif()
target_compile_definitions(app PRIVATE ZF_STORAGE_SPI=$<BOOL:${CONFIG_SPI_SDHC}>)
target_compile_definitions(app PRIVATE ZF_ENABLE_STORAGE=$<BOOL:${ZF_ENABLE_STORAGE}>)
target_compile_definitions(app PRIVATE ZF_ENABLE_STORAGE_BENCHMARK=$<BOOL:${ZF_ENABLE_STORAGE_BENCHMARK}>)
if(ZF_ENABLE_STORAGE AND ZF_ENABLE_STORAGE_BENCHMARK)
  target_sources(app PRIVATE src/app/storage/benchmark.c)
endif()
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
  if(CONFIG_WIFI)
    set(zf_board_caps ZF_CAP_WIRELESS)
    set(zf_radio_mask 1u)
    list(APPEND zf_board_sources src/app/wireless/wifi/wifi.c)
    set(zf_pico_rand ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/pico_rand)
    target_sources(app PRIVATE src/platform/rpi_pico/random.c ${zf_pico_rand}/rand.c)
    target_include_directories(app PRIVATE ${zf_pico_rand}/include
      ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/common/pico_time/include)
    target_compile_definitions(app PRIVATE PICO_RAND_SEED_ENTROPY_SRC_BOARD_ID=0
      PICO_RAND_SEED_ENTROPY_SRC_RAM_HASH=0)
    # Map SDK state to Zephyr RAM and use timer waits for microsecond ROSC sampling, without SDK alarm pools.
    set_source_files_properties(${zf_pico_rand}/rand.c TARGET_DIRECTORY app PROPERTIES
      COMPILE_OPTIONS "-D__uninitialized_ram(name)=__attribute__((section(\".noinit\"))) name;-Dsleep_until=busy_wait_until")
    zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/random/random.h)
  else()
    set(zf_board_caps 0)
    set(zf_radio_mask 0u)
  endif()
  target_compile_definitions(app PRIVATE ZF_MCU=${zf_mcu} ZF_BOARD_MODEL=${zf_board} ZF_BOARD_CAPS=${zf_board_caps}
                                         ZF_RADIO_MASK=${zf_radio_mask})
  list(APPEND zf_board_sources src/platform/rpi_pico/platform.c)
  if(ZF_ENABLE_STORAGE AND CONFIG_ZF_SDHC_RPI_PICO_PIO)
    list(APPEND zf_board_sources src/platform/rpi_pico/sdhc_pio.c src/platform/rpi_pico/sdio_codec.c)
  endif()
else()
  if(CONFIG_SOC_ESP32S3)
    set(zf_mcu ZF_MCU_ESP32S3)
    set(zf_board ZF_BOARD_DEVKITC)
    set(zf_board_caps ZF_BOARD_CAPS_ESP32S3)
    set(zf_radio_mask 3u)
  elseif(CONFIG_SOC_ESP32C6)
    set(zf_mcu ZF_MCU_ESP32C6)
    set(zf_board ZF_BOARD_ESP32C6_DEVKITC)
    set(zf_board_caps ZF_BOARD_CAPS_ESP32C6)
    set(zf_radio_mask 3u)
    set(zf_sysclk_max_profile 2u)
  else()
    message(FATAL_ERROR "Unsupported Espressif SoC")
  endif()
  set(zf_clock_source src/platform/system_clock.c)
  set(zf_led_source src/platform/led.c)
  set(zf_board_sources src/app/wireless/bluetooth/ble.c src/app/wireless/wifi/wifi.c)
  target_compile_definitions(app PRIVATE ZF_MCU=${zf_mcu} ZF_BOARD_MODEL=${zf_board}
    ZF_BOARD_CAPS=${zf_board_caps} ZF_RADIO_MASK=${zf_radio_mask})
  if(DEFINED zf_sysclk_max_profile)
    target_compile_definitions(app PRIVATE ZF_SYSCLK_MAX_PROFILE=${zf_sysclk_max_profile})
  endif()
endif()
if(ZF_ENABLE_STORAGE)
  set(zf_storage_sources src/app/storage/storage.c src/platform/storage.c)
else()
  set(zf_storage_sources src/platform/storage_none.c)
endif()
target_sources(app PRIVATE
  src/main.c
  src/config/board.c
  src/platform/boot_config.c
  src/platform/boot_flash.c
  ${zf_clock_source}
  src/app/telemetry.c
  src/app/control/control.c
  ${zf_storage_sources}
  ${zf_led_source}
  src/app/usb/hid_reports.c
  src/app/usb/usb_hid.c
  src/app/wireless/sha256.c
  src/app/wireless/wireless_auth.c
  src/app/wireless/wireless_stage.c
  ${zf_board_sources})
target_include_directories(app PRIVATE src)
# The pinned ESP32-S3 HAL leaves the PHY library's weak sensor-power hooks linked. They power the die-temperature
# sensor off during Wi-Fi modem sleep, and the next telemetry read then spins with interrupts locked. The HAL
# override reference-counts that power instead.
if(CONFIG_ESP32_TEMP AND CONFIG_SOC_ESP32S3)
  target_sources(app PRIVATE ${ZEPHYR_HAL_ESPRESSIF_MODULE_DIR}/components/esp_phy/src/phy_override.c)
endif()
if(DEFINED ENV{ZEPHYRFORGE_VERSION} AND NOT "$ENV{ZEPHYRFORGE_VERSION}" STREQUAL "")
  if(NOT "$ENV{ZEPHYRFORGE_VERSION}" MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "ZEPHYRFORGE_VERSION must be MAJOR.MINOR.PATCH with no leading zeros")
  endif()
  target_compile_definitions(app PRIVATE "ZEPHYRFORGE_VERSION=\"$ENV{ZEPHYRFORGE_VERSION}\"")
endif()

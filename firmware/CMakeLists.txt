cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyrforge)
target_compile_definitions(app PRIVATE ZF_STORAGE_SPI=$<BOOL:${CONFIG_SPI_SDHC}>)
option(ZF_ENABLE_STORAGE_BENCHMARK "Include temporary-file verification and throughput measurement" OFF)
target_compile_definitions(app PRIVATE ZF_ENABLE_STORAGE_BENCHMARK=$<BOOL:${ZF_ENABLE_STORAGE_BENCHMARK}>)
if(ZF_ENABLE_STORAGE_BENCHMARK)
  target_sources(app PRIVATE src/app/storage/benchmark.c)
endif()
if(CONFIG_SOC_FAMILY_RPI_PICO)
  # Report 1 identity: MCU 1 RP2040 or 2 RP2350; console UART0 is on GP0/GP1.
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
  target_compile_definitions(app PRIVATE ZF_MCU=${zf_mcu} ZF_BOARD_MODEL=${zf_board} ZF_BOARD_CAPS=0
                                         ZF_MONITOR_UART_TX=0u ZF_MONITOR_UART_RX=1u)
  set(zf_board_sources src/platform/rpi_pico/platform.c)
  if(CONFIG_ZF_SDHC_RPI_PICO_PIO)
    list(APPEND zf_board_sources src/platform/rpi_pico/sdhc_pio.c src/platform/rpi_pico/sdio_codec.c)
  endif()
else()
  set(zf_clock_source src/platform/system_clock.c)
  set(zf_led_source src/platform/led.c)
  set(zf_board_sources src/app/wireless/bluetooth/ble.c src/app/wireless/wifi/wifi.c)
endif()
target_sources(app PRIVATE
  src/main.c
  src/config/board.c
  src/platform/boot_config.c
  src/platform/boot_flash.c
  ${zf_clock_source}
  src/app/telemetry.c
  src/app/control/control.c
  src/app/storage/storage.c
  src/platform/storage.c
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
if(CONFIG_ESP32_TEMP)
  target_sources(app PRIVATE ${ZEPHYR_HAL_ESPRESSIF_MODULE_DIR}/components/esp_phy/src/phy_override.c)
endif()
if(DEFINED ENV{ZEPHYRFORGE_VERSION} AND NOT "$ENV{ZEPHYRFORGE_VERSION}" STREQUAL "")
  if(NOT "$ENV{ZEPHYRFORGE_VERSION}" MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "ZEPHYRFORGE_VERSION must be MAJOR.MINOR.PATCH with no leading zeros")
  endif()
  target_compile_definitions(app PRIVATE "ZEPHYRFORGE_VERSION=\"$ENV{ZEPHYRFORGE_VERSION}\"")
endif()

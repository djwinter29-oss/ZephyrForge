cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_esp32_lifecycle)
set(ZF_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../)
target_sources(app PRIVATE main.c ${ZF_ROOT}/src/app/wireless/wifi/wifi.c
  ${ZF_ROOT}/src/app/wireless/wifi/wifi_driver.c ${ZF_ROOT}/src/app/wireless/wifi/wifi_policy.c
  ${ZF_ROOT}/src/app/protocol/hid_reports.c)
target_include_directories(app PRIVATE ${ZF_ROOT}/src)
target_compile_definitions(app PRIVATE net_if_get_first_wifi=zf_test_wifi_iface)

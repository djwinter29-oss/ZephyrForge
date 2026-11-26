cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_recovery)
set(ZF_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../)
target_sources(app PRIVATE main.c ${ZF_ROOT}/src/wireless/wifi/wifi.c
  ${ZF_ROOT}/src/wireless/wifi/wifi_driver.c ${ZF_ROOT}/src/wireless/wifi/wifi_policy.c
  ${ZF_ROOT}/src/report/report.c ${ZF_ROOT}/src/wireless/wireless.c
  ${ZF_ROOT}/src/wireless/wifi/wifi_scan_table.c)
target_include_directories(app PRIVATE ${ZF_ROOT}/src)
target_compile_definitions(app PRIVATE net_if_get_first_wifi=zf_test_wifi_iface ZF_WIFI_TEST_AIROC_IF_DOWN=1)

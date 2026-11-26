cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_worker)
target_sources(app PRIVATE main.c ../../../src/wifi/wifi_station.c
  ../../../src/wifi/wifi_driver.c ../../../src/wifi/wifi_policy.c
  ../../../src/protocol/report_codec.c
  ../../../src/wifi/wifi_scan_table.c)
target_include_directories(app PRIVATE ../../../src)
target_compile_definitions(app PRIVATE CONFIG_WIFI_AIROC=1 CONFIG_ZF_WIFI=1 CONFIG_ZF_BLUETOOTH=1
  net_if_get_first_wifi=zf_test_wifi_iface)

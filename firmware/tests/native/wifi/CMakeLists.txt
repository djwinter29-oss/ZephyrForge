cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_worker)
target_sources(app PRIVATE main.c ../../../src/wireless/wifi/wifi.c
  ../../../src/wireless/wifi/wifi_driver.c ../../../src/wireless/wifi/wifi_policy.c
  ../../../src/report/report.c
  ../../../src/wireless/wireless.c
  ../../../src/wireless/wifi/wifi_scan_table.c)
target_include_directories(app PRIVATE ../../../src)
target_compile_definitions(app PRIVATE CONFIG_WIFI_AIROC=1 net_if_get_first_wifi=zf_test_wifi_iface)

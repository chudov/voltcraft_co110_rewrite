# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Out-of-tree board definition lives next to the application
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(co110)

target_sources(app PRIVATE src/lcd.c src/display.c)
target_include_directories(app PRIVATE src)

if(CONFIG_APP_BITMAP_TEST)
  target_sources(app PRIVATE src/bitmap_test.c)
elseif(CONFIG_APP_DEMO)
  target_sources(app PRIVATE src/demo.c)
else()
  target_sources(app PRIVATE src/live.c src/ui.c src/settings.c src/alarm.c src/leds.c src/sht30.c src/cm1106.c)
endif()

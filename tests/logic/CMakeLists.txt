# SPDX-License-Identifier: Apache-2.0
# Unit tests of the hardware independent logic (alarm zones, UI state machine, start-up sequence).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(co110_logic_tests)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_sources(app PRIVATE
  src/main.c
  src/stubs.c
  ${SRC}/alarm.c
  ${SRC}/ui.c
  ${SRC}/startup.c)
target_include_directories(app PRIVATE ${SRC})

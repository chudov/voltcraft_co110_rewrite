# SPDX-License-Identifier: Apache-2.0
# Unit tests of the flash ring of the chart history (src/history.c) with a RAM storage.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(co110_history_tests)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_sources(app PRIVATE src/main.c ${SRC}/history.c)
target_include_directories(app PRIVATE ${SRC})

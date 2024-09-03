# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# SET(BOARD esp32_devkitc_wrover/esp32/procpu)
SET(BOARD esp32_devkitc_wroom/esp32/procpu)
# SET(BOARD esp32s2_devkitc)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mqtt_test)

target_sources(app PRIVATE src/main.c)

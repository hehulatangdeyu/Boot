# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bootloader)

target_sources(app PRIVATE
    src/app/boot.c
    src/app/main.c
    src/app/work_queue.c
    src/app/hpatchlite.c
)

target_sources(app PRIVATE
    src/driver/bl_button.c
    src/driver/bl_led.c
    src/driver/bl_uart.c
)

target_sources(app PRIVATE
    src/flash/flash_area.c
    src/flash/norflash.c
)

target_sources(app PRIVATE
    src/HPatchLite/hpatch_lite.c    
)

target_sources(app PRIVATE
    src/thirdlib/tinyuz/tuz_dec.c
)

# 使用 Zephyr 的方式添加包含目录
zephyr_include_directories(src)
zephyr_include_directories(src/app)
zephyr_include_directories(src/driver)
zephyr_include_directories(src/flash)
zephyr_include_directories(src/HPatchLite)
zephyr_include_directories(src/thirdlib/tinyuz)

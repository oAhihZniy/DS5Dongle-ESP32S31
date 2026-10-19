cmake_minimum_required(VERSION 3.16)

add_compile_definitions(CFG_TUSB_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/main/s31_tusb_config.h" ENABLE_WAKE_HID ENABLE_SERIAL=0)
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ds5_dongle_s31 VERSION 0.2.0)

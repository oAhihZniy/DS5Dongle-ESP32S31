set(upstream upstream/audio.cpp upstream/usb.cpp upstream/usb_descriptors.cpp
    upstream/state_mgr.cpp upstream/cmd.cpp upstream/dse.cpp
    upstream/wake.cpp upstream/ps_shortcut.cpp)
idf_component_register(
    WHOLE_ARCHIVE
    SRCS "app_s31.cpp" "config_s31.cpp" "usb_speed_s31.cpp" "ds5_bridge.c" "ds5_display.c" ${upstream}
         "../../lib/WDL/WDL/resample.cpp"
    INCLUDE_DIRS "." "upstream" "../../lib/WDL/WDL"
    REQUIRES bt esp_driver_gpio esp_driver_spi efuse esp_lcd_co5300 tinyusb nvs_flash esp_timer esp_hw_support esp_system)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory("../../lib/opus" "${CMAKE_CURRENT_BINARY_DIR}/opus")
target_link_libraries(${COMPONENT_LIB} PRIVATE opus)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -fsigned-char -O2 -Wno-error=missing-field-initializers)
target_compile_options(opus PRIVATE -O2)
target_compile_definitions(${COMPONENT_LIB} PRIVATE WDL_RESAMPLE_TYPE=float
    DISABLE_SPEAKER_PROC=0 ENABLE_VERBOSE=0 PICO_PROGRAM_VERSION_STRING="0.7.2-s31.2")

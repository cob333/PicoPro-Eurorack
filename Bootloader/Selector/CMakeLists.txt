cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_BOARD)
  set(PICO_BOARD pico2 CACHE STRING "Pico SDK board target")
endif()

if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Pico SDK path from environment")
endif()

if(NOT DEFINED PICO_SDK_PATH)
  set(_ARDUINO_PICO_SDK "$ENV{HOME}/Library/Arduino15/packages/rp2040/hardware/rp2040/5.5.0/pico-sdk")
  if(EXISTS "${_ARDUINO_PICO_SDK}/external/pico_sdk_import.cmake")
    set(PICO_SDK_PATH "${_ARDUINO_PICO_SDK}" CACHE PATH "Local Pico SDK path")
  endif()
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
  message(FATAL_ERROR "PICO_SDK_PATH must point to a Pico SDK directory")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picopro_selector C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(picopro_selector
  ${CMAKE_CURRENT_LIST_DIR}/../boot_config.c
  selector.c
)
set_target_properties(picopro_selector PROPERTIES OUTPUT_NAME PicoPro_Firmware)

target_include_directories(picopro_selector PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/..
)

if(PICO_PLATFORM MATCHES "rp2350")
  target_compile_definitions(picopro_selector PRIVATE PICO_BOOT_TARGET_RP2350=1)
else()
  target_compile_definitions(picopro_selector PRIVATE PICO_BOOT_TARGET_RP2040=1)
endif()

target_link_libraries(picopro_selector PRIVATE
  pico_stdlib
  hardware_clocks
  hardware_flash
  hardware_gpio
  hardware_i2c
  hardware_sync
)

pico_enable_stdio_usb(picopro_selector 0)
pico_enable_stdio_uart(picopro_selector 0)
pico_add_extra_outputs(picopro_selector)

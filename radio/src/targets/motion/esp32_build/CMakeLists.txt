# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

add_definitions(-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_conf_edgetx.h")

set(EDGETX_ROOTDIR "${CMAKE_SOURCE_DIR}/../../../../..")
set(EDGETX_SRCDIR "${EDGETX_ROOTDIR}/radio/src")
set(EDGETX_TGT_CMN "${EDGETX_SRCDIR}/targets/common/esp32")

list(APPEND EXTRA_COMPONENT_DIRS "${EDGETX_SRCDIR}/thirdparty/libopenui/thirdparty/lvgl")
list(APPEND EXTRA_COMPONENT_DIRS "${EDGETX_TGT_CMN}/components/nimble_central_utils")
list(APPEND EXTRA_COMPONENT_DIRS "${EDGETX_TGT_CMN}/components/lvgl_esp32_drivers")
add_definitions(-I${CMAKE_SOURCE_DIR} -I${EDGETX_SRCDIR})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PCB "MUFFIN" CACHE STRING "Project Muffin")
option(BOOTLOADER "Include Bootloader" OFF)

project(edgetx)

target_link_libraries(${project_elf} PRIVATE radiolib_esp32 board_lib)

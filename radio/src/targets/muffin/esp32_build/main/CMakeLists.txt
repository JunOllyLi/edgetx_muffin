set(ESP_TINYUSB_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_tinyusb)
if(NOT CMAKE_SCRIPT_MODE_FILE AND EXISTS ${ESP_TINYUSB_COMPONENT_DIR}/tinyusb_msc.c)
    set_source_files_properties(${ESP_TINYUSB_COMPONENT_DIR}/tinyusb_msc.c
        PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

idf_component_register(SRCS "espidf_edgetx_main.cpp"
                    INCLUDE_DIRS ""
                    REQUIRES lvgl_esp32_drivers esp_tinyusb
                    LDFRAGMENTS "linker_fragments/esp32_edgetx.lf"
                )

set(PCB "MUFFIN" CACHE STRING "Project Muffin")
set(CMAKE_BUILD_TYPE Release)
option(EdgeTX_SUPERBUILD "Cross-compile EdgeTX for each toolchain" OFF)
set(DEBUG NO)
option(BOOTLOADER "Include Bootloader" OFF)

add_subdirectory(${EDGETX_ROOTDIR} edgetx)

idf_component_get_property(lvgl_drv_component lvgl_esp32_drivers COMPONENT_LIB)
target_include_directories(${lvgl_drv_component} PRIVATE
        ../../
        ${EDGETX_SRCDIR}/gui/colorlcd/
        ${EDGETX_SRCDIR}/thirdparty/lvgl
    )

set(DRIVER_LIST
        idf::esp_adc idf::esp_driver_uart idf::esp_driver_gptimer idf::esp_driver_rmt
        idf::esp_driver_sdmmc idf::esp_driver_i2s idf::esp_ringbuf idf::esp_driver_sdspi
        idf::nimble_central_utils idf::lvgl_esp32_drivers idf::bt idf::esp_timer idf::esp_wifi
        idf::driver idf::nvs_flash idf::app_update idf::sdmmc idf::fatfs idf::vfs
        idf::espressif__esp_tinyusb
        idf::esp_driver_usb_serial_jtag
)

set(ETC_TARGETS radiolib_esp32 board_lib board)
foreach(tgt IN LISTS ETC_TARGETS)
    target_link_libraries(${tgt} stdc++ ${DRIVER_LIST})
    target_include_directories(${tgt} PRIVATE
        ${EDGETX_SRCDIR}
        ${EDGETX_TGT_CMN}
        ${EDGETX_SRCDIR}/gui/colorlcd/
        ${EDGETX_SRCDIR}/thirdparty/lvgl
    )
    target_compile_options(${tgt} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
    target_compile_options(${tgt} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion> -Wno-error=overloaded-virtual= -Wno-error=address-of-packed-member)
    target_compile_options(${tgt} PRIVATE -Wno-ignored-qualifiers -Wno-error=dangling-pointer= -Wno-error=format -Wno-format -Wno-missing-field-initializers -Wno-extra)
endforeach()

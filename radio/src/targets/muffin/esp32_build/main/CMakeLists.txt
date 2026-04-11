idf_component_register(SRCS "espidf_edgetx_main.cpp"
                    INCLUDE_DIRS ""
                    REQUIRES lvgl_esp32_drivers
                    LDFRAGMENTS "linker_fragments/esp32_psram.lf"
                )

set(DISABLE_COMPANION YES)
set(GVARS YES)
set(LUA YES)
#set(DEBUG YES)
set(CMAKE_BUILD_TYPE Release)

option(EdgeTX_SUPERBUILD "Cross-compile EdgeTX for each toolchain" OFF)

add_subdirectory(${EDGETX_ROOTDIR} edgetx)

idf_component_get_property(lvgl_drv_component lvgl_esp32_drivers COMPONENT_LIB)

target_include_directories(${lvgl_drv_component} PRIVATE
    ../../
    ${EDGETX_SRCDIR}/gui/colorlcd/
    ${EDGETX_SRCDIR}/thirdparty/lvgl
    )

# TODO-MUFFIN
target_link_libraries(radiolib_esp32 stdc++)
target_link_libraries(radiolib_esp32 idf::esp_adc idf::nimble_central_utils idf::lvgl_esp32_drivers idf::bt idf::esp_timer idf::esp_wifi idf::driver idf::nvs_flash idf::app_update idf::sdmmc)
target_link_libraries(board_lib stdc++)
target_link_libraries(board_lib idf::esp_adc idf::esp_driver_uart idf::esp_driver_gptimer idf::esp_driver_rmt idf::esp_driver_sdmmc idf::esp_driver_i2s idf::esp_ringbuf idf::esp_driver_sdspi idf::nimble_central_utils idf::lvgl_esp32_drivers idf::bt idf::esp_timer idf::esp_wifi idf::driver idf::nvs_flash idf::app_update idf::sdmmc)
target_link_libraries(board stdc++)
target_link_libraries(board idf::esp_adc idf::esp_driver_rmt idf::esp_driver_uart idf::nimble_central_utils idf::lvgl_esp32_drivers idf::bt idf::esp_timer idf::esp_wifi idf::driver idf::nvs_flash idf::app_update idf::sdmmc)

target_include_directories(radiolib_esp32 PRIVATE 
    ${EDGETX_TGT_CMN}
    ${EDGETX_ROOTDIR}/src/gui/colorlcd/
    ${EDGETX_SRCDIR}/thirdparty/lvgl
)
target_include_directories(board PRIVATE
    ../../
    ${EDGETX_SRCDIR}/gui/colorlcd/
    ${EDGETX_SRCDIR}/thirdparty/lvgl
    )

target_compile_options(radiolib_esp32 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_options(radiolib_esp32 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion> -Wno-error=overloaded-virtual= -Wno-error=address-of-packed-member)
target_compile_options(radiolib_esp32 PRIVATE -Wno-error=strict-aliasing -Wno-ignored-qualifiers -Wno-error=dangling-pointer= -Wno-error=format -Wno-format -Wno-missing-field-initializers -Wno-extra)

target_compile_options(board PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_options(board PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
target_compile_options(board PRIVATE -Wno-ignored-qualifiers -Wno-error=dangling-pointer= -Wno-error=format -Wno-format -Wno-missing-field-initializers -Wno-extra)

target_compile_options(board_lib PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_options(board_lib PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
target_compile_options(board_lib PRIVATE -Wno-ignored-qualifiers -Wno-error=dangling-pointer= -Wno-error=format -Wno-format -Wno-missing-field-initializers -Wno-extra)

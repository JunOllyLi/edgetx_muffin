idf_component_register(SRCS "espidf_edgetx_main.cpp"
                    INCLUDE_DIRS ""
                    REQUIRES lvgl lvgl_esp32_drivers esp_jpeg
                )

set(DISABLE_COMPANION YES)
set(GVARS YES)
set(LUA YES)
set(DEBUG YES)
set(CMAKE_BUILD_TYPE Release)

option(EdgeTX_SUPERBUILD "Cross-compile EdgeTX for each toolchain" OFF)

add_subdirectory(${EDGETX_ROOTDIR} edgetx)

target_link_libraries(radiolib_esp32 stdc++)
target_link_libraries(radiolib_esp32 idf::esp_adc idf::nimble_central_utils idf::lvgl_esp32_drivers idf::lvgl idf::bt idf::esp_timer idf::esp_wifi idf::driver idf::nvs_flash idf::app_update idf::sdmmc idf::usb)

target_compile_options(radiolib_esp32 PRIVATE -I${EDGETX_TGT_CMN} -I../managed_components/espressif__esp_jpeg/include -I../managed_components/espressif__usb_host_uvc/include)
target_compile_options(radiolib_esp32 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_options(radiolib_esp32 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
target_compile_options(radiolib_esp32 PRIVATE -Wno-ignored-qualifiers -Wno-error=dangling-pointer= -Wno-error=format -Wno-format -Wno-missing-field-initializers -Wno-extra)


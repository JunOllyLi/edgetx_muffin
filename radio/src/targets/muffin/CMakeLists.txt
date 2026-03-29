
add_definitions(-DPCB_MUFFIN)

option(SHUTDOWN_CONFIRMATION "Shutdown confirmation" OFF)
option(AUTOUPDATE "Auto update internal chips from SD" OFF)

set(PWR_BUTTON "SWITCH" CACHE STRING "Pwr button type (PRESS/SWITCH)")

set(GUI_DIR colorlcd)
set(BITMAPS_DIR 480x272)
add_definitions(-DCOLORLCD -DLIBOPENUI -DHARDWARE_TOUCH -DHARDWARE_KEYS)
add_definitions(-DBATTGRAPH -DTHRTRACE)
set(BITMAPS_TARGET muffin_bitmaps)
set(STORAGE_MODELSLIST YES)

set(STATUS_LEDS NO)

if(DEBUG)
add_definitions(-DDEBUG)
endif()

set(FLAVOUR muffin)
set(NAVIGATION_TYPE 9x)  # TODO-MUFFIN
set(ROTARY_ENCODER NO)
set(LUA_EXPORT lua_export_muffin)
set(BLUETOOTH NO)  # TODO-MUFFIN
set(USB_CHARGER NO) # SW does not control the USB charger

set(SDCARD YES)
set(EEPROM EEPROM_RAW)
set(TARGET_DIR muffin)
option(PPM_LIMITS_SYMETRICAL "PPM limits symetrical" ON)

add_definitions(-DREQUIRED_SDCARD_VERSION="${SDCARD_VERSION}")
add_definitions(-DLUA_INPUTS -DTIMERS -DSOFTWARE_VOLUME)
add_definitions(-DPWR_BUTTON_${PWR_BUTTON})

option(INTERNAL_MODULE_MULTI "Support for MULTI internal module" ON)
option(INTERNAL_MODULE_ELRS "Support for CRSF internal module" ON)
option(INTERNAL_MODULE_PPM "Support for PPM internal module" OFF)
option(INTERNAL_MODULE_ESPNOW "Support for ESPNOW internal module" OFF)
option(INTERNAL_MODULE_BT_POWERUP "Support for Bluetooth internal module for POWERUP" OFF)
option(EXTERNAL_MODULE_MULTI "Support for MULTI external module" ON)
option(EXTERNAL_MODULE_CRSF "Support for CRSF external module" OFF)
option(EXTERNAL_MODULE_PPM "Support for PPM external module" OFF)
option(EXTERNAL_MODULE_ESPNOW "Support for ESPNOW external module" ON)
option(EXTERNAL_MODULE_BT_POWERUP "Support for Bluetooth internal module for POWERUP" ON)

include(${RADIO_SRC_DIR}/targets/common/esp32/CMakeLists.txt)

set(TARGET_SRC
  ${TARGET_SRC}
  sh1107_top_lcd.cpp
  trainer_driver.cpp
  lcd_lvgl_esp.cpp
  backlight_driver.cpp
  flyskyHallStick_driver.cpp
  ads1015_adc.cpp
  switch_driver.cpp
  module_drivers.cpp
  mcp23017_io_driver.cpp
  )

set(FIRMWARE_TARGET_SRC
  ${FIRMWARE_TARGET_SRC}
  board.cpp

  pwr_driver.cpp
  rtc_driver.cpp
  )

set(RADIO_DEPENDENCIES ${RADIO_DEPENDENCIES} ${BITMAPS_TARGET})

foreach(LANGUAGE ${TTS_LANGUAGES})
  string(TOLOWER ${LANGUAGE} lang_lower)
  set(SRC ${SRC} translations/tts_${lang_lower}.cpp)
endforeach()

if (MULTIMODULE)
  add_definitions(-DMULTI_PROTOLIST)
  set(SRC ${SRC}
      io/multi_protolist.cpp
  )
include_directories(${RADIO_SRC_DIR}/fonts/colorlcd gui/${GUI_DIR} gui/${GUI_DIR}/layouts)

file(GLOB THEMES_SRC RELATIVE ${RADIO_SRC_DIR}/gui/colorlcd ${RADIO_SRC_DIR}/gui/colorlcd/themes/*.cpp)
file(GLOB LAYOUTS_SRC RELATIVE ${RADIO_SRC_DIR}/gui/colorlcd ${RADIO_SRC_DIR}/gui/colorlcd/layouts/*.cpp)
file(GLOB WIDGETS_SRC RELATIVE ${RADIO_SRC_DIR}/gui/colorlcd ${RADIO_SRC_DIR}/gui/colorlcd/widgets/*.cpp)
endif()

set(HW_DESC_JSON ${FLAVOUR}.json)
AddHWGenTarget(${HW_DESC_JSON} mcp23017_keys mcp23017_keys.inc)
AddHWGenTarget(${HW_DESC_JSON} mcp23017_switches mcp23017_switches.inc)
AddHWGenTarget(${HW_DESC_JSON} hal_adc_inputs hal_adc_inputs.inc)
AddHWGenTarget(${HW_DESC_JSON} ads1015_adc_inputs ads1015_adc_inputs.inc)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(board_lib OBJECT EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_BINARY_DIR}/mcp23017_keys.inc
  ${CMAKE_CURRENT_BINARY_DIR}/mcp23017_switches.inc
  ${CMAKE_CURRENT_BINARY_DIR}/hal_adc_inputs.inc
  ${CMAKE_CURRENT_BINARY_DIR}/ads1015_adc_inputs.inc
  ${CMAKE_CURRENT_BINARY_DIR}/${HW_DESC_JSON}
  )


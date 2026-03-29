
add_definitions(-DAUDIO -DVOICE -DRTCLOCK)
add_definitions(-DSBUS_TRAINER)

add_definitions(-DSTORAGE_USE_SDCARD_SPI)

# TARGET_SRC relative dir TODO-MUFFIN
set(ESP_CMN_FOLDER ../common/esp32)

set(CPU_FAMILY ESP32)

if(INTERNAL_MODULE_ELRS OR EXTERNAL_MODULE_ELRS)
  set(CROSSFIRE ON)
  add_definitions(-DCROSSFIRE)
  if(EXTERNAL_MODULE_ELRS)
    add_definitions(-DHARDWARE_EXTERNAL_MODULE)
  endif()
  if(INTERNAL_MODULE_ELRS)
    add_definitions(-DHARDWARE_INTERNAL_MODULE)
    add_definitions(-DINTERNAL_MODULE_CRSF)
  endif()
endif()

if(INTERNAL_MODULE_PPM)
  add_definitions(-DHARDWARE_INTERNAL_MODULE)
  add_definitions(-DINTERNAL_MODULE_PPM)
endif()

if(INTERNAL_MODULE_BT_POWERUP OR EXTERNAL_MODULE_BT_POWERUP)
  if(EXTERNAL_MODULE_BT_POWERUP)
    add_definitions(-DHARDWARE_EXTERNAL_MODULE)
  endif()
  if(INTERNAL_MODULE_BT_POWERUP)
    add_definitions(-DHARDWARE_INTERNAL_MODULE)
    add_definitions(-DINTERNAL_MODULE_BT_POWERUP)
  endif()

  add_definitions(-DBT_POWERUP)
  set(TARGET_SRC
    ${TARGET_SRC}
    ${ESP_CMN_FOLDER}/esp_ble_powerup.c
    ${ESP_CMN_FOLDER}/pulses_bt_powerup.cpp
    )
endif()

if(BT_POWERUP)
endif()

if(INTERNAL_MODULE_ESPNOW OR EXTERNAL_MODULE_ESPNOW)
  if(EXTERNAL_MODULE_ESPNOW)
    add_definitions(-DHARDWARE_EXTERNAL_MODULE)
  endif()
  if(INTERNAL_MODULE_ESPNOW)
    add_definitions(-DHARDWARE_INTERNAL_MODULE)
    add_definitions(-DINTERNAL_MODULE_ESPNOW)
  endif()

  add_definitions(-DESPNOW)
  set(TARGET_SRC
    ${TARGET_SRC}
    ${ESP_CMN_FOLDER}/pulses_espnow.cpp
    )
endif()

if(INTERNAL_MODULE_MULTI OR EXTERNAL_MODULE_MULTI)
  if(EXTERNAL_MODULE_MULTI)
    add_definitions(-DHARDWARE_EXTERNAL_MODULE)
  endif()
  if(INTERNAL_MODULE_MULTI)
    add_definitions(-DHARDWARE_INTERNAL_MODULE)
    add_definitions(-DINTERNAL_MODULE_MULTI)
  endif()

  add_definitions(-DUSE_MULTI_RX)
  option(DSM2 "DSM2 TX Module" ON)
  add_definitions(-DDSM2)
  option(MULTIMODULE "DIY Multiprotocol TX Module (https://github.com/pascallanger/DIY-Multiprotocol-TX-Module)" ON)

  add_definitions(-DMULTIMODULE)
  set(SRC ${SRC} pulses/multi.cpp telemetry/spektrum.cpp telemetry/hitec.cpp telemetry/hott.cpp telemetry/mlink.cpp telemetry/multi.cpp io/multi_firmware_update.cpp)
endif()

set(SRC
  ${SRC}
  main.cpp
  tasks.cpp
  audio.cpp
  haptic.cpp
  dump.cpp
  pulses/pulses.cpp
  pulses/crossfire.cpp
  telemetry/telemetry.cpp
  telemetry/telemetry_sensors.cpp
  telemetry/crossfire.cpp
  telemetry/frsky.cpp
  telemetry/frsky_d.cpp
  telemetry/frsky_sport.cpp
  telemetry/frsky_pxx2.cpp
  telemetry/flysky_ibus.cpp
  crc.cpp
  vario.cpp
  )

set(SRC
  ${SRC}
  io/frsky_firmware_update.cpp
  io/multi_firmware_update.cpp
  sbus.cpp
  )

set(FIRMWARE_SRC
  ${FIRMWARE_SRC}
  syscalls.c
  hal/adc_driver.cpp
  serial.cpp
  )

set(PULSES_SRC
  ${PULSES_SRC}
  pulses.cpp
  ppm.cpp
  modules_helpers.cpp
  )

if(DSM2)
  set(PULSES_SRC
    ${PULSES_SRC}
    dsm2.cpp
    )
endif()

if(SBUS)
  set(PULSES_SRC
    ${PULSES_SRC}
    sbus.cpp
    )
endif()

if(BLUETOOTH)
  add_definitions(-DBLUETOOTH)
  set(TARGET_SRC
    ${TARGET_SRC}
    #../common/arm/stm32/bluetooth_driver.cpp
    )
  set(SRC
    ${SRC}
    bluetooth.cpp
    )
endif()

set(TARGET_SRC
  ${TARGET_SRC}
  ${ESP_CMN_FOLDER}/diskio_spi.cpp
  ${ESP_CMN_FOLDER}/sound_i2s.cpp
  ${ESP_CMN_FOLDER}/abnormal_reboot.cpp
  ${ESP_CMN_FOLDER}/watchdog_driver.cpp
  ${ESP_CMN_FOLDER}/usb_driver.cpp
  ${ESP_CMN_FOLDER}/i2c_driver.cpp
  ${ESP_CMN_FOLDER}/timers_driver.cpp
  ${ESP_CMN_FOLDER}/esp32_uart_driver.cpp
  ${ESP_CMN_FOLDER}/mixer_scheduler_driver.cpp

  ${ESP_CMN_FOLDER}/esp32_rmt_rx.cpp
  ${ESP_CMN_FOLDER}/esp32_rmt_uart.cpp
  ${ESP_CMN_FOLDER}/rmt_uart_tx_encoder.cpp

  ${ESP_CMN_FOLDER}/ftp-server/WiFi.c
  ${ESP_CMN_FOLDER}/ftp-server/initWiFi.cpp
  ${ESP_CMN_FOLDER}/ftp-server/ftp.c
  ${ESP_CMN_FOLDER}/ota_server/ota_server.c
)
include_directories(
    ${RADIO_SRC_DIR}/targets/common/esp32/ftp-server
    gui/${GUI_DIR}
    ${RADIO_SRC_DIR}/targets/common/esp32/ota_server/include
)

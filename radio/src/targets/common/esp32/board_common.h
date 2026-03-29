
#ifndef ESP32_COMMON_H_
#define ESP32_COMMON_H_

#if defined(ESP_PLATFORM)
#include <rtos.h>
#else
// for YAML generation
#endif

#include <inttypes.h>
#include "definitions.h"
#include "edgetx_constants.h"
#include "hal.h"
#include "hal/serial_port.h"
#include "hal/watchdog_driver.h"
#include "esp_log.h"

#define SYSTEM_TICKS_1MS pdMS_TO_TICKS(1)

void init2MhzTimer();

#define UINT16_MIN 0U

#endif // ESP32_COMMON_H

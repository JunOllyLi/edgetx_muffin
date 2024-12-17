/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
  *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"
#include "hal/adc_driver.h"
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "i2c_driver.h"
#include "ads1x15.h"
#include "hal_adc_inputs.inc"

extern i2c_master_bus_handle_t ads_i2c_bus_handle;

typedef struct {
    uint8_t ads_index;
    uint16_t mux;
    uint8_t etx_adc_channel;
} ads1015_channel_t;

#include "ads1015_adc_inputs.inc"

static TaskHandle_t s_task_handle;

static bool ads1015_hal_adc_init()
{
    return true;
}

static void startADCReading(i2c_master_dev_handle_t ads, uint16_t mux, adsGain_t gain) {
    // Start with default values
    uint16_t config =
        ADS1X15_REG_CONFIG_CQUE_1CONV |   // Set CQUE to any value other than
                                          // None so we can use it in RDY mode
        ADS1X15_REG_CONFIG_CLAT_NONLAT |  // Non-latching (default val)
        ADS1X15_REG_CONFIG_CPOL_ACTVLOW | // Alert/Rdy active low   (default val)
        ADS1X15_REG_CONFIG_CMODE_TRAD;    // Traditional comparator (default val)

    //config |= ADS1X15_REG_CONFIG_MODE_CONTIN;
    config |= ADS1X15_REG_CONFIG_MODE_SINGLE;

    // Set PGA/voltage range
    config |= gain;

    // Set data rate
    config |= RATE_ADS1015_1600SPS;

    // Set channels
    config |= mux;

    // Set 'start single-conversion' bit
    config |= ADS1X15_REG_CONFIG_OS_SINGLE;

    // Write config register to the ADC
    i2c_register_write_uint16(ads, ADS1X15_REG_POINTER_CONFIG, config);

    // Set ALERT/RDY to RDY mode.
    i2c_register_write_uint16(ads, ADS1X15_REG_POINTER_HITHRESH, 0x8000);
    i2c_register_write_uint16(ads, ADS1X15_REG_POINTER_LOWTHRESH, 0x0000);
}

static int16_t getLastConversionResults(i2c_master_dev_handle_t ads) {
    // Read the conversion results
    uint16_t res = 0;
    i2c_register_read_uint16(ads, ADS1X15_REG_POINTER_CONVERT, &res);
    res = res >> 4;
    // Shift 12-bit results right 4 bits for the ADS1015,
    // making sure we keep the sign bit intact
    if (res > 0x07FF) {
      // negative number - extend the sign to 16th bit
      res |= 0xF000;
    }
    return (int16_t)res;
}

static bool isConversionDone(i2c_master_dev_handle_t ads) {
    uint16_t result = 0;
    i2c_register_read_uint16(ads, ADS1X15_REG_POINTER_CONFIG, &result);
    return (0 != (result & 0x8000));
}

static bool ads1015_hal_adc_start_read()
{
    return true;
}

static void ads1015_hal_adc_wait_completion() {
}

static const etx_hal_adc_driver_t ads1015_hal_adc_driver = {
  .inputs = _hal_inputs,
  .default_pots_cfg = _pot_default_config,
  .init = ads1015_hal_adc_init,
  .start_conversion = ads1015_hal_adc_start_read,
  .wait_completion = ads1015_hal_adc_wait_completion,
};

static void task_adc(void * pdata) {
    s_task_handle = xTaskGetCurrentTaskHandle();
    int channel_cnt = sizeof(ads_channels)/sizeof(ads_channels[0]);
    int index = 0;
    while(1) {
        if (0xFFFF == ads_channels[index].mux) {
            // for RTC Batt, Muffin uses main batt
            setAnalogValue(ads_channels[index].etx_adc_channel, getAnalogValue(ADC_INPUT_VBAT));
        } else {
            RTOS_WAIT_MS(MS_BETWEEN_CHANNEL);
            startADCReading(ads[ads_channels[index].ads_index], ads_channels[index].mux, GAIN_TWO);
            while(!isConversionDone(ads[ads_channels[index].ads_index]));
            int16_t volt = getLastConversionResults(ads[ads_channels[index].ads_index]);
            //TRACE("=X==== %d %x => %d", index, volt, ads_channels[index].etx_adc_channel);
            setAnalogValue(ads_channels[index].etx_adc_channel, volt);
        }
        index++;
        if (index >= channel_cnt) {
            index = 0;
        }
    }
}

#define TASKADC_STACK_SIZE (1024 * 4)
#define TASKADC_PRIO 5

static RTOS_TASK_HANDLE taskIdADC;
RTOS_DEFINE_STACK(taskIdADC, taskADC_stack, TASKADC_STACK_SIZE);
void ads1015_adc_init(void) {
    for (int i = 0; i < NUM_OF_ADS; i++) {
        ESP_ERROR_CHECK(i2c_master_bus_add_device(ads_i2c_bus_handle, &i2c_dev_conf[i], &ads[i]));
    }

    adcInit(&ads1015_hal_adc_driver);

    // The stuff (POTs, VBATT) on ADS1015 are not that critical, so start a task and read it in the background
    RTOS_CREATE_TASK_EX(taskIdADC,task_adc,"ADC task",taskADC_stack,TASKADC_STACK_SIZE,TASKADC_PRIO,MIXER_TASK_CORE);
}
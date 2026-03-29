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
#include "esp_adc/adc_continuous.h"

#define AVERAGE_POINTS 80
#define ETX_SAMPLE_RATE 4000
#define ETX_READ_LEN   SOC_ADC_DIGI_DATA_BYTES_PER_CONV*AVERAGE_POINTS
#define ETX_ADC_CONV_MODE           ADC_CONV_SINGLE_UNIT_1

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define ETX_ADC_USE_OUTPUT_TYPE1    1
#define ETX_ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE1
#else
#define ETX_ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE2
#endif

static adc_channel_t channel[3] = {POT1_ADC_CHANNEL, POT2_ADC_CHANNEL, BATT_ADC_CHANNEL};

static uint32_t average[sizeof(channel)/sizeof(channel[0])] = {0};

static  adc_continuous_handle_t handle = NULL;

static TaskHandle_t s_task_handle;
static const char *TAG = "ADC";

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    //Notify that ADC continuous driver has done enough number of conversions
    vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

    return (mustYield == pdTRUE);
}

static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle)
{
    adc_continuous_handle_t handle = NULL;

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = ETX_READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = ETX_SAMPLE_RATE,
        .conv_mode = ETX_ADC_CONV_MODE,
        .format = ETX_ADC_OUTPUT_TYPE,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = channel_num;
    for (int i = 0; i < channel_num; i++) {
        uint8_t unit = ADC_UNIT_1;
        uint8_t ch = channel[i] & 0x7;
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = ch;
        adc_pattern[i].unit = unit;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

    *out_handle = handle;
}

static bool check_valid_data(const adc_digi_output_data_t *data)
{
#if ETX_ADC_USE_OUTPUT_TYPE1
    if (data->type1.channel >= SOC_ADC_CHANNEL_NUM(ADC_UNIT_1)) {
        return false;
    }
#else
    if (data->type2.channel >= SOC_ADC_CHANNEL_NUM(ADC_UNIT_1)) {
        return false;
    }
#endif

    return true;
}

static bool arduino_hal_adc_init()
{

    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &handle);

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
  return true;
}

static bool arduino_hal_adc_start_read()
{
  return true;
}

static void arduino_hal_adc_wait_completion() {
}

static const etx_hal_adc_driver_t arduino_hal_adc_driver = {
  .init = arduino_hal_adc_init,
  .start_conversion = arduino_hal_adc_start_read,
  .wait_completion = arduino_hal_adc_wait_completion
};

static void task_adc(void * pdata) {
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[ETX_READ_LEN] = {0};
    s_task_handle = xTaskGetCurrentTaskHandle();

    memset(result, 0xcc, ETX_READ_LEN);
    ESP_ERROR_CHECK(adc_continuous_start(handle));
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (1) {
            ret = adc_continuous_read(handle, result, ETX_READ_LEN, &ret_num, 0);
            if (ret == ESP_OK) {
                int index = 0;
                for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                    if (check_valid_data(p)) {
                        uint32_t data;
                        uint32_t chan;
                #if ETX_ADC_USE_OUTPUT_TYPE1
                        data = p->type1.data;
                        chan = p->type1.channel;
                        //ESP_LOGI(TAG, "Unit: %d, Channel: %d, Value: %x", 1, p->type1.channel, p->type1.data);
                #else
                        data = p->type2.data;
                        chan = p->type2.channel;
                        //ESP_LOGI(TAG, "Unit: %d,_Channel: %d, Value: %x", 1, p->type2.channel, p->type2.data);
                #endif
                        for (index = 0; index < sizeof(channel)/sizeof(channel[0]); index++) {
                          if (chan == channel[index]) {
                            average[index] = (average[index] * AVERAGE_POINTS + data) / (AVERAGE_POINTS + 1);
                            setAnalogValue(4 + index, average[index]);
                          }
                        }
                    } else {
                        ESP_LOGI(TAG, "Invalid data");
                    }
                }
            } else if (ret == ESP_ERR_TIMEOUT) {
                //We try to read `ETX_READ_LEN` until API returns timeout, which means there's no available data
                break;
            }
        }
    }
    ESP_ERROR_CHECK(adc_continuous_stop(handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(handle));
}

#define TASKADC_STACK_SIZE (1024 * 4)
#define TASKADC_PRIO 5

static RTOS_TASK_HANDLE taskIdADC;
RTOS_DEFINE_STACK(taskIdADC, taskADC_stack, TASKADC_STACK_SIZE);
void adruino_adc_init(void) {
  adcInit(&arduino_hal_adc_driver);

  // The stuff on ADC are not that critical, so start a task and read it in the background
  RTOS_CREATE_TASK_EX(taskIdADC,task_adc,"ADC task",taskADC_stack,TASKADC_STACK_SIZE,TASKADC_PRIO,MIXER_TASK_CORE);
}
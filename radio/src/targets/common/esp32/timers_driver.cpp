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
#include "driver/gptimer.h"

static gptimer_handle_t MyTim2Mhz = NULL;
static SemaphoreHandle_t sem5ms;

static bool IRAM_ATTR alarm_5ms_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    uint64_t count = 0;
    gptimer_get_raw_count(MyTim2Mhz, &count);
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = count + 10000, // period = 5ms
        .reload_count = 0,
        .flags = {
            .auto_reload_on_alarm = false,
        }
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(MyTim2Mhz, &alarm_config));

    xSemaphoreGiveFromISR(sem5ms, &high_task_awoken);
    // return whether we need to yield at the end of ISR
    return (high_task_awoken == pdTRUE);
}

static void task5ms(void * pdata) {
  static uint8_t pre_scale = 0;       // Used to get 10 Hz counter

  while (true) {
    if (xSemaphoreTake(sem5ms, portMAX_DELAY)) {
      ++pre_scale;
      per5ms();

      if (pre_scale == 2) {
        pre_scale = 0;
        per10ms();
      }
    }
  }
}

#define TIM5MS_STACK_SIZE (1024 * 3)
RTOS_TASK_HANDLE taskId5ms;
RTOS_DEFINE_STACK(taskId5ms, task5ms_stack, TIM5MS_STACK_SIZE);
#define TMR_5MS_CORE 1 // TODO

// Start TIMER at 2000000Hz
void init2MhzTimer()
{
    sem5ms = xSemaphoreCreateBinary();

    RTOS_CREATE_TASK_EX(taskId5ms,task5ms,"5ms timer",task5ms_stack,TIM5MS_STACK_SIZE,5,TMR_5MS_CORE);  // TODO-feather priority

    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 2000000, // 2MHz, 1 tick=0.5us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &MyTim2Mhz));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = alarm_5ms_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(MyTim2Mhz, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(MyTim2Mhz));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 10000, // period = 5ms
        .reload_count = 0,
        .flags = {
            .auto_reload_on_alarm = false,
        }
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(MyTim2Mhz, &alarm_config));

    ESP_ERROR_CHECK(gptimer_start(MyTim2Mhz));
}

uint16_t getTmr2MHz() {
    uint64_t count = 0;
    gptimer_get_raw_count(MyTim2Mhz, &count);
    return (uint16_t)(count & 0xFFFF);
}

tmr10ms_t get_tmr10ms() {
    uint64_t count = 0;
    gptimer_get_raw_count(MyTim2Mhz, &count);
    return (tmr10ms_t)((count / 20000) & 0xFFFFFFFF); // 2MHz => 100Hz
}

uint32_t timersGetMsTick()
{
    uint64_t count = 0;
    gptimer_get_raw_count(MyTim2Mhz, &count);
    return (uint32_t)((count / 2000) & 0xFFFFFFFF); // 2MHz => 1kHz
}

uint32_t timersGetUsTick()
{
    uint64_t count = 0;
    gptimer_get_raw_count(MyTim2Mhz, &count);
    return (uint32_t)((count / 2) & 0xFFFFFFFF); // 2MHz => 1MHz
}

#if configUSE_TICK_HOOK > 0
#include <lvgl/lvgl.h>

extern "C" void vApplicationTickHook( void )
{
  lv_tick_inc(1);
}
#endif

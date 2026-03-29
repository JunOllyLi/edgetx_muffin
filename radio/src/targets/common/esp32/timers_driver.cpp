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

#include "edgetx.h"
#include "os/task.h"
#include "os/time.h"
#include "driver/gptimer.h"
#include <lvgl/lvgl.h>

static gptimer_handle_t MyTim2Mhz = NULL;

// Start TIMER at 2000000Hz
void init2MhzTimer()
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 2000000, // 2MHz, 1 tick=0.5us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &MyTim2Mhz));
    ESP_ERROR_CHECK(gptimer_enable(MyTim2Mhz));
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
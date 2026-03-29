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
#include "mixer_scheduler.h"
#include "driver/gptimer.h"
#include "esp_log.h"
static gptimer_handle_t mixer_timer = NULL;

static bool IRAM_ATTR alarm_mixer_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    size_t usec = getMixerSchedulerPeriod();
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = usec,
        .reload_count = 0,
        .flags = {
            .auto_reload_on_alarm = true,
        }
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(mixer_timer, &alarm_config));
    mixerSchedulerISRTrigger();
    return pdFALSE;
}

// Start scheduler with default period
void mixerSchedulerStart()
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
        .flags = {
            .intr_shared = false,
        }
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &mixer_timer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = alarm_mixer_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(mixer_timer, &cbs, NULL));

    ESP_ERROR_CHECK(gptimer_enable(mixer_timer));

    size_t usec = getMixerSchedulerPeriod();
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = usec,
        .reload_count = 0,
        .flags = {
            .auto_reload_on_alarm = true,
        }
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(mixer_timer, &alarm_config));
    gptimer_start(mixer_timer);
}

void mixerSchedulerEnableTrigger()
{
}


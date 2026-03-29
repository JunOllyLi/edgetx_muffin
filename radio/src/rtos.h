/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
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

#pragma once

#ifdef __cplusplus
extern "C++" {
#endif

#if defined(SIMU)

  static inline void RTOS_START() {}

#elif defined(FREE_RTOS)

#if defined(ESP_PLATFORM)
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    #include "freertos/event_groups.h"
#else
  #include <FreeRTOS/include/FreeRTOS.h>
  #include <FreeRTOS/include/task.h>
    #include <FreeRTOS/include/timers.h>
#endif
  
  static inline void RTOS_START()
  {
#ifdef ESP_PLATFORM
    // ESP-IDF start the scheduler before starting EdgeTX
    // and is running the EdgeTX app_main() in a thread already
    // so delete this task to save resource
    vTaskDelete(NULL);
#else
    vTaskStartScheduler();
#endif
  }

#ifndef ESP_PLATFORM
    h->rtos_handle = xTaskCreateStatic(
        pxTaskCode, name, ulStackDepth, 0, uxPriority,
        puxStackBuffer, &h->task_struct);
#else
    h->rtos_handle = xTaskCreateStaticPinnedToCore(
        pxTaskCode, name, ulStackDepth, 0, uxPriority,
        puxStackBuffer, &h->task_struct, 1);
#endif
  }

static inline void _RTOS_CREATE_TASK_EX(RTOS_TASK_HANDLE *h,
                                       TaskFunction_t pxTaskCode,
                                       const char *name,
                                       StackType_t *const puxStackBuffer,
                                       const uint32_t ulStackDepth,
                                       UBaseType_t uxPriority, const BaseType_t xCoreID)
  {
#ifndef ESP_PLATFORM
#else
    h->rtos_handle = xTaskCreateStaticPinnedToCore(
        pxTaskCode, name, ulStackDepth, 0, uxPriority,
        puxStackBuffer, &h->task_struct, xCoreID);
#endif

  #define RTOS_CREATE_TASK_EX(h,task,name,stackStruct,stackSize,prio,core) \
    _RTOS_CREATE_TASK_EX(&h,task,name,stackStruct.stack,stackSize,prio,core)

#endif  // RTOS type

#ifdef __cplusplus
}
#endif

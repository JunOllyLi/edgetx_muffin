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
#endif
  
  static inline void RTOS_START() {}

// TODO-MUFFIN
#ifdef ESP_PLATFORM
typedef struct {
    TaskHandle_t rtos_handle;
    StaticTask_t task_struct;
  } RTOS_TASK_HANDLE;

#ifdef __cplusplus
  template<int SIZE>
  class TaskStack
  {
    public:
      TaskStack(RTOS_TASK_HANDLE *h) {
        this->h = h;
      }

      uint32_t size()
      {
        return SIZE * 4;
      }

      uint32_t available()
      {
        return uxTaskGetStackHighWaterMark(h->rtos_handle);
      }

      StackType_t stack[SIZE];
    protected:
      RTOS_TASK_HANDLE *h;
  };
#endif // __cplusplus
  #define RTOS_DEFINE_STACK(taskHandle, name, size) TaskStack<size> __ALIGNED(8) name __CCMRAM (&taskHandle) 

static inline void _RTOS_CREATE_TASK_EX(RTOS_TASK_HANDLE *h,
                                       TaskFunction_t pxTaskCode,
                                       const char *name,
                                       StackType_t *const puxStackBuffer,
                                       const uint32_t ulStackDepth,
                                       UBaseType_t uxPriority, const BaseType_t xCoreID)
  {
    h->rtos_handle = xTaskCreateStaticPinnedToCore(
        pxTaskCode, name, ulStackDepth, 0, uxPriority,
        puxStackBuffer, &h->task_struct, xCoreID);
  }
#endif

  #define RTOS_CREATE_TASK_EX(h,task,name,stackStruct,stackSize,prio,core) \
    _RTOS_CREATE_TASK_EX(&h,task,name,stackStruct.stack,stackSize,prio,core)

  #define RTOS_MS_PER_TICK portTICK_PERIOD_MS
  
  static inline TickType_t RTOS_GET_TIME(void)
  {
    return xTaskGetTickCount();
  }

  static inline uint32_t RTOS_GET_MS(void)
  {
    return (RTOS_GET_TIME() * RTOS_MS_PER_TICK);
  }

#define RTOS_MS_PER_TICK portTICK_PERIOD_MS

  static inline void RTOS_WAIT_MS(uint32_t x)
  {
    if (!x)
      return;
    if ((x = x / RTOS_MS_PER_TICK) < 1)
      x = 1;

    vTaskDelay(x);
  }
#endif  // RTOS type

#ifdef __cplusplus
}
#endif

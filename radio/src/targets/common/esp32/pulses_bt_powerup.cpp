/*
 * Copyright (C) OpenTX
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
#include "pulses_esp32.h"
#include "esp_random.h"

#define STICK_MAX_VALUE (1024)
#define STICK_MIN_VALUE (-1024)
#define THR_STICK_CHANNEL 2  // channel 3
#define RDR_STICK_CHANNEL 3  // channel 4

extern "C" {
    void esp_start_ble_scan(void);
    void ble_write_pwrup_thottle(uint8_t data);
    void ble_write_pwrup_rudder(int8_t data);
    void esp_end_ble(void);
    void task_pwrup(void * pdata);
    extern TaskHandle_t pwrup_task_handle;
}

#define TASKPWRUP_STACK_SIZE (1024 * 4)
#define TASKPWRUP_PRIO 5

static RTOS_TASK_HANDLE taskIdPWRUP;
EXT_RAM_BSS_ATTR RTOS_DEFINE_STACK(taskIdPWRUP, taskPWRUP_stack, TASKPWRUP_STACK_SIZE);
static void* BtPowerUPInit(uint8_t module)
{
    if (NULL == pwrup_task_handle) {
        RTOS_CREATE_TASK_EX(taskIdPWRUP,task_pwrup,"PowerUP task",taskPWRUP_stack,TASKPWRUP_STACK_SIZE,TASKPWRUP_PRIO,MENU_TASK_CORE);
        pwrup_task_handle = taskIdPWRUP.rtos_handle;
    }

    esp_start_ble_scan();
    return (void *)1;
}

static void BtPowerUPDeInit(void* context)
{
    esp_end_ble();
}

static void BtPowerUPSetupPulses(void* context, int16_t* channels, uint8_t nChannels)
{
    // nothing to do
}

static void BtPowerUPSendPulses(void* context, uint8_t* buffer, int16_t* channels, uint8_t nChannels)
{
    static int prevThr = 0;
    static int prevRdr = 0;
    uint8_t thr = (uint8_t)((channelOutputs[THR_STICK_CHANNEL] + ((STICK_MAX_VALUE - STICK_MIN_VALUE) / 2)) * 254 /
            (STICK_MAX_VALUE - STICK_MIN_VALUE));
    int8_t rdr = (0 <= channelOutputs[RDR_STICK_CHANNEL]) ?
            (int8_t)(channelOutputs[RDR_STICK_CHANNEL] * (-128) / STICK_MAX_VALUE) :
            (int8_t)(channelOutputs[RDR_STICK_CHANNEL] * 127 / STICK_MIN_VALUE);
    
    static uint32_t thrTick = 0;

    uint32_t now = RTOS_GET_MS();
    if (prevThr != thr) {
        prevThr = thr;
        ble_write_pwrup_thottle(thr);
    } else {
        // TODO: seems the module would power down motor if the value was not sent or changed for a while. So do some kind of dithering here
        if (now - thrTick > 1000) {
            static int offset = 0;
            if (offset == 0) {
                offset = -1;
            } else {
                offset = 0;
            }
            if (0 != thr) {
                ble_write_pwrup_thottle(thr + offset);
            }
            thrTick = now;
        }
    }
    if (prevRdr != rdr) {
        prevRdr = rdr;
        ble_write_pwrup_rudder(rdr);
    }
}

static int BtPowerUPGetByte(void* context, uint8_t* data)
{
    return 0;
}

static void BtPowerUPProcessData(void* context, uint8_t data, uint8_t* buffer, uint8_t* len)
{
}

#include "hal/module_driver.h"

const etx_proto_driver_t BtPowerUPDriver = {
    .protocol = PROTOCOL_CHANNELS_ESPNOW,
    .init = BtPowerUPInit,
    .deinit = BtPowerUPDeInit,
    .sendPulses = BtPowerUPSendPulses,
    //.processData = BtPowerUPProcessData,
};
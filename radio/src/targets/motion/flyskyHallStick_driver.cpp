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

#include "opentx.h"
#include "flyskyHallStick_driver.h"
#include "esp32_rmt_rx.h"
#include "hal/adc_driver.h"
#include "esp32_uart_driver.h"
#include "crc.h"

#if FLYSKY_UART_PORT == USE_RMT
// Use RMT UART for gimbal. It works fine, except for roughly 1 out of 10000 packets gets CRC error.
static const etx_rmt_uart_hw_def_t gimbal_uart_hw_def = {
    .rx_pin = FLYSKY_UART_RX_PIN,
    .tx_pin = FLYSKY_UART_TX_PIN,
    .memsize = 256,
    .rx_task_stack_size = 1024 * 6,
    .resolution_hz = 80000000,
    .idle_threshold_in_ns = 400000,
    .min_pulse_in_ns = 800,
};
static const etx_serial_driver_t *pUart = &rmtuartSerialDriver;
#else
static const etx_esp32_uart_hw_def_t gimbal_uart_hw_def = {
    .uart_port = FLYSKY_UART_PORT,
    .rx_pin = FLYSKY_UART_RX_PIN,
    .tx_pin = FLYSKY_UART_TX_PIN,
    .fifoSize = 4096,
    .queueSize = 10
};
static const etx_serial_driver_t *pUart = &ESPUartSerialDriver;
#endif

static etx_serial_init param = {
    .baudrate = FLYSKY_HALL_BAUDRATE,
    .encoding = ETX_Encoding_8N1,
    .direction = ETX_Dir_RX,
    .polarity = ETX_Pol_Normal
};

static STRUCT_HALL HallProtocol = { 0 };

static void* _fs_usart_ctx = nullptr;

static int _fs_get_byte(uint8_t* data)
{
    return pUart->getByte(_fs_usart_ctx, data);
}

static uint32_t crc_ok_cnt = 0;
static uint32_t crc_err_cnt = 0;
static void _fs_parse(STRUCT_HALL *hallBuffer, unsigned char ch)
{
    switch (hallBuffer->status) {
    case GET_START:
        if (FLYSKY_HALL_PROTOLO_HEAD == ch) {
            hallBuffer->head = FLYSKY_HALL_PROTOLO_HEAD;
            hallBuffer->status = GET_ID;
            hallBuffer->msg_OK = 0;
        }
        break;
    case GET_ID:
        hallBuffer->hallID.ID = ch;
        hallBuffer->status = GET_LENGTH;
        break;
    case GET_LENGTH:
        hallBuffer->length = ch;
        hallBuffer->dataIndex = 0;
        hallBuffer->status = GET_DATA;
        if (0 == hallBuffer->length) {
            hallBuffer->status = GET_CHECKSUM;
            hallBuffer->checkSum = 0;
        }
        break;
    case GET_DATA:
        hallBuffer->data[hallBuffer->dataIndex++] = ch;
        if (hallBuffer->dataIndex >= hallBuffer->length) {
            hallBuffer->checkSum = 0;
            hallBuffer->dataIndex = 0;
            hallBuffer->status = GET_STATE;
        }
        break;
    case GET_STATE:
        hallBuffer->checkSum = 0;
        hallBuffer->dataIndex = 0;
        hallBuffer->status = GET_CHECKSUM;
        // fall through!
    case GET_CHECKSUM:
        hallBuffer->checkSum |= ch << ((hallBuffer->dataIndex++) * 8);
        if (hallBuffer->dataIndex >= 2) {
            hallBuffer->dataIndex = 0;
            hallBuffer->status = CHECKSUM;
            // fall through!
        } else {
            break;
        }
    case CHECKSUM:
        if (hallBuffer->checkSum ==
                crc16(CRC_1021, &hallBuffer->head, hallBuffer->length + 3, 0xffff)) {
            hallBuffer->msg_OK = 1;
            crc_ok_cnt++;
        } else {
            crc_err_cnt++;
            TRACE("Gimbal CRC err: %d, OK: %d", crc_err_cnt, crc_ok_cnt);
        }
        hallBuffer->status = GET_START;
        break;
    }
}

static volatile bool _fs_gimbal_detected;

static void flysky_gimbal_loop(void*)
{
    uint8_t byte;

    while (_fs_get_byte(&byte)) {
        HallProtocol.index++;

        _fs_parse(&HallProtocol, byte);
        if (HallProtocol.msg_OK) {
            HallProtocol.msg_OK = 0;
            HallProtocol.stickState = HallProtocol.data[HallProtocol.length - 1];

            switch (HallProtocol.hallID.hall_Id.receiverID) {
            case TRANSFER_DIR_TXMCU:
                if (HallProtocol.hallID.hall_Id.packetID == FLYSKY_HALL_RESP_TYPE_VALUES) {
                    int16_t* p_values = (int16_t*)HallProtocol.data;
                    uint16_t* adcValues = getAnalogValues();
                    for (uint8_t i = 0; i < 4; i++) {
                        adcValues[i] = FLYSKY_OFFSET_VALUE - p_values[i];
                    }
                    //TRACE("Gimbal reading: %d %d %d %d", adcValues[0], adcValues[1], adcValues[2], adcValues[3]);
                }
                break;
            }
            _fs_gimbal_detected = true;
        }
    }
}

static void flysky_gimbal_deinit()
{
    pUart->deinit(_fs_usart_ctx);
}

bool flysky_gimbal_init()
{
    _fs_gimbal_detected = false;
    _fs_usart_ctx = pUart->init((void *)&gimbal_uart_hw_def, &param);
    pUart->setIdleCb(_fs_usart_ctx, flysky_gimbal_loop, 0);

    // Wait 70ms for FlySky gimbals to respond. According to LA trace, minimally 23ms is required
    for (uint8_t i = 0; i < 70; i++) {
        RTOS_WAIT_MS(1);
        if (_fs_gimbal_detected) {
            // Mask the first 4 inputs (sticks)
            return true;
        }
    }

    flysky_gimbal_deinit();
    return false;
}

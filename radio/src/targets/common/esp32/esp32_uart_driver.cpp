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
#include "esp32_uart_driver.h"

#include "esp_log.h"
#define TAG "UART"

typedef struct {
    uart_port_t port;
    void (*on_idle_cb)(void *);
    void *on_idle_cb_param;
    QueueHandle_t uart_queue;
    TaskHandle_t rx_task;
} esp32_uart_ctx_t;

static esp32_uart_ctx_t uarts[SOC_UART_HP_NUM]; // TODO-MUFFIN init

static inline esp32_uart_ctx_t *ctx_to_port(void* ctx) {
    return (esp32_uart_ctx_t *)ctx;
}

static void espuart_setIdleCb(void* ctx, void (*on_idle)(void*), void* param) {
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    port->on_idle_cb = on_idle;
    port->on_idle_cb_param = param;
}

static void espuart_uart_event_task(void *pvParameters)
{
    esp32_uart_ctx_t *port = ctx_to_port(pvParameters);
    uart_event_t event;
    size_t buffered_size;
    while (true) {
        //Waiting for UART event.
        if (xQueueReceive(port->uart_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
            //Event of UART receving data
            /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
            case UART_DATA:
                //ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                if (NULL != port->on_idle_cb) {
                    port->on_idle_cb(port->on_idle_cb_param);
                }
                break;
            //Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGI(TAG, "hw fifo overflow");
                // If fifo overflow happened, you should consider adding flow control for your application.
                // The ISR has already reset the rx FIFO,
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(port->port);
                xQueueReset(port->uart_queue);
                break;
            //Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGI(TAG, "ring buffer full");
                // If buffer full happened, you should consider increasing your buffer size
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(port->port);
                xQueueReset(port->uart_queue);
                break;
            //Event of UART RX break detected
            case UART_BREAK:
                //ESP_LOGI(TAG, "uart rx break");
                if (NULL != port->on_idle_cb) {
                    port->on_idle_cb(port->on_idle_cb_param);
                }
                break;
            //Event of UART parity check error
            case UART_PARITY_ERR:
                ESP_LOGI(TAG, "uart parity error");
                break;
            //Event of UART frame error
            case UART_FRAME_ERR:
                ESP_LOGI(TAG, "uart frame error");
                break;
            //UART_PATTERN_DET
            case UART_PATTERN_DET:
            {
                uart_get_buffered_data_len(port->port, &buffered_size);
                int pos = uart_pattern_pop_pos(port->port);
                ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                break;
            }
            //Others
            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
}

static void get_serial_config(const etx_serial_init* params, uart_config_t *uart_config) {
    switch (params->encoding) {
    case ETX_Encoding_8N1:
        uart_config->parity = UART_PARITY_DISABLE;
        uart_config->stop_bits = UART_STOP_BITS_1;
        break;
    case ETX_Encoding_8E2:
        uart_config->parity = UART_PARITY_EVEN;
        uart_config->stop_bits = UART_STOP_BITS_2;
        break;
    default:
        break;
    }
}

void* espUartSerialStart(void *hw_def, const etx_serial_init* params)
{
    etx_esp32_uart_hw_def_t *hw = (etx_esp32_uart_hw_def_t *)hw_def;
    esp32_uart_ctx_t *port = &uarts[(int)hw->uart_port];
    port->port = hw->uart_port;

    uart_config_t uart_config = {
        .baud_rate = (int)params->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    get_serial_config(params, &uart_config);

    // We won't use a buffer for sending data.
    uart_driver_install(port->port, hw->fifoSize, 0,
        hw->queueSize, &port->uart_queue,
        ESP_INTR_FLAG_IRAM);
    uart_param_config(port->port, &uart_config);
    uart_set_pin(port->port, hw->tx_pin, hw->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    xTaskCreate(espuart_uart_event_task, "espuart_uart_event_task", 4096,
        (void *)port, 12, &port->rx_task);
    return (void *)port;
}

void espUartSendByte(void* ctx, uint8_t byte)
{
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    uint8_t data = byte;
    uart_write_bytes(port->port, &data, 1);
}

void espUartSendBuffer(void* ctx, const uint8_t * data, uint32_t size)
{
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    uart_write_bytes(port->port, data, size);
}

void espUartWaitForTxCompleted(void* ctx)
{
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    uart_wait_tx_done(port->port, portMAX_DELAY);
}

static int espUartGetByte(void* ctx, uint8_t* data)
{
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    int r = uart_read_bytes(port->port, data, 1, 0);
    return r;
}

static void espUartClearRxBuffer(void* ctx)
{
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    uart_flush(port->port);
}

static void espUartSerialStop(void* ctx)
{
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    vTaskDelete(port->rx_task);
    uart_driver_delete(port->port);
}

int espUartGetBufferedBytes(void* ctx) {
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    size_t size = 0U;
    if (ESP_OK != uart_get_buffered_data_len(port->port, &size)) {
      size = 0U;
    }
    return 0;
}

int espUartCopyRxBuffer(void* ctx, uint8_t* buf, uint32_t len) {
    esp32_uart_ctx_t *port = ctx_to_port(ctx);
    int r = uart_read_bytes(port->port, buf, len, 0);
    return r;
}

const etx_serial_driver_t ESPUartSerialDriver = {
    .init = espUartSerialStart,
    .deinit = espUartSerialStop,
    .sendByte = espUartSendByte,
    .sendBuffer = espUartSendBuffer,
    .waitForTxCompleted = espUartWaitForTxCompleted,
    .getByte = espUartGetByte,
    .getBufferedBytes = espUartGetBufferedBytes,
    .copyRxBuffer = espUartCopyRxBuffer,
    .clearRxBuffer = espUartClearRxBuffer,
    .getBaudrate = nullptr,
    .setReceiveCb = nullptr,
    .setIdleCb = espuart_setIdleCb,
    .setBaudrateCb = nullptr,
};

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
#include "esp32_rmt_rx.h"
#include "opentx.h"
#include "esp32_uart_driver.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"
#define TAG "UART"

#define SAMPLES_PER_BIT 20
typedef struct {
    etx_rmt_uart_hw_def_t hw_def;
    etx_serial_init params;
    uint8_t bits_in_frame;
    uint8_t data_bits;

    int data_width;
    int info_width; // number of bits of data + parity
    int total_width; // number of bits of data + parity + stop
    int parity;  // -1 means no parity, 0 means even, 1 means odd
    uint16_t stop_bits;

    // RX
    rmt_ctx_t rx;
    StaticTask_t rx_task_buf;
    RingbufHandle_t rxRing;
    void (*on_idle_cb)(void *);
    void *on_idle_cb_param;

    int bit_num;
    uint32_t currentByte;
    int count_ones;
    // --- end RX

    // TX
    rmt_channel_handle_t tx;
    rmt_encoder_handle_t encoder;
} rmt_uart_t;

esp_err_t rmt_new_uart_encoder(etx_rmt_uart_hw_def_t *hw, const etx_serial_init* params, rmt_encoder_handle_t *ret_encoder);

static void rmt_uart_rx_reset_byte(rmt_uart_t *ctx) {
    ctx->bit_num = -1;
    ctx->currentByte = 0;
    ctx->count_ones = 0;
}

static void rmt_uart_decode_next_symbel(rmt_uart_t *ctx, uint16_t duration, uint16_t level) {
    size_t dur = duration;
    dur *= ctx->params.baudrate;
    int bit_count_in_sym =  (dur / ctx->hw_def.resolution_hz) +
            (((dur % ctx->hw_def.resolution_hz) << 1) / ctx->hw_def.resolution_hz);  // round to closest

    if (ctx->bit_num == -1) {
        if (1 == level) {
            // still waiting for start, do nothing
        } else {
            bit_count_in_sym--; // start bit
            ctx->bit_num = bit_count_in_sym;
        }
    } else {
        if (1 == level) {
            if ((ctx->bit_num <= ctx->info_width) && (bit_count_in_sym == 0)) {
                // recv done, means idle condition detected while the bits still pending
                // Indicates that the rest of this byte are all 1s
                bit_count_in_sym = ctx->total_width - ctx->bit_num;
            }
            if ((ctx->parity != -1) && (ctx->bit_num < ctx->info_width)) {
                size_t oc = bit_count_in_sym;
                if (oc > ctx->info_width - ctx->bit_num) {
                    oc = ctx->info_width - ctx->bit_num;
                }
                ctx->count_ones += oc;
            }
            ctx->currentByte |= (((1 << bit_count_in_sym) - 1) << ctx->bit_num);
        }
        ctx->bit_num += bit_count_in_sym;
    }
    //TRACE("RMT SYM %d %04X %d %d %d", ctx->bit_num, (uint16_t)(ctx->currentByte&0xFFFF), duration, bit_count_in_sym, level);
    if (ctx->bit_num >= ctx->total_width) {
        if ((ctx->parity != -1) && (((ctx->parity + ctx->count_ones) & 0x01) != 0)) {
            TRACE_ERROR("RMT_UART parity check failed %04X\n", (uint16_t)(ctx->currentByte&0xFFFF));
        } else if ((ctx->stop_bits & ctx->currentByte) != ctx->stop_bits) {
            TRACE_ERROR("RMT_UART invalid STOP %04X\n", (uint16_t)(ctx->currentByte&0xFFFF));
        } else {
            //TRACE("--- %04X", ctx->currentByte);
            uint8_t d = ctx->currentByte & 0xFF;
            xRingbufferSend(ctx->rxRing, &d, 1, portMAX_DELAY);
            if (NULL != ctx->on_idle_cb) {
                ctx->on_idle_cb(ctx->on_idle_cb_param);
            }
        }
        rmt_uart_rx_reset_byte(ctx);
    }
}

static void rmt_uart_decode_cb(void *ctx, rmt_rx_done_event_data_t *rxdata)
{
    rmt_uart_t *uart = (rmt_uart_t *)ctx;
    //TRACE("-------- %d", rxdata->num_symbols);
    for (size_t i = 0; i < rxdata->num_symbols; i++) {
        rmt_uart_decode_next_symbel(uart, rxdata->received_symbols[i].duration0, rxdata->received_symbols[i].level0);
        rmt_uart_decode_next_symbel(uart, rxdata->received_symbols[i].duration1, rxdata->received_symbols[i].level1);
    }
    rmt_uart_rx_reset_byte(uart);
}

static void rmtuart_setIdleCb(void* ctx, void (*on_idle)(void*), void* param) {
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    port->on_idle_cb = on_idle;
    port->on_idle_cb_param = param;
}

static void parse_params(rmt_uart_t *ctx, const etx_serial_init* params) {
    switch (params->encoding) {
    case ETX_Encoding_8N1:
        ctx->parity = -1;
        ctx->data_width = 8;
        ctx->info_width = 8;
        ctx->total_width = 9;
        ctx->stop_bits = 0x100;
        break;
    case ETX_Encoding_8E2:
        ctx->parity = 0;
        ctx->data_width = 8;
        ctx->info_width = 9;
        ctx->total_width = 11;
        ctx->stop_bits = 0x600;
        break;
    default:
        break;
    }
}

static void esp32_rmt_tx_init(rmt_uart_t *port, etx_rmt_uart_hw_def_t *hw) {
    assert (NULL != port);
    assert (NULL != hw);
    rmt_tx_channel_config_t tx_channel_cfg = {
        .gpio_num = (gpio_num_t)hw->tx_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = hw->resolution_hz,
        .mem_block_symbols = hw->memsize,
        .trans_queue_depth = hw->memsize,
        .flags = {
            .with_dma = true,
        }
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &port->tx));

    ESP_ERROR_CHECK(rmt_new_uart_encoder(hw, &port->params, &port->encoder));

    ESP_ERROR_CHECK(rmt_enable(port->tx));
}

static void* rmtuartSerialStart(void *hw_def, const etx_serial_init* params)
{
    etx_rmt_uart_hw_def_t *hw = (etx_rmt_uart_hw_def_t *)hw_def;
    rmt_uart_t *rvalue = (rmt_uart_t *)heap_caps_malloc(sizeof(rmt_uart_t), MALLOC_CAP_INTERNAL);
    if (NULL != rvalue) {
        memset(rvalue, 0, sizeof(rmt_uart_t));
	    memcpy(&rvalue->params, params, sizeof(rvalue->params));
	    memcpy(&rvalue->hw_def, hw, sizeof(rvalue->hw_def));

        parse_params(rvalue, params);

        if ((params->direction == ETX_Dir_RX) || (params->direction == ETX_Dir_TX_RX)) {
            // RX
            rvalue->rxRing = xRingbufferCreate(hw->memsize, RINGBUF_TYPE_BYTEBUF);
	        esp32_rmt_rx_init(&rvalue->rx, hw->rx_pin, hw->memsize, hw->resolution_hz, rmt_uart_decode_cb);

            rmt_uart_rx_reset_byte(rvalue);
            esp32_rmt_rx_start(&rvalue->rx, (void *)rvalue, hw->rx_task_stack_size,
                    hw->idle_threshold_in_ns, hw->min_pulse_in_ns);
        }

        if ((params->direction == ETX_Dir_TX) || (params->direction == ETX_Dir_TX_RX)) {
            // TX
            esp32_rmt_tx_init(rvalue, hw);
        }
    }

    return (void *)rvalue;
}

void rmtuartSendByte(void* ctx, uint8_t byte)
{
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    rmt_transmit_config_t trans_cfg = {
        .loop_count = 0,
        .flags = {
            .eot_level = 1,
            .queue_nonblocking = 0,
        }
    };
    ESP_ERROR_CHECK(rmt_transmit(port->tx, port->encoder, &byte, 1, &trans_cfg));
}

void rmtuartSendBuffer(void* ctx, const uint8_t * data, uint32_t size)
{
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    rmt_transmit_config_t trans_cfg = {
        .loop_count = 0,
        .flags = {
            .eot_level = 1,
            .queue_nonblocking = 0,
        }
    };
    for (int i = 0; i < size; i++) {
        // send data 1 byte at a time so the encoder can encode each byte
        ESP_ERROR_CHECK(rmt_transmit(port->tx, port->encoder, &data[i], 1, &trans_cfg));
    }
}

void rmtuartWaitForTxCompleted(void* ctx)
{
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(port->tx, -1));
}

static int rmtuartGetByte(void* ctx, uint8_t* data)
{
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    size_t itemSize = 0;
    uint8_t *out = (uint8_t *)xRingbufferReceive(port->rxRing, &itemSize, 0);
    if (NULL != out) {
        *data = *out;
        vRingbufferReturnItem(port->rxRing, out);
    }
    return itemSize;
}

static void rmtuartClearRxBuffer(void* ctx)
{
    rmt_uart_t *port = (rmt_uart_t *)ctx;
}

static void rmtuartSerialStop(void* ctx)
{
    rmt_uart_t *port = (rmt_uart_t *)ctx;
}

int rmtuartGetBufferedBytes(void* ctx) {
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    return 0;
}

int rmtuartCopyRxBuffer(void* ctx, uint8_t* buf, uint32_t len) {
    rmt_uart_t *port = (rmt_uart_t *)ctx;
    return 0;
}

const etx_serial_driver_t rmtuartSerialDriver = {
    .init = rmtuartSerialStart,
    .deinit = rmtuartSerialStop,
    .sendByte = rmtuartSendByte,
    .sendBuffer = rmtuartSendBuffer,
    .waitForTxCompleted = rmtuartWaitForTxCompleted,
    .getByte = rmtuartGetByte,
    .getBufferedBytes = rmtuartGetBufferedBytes,
    .copyRxBuffer = rmtuartCopyRxBuffer,
    .clearRxBuffer = rmtuartClearRxBuffer,
    .getBaudrate = nullptr,
    .setReceiveCb = nullptr,
    .setIdleCb = rmtuart_setIdleCb,
    .setBaudrateCb = nullptr,
};

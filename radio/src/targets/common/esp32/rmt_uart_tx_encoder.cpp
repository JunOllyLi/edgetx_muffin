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
#include "driver/rmt_encoder.h"

#include "esp_log.h"
#define TAG "UART"

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;

    int state;
    int parity;  // -1 means no parity, 0 means even, 1 means odd
    int num_stops;
    rmt_symbol_word_t start_bit;
    rmt_symbol_word_t stop_bit;
    rmt_symbol_word_t p0_stop_bit; // Parity bit 0 and STOP
    rmt_symbol_word_t p1_stop_bit; // Parity bit 1 and STOP
} rmt_uart_encoder_t;

static void parse_params(rmt_uart_encoder_t *ctx, const etx_serial_init* params) {
    switch (params->encoding) {
    case ETX_Encoding_8N1:
        ctx->parity = -1;
        ctx->num_stops = 1;
        break;
    case ETX_Encoding_8E2:
        ctx->parity = 0;
        ctx->num_stops = 2;
        break;
    default:
        break;
    }
}

static size_t rmt_encode_uart(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_uart_encoder_t *uart_encoder = __containerof(encoder, rmt_uart_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = uart_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = uart_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    int state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    rmt_symbol_word_t *stop_bit = &uart_encoder->stop_bit;
    if (uart_encoder->parity != -1) {
        int cnt1 = 0;
        uint8_t *p = (uint8_t *)primary_data;
        for (int i = 0; i < 8; i++) {
            if (*p & (1 << i)) {
                cnt1++;
            }
        }
        if (0 == ((cnt1 + uart_encoder->parity) & 0x01)) {
            stop_bit = &uart_encoder->p0_stop_bit;
        } else {
            stop_bit = &uart_encoder->p1_stop_bit;
        }
    }
    switch (uart_encoder->state) {
    case 0: // send START
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &uart_encoder->start_bit,
                                                sizeof(uart_encoder->start_bit), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            uart_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            uart_encoder->state = 1; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    // fall-through
    case 1: // send data byte
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            uart_encoder->state = 2; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    // fall-through
    case 2: // send STOP
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, stop_bit,
                                                sizeof(*stop_bit), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            uart_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    }
out:
    *ret_state = (rmt_encode_state_t)state;
    return encoded_symbols;
}

static esp_err_t rmt_del_uart_encoder(rmt_encoder_t *encoder)
{
    rmt_uart_encoder_t *uart_encoder = __containerof(encoder, rmt_uart_encoder_t, base);
    rmt_del_encoder(uart_encoder->bytes_encoder);
    rmt_del_encoder(uart_encoder->copy_encoder);
    free(uart_encoder);
    return ESP_OK;
}

static esp_err_t rmt_uart_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_uart_encoder_t *uart_encoder = __containerof(encoder, rmt_uart_encoder_t, base);
    rmt_encoder_reset(uart_encoder->bytes_encoder);
    rmt_encoder_reset(uart_encoder->copy_encoder);
    uart_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_uart_encoder(etx_rmt_uart_hw_def_t *hw, const etx_serial_init* params, rmt_encoder_handle_t *ret_encoder)
{
    rmt_uart_encoder_t *uart_encoder = NULL;
    uart_encoder = (rmt_uart_encoder_t *)rmt_alloc_encoder_mem(sizeof(rmt_uart_encoder_t));
    uart_encoder->base.encode = rmt_encode_uart;
    uart_encoder->base.del = rmt_del_uart_encoder;
    uart_encoder->base.reset = rmt_uart_encoder_reset;

    parse_params(uart_encoder, params);

    // different led strip might have its own timing requirements, following parameter is for WS2812
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .duration0 = (uint16_t)(hw->resolution_hz / (params->baudrate * 2)),
            .level0 = 0,
            .duration1 = (uint16_t)(hw->resolution_hz / (params->baudrate * 2)),
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = (uint16_t)(hw->resolution_hz / (params->baudrate * 2)),
            .level0 = 1,
            .duration1 = (uint16_t)(hw->resolution_hz / (params->baudrate * 2)),
            .level1 = 1,
        },
        .flags = {
            .msb_first = 0 // UART transfers LSB
        }
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &uart_encoder->bytes_encoder));
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &uart_encoder->copy_encoder));

    uart_encoder->start_bit = (rmt_symbol_word_t) {
        .duration0 = (uint16_t)(hw->resolution_hz / (params->baudrate * 2)),
        .level0 = 0,
        .duration1 = (uint16_t)(hw->resolution_hz / (params->baudrate * 2)),
        .level1 = 0,
    };
    uart_encoder->stop_bit = (rmt_symbol_word_t) {
        .duration0 = (uint16_t)(hw->resolution_hz * uart_encoder->num_stops / (params->baudrate * 2)),
        .level0 = 1,
        .duration1 = (uint16_t)(hw->resolution_hz * uart_encoder->num_stops / (params->baudrate * 2)),
        .level1 = 1,
    };

    uart_encoder->p0_stop_bit = (rmt_symbol_word_t) {
        .duration0 = (uint16_t)(hw->resolution_hz / params->baudrate),
        .level0 = 0,
        .duration1 = (uint16_t)(hw->resolution_hz * uart_encoder->num_stops / params->baudrate),
        .level1 = 1,
    };
    uart_encoder->p1_stop_bit = (rmt_symbol_word_t) {
        .duration0 = (uint16_t)(hw->resolution_hz / params->baudrate),
        .level0 = 1,
        .duration1 = (uint16_t)(hw->resolution_hz * uart_encoder->num_stops / params->baudrate),
        .level1 = 1,
    };
    *ret_encoder = &uart_encoder->base;
    return ESP_OK;
}
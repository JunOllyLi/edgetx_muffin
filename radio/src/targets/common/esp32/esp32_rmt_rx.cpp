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

static void esp32_rmt_ctx_free(rmt_ctx_t *ctx);

rmt_symbol_word_t raw_symbols[512];  // TODO-MUFFIN
static void esp32_rmt_rx_task(void * pdata) {
    rmt_ctx_t *ctx = (rmt_ctx_t *)pdata;
    
    rmt_rx_done_event_data_t rx_data;

    rmt_enable(ctx->rmt);
    while(!ctx->exit) {
        // ready to receive
        ESP_ERROR_CHECK(rmt_receive(ctx->rmt, raw_symbols, sizeof(raw_symbols), &ctx->rx_cfg));
        xQueueReceive(ctx->rxQueue, &rx_data, portMAX_DELAY);
        if (!ctx->exit) {
            ctx->decoder(ctx->decoder_ctx, &rx_data);
        }
     }
    esp32_rmt_ctx_free(ctx); // done with the ctx
    vTaskDelete(NULL);
}

IRAM_ATTR static bool rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

void esp32_rmt_rx_init(rmt_ctx_t *ctxmem, int pin, rmt_reserve_memsize_t memsize, size_t resolution_hz, rmt_rx_decode_cb_t dec_fn) {
    assert (NULL != ctxmem);
    rmt_rx_channel_config_t rx_channel_cfg = {
        .gpio_num = (gpio_num_t)pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = resolution_hz,
        .mem_block_symbols = memsize,
        .flags = {
            .with_dma = true,
        }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &ctxmem->rmt));

    ctxmem->decoder = dec_fn;
}

StaticTask_t rx_task_buf;
void esp32_rmt_rx_start(rmt_ctx_t *ctx, void *decoder_ctx, size_t rx_task_stack_size, size_t idle_threshold_in_ns, size_t min_pulse_in_ns) {
    assert(NULL != ctx);
    ctx->stack_size = rx_task_stack_size;
    ctx->rmt_task_stack = (StackType_t *)malloc(ctx->stack_size);
    assert(NULL != ctx->rmt_task_stack);

    ctx->rxQueue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    assert(NULL != ctx->rxQueue);
    ctx->exit = false;
    ctx->rx_cfg.signal_range_min_ns = min_pulse_in_ns;
    ctx->rx_cfg.signal_range_max_ns = idle_threshold_in_ns;
    ctx->decoder_ctx = decoder_ctx;
    ctx->task_id = xTaskCreateStaticPinnedToCore(esp32_rmt_rx_task, "esp32_rmt_rx_task", ctx->stack_size,
            ctx, 5, ctx->rmt_task_stack, &rx_task_buf, TRAINER_PPM_OUT_TASK_CORE); // TODO-MUFFIN priority
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(ctx->rmt, &cbs, ctx->rxQueue));
}


void esp32_rmt_stop(rmt_ctx_t *ctx) {
    ctx->exit = true;
    if (NULL != ctx->rxQueue) {
        rmt_rx_done_event_data_t end = {0};
        xQueueSend(ctx->rxQueue, &end, 0); // doesn't matter what was sent, just send to end the loop
    }
    // TODO-v5: no thing special need to be done for TX. It will end after the last frame sends out
}

static void esp32_rmt_ctx_free(rmt_ctx_t *ctx) {
    if (NULL != ctx) {
        rmt_del_channel(ctx->rmt);
        free(ctx);
    }
}

int rmt_ppm_decode_cb(rmt_ctx_t *ctx, rmt_symbol_word_t *rxdata, size_t rxdata_len, int16_t *ppm_decode_buf)
{
    int channel = 0;
    uint32_t high = 0U;
    for (size_t i = 0; i < rxdata_len; i++) {
        if (i != 0) {
            uint32_t val = (high + rxdata[i].duration0) * (ctx->tick_in_ns / 1000); // result in us
            if (val > 800 && val < 2200) {
                ppm_decode_buf[channel++] =
                        (int16_t)(val - 1500) * (g_eeGeneral.PPM_Multiplier+10) / 10;  // +-500 != 512, but close enough.
            } else {
                channel = -1;
                break;
            }
        }
        high = rxdata[i].duration1;
    }
    return channel;
}

void rmt_ppm_encode_cb(rmt_ctx_t *ctx, uint16_t *ppm, size_t len)
{
#if 0
    // setupPulsesPPM() always setup data with 0.5us tick
    for (int i = 0; i < len; i++) {
        ctx->data[i].duration0 = ppm[i] - 600;
        ctx->data[i].level0 = 1;
        ctx->data[i].duration1 = 600;
        ctx->data[i].level1 = 0;
    }
    ctx->data[len].val = 0;
#endif
}

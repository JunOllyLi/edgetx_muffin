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
#include "esp32_rmt_rx.h"
#if 0
static rmt_ctx_t* rmt_send = NULL;
static rmt_ctx_t* rmt_recv = NULL;

static void ppm_trainer_decode_cb(rmt_ctx_t *ctx, rmt_rx_done_event_data_t *rxdata)
{
    int16_t ppm[MAX_TRAINER_CHANNELS];
    int channel = rmt_ppm_decode_cb(ctx, rxdata->received_symbols, rxdata->num_symbols, ppm);
    if (channel > 0) {
        //ppmInputValidityTimer = PPM_IN_VALID_TIMEOUT;
        //memcpy(ppmInput, ppm, sizeof(ppm));
    }
}

static StaticTask_t rx_task_buf;
EXT_RAM_BSS_ATTR static rmt_ctx_t rxctxbuf;
void init_trainer_capture()
{
#if 0
    rxctxbuf.task_struct = &rx_task_buf;
    rmt_recv = esp32_rmt_rx_init(&rxctxbuf, TRAINER_IN_GPIO, 64,
            RMT_PPM_IN_TICK_NS,
            ppm_trainer_decode_cb,
            MAX_TRAINER_CHANNELS,
            RMT_PPM_IDLE_THRESHOLD_NS);
#endif
}

void stop_trainer_capture()
{
    esp32_rmt_stop(rmt_recv);
}

static size_t esp32_rmt_ppm_encode_cb(rmt_ctx_t *ctx) {
    setupPulsesPPMTrainer();
    size_t count = 0;
    while (0 != trainerPulsesData.ppm.pulses[count]) {
        count++;
    }
    //rmt_ppm_encode_cb(ctx, (uint16_t *)trainerPulsesData.ppm.pulses, count);
    return count;
}

static StaticTask_t tx_task_buf;
EXT_RAM_BSS_ATTR static rmt_ctx_t txctxbuf;
void init_trainer_ppm()
{
#if 0
    txctxbuf.task_struct = &tx_task_buf;
    rmt_send = esp32_rmt_tx_init(&txctxbuf, RMT_TX_PIN, 64,
            RMT_PPM_OUT_TICK_NS,
            esp32_rmt_ppm_encode_cb,
            MAX_TRAINER_CHANNELS + 2); // extra two for idle pulse and termination
#endif
}

void stop_trainer_ppm()
{
    esp32_rmt_stop(rmt_send);
}

#if defined(TRAINER_MODULE_CPPM)
void init_trainer_module_cppm()
{
}
#endif

#if defined(TRAINER_MODULE_SBUS)
void init_trainer_module_sbus()
{
}

void stop_trainer_module_sbus()
{
}
#endif

#if defined(SBUS_TRAINER)
int sbusGetByte(uint8_t * byte)
{
    return 0;
}
#endif

#endif

bool trainer_dsc_available() { return true; }
void trainer_init_dsc_out() {}
void trainer_init_dsc_in() {}
void trainer_stop_dsc() {}

bool is_trainer_dsc_connected() { return false; }
void trainer_init_module_cppm() {}
void trainer_stop_module_cppm() {}
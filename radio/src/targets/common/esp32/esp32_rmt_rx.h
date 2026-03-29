
#ifndef _ESP32_RMT_PULSE_H_
#define _ESP32_RMT_PULSE_H_

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

typedef struct _rmt_rx_ctx_ rmt_ctx_t;

// decode RMT format to data
typedef void (*rmt_rx_decode_cb_t)(void *ctx, rmt_rx_done_event_data_t *rxdata);

typedef size_t rmt_reserve_memsize_t;

struct _rmt_rx_ctx_ {
    rmt_channel_handle_t rmt;
    bool exit;
    size_t pulse_in_frame;
    rmt_symbol_word_t *data;
    QueueHandle_t rxQueue;
    float tick_in_ns;
    TaskHandle_t task_id;
    StaticTask_t *task_struct;
    StackType_t *rmt_task_stack;
    size_t stack_size;

    rmt_reserve_memsize_t memsize;
    rmt_receive_config_t rx_cfg;
    
    rmt_rx_decode_cb_t decoder;
    void *decoder_ctx;
};

void esp32_rmt_rx_init(rmt_ctx_t *ctxmem, int pin, rmt_reserve_memsize_t memsize, size_t resolution_hz, rmt_rx_decode_cb_t dec_fn);
void esp32_rmt_rx_start(rmt_ctx_t *ctx, void *decoder_ctx, size_t rx_task_stack_size, size_t idle_threshold_in_ns, size_t min_pulse_in_ns = 0);

void esp32_rmt_stop(rmt_ctx_t *ctx);

// return number of channel decoded
int rmt_ppm_decode_cb(rmt_ctx_t *ctx, rmt_symbol_word_t *rxdata, size_t rxdata_len, int16_t *ppm_decode_buf);

#define RMT_PPM_OUT_TICK_NS 500  // Upper layer uses 0.5us
#define RMT_PPM_IN_TICK_NS 1000
#define RMT_PPM_IDLE_THRESHOLD_NS 4000000 // 4ms

#endif
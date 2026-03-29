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

#if !defined(_ETX_ESP32_UART_H_)
#define _ETX_ESP32_UART_H_

#include "edgetx.h"
#include "driver/gpio.h"
#include "driver/uart.h"

typedef struct {
    uart_port_t uart_port;
    gpio_num_t rx_pin;
    gpio_num_t tx_pin;
    size_t fifoSize;
    size_t queueSize;
} etx_esp32_uart_hw_def_t;

extern const etx_serial_driver_t ESPUartSerialDriver;

typedef struct {
    gpio_num_t rx_pin;
    gpio_num_t tx_pin;

    size_t memsize;
    size_t rx_task_stack_size;
    size_t resolution_hz;
    size_t idle_threshold_in_ns;
    size_t min_pulse_in_ns;
} etx_rmt_uart_hw_def_t;

extern const etx_serial_driver_t rmtuartSerialDriver;

#endif /* _ETX_ESP32_UART_H_ */
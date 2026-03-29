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
#include "i2c_driver.h"
#include "mcp23xxx.h"

#include "mcp_pins.h"
#include "mcp23017_keys.inc"

extern i2c_master_bus_handle_t gpioext_i2c_bus_handle;
static i2c_master_dev_handle_t mcp[2] = {NULL, NULL};

static uint32_t ShadowOutput = 0U;
static uint8_t *pShadowData = (uint8_t *)&ShadowOutput;
uint32_t ShadowInput = MCP23017_PULLUP;
static uint8_t *pShadowInput = (uint8_t *)&ShadowInput;


static void mcp_set_gpio(uint32_t pin, uint32_t level) {
    if (0 == level) {
        ShadowOutput &= ~(1 << pin);
    } else {
        ShadowOutput |= (1 << pin);
    }
    uint32_t port = MCP_PORT(pin);

    ESP_ERROR_CHECK(i2c_register_write_byte(MCP_HANDLE(port), MCP_REG_ADDR(MCP23XXX_GPIO, port), pShadowData[port]));
}

uint32_t readKeys()
{
    uint32_t result = 0;

    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(i2c_register_read(MCP_HANDLE(i), MCP_REG_ADDR(MCP23XXX_GPIO, i), &pShadowInput[i], 1));
    }

    for (int i = 0; i < sizeof(key_mapping)/sizeof(key_mapping[0]); i++) {
        if ((key_mapping[i].bit & ShadowInput) ^ key_mapping[i].xor_bit) {
            result |= key_mapping[i].key_code_bit;
        }
    }

    return result;
}

uint32_t readTrims()
{
    uint32_t result = 0;
    for (int i = 0; i < sizeof(trim_mapping)/sizeof(trim_mapping[0]); i++) {
        if ((trim_mapping[i].bit & ShadowInput) ^ trim_mapping[i].xor_bit) {
            result |= trim_mapping[i].key_code_bit;
        }
    }
    return result;
}


void keysInit()
{
    i2c_device_config_t i2c_dev0_conf = {
        .device_address = MCP23XXX_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(gpioext_i2c_bus_handle, &i2c_dev0_conf, &mcp[0]));

    i2c_device_config_t i2c_dev1_conf = {
        .device_address = MCP23XXX_ADDR + 1,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(gpioext_i2c_bus_handle, &i2c_dev1_conf, &mcp[1]));

    esp_err_t ret  = ESP_OK;
    uint32_t pullup = MCP23017_PULLUP;
    uint32_t dir = MCP23017_DIR_REG;
    for (int i = 0; i < 4; i++) {
        if (ESP_OK == ret) {
            ret = i2c_register_write_byte(MCP_HANDLE(i), MCP_REG_ADDR(MCP23XXX_GPPU, i), ((uint8_t *)&pullup)[i]);
        }
        if (ESP_OK == ret) {
            ret = i2c_register_write_byte(MCP_HANDLE(i), MCP_REG_ADDR(MCP23XXX_GPPU, i), ((uint8_t *)&pullup)[i]); // TODO: for some reason need to set pull up twice?
        }
        if (ESP_OK == ret) {
            ret = i2c_register_write_byte(MCP_HANDLE(i), MCP_REG_ADDR(MCP23XXX_GPIO, i), pShadowData[i]);
        }
        if (ESP_OK == ret) {
            ret = i2c_register_write_byte(MCP_HANDLE(i), MCP_REG_ADDR(MCP23XXX_IODIR, i), ((uint8_t *)&dir)[i]);
        }
    }

    if (ESP_OK != ret) {
        TRACE_ERROR("Error during MCP23017 init");
    } else {
        TRACE("MCP23017 initialized, now hold 3V3 active and enable 5V");
        mcp_set_gpio(MCP_PWR_EN, 1);
        mcp_set_gpio(MCP_5V_EN, 1);
    }
}

void INTERNAL_MODULE_ON(void) {
    mcp_set_gpio(MCP_INTMOD_BOOT, 1);
    mcp_set_gpio(MCP_INTMOD_5V_EN, 1);
}
void INTERNAL_MODULE_OFF(void) {
    mcp_set_gpio(MCP_INTMOD_5V_EN, 0);
    mcp_set_gpio(MCP_INTMOD_BOOT, 0);
}

void EXTERNAL_MODULE_ON(void) {
    mcp_set_gpio(MCP_EXTMOD_5V_EN, 1);
}
void EXTERNAL_MODULE_OFF(void) {
    mcp_set_gpio(MCP_EXTMOD_5V_EN, 0);
}

void internal_protocol_led_on(bool on) {
    mcp_set_gpio(MCP_INTERNAL_PROTO_LED, on);
}

void pwrOff()
{
    TRACE("Power off");
    RTOS_WAIT_MS(200);
    mcp_set_gpio(MCP_5V_EN, 0);
    mcp_set_gpio(MCP_PWR_EN, 0);
    while (1) RTOS_WAIT_MS(20); // should never return
}

bool pwrPressed()
{
  return (0 != (ShadowInput & MCP_PWR_SW_DET));
}

bool pwrOffPressed() {
    return !pwrPressed(); // Muffin uses switch for power    
}
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

#define DS3231_ADDRESS 0x68   ///< I2C address for DS3231
#define DS3231_TIME 0x00      ///< Time register
#define DS3231_ALARM1 0x07    ///< Alarm 1 register
#define DS3231_ALARM2 0x0B    ///< Alarm 2 register
#define DS3231_CONTROL 0x0E   ///< Control register
#define DS3231_STATUSREG 0x0F ///< Status register
#define DS3231_TEMPERATUREREG                                                  \
  0x11 ///< Temperature register (high byte - low byte is at 0x12), 10-bit
       ///< temperature value

extern i2c_master_bus_handle_t rtc_i2c_bus_handle;
static i2c_master_dev_handle_t rtc_handle = NULL;

static uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }
static uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }

void rtcSetTime(const struct gtm * t)
{
    TRACE("rtcSetTime %d/%d/%d %d:%d:%d",
            t->tm_year + TM_YEAR_BASE,
            t->tm_mon + 1,
            t->tm_mday,
            t->tm_hour,
            t->tm_min,
            t->tm_sec);
  
    uint8_t buffer[8] = {DS3231_TIME,
                       bin2bcd(t->tm_sec),
                       bin2bcd(t->tm_min),
                       bin2bcd(t->tm_hour),
                       bin2bcd(t->tm_wday),
                       bin2bcd(t->tm_mday),
                       bin2bcd(t->tm_mon + 1),
                       bin2bcd(t->tm_year + TM_YEAR_BASE - 2000U)};
    i2c_register_write_buf(rtc_handle, buffer, 8);

    uint8_t statreg = 0;
    i2c_register_read(rtc_handle, DS3231_STATUSREG, &statreg, sizeof(statreg));
    statreg &= ~0x80; // flip OSF bit
    i2c_register_write_byte(rtc_handle, DS3231_STATUSREG, statreg);
}

void rtcGetTime(struct gtm * t)
{
    uint8_t buffer[7];
    buffer[0] = 0;
    i2c_register_write_read_buf(rtc_handle, buffer, 1, buffer, 7);

    TRACE("rtcGetTime %d/%d/%d %d:%d:%d",
            bcd2bin(buffer[6]) + 2000U,
            bcd2bin(buffer[5] & 0x7F),
            bcd2bin(buffer[4]),
            bcd2bin(buffer[2]),
            bcd2bin(buffer[1]),
            bcd2bin(buffer[0] & 0x7F));

    t->tm_year = bcd2bin(buffer[6]) + 2000U - TM_YEAR_BASE;
    t->tm_mon = bcd2bin(buffer[5] & 0x7F) - 1;
    t->tm_mday = bcd2bin(buffer[4]);
    t->tm_hour = bcd2bin(buffer[2]);
    t->tm_min = bcd2bin(buffer[1]);
    t->tm_sec = bcd2bin(buffer[0] & 0x7F);
}

void rtcInit()
{
    i2c_device_config_t i2c_dev_conf = {
        .device_address = DS3231_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(rtc_i2c_bus_handle, &i2c_dev_conf, &rtc_handle));

    struct gtm utm;
    rtcGetTime(&utm);
    g_rtcTime = gmktime(&utm);
}

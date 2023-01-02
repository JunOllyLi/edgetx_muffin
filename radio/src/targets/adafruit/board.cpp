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
#include "Arduino.h"

extern void adruino_adc_init(void);
extern void flysky_hall_stick_init();

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
extern pixel_t LCD_FIRST_FRAME_BUFFER[];
extern pixel_t LCD_SECOND_FRAME_BUFFER[];
#endif

HardwareOptions hardwareOptions;

#if defined(__cplusplus)
extern "C" {
#endif
//#include "usb_dcd_int.h"
//#include "usb_bsp.h"
#if defined(__cplusplus)
}
#endif

//HardwareOptions hardwareOptions;

void watchdogInit(unsigned int duration)
{
}

#if defined(SPORT_UPDATE_PWR_GPIO)
void sportUpdateInit()
{
}

void sportUpdatePowerOn()
{
}

void sportUpdatePowerOff()
{
}

void sportUpdatePowerInit()
{
  if (g_eeGeneral.sportUpdatePower == 1)
    sportUpdatePowerOn();
  else
    sportUpdatePowerOff();
}
#endif

void loop() {
  //display.display();
}

void boardInit()
{
  disableCore0WDT();
  disableCore1WDT();

#ifdef ARDUINO_FEATHER_F405
#else // default ESP32V2
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
#endif

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2) && defined(COLORLCD)
  psramInit();
  // The following were put in the "noinit" section of PSRAM, so need to clear those.
  memset(&g_eeGeneral, 0, sizeof(g_eeGeneral));
#if defined(LCD_CONTRAST_DEFAULT)
  g_eeGeneral.contrast = LCD_CONTRAST_DEFAULT;
#endif
  memset(&g_model, 0, sizeof(g_model));
  memset(LCD_SECOND_FRAME_BUFFER, 0, LCD_W * LCD_H * sizeof(pixel_t));
  memset(LCD_FIRST_FRAME_BUFFER, 0, LCD_W * LCD_H * sizeof(pixel_t));
#endif

#if defined(DEBUG)
  serialSetMode(SP_AUX1, UART_MODE_DEBUG);
  serialInit(SP_AUX1, UART_MODE_DEBUG);
#endif
#if !defined(COLORLCD)
  lcdInit();
#endif

  //eepromInit();
  keysInit();

  flysky_hall_stick_init();
  init5msTimer();
  init2MhzTimer();
  adruino_adc_init();
}

void boardOff()
{
  // this function must not return!
}

#if defined(AUDIO_SPEAKER_ENABLE_GPIO)
void initSpeakerEnable()
{
}

void enableSpeaker()
{
}

void disableSpeaker()
{
}
#endif

#if defined(HEADPHONE_TRAINER_SWITCH_GPIO)
void initHeadphoneTrainerSwitch()
{
}

void enableHeadphone()
{
}

void enableTrainer()
{
}
#endif

#if defined(JACK_DETECT_GPIO)
void initJackDetect(void)
{
}
#endif

int usbPlugged() {
  return 0;// TODO-feather
}

void enableVBatBridge() {

}
void disableVBatBridge() {

}
bool isVBatBridgeEnabled() {
  return false;
}
#ifndef ARDUINO_FEATHER_F405
void NVIC_SystemReset(void) {}
#endif

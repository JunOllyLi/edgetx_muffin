/**
 * @file lv_conf.h
 * Configuration file for v8.2.0
 */

/*
 * Copy this file as `lv_conf.h`
 * 1. simply next to the `lvgl` folder
 * 2. or any other places and
 *    - define `LV_CONF_INCLUDE_SIMPLE`
 *    - add the path as include path
 */

/* clang-format off */
#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_EDGETX_H
#define LV_CONF_EDGETX_H

#if !defined(CONFIG_LV_TFT_DISPLAY_CONTROLLER_RA8875)
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 320
#else
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 272
#endif

#define DRAW_BUF_STRIP_DMA
#define DRAW_BUF_H 40

#include "lv_conf.h"

#undef LV_TICK_CUSTOM
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((esp_timer_get_time() / 1000LL))

#endif /*LV_CONF_EDGETX_H*/

#endif /*End of "Content enable"*/

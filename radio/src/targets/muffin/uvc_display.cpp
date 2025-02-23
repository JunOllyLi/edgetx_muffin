/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
//#include "esp_lcd_ra8875.h"
#include "uvc_display.h"

#define MCU_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

static esp_lcd_i80_bus_handle_t i80_bus = NULL;
static esp_lcd_i80_bus_config_t bus_config = {
    .dc_gpio_num = CONFIG_LV_TFT_MCU_DC_NUM,
    .wr_gpio_num = CONFIG_LV_TFT_MCU_WR_NUM,
    .clk_src = LCD_CLK_SRC_PLL160M,
    .data_gpio_nums = {
        CONFIG_LV_TFT_MCU_DATA0,
        CONFIG_LV_TFT_MCU_DATA1,
        CONFIG_LV_TFT_MCU_DATA2,
        CONFIG_LV_TFT_MCU_DATA3,
        CONFIG_LV_TFT_MCU_DATA4,
        CONFIG_LV_TFT_MCU_DATA5,
        CONFIG_LV_TFT_MCU_DATA6,
        CONFIG_LV_TFT_MCU_DATA7,
#if CONFIG_LV_TFT_MCU_BUS_WIDTH_16
        CONFIG_LV_TFT_MCU_DATA8,
        CONFIG_LV_TFT_MCU_DATA9,
        CONFIG_LV_TFT_MCU_DATA10,
        CONFIG_LV_TFT_MCU_DATA11,
        CONFIG_LV_TFT_MCU_DATA12,
        CONFIG_LV_TFT_MCU_DATA13,
        CONFIG_LV_TFT_MCU_DATA14,
        CONFIG_LV_TFT_MCU_DATA15,
#endif
    },
#if CONFIG_LV_TFT_MCU_BUS_WIDTH_16
    .bus_width = 16,
#else
    .bus_width = 8,
#endif
    .max_transfer_bytes = 480 * 40 * 2
};

static bool mcu_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_io_i80_config_t io_config = {
    .cs_gpio_num = CONFIG_LV_TFT_MCU_CS_NUM,
    .pclk_hz = MCU_LCD_PIXEL_CLOCK_HZ,
    .trans_queue_depth = 10,
    .on_color_trans_done = NULL, // TODO mcu_notify_lvgl_flush_ready,
    .user_ctx = NULL,
#if CONFIG_LV_TFT_LCD_CMD_WIDTH_16
    .lcd_cmd_bits = 16,
#else
    .lcd_cmd_bits = 8,
#endif
#if CONFIG_LV_TFT_LCD_PARAM_WIDTH_16
    .lcd_param_bits = 16,
#else
    .lcd_param_bits = 8,
#endif
    .dc_levels = {
        .dc_idle_level = 0,
        .dc_cmd_level = 0,
        .dc_dummy_level = 0,
        .dc_data_level = 1,
    },
    .flags = {
        .cs_active_high = 0,
        .reverse_color_bits = 0,
        .swap_color_bytes = 1,
        .pclk_active_neg = 0,
        .pclk_idle_low = 0,
    },
};

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = CONFIG_LV_TFT_MCU_RST_NUM,
    .color_space = (lcd_rgb_element_order_t)ESP_LCD_COLOR_SPACE_BGR,
    .bits_per_pixel = 16,
};

static const char *TAG = "uvc_disp";

extern "C" esp_err_t esp_lcd_new_panel_ili9488(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGD(TAG, "Initialize Intel 8080 bus");
    bus_config.max_transfer_bytes = config->max_transfer_sz;
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &i80_bus), TAG, "I80 init failed");

    ESP_LOGD(TAG, "Install panel IO");
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_i80(i80_bus, &io_config, ret_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver of RA8875");
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ili9488(*ret_io, &panel_config, ret_panel), err, TAG, "New panel ILI9488 failed");

    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(*ret_panel), err, TAG, "");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(*ret_panel), err, TAG, "");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(*ret_panel, true), err, TAG, "");
    return ret;

err:
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
    }
    if (i80_bus) {
        esp_lcd_del_i80_bus(i80_bus);
    }
    return ret;
}
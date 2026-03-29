/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "hx8357.h"

static const char *TAG = "lcd_panel.hx8357";

static esp_err_t panel_hx8357_del(esp_lcd_panel_t *panel);
static esp_err_t panel_hx8357_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_hx8357_init(esp_lcd_panel_t *panel);
static esp_err_t panel_hx8357_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_hx8357_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_hx8357_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_hx8357_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_hx8357_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_hx8357_disp_on_off(esp_lcd_panel_t *panel, bool on_off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_cal; // save surrent value of LCD_CMD_COLMOD register
} hx8357_panel_t;

esp_err_t esp_lcd_new_panel_hx8357(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    hx8357_panel_t *hx8357 = NULL;
    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    hx8357 = calloc(1, sizeof(hx8357_panel_t));
    ESP_GOTO_ON_FALSE(hx8357, ESP_ERR_NO_MEM, err, TAG, "no mem for hx8357 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->color_space) {
    case ESP_LCD_COLOR_SPACE_RGB:
        hx8357->madctl_val = 0;
        break;
    case ESP_LCD_COLOR_SPACE_BGR:
        hx8357->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    uint8_t fb_bits_per_pixel = 0;
    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        hx8357->colmod_cal = 0x55;
        fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        hx8357->colmod_cal = 0x66;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    hx8357->io = io;
    hx8357->fb_bits_per_pixel = fb_bits_per_pixel;
    hx8357->reset_gpio_num = panel_dev_config->reset_gpio_num;
    hx8357->reset_level = panel_dev_config->flags.reset_active_high;
    hx8357->base.del = panel_hx8357_del;
    hx8357->base.reset = panel_hx8357_reset;
    hx8357->base.init = panel_hx8357_init;
    hx8357->base.draw_bitmap = panel_hx8357_draw_bitmap;
    hx8357->base.invert_color = panel_hx8357_invert_color;
    hx8357->base.set_gap = panel_hx8357_set_gap;
    hx8357->base.mirror = panel_hx8357_mirror;
    hx8357->base.swap_xy = panel_hx8357_swap_xy;
    hx8357->base.disp_on_off = panel_hx8357_disp_on_off;
    *ret_panel = &(hx8357->base);
    ESP_LOGD(TAG, "new hx8357 panel @%p", hx8357);

    return ESP_OK;

err:
    if (hx8357) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(hx8357);
    }
    return ret;
}

static esp_err_t panel_hx8357_del(esp_lcd_panel_t *panel)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);

    if (hx8357->reset_gpio_num >= 0) {
        gpio_reset_pin(hx8357->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del hx8357 panel @%p", hx8357);
    free(hx8357);
    return ESP_OK;
}

static esp_err_t panel_hx8357_reset(esp_lcd_panel_t *panel)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    esp_lcd_panel_io_handle_t io = hx8357->io;

    // perform hardware reset
    if (hx8357->reset_gpio_num >= 0) {
        gpio_set_level(hx8357->reset_gpio_num, hx8357->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(hx8357->reset_gpio_num, !hx8357->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else { // perform software reset
        esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20)); // spec, wait at least 5m before sending new command
    }

    return ESP_OK;
}

const uint8_t
  initb[] = {
    HX8357B_SETPOWER, 3,
      0x44, 0x41, 0x06,
    HX8357B_SETVCOM, 2,
      0x40, 0x10,
    HX8357B_SETPWRNORMAL, 2,
      0x05, 0x12,
    HX8357B_SET_PANEL_DRIVING, 5,
      0x14, 0x3b, 0x00, 0x02, 0x11,
    HX8357B_SETDISPLAYFRAME, 1,
      0x0c,                      // 6.8mhz
    HX8357B_SETPANELRELATED, 1,
      0x01,                      // BGR
    0xEA, 3,                     // seq_undefined1, 3 args
      0x03, 0x00, 0x00,
    0xEB, 4,                     // undef2, 4 args
      0x40, 0x54, 0x26, 0xdb,
    HX8357B_SETGAMMA, 12,
      0x00, 0x15, 0x00, 0x22, 0x00, 0x08, 0x77, 0x26, 0x66, 0x22, 0x04, 0x00,
    HX8357_MADCTL, 1,
      0xC0,
    HX8357_COLMOD, 1,
      0x55,
    HX8357_PASET, 4,
      0x00, 0x00, 0x01, 0xDF,
    HX8357_CASET, 4,
      0x00, 0x00, 0x01, 0x3F,
    HX8357B_SETDISPMODE, 1,
      0x00,                      // CPU (DBI) and internal oscillation ??
    HX8357_SLPOUT, 0x80 + 120/5, // Exit sleep, then delay 120 ms
    HX8357_DISPON, 0x80 +  10/5, // Main screen turn on, delay 10 ms
    0                            // END OF COMMAND LIST
  }, initd[] = {
    HX8357_SWRESET, 0x80 + 100/5, // Soft reset, then delay 10 ms
    HX8357D_SETC, 3,
      0xFF, 0x83, 0x57,
    0xFF, 0x80 + 500/5,          // No command, just delay 300 ms
    HX8357_SETRGB, 4,
      0x80, 0x00, 0x06, 0x06,    // 0x80 enables SDO pin (0x00 disables)
    HX8357D_SETCOM, 1,
      0x25,                      // -1.52V
    HX8357_SETOSC, 1,
      0x68,                      // Normal mode 70Hz, Idle mode 55 Hz
    HX8357_SETPANEL, 1,
      0x05,                      // BGR, Gate direction swapped
    HX8357_SETPWR1, 6,
      0x00,                      // Not deep standby
      0x15,                      // BT
      0x1C,                      // VSPR
      0x1C,                      // VSNR
      0x83,                      // AP
      0xAA,                      // FS
    HX8357D_SETSTBA, 6,
      0x50,                      // OPON normal
      0x50,                      // OPON idle
      0x01,                      // STBA
      0x3C,                      // STBA
      0x1E,                      // STBA
      0x08,                      // GEN
    HX8357D_SETCYC, 7,
      0x02,                      // NW 0x02
      0x40,                      // RTN
      0x00,                      // DIV
      0x2A,                      // DUM
      0x2A,                      // DUM
      0x0D,                      // GDON
      0x78,                      // GDOFF
    HX8357_COLMOD, 1,
      0x55,                      // 16 bit
    HX8357_MADCTL, 1,
      0xC0,
    HX8357_TEON, 1,
      0x00,                      // TW off
    HX8357_TEARLINE, 2,
      0x00, 0x02,
    HX8357_SLPOUT, 0x80 + 150/5, // Exit Sleep, then delay 150 ms
    HX8357_DISPON, 0x80 +  50/5, // Main screen turn on, delay 50 ms
    0,                           // END OF COMMAND LIST
  };

uint8_t displayType = HX8357D;
static esp_err_t panel_hx8357_init(esp_lcd_panel_t *panel)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    esp_lcd_panel_io_handle_t io = hx8357->io;

  	const uint8_t *addr = (displayType == HX8357B) ? initb : initd;
	  uint8_t        cmd, x, numArgs;
	  while((cmd = *addr++) > 0) { // '0' command ends list
		    x = *addr++;
		    numArgs = x & 0x7F;
		    if (cmd != 0xFF) { // '255' is ignored
			      if (x & 0x80) {  // If high bit set, numArgs is a delay time
                esp_lcd_panel_io_tx_param(io, cmd, NULL, 0);
			      } else {
                esp_lcd_panel_io_tx_param(io, cmd, addr, numArgs);
				        addr += numArgs;
			      }
		    }
		    if (x & 0x80) {       // If high bit set...
			      vTaskDelay(numArgs * 5 / portTICK_PERIOD_MS); // numArgs is actually a delay time (5ms units)
		    }
	  }

    panel_hx8357_swap_xy(panel, true);
    panel_hx8357_mirror(panel, true, false);
    return ESP_OK;
}

static esp_err_t panel_hx8357_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = hx8357->io;

    x_start += hx8357->x_gap;
    x_end += hx8357->x_gap;
    y_start += hx8357->y_gap;
    y_end += hx8357->y_gap;

    // define an area of frame memory where MCU can access
    esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4);
    esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4);
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * hx8357->fb_bits_per_pixel / 8;
    esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_hx8357_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    esp_lcd_panel_io_handle_t io = hx8357->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    esp_lcd_panel_io_tx_param(io, command, NULL, 0);
    return ESP_OK;
}

static esp_err_t panel_hx8357_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    esp_lcd_panel_io_handle_t io = hx8357->io;
    if (mirror_x) {
        hx8357->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        hx8357->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        hx8357->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        hx8357->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        hx8357->madctl_val
    }, 1);
    return ESP_OK;
}

static esp_err_t panel_hx8357_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    esp_lcd_panel_io_handle_t io = hx8357->io;
    if (swap_axes) {
        hx8357->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        hx8357->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        hx8357->madctl_val
    }, 1);
    return ESP_OK;
}

static esp_err_t panel_hx8357_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    hx8357->x_gap = x_gap;
    hx8357->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_hx8357_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    hx8357_panel_t *hx8357 = __containerof(panel, hx8357_panel_t, base);
    esp_lcd_panel_io_handle_t io = hx8357->io;
    int command = 0;
    if (!on_off) {
        command = LCD_CMD_DISPOFF;
    } else {
        command = LCD_CMD_DISPON;
    }
    esp_lcd_panel_io_tx_param(io, command, NULL, 0);
    return ESP_OK;
}

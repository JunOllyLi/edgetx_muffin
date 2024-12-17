/**
 * @file sh1107.cpp
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "opentx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_driver.h"
#include "lvgl.h"

extern i2c_master_bus_handle_t toplcd_i2c_bus_handle;
static i2c_master_dev_handle_t toplcd_handle = NULL;

/*********************
 *      DEFINES
 *********************/
#define TAG "SH1107"

#define SH1107_ADDR 0x3C

#define OLED_W 128
#define OLED_H 64

#define MAX_CHAR_IN_STR 16

/*The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct. */
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static esp_err_t sh1107_send_cmd(uint8_t cmd);
static void sh1107_send_data(void *data, uint16_t length);

static bool top_lcd_exists = true;

EXT_RAM_BSS_ATTR static uint8_t oled_buf[OLED_H][(OLED_W / 8) + 1];

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void reset_oled_buf(void) {
    for (int i = 0; i < OLED_H; i++) {
        memset(oled_buf[i], 0, (OLED_W / 8) + 1);
        oled_buf[i][0] = 0x40;
    }
}

static esp_err_t sh1107_init(void) {
    esp_err_t err = ESP_OK;
    i2c_device_config_t i2c_dev_conf = {
        .device_address = SH1107_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(toplcd_i2c_bus_handle,
            &i2c_dev_conf, &toplcd_handle));

    // Use Double Bytes Commands if necessary, but not Command+Data
    // Initialization taken from https://github.com/nopnop2002/esp-idf-m5stick
    lcd_init_cmd_t init_cmds[] = {
        { 0xAE, { 0 }, 0 }, // Turn display off
        { 0xDC, { 0 }, 0 }, // Set display start line
        { 0x00, { 0 }, 0 }, // ...value
        { 0x81, { 0 }, 0 }, // Set display contrast
        { 0xFF, { 0 }, 0 }, // ...value
        { 0x21, { 0 }, 0 }, // Set memory mode
        { 0xA0, { 0 }, 0 }, // Non-rotated display
        { 0xC8, { 0 }, 0 }, // landscape, flipped vertical
        //{ 0xC7, {0}, 0},	// portrait, flipped vertical
        { 0xA8, { 0 }, 0 }, // Set multiplex ratio
        { 0x7F, { 0 }, 0 }, // ...value
        { 0xD3, { 0 }, 0 }, // Set display offset to zero
        { 0x60, { 0 }, 0 }, // ...value
        { 0xD5, { 0 }, 0 }, // Set display clock divider
        { 0x51, { 0 }, 0 }, // ...value
        { 0xD9, { 0 }, 0 }, // Set pre-charge
        { 0x22, { 0 }, 0 }, // ...value
        { 0xDB, { 0 }, 0 }, // Set com detect
        { 0x35, { 0 }, 0 }, // ...value
        { 0xB0, { 0 }, 0 }, // Set page address
        { 0xDA, { 0 }, 0 }, // Set com pins
        { 0x12, { 0 }, 0 }, // ...value
        { 0xA4, { 0 }, 0 }, // output ram to display
        //{ 0xA7, {0}, 0},	// inverted display
        { 0xA6, { 0 }, 0 }, // Non-inverted display
        { 0xAF, { 0 }, 0 }, // Turn display on
        { 0, { 0 }, 0xff },
    };

    //Send all the commands
    uint16_t cmd = 0;
    while (init_cmds[cmd].databytes != 0xff) {
        err = sh1107_send_cmd(init_cmds[cmd].cmd);
        if (ESP_OK != err) {
            break;
        }
        sh1107_send_data(init_cmds[cmd].data, init_cmds[cmd].databytes & 0x1F);
        if (init_cmds[cmd].databytes & 0x80) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        cmd++;
    }
    return err;
}

static void sh1107_flush(void) {
    for (int y = 0; y < OLED_H; y++) {
        uint8_t columnLow = y & 0x0F;
        uint8_t columnHigh = (y >> 4) & 0x0F;
        sh1107_send_cmd(0x10 | columnHigh); // Set Higher Column Start Address for Page Addressing Mode
        sh1107_send_cmd(0x00 | columnLow); // Set Lower Column Start Address for Page Addressing Mode
        sh1107_send_cmd( 0xB0 ); // Set Page Start Address for Page Addressing Mode (page 0)
        i2c_register_write_buf(toplcd_handle, (uint8_t *)oled_buf[y], (OLED_W / 8) + 1);
    }
}

static esp_err_t sh1107_send_cmd(uint8_t cmd) {
    return i2c_register_write_byte(toplcd_handle, 0, cmd);
}

static void sh1107_send_data(void *data, uint16_t length) {
    if (0 != length) {
        uint8_t buf[OLED_W / 8 + 1] = {0x40};
        memcpy(&buf[1], data, length);
        i2c_register_write_buf(toplcd_handle, (uint8_t *)buf, length + 1);
    }
}

static void draw_str(uint32_t start_x, uint32_t start_y, char *str, LcdFlags flags) {
    const lv_font_t *pfont = getFont(flags);
    const unsigned char *bitmaps[MAX_CHAR_IN_STR] = {0};
    lv_font_glyph_dsc_t g[MAX_CHAR_IN_STR] = {0};
    int num_letters = 0;

    int top = 0, bottom = 0;
    for (int i = 0; i < MAX_CHAR_IN_STR; i++) {
        if ('\0' == str[i]) {
            break;
        }
        num_letters++;
        lv_font_get_glyph_dsc(pfont, &g[i], str[i], str[i+1]);
        if (g[i].ofs_y - g[i].box_h < top) {
            top = g[i].ofs_y - g[i].box_h;
        }
        if (-g[i].ofs_y > bottom) {
            bottom = -g[i].ofs_y;
        }
        bitmaps[i]= lv_font_get_glyph_bitmap(pfont, str[i]);
    }

    for (int y = top; y < bottom; y++) {
        // top is negative, caller's needs to make sure to have a positive position offset
        uint8_t *col = (uint8_t *)&oled_buf[y + start_y][start_x/8 + 1];
        uint8_t bit = (1 << (start_x % 8));
        for (int i = 0; i < num_letters; i++) {
            if ((y >= - g[i].ofs_y - g[i].box_h) && (y < -g[i].ofs_y)) {
                for (int x = 0; x < g[i].adv_w; x++) {
                    if (x < g[i].ofs_x || x >= (g[i].ofs_x + g[i].box_w)) {
                        *col &= ~bit;
                        if (bit == 0x80) {
                            col++;
                            bit = 1;
                        } else {
                            bit <<= 1;
                        }
                    } else {
                        int idx_pixel = (y + g[i].box_h + g[i].ofs_y) * g[i].box_w + x - g[i].ofs_x;
                        uint8_t pix = 0;
                        if (0 != (idx_pixel & 1)) {
                            pix = bitmaps[i][(idx_pixel / 2)] & 0x0F;
                        } else {
                            pix = (bitmaps[i][(idx_pixel / 2)] >> 4) & 0x0F;
                        }

                        if (pix & 0x08) {
                            *col |= bit;
                        } else {
                            *col &= ~bit;
                        }

                        if (bit == 0x80) {
                            col++;
                            bit = 1;
                        } else {
                            bit <<= 1;
                        }
                    }
                }
            } else {
                for (int x = 0; x < g[i].adv_w; x++) {
                    *col &= ~bit;
                    if (bit == 0x80) {
                        col++;
                        bit = 1;
                    } else {
                        bit <<= 1;
                    }
                }
            }
        }
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void toplcdInit()
{
    reset_oled_buf();
    if (ESP_OK != sh1107_init()) {
        TRACE("SH1107 Top LCD not detected");
        top_lcd_exists = false;
    } else {
        TRACE("SH1107 Top LCD initialized");
        draw_str(0, 40, "EdgeTX", FONT(XL));
        sh1107_flush();
    }
}

void toplcdRefresh()
{
#if 1
    // TODO: this is only getting the RX VBATT and TRSS from the FlySKy2A RX. need to figure out more generic way
    reset_oled_buf();
    for (uint8_t idx = 0; idx < MAX_TELEMETRY_SENSORS; idx++) {
        if (g_model.telemetrySensors[idx].isAvailable()) {
            TelemetryItem & telemetryItem = telemetryItems[idx];
            int index = idx;// - ITEM_TELEMETRY_SENSOR_FIRST;
            //TRACE("====== %s %d %d", g_model.telemetrySensors[idx].label, isTelemetryFieldAvailable(idx), getValue(MIXSRC_FIRST_TELEM+3*index));
            if (strcmp(g_model.telemetrySensors[idx].label, "A3") == 0) {
                char buf[20] = {0};
                sprintf(buf, "%0.1f", ((float)getValue(MIXSRC_FIRST_TELEM+3*index)) / 100.);
                draw_str(0, 50, buf, FONT(XL));
            }
            if (strcmp(g_model.telemetrySensors[idx].label, "TRSS") == 0) {
                char buf[20] = {0};
                sprintf(buf, "%d", getValue(MIXSRC_FIRST_TELEM+3*index));
                draw_str(64, 50, buf, FONT(XL));
            }
        }
    }
#endif
    if (top_lcd_exists) {
        sh1107_flush();
    }
}
/**
 * @file esp_lcd_backlight.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "esp_lcd_backlight.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdlib.h>

typedef struct {
    bool pwm_control; // true: LEDC is used, false: GPIO is used
    int index;        // Either GPIO or LEDC channel
} disp_backlight_t;

static const char *TAG = "disp_backlight";

disp_backlight_h disp_backlight_new(const disp_backlight_config_t *config)
{
    if (config == NULL)
        return NULL;

    if (!GPIO_IS_VALID_OUTPUT_GPIO(config->gpio_num)) {
        ESP_LOGW(TAG, "Invalid GPIO number");
        return NULL;
    }

    disp_backlight_t *bckl_dev = calloc(1, sizeof(disp_backlight_t));
    if (bckl_dev == NULL) {
        ESP_LOGW(TAG, "Not enough memory");
        return NULL;
    }

    if (config->pwm_control) {
        bckl_dev->pwm_control = true;
        bckl_dev->index = config->channel_idx;

        ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = config->timer_idx,
            .freq_hz = 50000,
            .clk_cfg = LEDC_AUTO_CLK   // ✅ updated
        };

        ledc_channel_config_t channel = {
            .gpio_num = config->gpio_num,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = config->channel_idx,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = config->timer_idx,
            .duty = 0,
            .hpoint = 0
        };

        ESP_ERROR_CHECK(ledc_timer_config(&timer));
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
    } else {
        bckl_dev->pwm_control = false;
        bckl_dev->index = config->gpio_num;

        // ✅ simpler + safer GPIO init
        ESP_ERROR_CHECK(gpio_reset_pin(config->gpio_num));
        ESP_ERROR_CHECK(gpio_set_direction(config->gpio_num, GPIO_MODE_OUTPUT));
    }

    return (disp_backlight_h)bckl_dev;
}

void disp_backlight_set(disp_backlight_h bckl, int brightness_percent)
{
    if (bckl == NULL)
        return;

    if (brightness_percent > 100)
        brightness_percent = 100;
    if (brightness_percent < 0)
        brightness_percent = 0;

    disp_backlight_t *bckl_dev = (disp_backlight_t *)bckl;

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);

    if (bckl_dev->pwm_control) {
        uint32_t duty = (255 * brightness_percent) / 100;

        ESP_ERROR_CHECK(ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            bckl_dev->index,
            duty));

        ESP_ERROR_CHECK(ledc_update_duty(
            LEDC_LOW_SPEED_MODE,
            bckl_dev->index));
    } else {
        // treat any non-zero as ON
        ESP_ERROR_CHECK(gpio_set_level(
            bckl_dev->index,
            brightness_percent > 0));
    }
}

void disp_backlight_delete(disp_backlight_h bckl)
{
    if (bckl == NULL)
        return;

    disp_backlight_t *bckl_dev = (disp_backlight_t *)bckl;

    if (bckl_dev->pwm_control) {
        ledc_stop(LEDC_LOW_SPEED_MODE, bckl_dev->index, 0);
    } else {
        gpio_reset_pin(bckl_dev->index);
    }

    free(bckl_dev);
}
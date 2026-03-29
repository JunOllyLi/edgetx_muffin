#include "edgetx.h"

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#include "lvgl_helpers.h"
#include "driver/gpio.h"

#define MIN_PWM_DUTY_CYCLE 100
#define BLITE_OFF_DUTY_CYCLE 50

static disp_backlight_h bklite_handle = NULL;
static disp_backlight_config_t bklite_cfg = {
    .pwm_control = true,
    .output_invert = false,
    .gpio_num = BACKLITE_PIN,

    // Relevant only for PWM controlled backlight
    // Ignored for switch (ON/OFF) backlight control
    .timer_idx = 0,
    .channel_idx = 0
};

static bool bkl_enabled = false;
void backlightInit() {
    bklite_handle = disp_backlight_new(&bklite_cfg);
    disp_backlight_set(bklite_handle, MIN_PWM_DUTY_CYCLE);
}

static uint8_t prev_duty_cycle = 0;
void backlightEnable(uint8_t level) {
    uint16_t l = (((uint16_t)level) * (100 - MIN_PWM_DUTY_CYCLE) / BACKLIGHT_LEVEL_MAX) + MIN_PWM_DUTY_CYCLE;
    if (l != prev_duty_cycle) {
        prev_duty_cycle = l;
        TRACE("Set backlight level %d/%d duty cycle %d", level, BACKLIGHT_LEVEL_MAX, l);
        disp_backlight_set(bklite_handle, l);
    }
    bkl_enabled = true;
}

void backlightDisable() {
    if (prev_duty_cycle != BLITE_OFF_DUTY_CYCLE) {
        TRACE("backlight disable");
        prev_duty_cycle = BLITE_OFF_DUTY_CYCLE;
        disp_backlight_set(bklite_handle, BLITE_OFF_DUTY_CYCLE);
    }
    bkl_enabled = false;
}
bool isBacklightEnabled() {
    return bkl_enabled;
}

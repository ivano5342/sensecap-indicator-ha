#ifndef DISPLAY_LOGIC_H
#define DISPLAY_LOGIC_H

/*
 * Pure (hardware-independent) display settings logic: backlight duty
 * calculation, default config, and sleep-timer arithmetic.
 *
 * No ESP-IDF, LEDC, esp_timer or LVGL dependencies; unit tested in tests/host/.
 */

#include <stdbool.h>
#include <stdint.h>

#include "view_data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_BRIGHTNESS_MIN     1
#define DISPLAY_BRIGHTNESS_MAX     99
#define DISPLAY_BRIGHTNESS_DEFAULT 80
/* LEDC duty range for LEDC_TIMER_13_BIT. */
#define DISPLAY_LEDC_DUTY_FULL     (8192 - 1)

/* Clamp a requested brightness to [DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX]. */
int display_brightness_clamp(int brightness);

/* Backlight LEDC duty for a brightness percentage (clamped first). */
uint32_t display_brightness_to_duty(int brightness);

/* Factory defaults used when no config is stored in NVS. */
void display_cfg_default(struct view_data_display* cfg);

/* Sleep timer timeout in microseconds, or 0 when the timer should not run. */
uint64_t display_sleep_timeout_us(bool sleep_mode_en, int sleep_mode_time_min);

/* Sleep minutes from the settings text field: 0 when disabled or empty. */
int display_sleep_minutes_parse(bool sleep_mode_en, const char* text);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_LOGIC_H */

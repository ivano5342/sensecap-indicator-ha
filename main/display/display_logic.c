#include "display_logic.h"

#include <stdlib.h>

int display_brightness_clamp(int brightness) {
	if(brightness > DISPLAY_BRIGHTNESS_MAX)
	{
		return DISPLAY_BRIGHTNESS_MAX;
	}
	if(brightness < DISPLAY_BRIGHTNESS_MIN)
	{
		return DISPLAY_BRIGHTNESS_MIN;
	}
	return brightness;
}

uint32_t display_brightness_to_duty(int brightness) {
	brightness = display_brightness_clamp(brightness);
	return (uint32_t)DISPLAY_LEDC_DUTY_FULL * (uint32_t)brightness / 100U;
}

void display_cfg_default(struct view_data_display* cfg) {
	cfg->brightness = DISPLAY_BRIGHTNESS_DEFAULT;
	cfg->sleep_mode_en = false;
	cfg->sleep_mode_time_min = 0;
}

uint64_t display_sleep_timeout_us(bool sleep_mode_en, int sleep_mode_time_min) {
	if(!sleep_mode_en || sleep_mode_time_min == 0)
	{
		return 0;
	}
	return (uint64_t)sleep_mode_time_min * 60 * 1000000;
}

int display_sleep_minutes_parse(bool sleep_mode_en, const char* text) {
	return (sleep_mode_en && text) ? atoi(text) : 0;
}

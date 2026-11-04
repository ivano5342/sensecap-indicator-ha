#ifndef AQI_H
#define AQI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * US EPA PM2.5 AQI, computed on the device for data that does not carry its own AQI
 * (the PurpleAir cloud API returns PM2.5 but no AQI or colour).
 *
 * Breakpoints are the pre-2024 EPA table (Good ends at 12.0 ug/m3), on purpose: the
 * sensor's own local `pm2.5_aqi` uses them, so the number does not jump when the display
 * switches between local and cloud data. The 2024 table (Good ends at 9.0) is not used.
 *
 * Pure C with no ESP-IDF or LVGL dependency, so scripts/test_aqi.py can build it on the host.
 */

/* AQI (0..500) for a PM2.5 concentration in ug/m3. The input is truncated to 0.1 ug/m3 as
 * EPA specifies; negative input gives 0, input above the table gives 500. */
int aqi_from_pm25(float pm25);

/* EPA category colour for an AQI value, packed as 0xRRGGBB. */
uint32_t aqi_color_rgb(int aqi);

#ifdef __cplusplus
}
#endif

#endif /* AQI_H */

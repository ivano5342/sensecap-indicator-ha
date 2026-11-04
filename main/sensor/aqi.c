#include "aqi.h"

#include <math.h>

typedef struct {
    float c_lo;  /* ug/m3, truncated to 0.1 */
    float c_hi;
    int i_lo;
    int i_hi;
} aqi_breakpoint_t;

/* Pre-2024 EPA PM2.5 breakpoints (see aqi.h for why). */
static const aqi_breakpoint_t s_pm25_table[] = {
    {0.0f, 12.0f, 0, 50},
    {12.1f, 35.4f, 51, 100},
    {35.5f, 55.4f, 101, 150},
    {55.5f, 150.4f, 151, 200},
    {150.5f, 250.4f, 201, 300},
    {250.5f, 350.4f, 301, 400},
    {350.5f, 500.4f, 401, 500},
};

#define AQI_TABLE_LEN (sizeof(s_pm25_table) / sizeof(s_pm25_table[0]))

int aqi_from_pm25(float pm25) {
    if (!(pm25 > 0.0f)) {  /* also catches NaN */
        return 0;
    }
    /* Work in tenths as integers so 12.0 vs 12.1 never falls between two rows. The small
     * epsilon keeps values like 35.4f (stored as 35.39999) from truncating to 35.3. */
    long tenths = (long)floor((double)pm25 * 10.0 + 1e-4);

    for (unsigned i = 0; i < AQI_TABLE_LEN; i++) {
        const aqi_breakpoint_t *b = &s_pm25_table[i];
        long lo = lroundf(b->c_lo * 10.0f);
        long hi = lroundf(b->c_hi * 10.0f);
        if (tenths >= lo && tenths <= hi) {
            double c = tenths / 10.0;
            double aqi = (double)(b->i_hi - b->i_lo) / (b->c_hi - b->c_lo) * (c - b->c_lo) + b->i_lo;
            return (int)lround(aqi);
        }
    }
    return 500;
}

uint32_t aqi_color_rgb(int aqi) {
    if (aqi <= 50) return 0x00E400;   /* Good: green */
    if (aqi <= 100) return 0xFFFF00;  /* Moderate: yellow */
    if (aqi <= 150) return 0xFF7E00;  /* Unhealthy for sensitive groups: orange */
    if (aqi <= 200) return 0xFF0000;  /* Unhealthy: red */
    if (aqi <= 300) return 0x8F3F97;  /* Very unhealthy: purple */
    return 0x7E0023;                  /* Hazardous: maroon */
}

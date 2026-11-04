#ifndef SENSOR_FORMAT_H
#define SENSOR_FORMAT_H

/*
 * Pure (hardware-independent) built-in sensor helpers:
 *  - decoding legacy RP2040 sensor packets ([pkt_type][float LE] ...)
 *  - formatting a reading for the sensor data cards.
 *
 * No ESP-IDF, FreeRTOS or LVGL dependencies; unit tested in tests/host/.
 */

#include <stddef.h>
#include <stdint.h>

#include "view_data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_FORMAT_NA "N/A"

typedef enum {
    SENSOR_PKT_TOO_SHORT = -1, /* fewer than 1 type byte + sizeof(float) */
    SENSOR_PKT_UNKNOWN   = 0,  /* well-formed but not a built-in sensor type */
    SENSOR_PKT_OK        = 1,  /* *type_out / *value_out are valid */
} sensor_pkt_status_t;

/* Map an RP2040 legacy packet type byte to a built-in sensor type. */
int sensor_pkt_type_to_sensor(uint8_t pkt_type, enum sensor_data_type *type_out);

/* Read the (unaligned, native-endian) float payload of a sensor packet. */
float sensor_pkt_read_float(const uint8_t *p);

/* Decode a legacy RP2040 sensor packet. type_out/value_out may be NULL. */
sensor_pkt_status_t sensor_pkt_decode(const uint8_t *p_data, size_t len,
                                      enum sensor_data_type *type_out, float *value_out);

/*
 * Format a sensor reading for display: negative -> "N/A", temperature with one
 * decimal, everything else rounded to an integer.
 */
void sensor_format_value(char *buf, size_t buf_size, enum sensor_data_type type, float value);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_FORMAT_H */

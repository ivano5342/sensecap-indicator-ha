#include "sensor_format.h"

#include <stdio.h>
#include <string.h>

#include "rp2040_protocol.h"

int sensor_pkt_type_to_sensor(uint8_t pkt_type, enum sensor_data_type *type_out)
{
    enum sensor_data_type type;

    switch (pkt_type) {
        case PKT_TYPE_SENSOR_SCD41_CO2:
            type = SCD41_SENSOR_CO2;
            break;
        case PKT_TYPE_SENSOR_SGP40_TVOC_INDEX:
            type = SGP40_SENSOR_TVOC;
            break;
        case PKT_TYPE_SENSOR_SHT41_TEMP:
            type = SHT41_SENSOR_TEMP;
            break;
        case PKT_TYPE_SENSOR_SHT41_HUMIDITY:
            type = SHT41_SENSOR_HUMIDITY;
            break;
        default:
            return 0;
    }

    if (type_out) {
        *type_out = type;
    }
    return 1;
}

float sensor_pkt_read_float(const uint8_t *p)
{
    float value;
    memcpy(&value, p, sizeof(float));
    return value;
}

sensor_pkt_status_t sensor_pkt_decode(const uint8_t *p_data, size_t len,
                                      enum sensor_data_type *type_out, float *value_out)
{
    if (p_data == NULL || len < sizeof(float) + 1) {
        return SENSOR_PKT_TOO_SHORT;
    }

    enum sensor_data_type type;
    if (!sensor_pkt_type_to_sensor(p_data[0], &type)) {
        return SENSOR_PKT_UNKNOWN;
    }

    if (type_out) {
        *type_out = type;
    }
    if (value_out) {
        *value_out = sensor_pkt_read_float(p_data + 1);
    }
    return SENSOR_PKT_OK;
}

void sensor_format_value(char *buf, size_t buf_size, enum sensor_data_type type, float value)
{
    const char *format_style;

    if (buf == NULL || buf_size == 0) {
        return;
    }

    if (value < 0) {
        snprintf(buf, buf_size, SENSOR_FORMAT_NA);
        return;
    }

    switch (type) {
        case SHT41_SENSOR_TEMP:
            format_style = "%.1f";
            break;
        case SCD41_SENSOR_CO2:
        case SGP40_SENSOR_TVOC:
        case SHT41_SENSOR_HUMIDITY:
        default:
            format_style = "%.0f";
            break;
    }
    snprintf(buf, buf_size, format_style, value);
}

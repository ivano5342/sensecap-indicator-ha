#ifndef NETWORK_SENSOR_CLOUD_PARSE_H
#define NETWORK_SENSOR_CLOUD_PARSE_H

#include <stdbool.h>
#include <stdint.h>
#include "view_data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parser for the PurpleAir cloud API reply to
 *   GET https://api.purpleair.com/v1/sensors/<sensor_index>?fields=<NETWORK_SENSOR_CLOUD_FIELDS>
 *
 * Reply shape (PurpleAir API docs, checked 2026-11-04):
 *   { "time_stamp": <unix s>, "data_time_stamp": <unix s>,
 *     "sensor": { "sensor_index": N, "last_seen": <unix s>, "pm2.5_atm_a": 3.2, ... } }
 *
 * The cloud has no AQI or AQI colour fields, so the parser computes both per channel from
 * PM2.5 ATM (see aqi.h). It also has no equivalent of the local `gas_680` field.
 *
 * Pure C plus cJSON, no ESP-IDF calls, so scripts/test_network_sensor_cloud.py can build it
 * on the host.
 */

/* Only these fields are requested: PurpleAir bills per field. */
#define NETWORK_SENSOR_CLOUD_FIELDS \
    "pm1.0_atm_a,pm1.0_atm_b,pm2.5_atm_a,pm2.5_atm_b,pm10.0_atm_a,pm10.0_atm_b,last_seen"

#define NETWORK_SENSOR_CLOUD_MAX_VALUES 10

typedef struct {
    enum sensor_data_type type;
    float value;  /* AQI colours are 0xRRGGBB bit-cast into the float, like the local parser */
} network_sensor_cloud_value_t;

typedef struct {
    network_sensor_cloud_value_t values[NETWORK_SENSOR_CLOUD_MAX_VALUES];
    int value_count;
    int64_t age_s;   /* time_stamp - last_seen: how old the sensor's last upload is */
} network_sensor_cloud_reading_t;

#define NETWORK_SENSOR_CLOUD_PARSE_OK          0
#define NETWORK_SENSOR_CLOUD_PARSE_ERR_JSON   -1  /* not JSON, or no "sensor" object */
#define NETWORK_SENSOR_CLOUD_PARSE_ERR_API    -2  /* the API returned an "error" field */
#define NETWORK_SENSOR_CLOUD_PARSE_ERR_NODATA -3  /* no PM field or no time stamps */

/* Parse one reply. On NETWORK_SENSOR_CLOUD_PARSE_ERR_API, `err` (if not NULL) gets the API's
 * error name, e.g. "ApiKeyInvalidError". */
int network_sensor_cloud_parse(const char *json, network_sensor_cloud_reading_t *out,
                               char *err, int err_len);

/* Pack 0xRRGGBB into a float the way the local parser and the view expect. */
float network_sensor_pack_rgb(uint32_t rgb);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_SENSOR_CLOUD_PARSE_H */

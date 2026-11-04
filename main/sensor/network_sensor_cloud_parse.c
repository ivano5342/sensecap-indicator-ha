#include "network_sensor_cloud_parse.h"
#include "aqi.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *json_key;
    enum sensor_data_type sensor_type;
} cloud_field_t;

/* Same sensor types as the local field map, so the view needs no cloud-specific code. */
static const cloud_field_t s_cloud_fields[] = {
    {"pm1.0_atm_a", NETWORK_SENSOR_PM1_0_A},
    {"pm2.5_atm_a", NETWORK_SENSOR_PM2_5_A},
    {"pm10.0_atm_a", NETWORK_SENSOR_PM10_0_A},
    {"pm1.0_atm_b", NETWORK_SENSOR_PM1_0_B},
    {"pm2.5_atm_b", NETWORK_SENSOR_PM2_5_B},
    {"pm10.0_atm_b", NETWORK_SENSOR_PM10_0_B},
};

#define CLOUD_FIELD_COUNT (sizeof(s_cloud_fields) / sizeof(s_cloud_fields[0]))

float network_sensor_pack_rgb(uint32_t rgb) {
    float f;
    memcpy(&f, &rgb, sizeof(f));
    return f;
}

static void _add(network_sensor_cloud_reading_t *out, enum sensor_data_type type, float value) {
    if (out->value_count < NETWORK_SENSOR_CLOUD_MAX_VALUES) {
        out->values[out->value_count].type = type;
        out->values[out->value_count].value = value;
        out->value_count++;
    }
}

static void _add_aqi(network_sensor_cloud_reading_t *out, float pm25,
                     enum sensor_data_type aqi_type, enum sensor_data_type color_type) {
    int aqi = aqi_from_pm25(pm25);
    _add(out, aqi_type, (float)aqi);
    _add(out, color_type, network_sensor_pack_rgb(aqi_color_rgb(aqi)));
}

int network_sensor_cloud_parse(const char *json, network_sensor_cloud_reading_t *out,
                               char *err, int err_len) {
    if (out == NULL) {
        return NETWORK_SENSOR_CLOUD_PARSE_ERR_JSON;
    }
    memset(out, 0, sizeof(*out));
    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    if (json == NULL) {
        return NETWORK_SENSOR_CLOUD_PARSE_ERR_JSON;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return NETWORK_SENSOR_CLOUD_PARSE_ERR_JSON;
    }

    int ret = NETWORK_SENSOR_CLOUD_PARSE_OK;
    const cJSON *api_err = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *sensor = cJSON_GetObjectItemCaseSensitive(root, "sensor");
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "time_stamp");

    if (api_err != NULL) {
        if (err != NULL && err_len > 0 && cJSON_IsString(api_err)) {
            snprintf(err, err_len, "%s", api_err->valuestring);
        }
        ret = NETWORK_SENSOR_CLOUD_PARSE_ERR_API;
        goto done;
    }
    if (!cJSON_IsObject(sensor)) {
        ret = NETWORK_SENSOR_CLOUD_PARSE_ERR_JSON;
        goto done;
    }

    const cJSON *last_seen = cJSON_GetObjectItemCaseSensitive(sensor, "last_seen");
    if (!cJSON_IsNumber(ts) || !cJSON_IsNumber(last_seen)) {
        ret = NETWORK_SENSOR_CLOUD_PARSE_ERR_NODATA;
        goto done;
    }
    out->age_s = (int64_t)ts->valuedouble - (int64_t)last_seen->valuedouble;
    if (out->age_s < 0) {
        out->age_s = 0;
    }

    bool have_a = false, have_b = false;
    float pm25_a = 0.0f, pm25_b = 0.0f;
    for (unsigned i = 0; i < CLOUD_FIELD_COUNT; i++) {
        const cJSON *f = cJSON_GetObjectItemCaseSensitive(sensor, s_cloud_fields[i].json_key);
        if (!cJSON_IsNumber(f)) {
            continue;  /* a channel can be missing, e.g. a single-channel sensor */
        }
        float v = (float)f->valuedouble;
        _add(out, s_cloud_fields[i].sensor_type, v);
        if (s_cloud_fields[i].sensor_type == NETWORK_SENSOR_PM2_5_A) {
            pm25_a = v;
            have_a = true;
        } else if (s_cloud_fields[i].sensor_type == NETWORK_SENSOR_PM2_5_B) {
            pm25_b = v;
            have_b = true;
        }
    }
    if (!have_a && !have_b) {
        ret = NETWORK_SENSOR_CLOUD_PARSE_ERR_NODATA;
        goto done;
    }
    if (have_a) {
        _add_aqi(out, pm25_a, NETWORK_SENSOR_PM25_AQI_A, NETWORK_SENSOR_AQI_COLOR_A);
    }
    if (have_b) {
        _add_aqi(out, pm25_b, NETWORK_SENSOR_PM25_AQI_B, NETWORK_SENSOR_AQI_COLOR_B);
    }

done:
    cJSON_Delete(root);
    return ret;
}

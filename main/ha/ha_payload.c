#include "ha_payload.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "home_assistant_config.h"

int ha_payload_topic_matches(const char *expected_topic, const char *topic, int topic_len)
{
    if (expected_topic == NULL || topic == NULL || topic_len < 0) {
        return 0;
    }
    return strncmp(topic, expected_topic, (size_t)topic_len) == 0;
}

/* Parse data and return the root if it has a named first member. */
static cJSON *_parse_root(const char *data, int data_len)
{
    if (data == NULL || data_len < 0) {
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(data, (size_t)data_len);
    if (root == NULL || root->child == NULL || root->child->string == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

ha_payload_status_t ha_payload_parse_switch(const char *expected_topic,
                                            const char *const *keys, int key_count,
                                            const char *topic, int topic_len,
                                            const char *data, int data_len,
                                            struct view_data_ha_switch_data *out)
{
    if (!ha_payload_topic_matches(expected_topic, topic, topic_len)) {
        return HA_PAYLOAD_TOPIC_MISMATCH;
    }

    cJSON *root = _parse_root(data, data_len);
    if (root == NULL) {
        return HA_PAYLOAD_INVALID_JSON;
    }

    const char *first_key = root->child->string;
    size_t key_len = strlen(first_key);

    for (int i = 0; i < key_count; i++) {
        if (strncmp(first_key, keys[i], key_len) == 0) {
            cJSON *item = cJSON_GetObjectItem(root, keys[i]);
            if (item != NULL && cJSON_IsNumber(item)) {
                if (out) {
                    out->index = (uint8_t)i;
                    out->value = item->valueint;
                }
                cJSON_Delete(root);
                return HA_PAYLOAD_OK;
            }
        }
    }

    cJSON_Delete(root);
    return HA_PAYLOAD_NO_MATCH;
}

ha_payload_status_t ha_payload_parse_sensor(const char *expected_topic,
                                            const char *const *keys, int key_count,
                                            const char *topic, int topic_len,
                                            const char *data, int data_len,
                                            struct view_data_ha_sensor_data *out)
{
    if (!ha_payload_topic_matches(expected_topic, topic, topic_len)) {
        return HA_PAYLOAD_TOPIC_MISMATCH;
    }

    cJSON *root = _parse_root(data, data_len);
    if (root == NULL) {
        return HA_PAYLOAD_INVALID_JSON;
    }

    const char *first_key = root->child->string;
    size_t key_len = strlen(first_key);

    for (int i = 0; i < key_count; i++) {
        if (strncmp(first_key, keys[i], key_len) == 0) {
            cJSON *item = cJSON_GetObjectItem(root, first_key);
            if (item != NULL && cJSON_IsString(item)) {
                if (out) {
                    memset(out, 0, sizeof(*out));
                    out->index = (uint8_t)i;
                    strncpy(out->value, item->valuestring, sizeof(out->value) - 1);
                }
                cJSON_Delete(root);
                return HA_PAYLOAD_OK;
            }
        }
    }

    cJSON_Delete(root);
    return HA_PAYLOAD_NO_MATCH;
}

static int _checked_len(int len, size_t buf_size)
{
    if (len > 0 && (size_t)len < buf_size) {
        return len;
    }
    return HA_PAYLOAD_FORMAT_ERROR;
}

int ha_payload_format_switch_state(char *buf, size_t buf_size, const char *key, int value)
{
    if (buf == NULL || buf_size == 0 || key == NULL) {
        return HA_PAYLOAD_FORMAT_ERROR;
    }
    int len = snprintf(buf, buf_size, "{\"%s\": %d}", key, value);
    return _checked_len(len, buf_size);
}

int ha_payload_format_sensor(char *buf, size_t buf_size, enum sensor_data_type type,
                             float value, const char **key_out)
{
    const char *key = NULL;
    int len = 0;

    if (buf == NULL || buf_size == 0) {
        return HA_PAYLOAD_FORMAT_ERROR;
    }

    switch (type) {
        case SCD41_SENSOR_CO2:
            key = CONFIG_SENSOR_BUILDIN_CO2_VALUE_KEY;
            len = snprintf(buf, buf_size, "{\"%s\":\"%d\"}", key, (int)value);
            break;
        case SGP40_SENSOR_TVOC:
            key = CONFIG_SENSOR_BUILDIN_TVOC_VALUE_KEY;
            len = snprintf(buf, buf_size, "{\"%s\":\"%d\"}", key, (int)value);
            break;
        case SHT41_SENSOR_TEMP:
            key = CONFIG_SENSOR_BUILDIN_TEMP_VALUE_KEY;
            len = snprintf(buf, buf_size, "{\"%s\":\"%.1f\"}", key, value);
            break;
        case SHT41_SENSOR_HUMIDITY:
            key = CONFIG_SENSOR_BUILDIN_HUMIDITY_VALUE_KEY;
            len = snprintf(buf, buf_size, "{\"%s\":\"%d\"}", key, (int)value);
            break;
        default:
            return HA_PAYLOAD_UNKNOWN_SENSOR;
    }

    if (key_out) {
        *key_out = key;
    }
    return _checked_len(len, buf_size);
}

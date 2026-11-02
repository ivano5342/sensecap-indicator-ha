#include "ha_sensor.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "home_assistant_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define MAX_DATA_BUF_LEN 64

static const char *TAG = "ha-sensor";

typedef struct ha_sensor_entity {
    int index;
    char *key;
    char *topic;
    int qos;
} ha_sensor_entity_t;

static ha_sensor_entity_t ha_sensor_entities[CONFIG_HA_SENSOR_ENTITY_NUM];

static void publish_sensor_data(const struct view_data_sensor_data *sensor_data)
{
    if (!mqtt_ha_instance.mqtt_connected_flag) {
        return;
    }

    char data_buf[MAX_DATA_BUF_LEN];
    int len = 0;
    const char *topic = CONFIG_SENSOR_BUILDIN_TOPIC_DATA;
    const char *key = NULL;

    switch (sensor_data->sensor_type) {
        case SCD41_SENSOR_CO2:
            key = CONFIG_SENSOR_BUILDIN_CO2_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SGP40_SENSOR_TVOC:
            key = CONFIG_SENSOR_BUILDIN_TVOC_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SHT41_SENSOR_TEMP:
            key = CONFIG_SENSOR_BUILDIN_TEMP_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SHT41_SENSOR_HUMIDITY:
            key = CONFIG_SENSOR_BUILDIN_HUMIDITY_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SEN66_SENSOR_PM1_0:
            key = CONFIG_SENSOR_SEN66_PM1_0_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SEN66_SENSOR_PM2_5:
            key = CONFIG_SENSOR_SEN66_PM2_5_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SEN66_SENSOR_PM4_0:
            key = CONFIG_SENSOR_SEN66_PM4_0_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SEN66_SENSOR_PM10_0:
            key = CONFIG_SENSOR_SEN66_PM10_0_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SEN66_SENSOR_VOC:
            key = CONFIG_SENSOR_SEN66_VOC_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SEN66_SENSOR_NOX:
            key = CONFIG_SENSOR_SEN66_NOX_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SEN66_SENSOR_CO2:
            key = CONFIG_SENSOR_SEN66_CO2_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        default:
            ESP_LOGW(TAG, "Unknown sensor type: %d", sensor_data->sensor_type);
            return;
    }

    if (len > 0 && len < MAX_DATA_BUF_LEN) {
        esp_mqtt_client_publish(mqtt_ha_instance.mqtt_client, topic, data_buf, len, 0, 0);
    } else {
        ESP_LOGE(TAG, "Failed to format sensor data");
    }
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id == VIEW_EVENT_SENSOR_DATA) {
        ESP_LOGI(TAG, "event: VIEW_EVENT_SENSOR_DATA");
        publish_sensor_data((struct view_data_sensor_data *)event_data);
    }
}

void ha_sensor_init(void)
{
    const char *sensor_keys[] = CONFIG_SENSOR_VALUE_KEYS;
    const char *sensor_topics[] = CONFIG_SENSOR_TOPICS;

    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        ha_sensor_entities[i] = (ha_sensor_entity_t){
            .index = i,
            .key = (char *)sensor_keys[i],
            .topic = (char *)sensor_topics[i],
            .qos = CONFIG_TOPIC_SENSOR_DATA_QOS,
        };
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA, view_event_handler, NULL, NULL));
}

void ha_sensor_subscribe(esp_mqtt_client_handle_t client)
{
    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        int msg_id = esp_mqtt_client_subscribe(client, ha_sensor_entities[i].topic, ha_sensor_entities[i].qos);
        ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_sensor_entities[i].topic, msg_id);
    }
}

int ha_sensor_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len)
{
    if (strncmp(topic, ha_sensor_entities[0].topic, topic_len) != 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL || root->child == NULL || root->child->string == NULL) {
        ESP_LOGE(TAG, "Invalid JSON structure");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *c_key = root->child;

    // The device receives its own SEN66 publishes back; nothing consumes them here.
    if (strncmp(c_key->string, CONFIG_SENSOR_SEN66_KEY_PREFIX, strlen(CONFIG_SENSOR_SEN66_KEY_PREFIX)) == 0) {
        cJSON_Delete(root);
        return 0;
    }

    ESP_LOGI(TAG, "c_key = %s", c_key->string);
    size_t key_len = strlen(c_key->string);

    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        if (strncmp(c_key->string, ha_sensor_entities[i].key, key_len) == 0) {
            cJSON *cjson_item = cJSON_GetObjectItem(root, c_key->string);
            if (cjson_item != NULL && cJSON_IsString(cjson_item)) {
                struct view_data_ha_sensor_data sensor_data = {.index = i};
                strncpy(sensor_data.value, cjson_item->valuestring, sizeof(sensor_data.value) - 1);

                ESP_LOGI(TAG, "MQTT message: sensor %s is %s", ha_sensor_entities[i].key, sensor_data.value);
                /*
                 * NOTE: VIEW_EVENT_HA_SENSOR currently has NO consumer, so this
                 * value is parsed and posted but never shown. Wiring it up turns
                 * the Indicator into a generic HA dashboard: display ANY entity
                 * pushed from Home Assistant over MQTT, with no physical sensor
                 * attached to the device (the use case in issue #5).
                 *
                 * To wire it up:
                 *   1. ha_switch_screen.c::_create_sensor_card already builds the
                 *      "N/A" data labels on NAV_TILE_HA_MIX but discards the handle.
                 *      Store each label keyed by card index (cf. the switch_slot_t
                 *      pattern).
                 *   2. Register a VIEW_EVENT_HA_SENSOR handler that, under the LVGL
                 *      lock, sets the label text from labels[data->index] and data->value.
                 *   3. Make sensor_data.index here map to the intended card slot.
                 */
                /*
                 * Runs in mqtt_task, which holds the MQTT client lock. Never wait for
                 * view queue space here: view_event_task may itself be waiting for that
                 * lock in esp_mqtt_client_publish, and the two would deadlock.
                 */
                if (esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR, &sensor_data, sizeof(sensor_data), 0) != ESP_OK) {
                    ESP_LOGW(TAG, "view queue full, dropped sensor %s", ha_sensor_entities[i].key);
                }
                cJSON_Delete(root);
                return 0;
            }
        }
    }

    ESP_LOGW(TAG, "No matching sensor topic or invalid JSON structure");
    cJSON_Delete(root);
    return -1;
}

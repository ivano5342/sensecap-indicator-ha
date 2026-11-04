#include "ha_sensor.h"
#include "ha_mqtt.h"
#include "ha_payload.h"
#include "view_data.h"
#include "home_assistant_config.h"
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
    const char *topic = CONFIG_SENSOR_BUILDIN_TOPIC_DATA;
    int len = ha_payload_format_sensor(data_buf, sizeof(data_buf), sensor_data->sensor_type, sensor_data->value, NULL);

    if (len == HA_PAYLOAD_UNKNOWN_SENSOR) {
        ESP_LOGW(TAG, "Unknown sensor type: %d", sensor_data->sensor_type);
        return;
    }

    if (len > 0) {
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
    const char *sensor_keys[] = CONFIG_SENSOR_VALUE_KEYS;
    struct view_data_ha_sensor_data sensor_data = {0};

    ha_payload_status_t st = ha_payload_parse_sensor(ha_sensor_entities[0].topic,
                                                     sensor_keys, CONFIG_HA_SENSOR_ENTITY_NUM,
                                                     topic, topic_len, data, data_len, &sensor_data);
    switch (st) {
        case HA_PAYLOAD_OK:
            ESP_LOGI(TAG, "MQTT message: sensor %s is %s", ha_sensor_entities[sensor_data.index].key, sensor_data.value);
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
             *      lock, sets labels[data->index] text to data->value
             *      (lv_label_set_text).
             *   3. Make sensor_data.index here map to the intended card slot.
             */
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR, &sensor_data, sizeof(sensor_data), portMAX_DELAY);
            return 0;
        case HA_PAYLOAD_TOPIC_MISMATCH:
            return -1;
        case HA_PAYLOAD_INVALID_JSON:
            ESP_LOGE(TAG, "Invalid JSON structure");
            return -1;
        default:
            ESP_LOGW(TAG, "No matching sensor topic or invalid JSON structure");
            return -1;
    }
}

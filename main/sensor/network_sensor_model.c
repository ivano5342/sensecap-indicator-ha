#include "network_sensor_model.h"
#include "sensor_model.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "view_data.h"
#include "network_sensor_http.h"

static const char* TAG = "network-sensor-model";

/* Set to 1 to inject fake AQI values for testing. Leave 0 for normal use. */
#define TEST_INJECT_AQI  0

static SemaphoreHandle_t _g_network_sensors_data_mutex = NULL;
static SensorData _g_network_sensor_cache[ENUM_SENSOR_ALL];
static esp_event_loop_handle_t _g_view_event_handle = NULL;
static bool _g_stale = false;

/**
 * @brief Initialize a network sensor data entry
 */
static int _init_network_sensor_data(SensorData* sensor, enum sensor_data_type type) {
    if (sensor == NULL || type >= ENUM_SENSOR_ALL) {
        return NETWORK_SENSOR_ERR_INVALID_TYPE;
    }
    sensor->type = type;
    sensor->value = 0.0f;
    sensor->status = SENSOR_STATUS_INITED;
    return NETWORK_SENSOR_OK;
}

/**
 * @brief WiFi state change event handler
 * Starts polling when WiFi connects, stops when disconnected
 */
static void _network_sensor_wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base,
                                                int32_t event_id, void* event_data) {
    if (event_base != VIEW_EVENT_BASE || event_id != VIEW_EVENT_WIFI_ST) {
        return;
    }

    struct view_data_wifi_st* wifi_st = (struct view_data_wifi_st*)event_data;
    
    if (wifi_st->is_network) {
        ESP_LOGI(TAG, "Network up, starting network sensor polling");
        network_sensor_polling_start();
    } else {
        ESP_LOGI(TAG, "Network down, stopping network sensor polling");
        network_sensor_polling_stop();
    }
}

/**
 * @brief Initialize the network sensor model
 */
void network_sensor_model_init(esp_event_loop_handle_t event_handle) {
    if (_g_network_sensors_data_mutex != NULL) {
        ESP_LOGW(TAG, "network_sensor_model_init already called");
        return;
    }

    _g_view_event_handle = event_handle;

    // Create semaphore for thread-safe cache access
    _g_network_sensors_data_mutex = xSemaphoreCreateMutex();
    if (_g_network_sensors_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex semaphore");
        return;
    }

    // Initialize all network sensor cache entries
    for (int i = 0; i < ENUM_SENSOR_ALL; i++) {
        if (_init_network_sensor_data(&_g_network_sensor_cache[i], i) != NETWORK_SENSOR_OK) {
            ESP_LOGE(TAG, "Failed to initialize sensor type %d", i);
        }
    }

    // Subscribe to WiFi state changes
    esp_event_handler_register_with(event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                     _network_sensor_wifi_event_handler, NULL);

#if TEST_INJECT_AQI
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGW(TAG, "TEST: injecting fake AQI values");
    network_sensor_update(NETWORK_SENSOR_PM25_AQI_A, 75.0f);
    network_sensor_update(NETWORK_SENSOR_AQI_COLOR_A, 0xFFA500); // orange
    network_sensor_update(NETWORK_SENSOR_PM25_AQI_B, 398.0f);
    network_sensor_update(NETWORK_SENSOR_AQI_COLOR_B, 0x301934); // purple
#endif

    ESP_LOGI(TAG, "network_sensor_model_init complete");
    // Initialize HTTP polling infrastructure
    network_sensor_http_init();
}

/**
 * @brief Update a network sensor value and post event
 */
int network_sensor_update(enum sensor_data_type type, float value) {
    if (type >= ENUM_SENSOR_ALL || _g_network_sensors_data_mutex == NULL) {
        return NETWORK_SENSOR_ERR_INVALID_TYPE;
    }

    if (xSemaphoreTake(_g_network_sensors_data_mutex, portMAX_DELAY) != pdTRUE) {
        return NETWORK_SENSOR_ERR_MUTEX_FAIL;
    }

    _g_network_sensor_cache[type].value = value;
    _g_network_sensor_cache[type].status = SENSOR_STATUS_OK;

    xSemaphoreGive(_g_network_sensors_data_mutex);

    // Post event to view event handle
    struct view_data_sensor_data v_data = {
        .sensor_type = type,
        .value = value,
    };

    esp_event_post_to(_g_view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_NETWORK_SENSOR_DATA,
                      &v_data, sizeof(struct view_data_sensor_data), portMAX_DELAY);

    return NETWORK_SENSOR_OK;
}

/**
 * @brief Mark the network sensor data as stale (or fresh again)
 */
void network_sensor_set_stale(bool is_stale) {
    if (_g_network_sensors_data_mutex == NULL || is_stale == _g_stale) {
        return;
    }
    _g_stale = is_stale;
    ESP_LOGW(TAG, "Network sensor data is %s", is_stale ? "STALE" : "fresh again");

    struct view_data_network_sensor_stale v_data = { .is_stale = is_stale };
    esp_event_post_to(_g_view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_NETWORK_SENSOR_STALE,
                      &v_data, sizeof(v_data), pdMS_TO_TICKS(100));  /* may run on the event loop task: never block forever */
}

/**
 * @brief Get current value of a network sensor
 */
float network_sensor_get_value(enum sensor_data_type type) {
    if (type >= ENUM_SENSOR_ALL || _g_network_sensors_data_mutex == NULL) {
        return 0.0f;
    }

    if (xSemaphoreTake(_g_network_sensors_data_mutex, portMAX_DELAY) != pdTRUE) {
        return 0.0f;
    }

    float value = _g_network_sensor_cache[type].value;
    xSemaphoreGive(_g_network_sensors_data_mutex);

    return value;
}

/**
 * @brief Start the HTTP polling task (implemented in network_sensor_http.c)
 */
void network_sensor_polling_start(void) {
    network_sensor_http_polling_start();
}

/**
 * @brief Stop the HTTP polling task (implemented in network_sensor_http.c)
 */
void network_sensor_polling_stop(void) {
    network_sensor_http_polling_stop();
}

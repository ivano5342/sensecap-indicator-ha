#include "network_sensor_http.h"
#include "network_sensor_model.h"
#include "network_sensor_config.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

static const char* TAG = "network-sensor-http";

static TaskHandle_t _g_polling_task_handle = NULL;
static volatile bool _g_polling_enabled = false;

/* Freshness tracking. Only the polling task touches these, except _g_polling_stop_pending. */
static int64_t _g_last_ok_us = 0;       /* last good poll, or first poll attempt if none yet */
static bool _g_have_data = false;       /* at least one good poll since boot */
static int _g_consecutive_failures = 0;

/* ── HTTP Response Buffer ────────────────────────────────────────────────── */
#define HTTP_RESPONSE_BUFFER_SIZE 4096
static char _g_http_response_buffer[HTTP_RESPONSE_BUFFER_SIZE] = {0};
static size_t _g_http_response_len = 0;
static bool _g_http_response_overflow = false;

/**
 * @brief HTTP client event handler
 * Accumulates response data into a buffer
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP connected");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP header sent");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP header: key=%s, value=%s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            // Accumulate response data
            if (_g_http_response_len + evt->data_len < HTTP_RESPONSE_BUFFER_SIZE) {
                memcpy(_g_http_response_buffer + _g_http_response_len, evt->data, evt->data_len);
                _g_http_response_len += evt->data_len;
            } else {
                _g_http_response_overflow = true;
                ESP_LOGE(TAG, "HTTP response too large, buffer overflow");
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP request finished");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP disconnected");
            break;

        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Parse JSON response and update network sensors
 */
static void _parse_json_response(const char* json_str) {
    if (json_str == NULL || strlen(json_str) == 0) {
        ESP_LOGE(TAG, "Invalid JSON string");
        return;
    }

    cJSON *json_root = cJSON_Parse(json_str);
    if (json_root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return;
    }

    // Iterate through field mappings and extract values
    for (int i = 0; network_sensor_field_map[i].json_key != NULL; i++) {
        const network_sensor_field_mapping_t *field = &network_sensor_field_map[i];

        cJSON *json_field = cJSON_GetObjectItemCaseSensitive(json_root, field->json_key);
        if (json_field == NULL) {
            ESP_LOGW(TAG, "JSON field '%s' not found", field->json_key);
            continue;
        }

        float value;

        if (cJSON_IsString(json_field)) {
            // Parse "rgb(r,g,b)" color strings - packed as RGB uint32_t reinterpreted as float
            const char* str = json_field->valuestring;
            uint8_t r, g, b;
            if (sscanf(str, "rgb(%hhu,%hhu,%hhu)", &r, &g, &b) == 3) {
                uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                value = *(float*)&rgb;
            } else {
                ESP_LOGW(TAG, "JSON field '%s' is not a valid rgb string", field->json_key);
                continue;
            }
        } else if (cJSON_IsNumber(json_field)) {
            value = (float)json_field->valuedouble;
        } else {
            ESP_LOGW(TAG, "JSON field '%s' is not a number or string", field->json_key);
            continue;
        }

        // Update sensor via model
        int ret = network_sensor_update(field->sensor_type, value);
        if (ret != NETWORK_SENSOR_OK) {
            ESP_LOGE(TAG, "Failed to update sensor %d: %d", field->sensor_type, ret);
        } else {
            ESP_LOGD(TAG, "Updated %s: %.2f %s", field->display_label, value, field->unit);
        }
    }

    cJSON_Delete(json_root);
}

/**
 * @brief One poll: fresh client per request so a bad connection never carries over
 */
static bool _poll_once(const char *url) {
    esp_http_client_config_t http_config = {
        .url = url,
        .event_handler = _http_event_handler,
        .timeout_ms = NETWORK_SENSOR_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&http_config);
    if (http_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    _g_http_response_len = 0;
    _g_http_response_overflow = false;

    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = esp_http_client_perform(http_client);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    bool ok = false;
    if (ret != ESP_OK) {
        /* The sensor stalls for 20-30 s at a time; a timeout is expected, anything else is not */
        if (ret == ESP_ERR_HTTP_EAGAIN || ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "HTTP request timed out: %s (%d ms, rssi=%d, heap=%u)",
                     esp_err_to_name(ret), ms, rssi, (unsigned)esp_get_free_heap_size());
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s (errno=%d, %d ms, rssi=%d, heap=%u)",
                     esp_err_to_name(ret), esp_http_client_get_errno(http_client), ms, rssi,
                     (unsigned)esp_get_free_heap_size());
        }
    } else {
        int status_code = esp_http_client_get_status_code(http_client);
        if (status_code != 200) {
            ESP_LOGE(TAG, "HTTP error status: %d", status_code);
        } else if (_g_http_response_overflow) {
            ESP_LOGE(TAG, "Response truncated, skipping parse");
        } else {
            _g_http_response_buffer[_g_http_response_len] = '\0';
            ESP_LOGI(TAG, "HTTP 200, %d bytes, %d ms, rssi=%d", (int)_g_http_response_len, ms, rssi);
            _parse_json_response(_g_http_response_buffer);
            ok = true;
        }
    }

    esp_http_client_cleanup(http_client);
    return ok;
}

/**
 * @brief Long-lived polling task.
 * Sleeps until enabled; start/stop notify it so state changes take effect immediately.
 */
static void _network_sensor_http_task(void *arg) {
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s",
             NETWORK_SENSOR_HOST, NETWORK_SENSOR_PORT, NETWORK_SENSOR_PATH);

    while (true) {
        if (!_g_polling_enabled) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (_g_last_ok_us == 0) {
            _g_last_ok_us = esp_timer_get_time();
        }
        if (_poll_once(url)) {
            _g_last_ok_us = esp_timer_get_time();
            _g_have_data = true;
            if (_g_consecutive_failures > 0) {
                ESP_LOGI(TAG, "Recovered after %d failed poll(s)", _g_consecutive_failures);
            }
            _g_consecutive_failures = 0;
            network_sensor_set_stale(false);
        } else {
            _g_consecutive_failures++;
            int64_t age_s = (esp_timer_get_time() - _g_last_ok_us) / 1000000;
            if (age_s >= NETWORK_SENSOR_STALE_AFTER_SEC) {
                network_sensor_set_stale(true);
            }
        }

        // Wait for next interval; a start/stop notification wakes us early
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NETWORK_SENSOR_POLL_INTERVAL_SEC * 1000));
    }
}

void network_sensor_http_init(void) {
    if (_g_polling_task_handle != NULL) {
        return;
    }
    BaseType_t ret = xTaskCreate(_network_sensor_http_task, "net_sensor_http",
                                 5120, NULL, 5, &_g_polling_task_handle);
    if (ret != pdPASS) {
        _g_polling_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create HTTP polling task");
        return;
    }
    ESP_LOGI(TAG, "HTTP polling initialized");
}

void network_sensor_http_polling_start(void) {
    if (_g_polling_task_handle == NULL) {
        ESP_LOGE(TAG, "HTTP polling not initialized");
        return;
    }
    if (_g_polling_enabled) {
        return;
    }
    _g_polling_enabled = true;
    xTaskNotifyGive(_g_polling_task_handle);
    ESP_LOGI(TAG, "HTTP polling enabled");
}

void network_sensor_http_polling_stop(void) {
    if (_g_polling_task_handle == NULL || !_g_polling_enabled) {
        return;
    }
    _g_polling_enabled = false;
    xTaskNotifyGive(_g_polling_task_handle);
    if (_g_have_data) {
        network_sensor_set_stale(true);  /* no polling means no refresh; values are kept */
    }
    ESP_LOGI(TAG, "HTTP polling disabled");
}

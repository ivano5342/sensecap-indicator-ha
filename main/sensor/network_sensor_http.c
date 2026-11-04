#include "network_sensor_http.h"
#include "network_sensor_model.h"
#include "network_sensor_config.h"
#include "network_sensor_cloud.h"
#include "network_sensor_cloud_parse.h"
#include "esp_crt_bundle.h"
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
#include <math.h>

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
 * @brief One GET into _g_http_response_buffer: fresh client per request so a bad connection
 * never carries over. Returns false if the request itself failed (no HTTP status).
 * `api_key` (may be NULL) is sent as X-API-Key; an https:// URL uses the certificate bundle.
 */
static bool _http_get(const char *url, const char *api_key, int timeout_ms, int *status_code) {
    esp_http_client_config_t http_config = {
        .url = url,
        .event_handler = _http_event_handler,
        .timeout_ms = timeout_ms,
        .buffer_size = 2048,
        .crt_bundle_attach = (strncmp(url, "https://", 8) == 0) ? esp_crt_bundle_attach : NULL,
    };

    *status_code = 0;
    esp_http_client_handle_t http_client = esp_http_client_init(&http_config);
    if (http_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }
    if (api_key != NULL && api_key[0] != '\0') {
        esp_http_client_set_header(http_client, "X-API-Key", api_key);
    }

    _g_http_response_len = 0;
    _g_http_response_overflow = false;

    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = esp_http_client_perform(http_client);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    bool performed = false;
    if (ret != ESP_OK) {
        /* The local sensor stalls ~36 s every 120 s; a timeout is expected, anything else is not */
        if (ret == ESP_ERR_HTTP_EAGAIN || ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "HTTP request timed out: %s (%d ms, rssi=%d, heap=%u)",
                     esp_err_to_name(ret), ms, rssi, (unsigned)esp_get_free_heap_size());
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s (errno=%d, %d ms, rssi=%d, heap=%u)",
                     esp_err_to_name(ret), esp_http_client_get_errno(http_client), ms, rssi,
                     (unsigned)esp_get_free_heap_size());
        }
    } else {
        performed = true;
        *status_code = esp_http_client_get_status_code(http_client);
        _g_http_response_buffer[_g_http_response_len] = '\0';
        ESP_LOGI(TAG, "HTTP %d, %d bytes, %d ms, rssi=%d", *status_code, (int)_g_http_response_len, ms, rssi);
    }

    esp_http_client_cleanup(http_client);
    return performed;
}

/**
 * @brief One local poll of the sensor's /json
 */
static bool _poll_local(const char *url) {
    int status_code;
    if (!_http_get(url, NULL, NETWORK_SENSOR_HTTP_TIMEOUT_MS, &status_code)) {
        return false;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error status: %d", status_code);
        return false;
    }
    if (_g_http_response_overflow) {
        ESP_LOGE(TAG, "Response truncated, skipping parse");
        return false;
    }
    _parse_json_response(_g_http_response_buffer);
    return true;
}

/**
 * @brief One PurpleAir cloud poll. On success, posts the values and sets *age_s to the age of
 * the sensor's last upload.
 */
static bool _poll_cloud(int64_t *age_s) {
    network_sensor_cloud_cfg_t cfg;
    network_sensor_cloud_cfg_get(&cfg);
    if (cfg.api_key[0] == '\0' || cfg.sensor_index == 0) {
        return false;
    }

    char url[256];
    int n = snprintf(url, sizeof(url), "https://%s/v1/sensors/%u?fields=%s",
                     NETWORK_SENSOR_CLOUD_HOST, (unsigned)cfg.sensor_index, NETWORK_SENSOR_CLOUD_FIELDS);
    if (cfg.read_key[0] != '\0' && n > 0 && n < (int)sizeof(url)) {
        n += snprintf(url + n, sizeof(url) - n, "&read_key=%s", cfg.read_key);
    }
    if (n <= 0 || n >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "Cloud URL too long");
        return false;
    }

    /* Never log the URL: it can hold the read key */
    ESP_LOGI(TAG, "Cloud poll: sensor %u", (unsigned)cfg.sensor_index);
    int status_code;
    if (!_http_get(url, cfg.api_key, NETWORK_SENSOR_CLOUD_TIMEOUT_MS, &status_code)) {
        return false;
    }
    if (_g_http_response_overflow) {
        ESP_LOGE(TAG, "Cloud response truncated, skipping parse");
        return false;
    }

    /* Parse error replies too: they name the problem, e.g. ApiKeyInvalidError */
    static network_sensor_cloud_reading_t reading;  /* static: keep it off the task stack */
    char api_err[48];
    int ret = network_sensor_cloud_parse(_g_http_response_buffer, &reading, api_err, sizeof(api_err));
    if (ret == NETWORK_SENSOR_CLOUD_PARSE_ERR_API) {
        ESP_LOGE(TAG, "PurpleAir API error (HTTP %d): %s", status_code, api_err);
        return false;
    }
    if (status_code != 200 || ret != NETWORK_SENSOR_CLOUD_PARSE_OK) {
        ESP_LOGE(TAG, "Cloud reply not usable: HTTP %d, parse %d", status_code, ret);
        return false;
    }

    for (int i = 0; i < reading.value_count; i++) {
        network_sensor_update(reading.values[i].type, reading.values[i].value);
    }
    /* The cloud has no gas reading: clear the last local one rather than show it as current */
    network_sensor_update(NETWORK_SENSOR_GAS_A, NAN);

    *age_s = reading.age_s;
    ESP_LOGI(TAG, "Cloud data is %lld s old", (long long)reading.age_s);
    return true;
}

/**
 * @brief Long-lived polling task.
 * Sleeps until enabled; start/stop notify it so state changes take effect immediately.
 *
 * Source state machine:
 *   LOCAL: poll the sensor's /json every NETWORK_SENSOR_POLL_INTERVAL_SEC.
 *   -> CLOUD when a cloud key is set and local polling has failed for NETWORK_SENSOR_CLOUD_AFTER_SEC.
 *   CLOUD: keep polling local at the same rate; poll the cloud every
 *          NETWORK_SENSOR_CLOUD_POLL_INTERVAL_SEC.
 *   -> LOCAL on the first good local poll, or when the cloud key is cleared.
 */
static void _network_sensor_http_task(void *arg) {
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s",
             NETWORK_SENSOR_HOST, NETWORK_SENSOR_PORT, NETWORK_SENSOR_PATH);

    bool in_cloud = false;
    int64_t next_cloud_us = 0;
    int64_t cloud_ok_us = 0;    /* last good cloud poll, or when cloud mode began if none yet */
    int64_t cloud_age_s = 0;    /* sensor upload age at cloud_ok_us */
    bool cloud_have = false;    /* a good cloud poll since cloud mode began */

    while (true) {
        if (!_g_polling_enabled) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (_g_last_ok_us == 0) {
            _g_last_ok_us = esp_timer_get_time();
        }
        if (_poll_local(url)) {
            _g_last_ok_us = esp_timer_get_time();
            _g_have_data = true;
            if (_g_consecutive_failures > 0) {
                ESP_LOGI(TAG, "Recovered after %d failed poll(s)", _g_consecutive_failures);
            }
            _g_consecutive_failures = 0;
            if (in_cloud) {
                ESP_LOGI(TAG, "Local sensor answering again, leaving cloud mode");
                in_cloud = false;
                network_sensor_set_source(NETWORK_SENSOR_SOURCE_LOCAL, 0);
            }
            network_sensor_set_stale(false);
        } else {
            _g_consecutive_failures++;
            int64_t now = esp_timer_get_time();
            int64_t local_age_s = (now - _g_last_ok_us) / 1000000;
            bool cloud_enabled = network_sensor_cloud_enabled();

            if (in_cloud && !cloud_enabled) {
                ESP_LOGI(TAG, "Cloud key cleared, leaving cloud mode");
                in_cloud = false;
                network_sensor_set_source(NETWORK_SENSOR_SOURCE_LOCAL, 0);
            }
            if (!in_cloud && cloud_enabled && local_age_s >= NETWORK_SENSOR_CLOUD_AFTER_SEC) {
                ESP_LOGW(TAG, "No good local poll for %lld s, switching to the PurpleAir cloud",
                         (long long)local_age_s);
                in_cloud = true;
                next_cloud_us = now;
                cloud_ok_us = now;
                cloud_age_s = 0;
                cloud_have = false;
            }

            if (!in_cloud) {
                if (local_age_s >= NETWORK_SENSOR_STALE_AFTER_SEC) {
                    network_sensor_set_stale(true);
                }
            } else if (now >= next_cloud_us) {
                next_cloud_us = now + (int64_t)NETWORK_SENSOR_CLOUD_POLL_INTERVAL_SEC * 1000000;
                int64_t age_s;
                if (_poll_cloud(&age_s)) {
                    cloud_ok_us = esp_timer_get_time();
                    cloud_age_s = age_s;
                    cloud_have = true;
                    _g_have_data = true;
                    network_sensor_set_source(NETWORK_SENSOR_SOURCE_CLOUD, (int32_t)age_s);
                }
                /* Stale once the newest data we hold is too old: upload age plus time since fetched.
                 * Before the first good cloud poll, local polling has already marked it stale. */
                if (cloud_have) {
                    int64_t data_age_s = cloud_age_s + (esp_timer_get_time() - cloud_ok_us) / 1000000;
                    network_sensor_set_stale(data_age_s >= NETWORK_SENSOR_CLOUD_STALE_AFTER_SEC);
                }
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
                                 8192, NULL, 5, &_g_polling_task_handle);
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

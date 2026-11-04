#include "network_sensor_cloud.h"
#include "storage_nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "network-sensor-cloud";

#define CLOUD_CFG_STORAGE "pa_cloud_cfg"  /* NVS keys are at most 15 characters */

static SemaphoreHandle_t s_mutex = NULL;
static network_sensor_cloud_cfg_t s_cfg;

void network_sensor_cloud_init(void) {
    if (s_mutex != NULL) {
        return;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    size_t len = sizeof(s_cfg);
    esp_err_t err = indicator_nvs_read(CLOUD_CFG_STORAGE, &s_cfg, &len);
    if (err != ESP_OK || len != sizeof(s_cfg)) {
        memset(&s_cfg, 0, sizeof(s_cfg));
        ESP_LOGI(TAG, "PurpleAir cloud fallback not configured (run setpurpleair)");
        return;
    }
    s_cfg.api_key[sizeof(s_cfg.api_key) - 1] = '\0';
    s_cfg.read_key[sizeof(s_cfg.read_key) - 1] = '\0';
    ESP_LOGI(TAG, "PurpleAir cloud fallback %s (sensor %u)",
             (s_cfg.api_key[0] && s_cfg.sensor_index) ? "enabled" : "incomplete",
             (unsigned)s_cfg.sensor_index);
}

void network_sensor_cloud_cfg_get(network_sensor_cloud_cfg_t *cfg) {
    if (cfg == NULL) {
        return;
    }
    if (s_mutex == NULL) {
        memset(cfg, 0, sizeof(*cfg));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *cfg = s_cfg;
    xSemaphoreGive(s_mutex);
}

esp_err_t network_sensor_cloud_cfg_set(const network_sensor_cloud_cfg_t *cfg) {
    if (cfg == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cfg = *cfg;
    s_cfg.api_key[sizeof(s_cfg.api_key) - 1] = '\0';
    s_cfg.read_key[sizeof(s_cfg.read_key) - 1] = '\0';
    esp_err_t err = indicator_nvs_write(CLOUD_CFG_STORAGE, &s_cfg, sizeof(s_cfg));
    xSemaphoreGive(s_mutex);
    return err;
}

bool network_sensor_cloud_enabled(void) {
    network_sensor_cloud_cfg_t cfg;
    network_sensor_cloud_cfg_get(&cfg);
    return cfg.api_key[0] != '\0' && cfg.sensor_index != 0;
}

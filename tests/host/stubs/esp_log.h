/* Host-test stand-in for ESP-IDF's esp_log.h: logging compiles to nothing. */
#pragma once

#define ESP_LOG_STUB(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ESP_LOG_STUB(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOG_STUB(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOG_STUB(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ESP_LOG_STUB(tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) ESP_LOG_STUB(tag, __VA_ARGS__)

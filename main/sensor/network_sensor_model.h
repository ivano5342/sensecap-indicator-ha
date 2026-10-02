#ifndef NETWORK_SENSOR_MODEL_H
#define NETWORK_SENSOR_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_SENSOR_OK               0
#define NETWORK_SENSOR_ERR_INVALID_TYPE -1
#define NETWORK_SENSOR_ERR_MUTEX_FAIL   -2

/**
 * @brief Initialize the network sensor model
 * Creates semaphore-protected cache and initializes all network sensor values
 * Subscribes to WiFi state events to control polling lifecycle
 * 
 * @param event_handle The view event handle for posting sensor updates
 * @return int NETWORK_SENSOR_OK on success
 */
void network_sensor_model_init(esp_event_loop_handle_t event_handle);

/**
 * @brief Update a network sensor value and post event
 * Thread-safe: uses semaphore to protect cache
 * 
 * @param type Network sensor type (NETWORK_SENSOR_PM25_AQI_A, etc.)
 * @param value Float value to store
 * @return int NETWORK_SENSOR_OK on success, error code otherwise
 */
int network_sensor_update(enum sensor_data_type type, float value);

/**
 * @brief Get current value of a network sensor
 * Thread-safe: uses semaphore protection
 * 
 * @param type Network sensor type
 * @return float The sensor value, or 0 if invalid type
 */
float network_sensor_get_value(enum sensor_data_type type);

/**
 * @brief Mark the network sensor data as stale (or fresh again)
 * Posts VIEW_EVENT_NETWORK_SENSOR_STALE only when the state changes.
 * Cached values are kept either way.
 */
void network_sensor_set_stale(bool is_stale);

/**
 * @brief Start the HTTP polling task
 * Called when WiFi connects
 */
void network_sensor_polling_start(void);

/**
 * @brief Stop the HTTP polling task
 * Called when WiFi disconnects
 */
void network_sensor_polling_stop(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // NETWORK_SENSOR_MODEL_H

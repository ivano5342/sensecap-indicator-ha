#ifndef NETWORK_SENSOR_HTTP_H
#define NETWORK_SENSOR_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HTTP polling task management
 * Creates the long-lived polling task (idle until started)
 */
void network_sensor_http_init(void);

/**
 * @brief Start the HTTP polling task
 * Begins periodic requests to the network sensor endpoint
 */
void network_sensor_http_polling_start(void);

/**
 * @brief Stop the HTTP polling task
 * Cleanly terminates the polling task
 */
void network_sensor_http_polling_stop(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // NETWORK_SENSOR_HTTP_H

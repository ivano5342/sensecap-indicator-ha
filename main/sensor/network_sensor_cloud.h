#ifndef NETWORK_SENSOR_CLOUD_H
#define NETWORK_SENSOR_CLOUD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PurpleAir cloud fallback settings, kept in NVS and set with the `setpurpleair` console
 * command. The fallback is off until an API key and a sensor index are both set.
 */

#define NETWORK_SENSOR_CLOUD_KEY_LEN 64

typedef struct {
    char api_key[NETWORK_SENSOR_CLOUD_KEY_LEN];   /* PurpleAir READ key, sent as X-API-Key */
    char read_key[NETWORK_SENSOR_CLOUD_KEY_LEN];  /* only for a private sensor; may be empty */
    uint32_t sensor_index;                        /* 0 = not set */
} network_sensor_cloud_cfg_t;

/* Load the settings from NVS. Call once, after NVS is initialised. */
void network_sensor_cloud_init(void);

/* Copy of the current settings (thread-safe). */
void network_sensor_cloud_cfg_get(network_sensor_cloud_cfg_t *cfg);

/* Replace the settings and save them to NVS (thread-safe). */
esp_err_t network_sensor_cloud_cfg_set(const network_sensor_cloud_cfg_t *cfg);

/* True when an API key and a sensor index are both set. */
bool network_sensor_cloud_enabled(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* NETWORK_SENSOR_CLOUD_H */

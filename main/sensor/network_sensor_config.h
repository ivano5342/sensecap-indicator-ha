#ifndef NETWORK_SENSOR_CONFIG_H
#define NETWORK_SENSOR_CONFIG_H

#include "view_data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── HTTP Endpoint Configuration ─────────────────────────────────────────── */

#define NETWORK_SENSOR_HOST         "10.0.0.161"  // TODO: Make configurable via UI
#define NETWORK_SENSOR_PORT 80
#define NETWORK_SENSOR_PATH         "/json?live=true"          // HTTP GET path
#define NETWORK_SENSOR_POLL_INTERVAL_SEC 10          // Poll every 10 seconds
#define NETWORK_SENSOR_HTTP_TIMEOUT_MS   2000        // Healthy replies take <~0.4 s; the sensor stalls for 20-30 s at a time
#define NETWORK_SENSOR_STALE_AFTER_SEC   90          // Longest observed stall run is ~48 s; no good poll for this long -> stale

/* ── JSON Field Mapping ──────────────────────────────────────────────────── */

/**
 * @brief Sensor column identifier for UI layout
 */
typedef enum {
    NETWORK_SENSOR_A = 0,
    NETWORK_SENSOR_B = 1,
    NETWORK_SENSOR_COUNT
} network_sensor_id_t;

/**
 * @brief Maps JSON object keys to sensor types and display information
 * The JSON response is expected to contain these keys
 */
typedef struct {
    const char *json_key;              // JSON object key to extract
    enum sensor_data_type sensor_type; // Mapped sensor type
    network_sensor_id_t sensor_id;     // Which sensor column (A or B)
    const char *display_label;         // UI display label
    const char *unit;                  // Unit string (e.g., "AQI", "ug/m3")
} network_sensor_field_mapping_t;

/**
 * @brief Field mapping table for PM sensor JSON response
 * Update this table to match your JSON structure
 *
 * The HTTP endpoint serves data from two sensors on the same host:
 *   - Sensor "A" keys have no suffix (e.g. pm2_5_atm, pm1_0_atm)
 *   - Sensor "B" keys end in _b (e.g. pm2_5_atm_b, pm1_0_atm_b)
 *
 * PM values use ATM (ambient mass) calibration, not CF-1.
 *
 * JSON Response Example:
 * {
 *   "gas_680": 103.76,
 *   "pm2.5_aqi_b": 4,
 *   "pm1_0_atm_b": 1.00,
 *   "pm2_5_atm_b": 1.00,
 *   "pm10_0_atm_b": 2.00,
 *   "pm2.5_aqi": 4,
 *   "pm1_0_atm": 1.00,
 *   "pm2_5_atm": 1.00,
 *   "pm10_0_atm": 2.00,
 *   ...
 * }
 */
static const network_sensor_field_mapping_t network_sensor_field_map[] = {
    // Sensor B readings
    {
        .json_key = "pm2.5_aqi_b",
        .sensor_type = NETWORK_SENSOR_PM25_AQI_B,
        .sensor_id = NETWORK_SENSOR_B,
        .display_label = "PM2.5 AQI",
        .unit = ""
    },
    {
        .json_key = "pm1_0_atm_b",
        .sensor_type = NETWORK_SENSOR_PM1_0_B,
        .sensor_id = NETWORK_SENSOR_B,
        .display_label = "PM1.0",
        .unit = "ug/m3"
    },
    {
        .json_key = "pm2_5_atm_b",
        .sensor_type = NETWORK_SENSOR_PM2_5_B,
        .sensor_id = NETWORK_SENSOR_B,
        .display_label = "PM2.5",
        .unit = "ug/m3"
    },
    {
        .json_key = "pm10_0_atm_b",
        .sensor_type = NETWORK_SENSOR_PM10_0_B,
        .sensor_id = NETWORK_SENSOR_B,
        .display_label = "PM10.0",
        .unit = "ug/m3"
    },
    // Note: gas_680 is a shared sensor (no _b suffix), shown in Sensor A column only
    // Sensor A readings
    {
        .json_key = "pm2.5_aqi",
        .sensor_type = NETWORK_SENSOR_PM25_AQI_A,
        .sensor_id = NETWORK_SENSOR_A,
        .display_label = "PM2.5 AQI",
        .unit = ""
    },
    {
        .json_key = "pm1_0_atm",
        .sensor_type = NETWORK_SENSOR_PM1_0_A,
        .sensor_id = NETWORK_SENSOR_A,
        .display_label = "PM1.0",
        .unit = "ug/m3"
    },
    {
        .json_key = "pm2_5_atm",
        .sensor_type = NETWORK_SENSOR_PM2_5_A,
        .sensor_id = NETWORK_SENSOR_A,
        .display_label = "PM2.5",
        .unit = "ug/m3"
    },
    {
        .json_key = "pm10_0_atm",
        .sensor_type = NETWORK_SENSOR_PM10_0_A,
        .sensor_id = NETWORK_SENSOR_A,
        .display_label = "PM10.0",
        .unit = "ug/m3"
    },
    {
        .json_key = "gas_680",
        .sensor_type = NETWORK_SENSOR_GAS_A,
        .sensor_id = NETWORK_SENSOR_A,
        .display_label = "Gas",
        .unit = "ohm"
    },
    // AQI color fields (rgb string packed as uint32_t reinterpreted as float)
    {
        .json_key = "p25aqic",
        .sensor_type = NETWORK_SENSOR_AQI_COLOR_A,
        .sensor_id = NETWORK_SENSOR_A,
        .display_label = "AQI Color A",
        .unit = ""
    },
    {
        .json_key = "p25aqic_b",
        .sensor_type = NETWORK_SENSOR_AQI_COLOR_B,
        .sensor_id = NETWORK_SENSOR_B,
        .display_label = "AQI Color B",
        .unit = ""
    },
    // Sentinel - marks end of table
    {
        .json_key = NULL,
        .sensor_type = ENUM_SENSOR_ALL,
        .sensor_id = NETWORK_SENSOR_COUNT,
        .display_label = NULL,
        .unit = NULL
    }
};

/**
 * @brief Get the number of network sensors to display
 */
#define NETWORK_SENSOR_FIELD_COUNT (sizeof(network_sensor_field_map) / sizeof(network_sensor_field_mapping_t) - 1)

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // NETWORK_SENSOR_CONFIG_H

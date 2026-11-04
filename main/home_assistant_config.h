/**
 * @file home_assistant_config.h
 * @brief Compile-time Home Assistant / MQTT settings.
 *
 * Macros here use the HA_ prefix on purpose: the CONFIG_ prefix belongs to
 * Kconfig (sdkconfig.h), and sharing it risks silent collisions.
 *
 * Topic names and JSON keys are product protocol. Changing them breaks
 * existing Home Assistant setups; keep them in sync with
 * examples/homeassistant/ and the README "MQTT Protocol" section.
 *
 * @copyright © 2024, Seeed Studio
 */

#ifndef HOME_ASSISTANT_CONFIG_H
#define HOME_ASSISTANT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Default broker ─────────────────────────────────────────────────────────
 * Used only until a broker config is saved to NVS (touchscreen or the
 * `setmqtt` console command). The default is a PUBLIC test broker.
 *   "mqtt://homeassistant.local"   mDNS name
 *   "mqtt://192.168.1.10"          IPv4
 */
#define HA_DEFAULT_BROKER_URL          "mqtt://broker.emqx.io"
#define HA_DEFAULT_MQTT_CLIENT_ID      "indicator-id"
#define HA_DEFAULT_MQTT_USERNAME       "indicator-usr"
#define HA_DEFAULT_MQTT_PASSWORD       "indicator-password"

/* ── Topics ─────────────────────────────────────────────────────────────── */
#define HA_TOPIC_SENSOR_DATA           "indicator/sensor"
#define HA_TOPIC_SENSOR_DATA_QOS       0

#define HA_TOPIC_SWITCH_STATE          "indicator/switch/state"
#define HA_TOPIC_SWITCH_SET            "indicator/switch/set"
#define HA_TOPIC_SWITCH_QOS            1

/* ── Built-in sensor JSON keys (published on HA_TOPIC_SENSOR_DATA) ──────── */
#define HA_SENSOR_KEY_CO2              "co2"
#define HA_SENSOR_KEY_TVOC             "tvoc"
#define HA_SENSOR_KEY_TEMP             "temp"
#define HA_SENSOR_KEY_HUMIDITY         "humidity"

#define HA_SENSOR_TEMP_UI_UNIT         "°C"

/* ── Sensor entities (subscribed; see ha_sensor.c) ──────────────────────────
 * One entry per entity, in index order. Arrays must have
 * HA_SENSOR_ENTITY_NUM entries (checked at compile time in ha_sensor.c).
 */
#define HA_SENSOR_ENTITY_NUM           6

#define HA_SENSOR_VALUE_KEYS {                                   \
    HA_SENSOR_KEY_TEMP, HA_SENSOR_KEY_HUMIDITY, HA_SENSOR_KEY_TVOC, \
    HA_SENSOR_KEY_CO2,  HA_SENSOR_KEY_TEMP,     HA_SENSOR_KEY_HUMIDITY \
}

#define HA_SENSOR_TOPICS {                                       \
    HA_TOPIC_SENSOR_DATA, HA_TOPIC_SENSOR_DATA, HA_TOPIC_SENSOR_DATA, \
    HA_TOPIC_SENSOR_DATA, HA_TOPIC_SENSOR_DATA, HA_TOPIC_SENSOR_DATA  \
}

/* ── Switch entities (see ha_switch.c / ha_switch_screen.c) ─────────────────
 * Index i maps to JSON key HA_SWITCH_VALUE_KEYS[i] on both switch topics.
 * switch5 and switch8 are numeric sliders; the rest are on/off switches.
 * Arrays must have HA_SWITCH_ENTITY_NUM entries (checked at compile time).
 */
#define HA_SWITCH_ENTITY_NUM           8

#define HA_SWITCH_VALUE_KEYS {                                   \
    "switch1", "switch2", "switch3", "switch4",                  \
    "switch5", "switch6", "switch7", "switch8"                   \
}

#define HA_SWITCH_UI_NAMES {                                     \
    "Switch1", "Switch2", "Switch3", "Switch4",                  \
    "Switch5", "Switch6", "Switch7", "Switch8"                   \
}

#define HA_SWITCH_TOPICS_STATE {                                 \
    HA_TOPIC_SWITCH_STATE, HA_TOPIC_SWITCH_STATE, HA_TOPIC_SWITCH_STATE, HA_TOPIC_SWITCH_STATE, \
    HA_TOPIC_SWITCH_STATE, HA_TOPIC_SWITCH_STATE, HA_TOPIC_SWITCH_STATE, HA_TOPIC_SWITCH_STATE  \
}

#define HA_SWITCH_TOPICS_SET {                                   \
    HA_TOPIC_SWITCH_SET, HA_TOPIC_SWITCH_SET, HA_TOPIC_SWITCH_SET, HA_TOPIC_SWITCH_SET, \
    HA_TOPIC_SWITCH_SET, HA_TOPIC_SWITCH_SET, HA_TOPIC_SWITCH_SET, HA_TOPIC_SWITCH_SET  \
}

#define HA_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*HOME_ASSISTANT_CONFIG_H*/

#ifndef HA_PAYLOAD_H
#define HA_PAYLOAD_H

/*
 * Pure (hardware-independent) Home Assistant MQTT payload codec.
 *
 * No ESP-IDF, FreeRTOS, MQTT client or LVGL dependencies: only libc, cJSON and
 * the shared view data types. ha_sensor.c / ha_switch.c own the MQTT client and
 * event-loop side; this module owns the wire format, so it can be unit tested
 * on the host (see tests/host/).
 *
 * The MQTT topics and payload shapes here are product behavior. The parse
 * functions deliberately keep the historical matching rules (see the notes on
 * each function); do not "fix" them without treating it as a protocol change.
 */

#include <stddef.h>

#include "view_data_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HA_PAYLOAD_OK             = 0,
    HA_PAYLOAD_TOPIC_MISMATCH = -1, /* not addressed to this entity group */
    HA_PAYLOAD_INVALID_JSON   = -2, /* unparsable, or no first named member */
    HA_PAYLOAD_NO_MATCH       = -3, /* no entity key / value type matched */
    HA_PAYLOAD_FORMAT_ERROR   = -4, /* output buffer too small or bad args */
    HA_PAYLOAD_UNKNOWN_SENSOR = -5, /* sensor type has no publish key */
} ha_payload_status_t;

/*
 * Topic match used for incoming messages: strncmp(topic, expected, topic_len).
 * The incoming topic is not NUL-terminated. Note that this is a prefix match of
 * the *incoming* topic against the configured one (historical behavior).
 */
int ha_payload_topic_matches(const char *expected_topic, const char *topic, int topic_len);

/*
 * Parse an incoming switch command, e.g. {"switch1": 1} on the switch set topic.
 *
 * Matching rules (unchanged from the original ha_switch.c):
 *  - only the first JSON member's name is considered;
 *  - an entity key matches when the first member name is a prefix of it;
 *  - the value is looked up by the entity key and must be a JSON number;
 *    out->value is cJSON's valueint (truncated/saturated double).
 */
ha_payload_status_t ha_payload_parse_switch(const char *expected_topic,
                                            const char *const *keys, int key_count,
                                            const char *topic, int topic_len,
                                            const char *data, int data_len,
                                            struct view_data_ha_switch_data *out);

/*
 * Parse an incoming sensor value, e.g. {"temp": "23.5"} on the sensor topic.
 *
 * Matching rules (unchanged from the original ha_sensor.c):
 *  - only the first JSON member is considered;
 *  - an entity key matches when the first member name is a prefix of it;
 *  - the value must be a JSON string; it is copied truncated to
 *    sizeof(out->value) - 1 characters.
 */
ha_payload_status_t ha_payload_parse_sensor(const char *expected_topic,
                                            const char *const *keys, int key_count,
                                            const char *topic, int topic_len,
                                            const char *data, int data_len,
                                            struct view_data_ha_sensor_data *out);

/*
 * Format an outgoing switch state: {"<key>": <value>}.
 * Returns the payload length (> 0) or HA_PAYLOAD_FORMAT_ERROR.
 */
int ha_payload_format_switch_state(char *buf, size_t buf_size, const char *key, int value);

/*
 * Format a built-in sensor reading: {"<key>":"<value>"}, value as "%.1f" for
 * temperature and truncated "%d" for everything else.
 * Returns the payload length (> 0), HA_PAYLOAD_UNKNOWN_SENSOR, or
 * HA_PAYLOAD_FORMAT_ERROR. *key_out (optional) receives the JSON key.
 */
int ha_payload_format_sensor(char *buf, size_t buf_size, enum sensor_data_type type,
                             float value, const char **key_out);

#ifdef __cplusplus
}
#endif

#endif /* HA_PAYLOAD_H */

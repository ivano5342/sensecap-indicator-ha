/*
 * Home Assistant MQTT payload codec (main/ha/ha_payload.c).
 *
 * Several tests pin down historical matching quirks ("characterization"
 * tests). They document current device behavior; changing one of them is a
 * protocol change, not a refactor.
 */
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "ha_payload.h"
#include "home_assistant_config.h"

static const char *const switch_keys[] = CONFIG_SWITCH_VALUE_KEYS;
static const char *const sensor_keys[] = CONFIG_SENSOR_VALUE_KEYS;

#define SET_TOPIC    CONFIG_TOPIC_SWITCH_SET
#define SENSOR_TOPIC CONFIG_TOPIC_SENSOR_DATA

void setUp(void) {}
void tearDown(void) {}

static ha_payload_status_t parse_switch(const char *topic, const char *payload,
                                        struct view_data_ha_switch_data *out)
{
    return ha_payload_parse_switch(SET_TOPIC, switch_keys, CONFIG_HA_SWITCH_ENTITY_NUM,
                                   topic, (int)strlen(topic), payload, (int)strlen(payload), out);
}

static ha_payload_status_t parse_sensor(const char *topic, const char *payload,
                                        struct view_data_ha_sensor_data *out)
{
    return ha_payload_parse_sensor(SENSOR_TOPIC, sensor_keys, CONFIG_HA_SENSOR_ENTITY_NUM,
                                   topic, (int)strlen(topic), payload, (int)strlen(payload), out);
}

/* ── Switch: incoming ───────────────────────────────────────────────────── */

static void test_switch_on_and_off(void)
{
    struct view_data_ha_switch_data out = {0};
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_switch(SET_TOPIC, "{\"switch1\": 1}", &out));
    TEST_ASSERT_EQUAL_UINT8(0, out.index);
    TEST_ASSERT_EQUAL_INT(1, out.value);

    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_switch(SET_TOPIC, "{\"switch2\":0}", &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.index);
    TEST_ASSERT_EQUAL_INT(0, out.value);
}

static void test_switch_every_index(void)
{
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        char payload[32];
        snprintf(payload, sizeof(payload), "{\"switch%d\": %d}", i + 1, 10 + i);
        struct view_data_ha_switch_data out = {0};
        TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_switch(SET_TOPIC, payload, &out));
        TEST_ASSERT_EQUAL_UINT8(i, out.index);
        TEST_ASSERT_EQUAL_INT(10 + i, out.value);
    }
}

static void test_switch_slider_value_and_fraction_truncates(void)
{
    struct view_data_ha_switch_data out = {0};
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_switch(SET_TOPIC, "{\"switch8\":50}", &out));
    TEST_ASSERT_EQUAL_INT(50, out.value);

    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_switch(SET_TOPIC, "{\"switch5\":24.9}", &out));
    TEST_ASSERT_EQUAL_UINT8(4, out.index);
    TEST_ASSERT_EQUAL_INT(24, out.value);
}

static void test_switch_wrong_topic(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_TOPIC_MISMATCH,
                      parse_switch(CONFIG_TOPIC_SWITCH_STATE, "{\"switch1\": 1}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_TOPIC_MISMATCH,
                      parse_switch("other/topic", "{\"switch1\": 1}", NULL));
}

static void test_switch_topic_is_prefix_matched(void)
{
    /* Quirk: strncmp(topic, expected, topic_len) accepts any prefix of the
     * configured topic, including an empty topic. */
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_switch("indicator/switch", "{\"switch1\": 1}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK,
                      ha_payload_parse_switch(SET_TOPIC, switch_keys, CONFIG_HA_SWITCH_ENTITY_NUM,
                                              "", 0, "{\"switch1\":1}", 13, NULL));
    /* ...but a longer topic does not match. */
    TEST_ASSERT_EQUAL(HA_PAYLOAD_TOPIC_MISMATCH,
                      parse_switch("indicator/switch/set/x", "{\"switch1\": 1}", NULL));
}

static void test_switch_topic_not_nul_terminated(void)
{
    /* esp-mqtt topics are length-delimited, not NUL-terminated. */
    const char topic[] = "indicator/switch/setGARBAGE";
    const char data[] = "{\"switch3\":1}trailing junk";
    struct view_data_ha_switch_data out = {0};
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK,
                      ha_payload_parse_switch(SET_TOPIC, switch_keys, CONFIG_HA_SWITCH_ENTITY_NUM,
                                              topic, (int)strlen(SET_TOPIC), data, 13, &out));
    TEST_ASSERT_EQUAL_UINT8(2, out.index);
}

static void test_switch_invalid_json(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_INVALID_JSON, parse_switch(SET_TOPIC, "not_json", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_INVALID_JSON, parse_switch(SET_TOPIC, "", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_INVALID_JSON, parse_switch(SET_TOPIC, "{}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_INVALID_JSON, parse_switch(SET_TOPIC, "[1, 2]", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_INVALID_JSON, parse_switch(SET_TOPIC, "{\"switch1\": ", NULL));
}

static void test_switch_rejects_non_numbers(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_switch(SET_TOPIC, "{\"switch1\": \"on\"}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_switch(SET_TOPIC, "{\"switch1\": true}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_switch(SET_TOPIC, "{\"switch1\": null}", NULL));
}

static void test_switch_unknown_key(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_switch(SET_TOPIC, "{\"unknown\": 1}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_switch(SET_TOPIC, "{\"switch9\": 1}", NULL));
}

static void test_switch_only_first_member_is_considered(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH,
                      parse_switch(SET_TOPIC, "{\"other\": 1, \"switch1\": 1}", NULL));

    struct view_data_ha_switch_data out = {0};
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK,
                      parse_switch(SET_TOPIC, "{\"switch4\": 1, \"switch1\": 0}", &out));
    TEST_ASSERT_EQUAL_UINT8(3, out.index);
}

static void test_switch_key_prefix_quirk(void)
{
    /* Quirk: the first member name only has to be a prefix of an entity key,
     * but the value is then looked up by the full entity key. So a bare
     * "switch" member matches nothing... */
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_switch(SET_TOPIC, "{\"switch\": 1}", NULL));

    /* ...unless the full key is also present further on. */
    struct view_data_ha_switch_data out = {0};
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK,
                      parse_switch(SET_TOPIC, "{\"switch\": 0, \"switch2\": 7}", &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.index);
    TEST_ASSERT_EQUAL_INT(7, out.value);
}

/* ── Switch: outgoing ───────────────────────────────────────────────────── */

static void test_switch_state_format(void)
{
    char buf[64];
    int len = ha_payload_format_switch_state(buf, sizeof(buf), "switch1", 1);
    TEST_ASSERT_EQUAL_STRING("{\"switch1\": 1}", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);

    ha_payload_format_switch_state(buf, sizeof(buf), "switch8", 50);
    TEST_ASSERT_EQUAL_STRING("{\"switch8\": 50}", buf);

    ha_payload_format_switch_state(buf, sizeof(buf), "switch4", -3);
    TEST_ASSERT_EQUAL_STRING("{\"switch4\": -3}", buf);
}

static void test_switch_state_format_too_small(void)
{
    char buf[14]; /* needs 15 incl. NUL */
    TEST_ASSERT_EQUAL(HA_PAYLOAD_FORMAT_ERROR,
                      ha_payload_format_switch_state(buf, sizeof(buf), "switch1", 1));
    char ok[15];
    TEST_ASSERT_EQUAL_INT(14, ha_payload_format_switch_state(ok, sizeof(ok), "switch1", 1));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_FORMAT_ERROR,
                      ha_payload_format_switch_state(NULL, 0, "switch1", 1));
}

static void test_switch_round_trip(void)
{
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        char buf[64];
        int len = ha_payload_format_switch_state(buf, sizeof(buf), switch_keys[i], i * 3);
        TEST_ASSERT_GREATER_THAN(0, len);
        struct view_data_ha_switch_data out = {0};
        TEST_ASSERT_EQUAL(HA_PAYLOAD_OK,
                          ha_payload_parse_switch(SET_TOPIC, switch_keys, CONFIG_HA_SWITCH_ENTITY_NUM,
                                                  SET_TOPIC, (int)strlen(SET_TOPIC), buf, len, &out));
        TEST_ASSERT_EQUAL_UINT8(i, out.index);
        TEST_ASSERT_EQUAL_INT(i * 3, out.value);
    }
}

/* ── Sensor: incoming ───────────────────────────────────────────────────── */

static void test_sensor_string_values(void)
{
    struct view_data_ha_sensor_data out;
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"temp\":\"23.5\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(0, out.index);
    TEST_ASSERT_EQUAL_STRING("23.5", out.value);

    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"humidity\":\"55\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.index);
    TEST_ASSERT_EQUAL_STRING("55", out.value);

    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"tvoc\":\"12\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(2, out.index);

    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"co2\":\"600\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(3, out.index);
    TEST_ASSERT_EQUAL_STRING("600", out.value);
}

static void test_sensor_duplicate_keys_map_to_first_entity(void)
{
    /* sensor5/6 reuse "temp"/"humidity"; the first entity always wins. */
    struct view_data_ha_sensor_data out;
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"humidity\":\"1\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.index);
}

static void test_sensor_rejects_numbers(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_sensor(SENSOR_TOPIC, "{\"temp\":23.5}", NULL));
}

static void test_sensor_wrong_topic_and_bad_json(void)
{
    TEST_ASSERT_EQUAL(HA_PAYLOAD_TOPIC_MISMATCH,
                      parse_sensor(CONFIG_TOPIC_SWITCH_SET, "{\"temp\":\"1\"}", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_INVALID_JSON, parse_sensor(SENSOR_TOPIC, "{oops", NULL));
    TEST_ASSERT_EQUAL(HA_PAYLOAD_NO_MATCH, parse_sensor(SENSOR_TOPIC, "{\"pm25\":\"3\"}", NULL));
}

static void test_sensor_value_truncated_to_buffer(void)
{
    struct view_data_ha_sensor_data out;
    const char *payload = "{\"temp\":\"0123456789012345678901234567890123456789\"}";
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, payload, &out));
    TEST_ASSERT_EQUAL_size_t(sizeof(out.value) - 1, strlen(out.value));
    TEST_ASSERT_EQUAL_STRING("0123456789012345678901234567890", out.value);
}

static void test_sensor_key_prefix_quirk(void)
{
    /* Quirk: a member name that is a prefix of an entity key matches it, and
     * (unlike switches) the value is read back under the sent name. */
    struct view_data_ha_sensor_data out;
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"te\":\"9\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(0, out.index);
    TEST_ASSERT_EQUAL_STRING("9", out.value);
    TEST_ASSERT_EQUAL(HA_PAYLOAD_OK, parse_sensor(SENSOR_TOPIC, "{\"\":\"x\"}", &out));
    TEST_ASSERT_EQUAL_UINT8(0, out.index);
}

/* ── Sensor: outgoing ───────────────────────────────────────────────────── */

static void test_sensor_format_builtin(void)
{
    char buf[64];
    const char *key = NULL;

    ha_payload_format_sensor(buf, sizeof(buf), SCD41_SENSOR_CO2, 600.7f, &key);
    TEST_ASSERT_EQUAL_STRING("{\"co2\":\"600\"}", buf);
    TEST_ASSERT_EQUAL_STRING(CONFIG_SENSOR_BUILDIN_CO2_VALUE_KEY, key);

    ha_payload_format_sensor(buf, sizeof(buf), SGP40_SENSOR_TVOC, 12.0f, &key);
    TEST_ASSERT_EQUAL_STRING("{\"tvoc\":\"12\"}", buf);

    ha_payload_format_sensor(buf, sizeof(buf), SHT41_SENSOR_TEMP, 23.46f, &key);
    TEST_ASSERT_EQUAL_STRING("{\"temp\":\"23.5\"}", buf);

    ha_payload_format_sensor(buf, sizeof(buf), SHT41_SENSOR_TEMP, -4.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"temp\":\"-4.0\"}", buf);

    int len = ha_payload_format_sensor(buf, sizeof(buf), SHT41_SENSOR_HUMIDITY, 55.9f, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"humidity\":\"55\"}", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);
}

static void test_sensor_format_unknown_type(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL(HA_PAYLOAD_UNKNOWN_SENSOR,
                      ha_payload_format_sensor(buf, sizeof(buf), ENUM_SENSOR_ALL, 1.0f, NULL));
}

static void test_sensor_format_too_small(void)
{
    char buf[8];
    TEST_ASSERT_EQUAL(HA_PAYLOAD_FORMAT_ERROR,
                      ha_payload_format_sensor(buf, sizeof(buf), SCD41_SENSOR_CO2, 600.0f, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_switch_on_and_off);
    RUN_TEST(test_switch_every_index);
    RUN_TEST(test_switch_slider_value_and_fraction_truncates);
    RUN_TEST(test_switch_wrong_topic);
    RUN_TEST(test_switch_topic_is_prefix_matched);
    RUN_TEST(test_switch_topic_not_nul_terminated);
    RUN_TEST(test_switch_invalid_json);
    RUN_TEST(test_switch_rejects_non_numbers);
    RUN_TEST(test_switch_unknown_key);
    RUN_TEST(test_switch_only_first_member_is_considered);
    RUN_TEST(test_switch_key_prefix_quirk);
    RUN_TEST(test_switch_state_format);
    RUN_TEST(test_switch_state_format_too_small);
    RUN_TEST(test_switch_round_trip);
    RUN_TEST(test_sensor_string_values);
    RUN_TEST(test_sensor_duplicate_keys_map_to_first_entity);
    RUN_TEST(test_sensor_rejects_numbers);
    RUN_TEST(test_sensor_wrong_topic_and_bad_json);
    RUN_TEST(test_sensor_value_truncated_to_buffer);
    RUN_TEST(test_sensor_key_prefix_quirk);
    RUN_TEST(test_sensor_format_builtin);
    RUN_TEST(test_sensor_format_unknown_type);
    RUN_TEST(test_sensor_format_too_small);
    return UNITY_END();
}

/* Built-in sensor packet decoding and display formatting (main/sensor/sensor_format.c). */
#include <string.h>

#include "unity.h"

#include "rp2040_protocol.h"
#include "sensor_format.h"

void setUp(void) {}
void tearDown(void) {}

static void make_pkt(uint8_t *pkt, uint8_t type, float value)
{
    pkt[0] = type;
    memcpy(pkt + 1, &value, sizeof(value));
}

static void test_pkt_type_mapping(void)
{
    enum sensor_data_type t;
    TEST_ASSERT_TRUE(sensor_pkt_type_to_sensor(PKT_TYPE_SENSOR_SCD41_CO2, &t));
    TEST_ASSERT_EQUAL(SCD41_SENSOR_CO2, t);
    TEST_ASSERT_TRUE(sensor_pkt_type_to_sensor(PKT_TYPE_SENSOR_SGP40_TVOC_INDEX, &t));
    TEST_ASSERT_EQUAL(SGP40_SENSOR_TVOC, t);
    TEST_ASSERT_TRUE(sensor_pkt_type_to_sensor(PKT_TYPE_SENSOR_SHT41_TEMP, &t));
    TEST_ASSERT_EQUAL(SHT41_SENSOR_TEMP, t);
    TEST_ASSERT_TRUE(sensor_pkt_type_to_sensor(PKT_TYPE_SENSOR_SHT41_HUMIDITY, &t));
    TEST_ASSERT_EQUAL(SHT41_SENSOR_HUMIDITY, t);

    TEST_ASSERT_FALSE(sensor_pkt_type_to_sensor(PKT_TYPE_SENSOR_SEN5X_massConcentrationPm1p0, &t));
    TEST_ASSERT_FALSE(sensor_pkt_type_to_sensor(PKT_TYPE_CMD_POWER_ON, &t));
    TEST_ASSERT_FALSE(sensor_pkt_type_to_sensor(0x00, NULL));
}

static void test_pkt_decode_ok(void)
{
    uint8_t pkt[5];
    make_pkt(pkt, PKT_TYPE_SENSOR_SCD41_CO2, 612.5f);
    enum sensor_data_type t = ENUM_SENSOR_ALL;
    float v = 0;
    TEST_ASSERT_EQUAL(SENSOR_PKT_OK, sensor_pkt_decode(pkt, sizeof(pkt), &t, &v));
    TEST_ASSERT_EQUAL(SCD41_SENSOR_CO2, t);
    TEST_ASSERT_EQUAL_FLOAT(612.5f, v);
}

static void test_pkt_decode_extra_bytes_ignored(void)
{
    uint8_t pkt[9] = {0};
    make_pkt(pkt, PKT_TYPE_SENSOR_SHT41_TEMP, -3.25f);
    float v = 0;
    TEST_ASSERT_EQUAL(SENSOR_PKT_OK, sensor_pkt_decode(pkt, sizeof(pkt), NULL, &v));
    TEST_ASSERT_EQUAL_FLOAT(-3.25f, v);
}

static void test_pkt_decode_too_short(void)
{
    uint8_t pkt[5];
    make_pkt(pkt, PKT_TYPE_SENSOR_SCD41_CO2, 1.0f);
    TEST_ASSERT_EQUAL(SENSOR_PKT_TOO_SHORT, sensor_pkt_decode(pkt, 4, NULL, NULL));
    TEST_ASSERT_EQUAL(SENSOR_PKT_TOO_SHORT, sensor_pkt_decode(pkt, 0, NULL, NULL));
    TEST_ASSERT_EQUAL(SENSOR_PKT_TOO_SHORT, sensor_pkt_decode(NULL, 5, NULL, NULL));
}

static void test_pkt_decode_unknown_type(void)
{
    uint8_t pkt[5];
    make_pkt(pkt, PKT_TYPE_SENSOR_SFA3X_HCHO, 1.0f);
    TEST_ASSERT_EQUAL(SENSOR_PKT_UNKNOWN, sensor_pkt_decode(pkt, sizeof(pkt), NULL, NULL));
}

static void test_read_float_unaligned(void)
{
    uint8_t raw[8] = {0};
    float in = 42.125f;
    memcpy(raw + 3, &in, sizeof(in));
    TEST_ASSERT_EQUAL_FLOAT(42.125f, sensor_pkt_read_float(raw + 3));
}

static void assert_fmt(const char *expected, enum sensor_data_type t, float v)
{
    char buf[32];
    memset(buf, 'x', sizeof(buf));
    sensor_format_value(buf, sizeof(buf), t, v);
    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_format_temperature_one_decimal(void)
{
    assert_fmt("23.5", SHT41_SENSOR_TEMP, 23.46f);
    assert_fmt("0.0", SHT41_SENSOR_TEMP, 0.0f);
    assert_fmt("100.0", SHT41_SENSOR_TEMP, 100.0f);
}

static void test_format_others_integer(void)
{
    assert_fmt("56", SHT41_SENSOR_HUMIDITY, 55.6f);
    assert_fmt("600", SCD41_SENSOR_CO2, 600.4f);
    assert_fmt("12", SGP40_SENSOR_TVOC, 12.0f);
    assert_fmt("7", ENUM_SENSOR_ALL, 7.0f); /* default branch */
}

static void test_format_negative_is_na(void)
{
    assert_fmt(SENSOR_FORMAT_NA, SHT41_SENSOR_TEMP, -0.1f);
    assert_fmt("N/A", SCD41_SENSOR_CO2, -1.0f);
}

static void test_format_truncates_to_buffer(void)
{
    char buf[4];
    sensor_format_value(buf, sizeof(buf), SCD41_SENSOR_CO2, 123456.0f);
    TEST_ASSERT_EQUAL_STRING("123", buf);
    sensor_format_value(NULL, 0, SCD41_SENSOR_CO2, 1.0f); /* must not crash */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pkt_type_mapping);
    RUN_TEST(test_pkt_decode_ok);
    RUN_TEST(test_pkt_decode_extra_bytes_ignored);
    RUN_TEST(test_pkt_decode_too_short);
    RUN_TEST(test_pkt_decode_unknown_type);
    RUN_TEST(test_read_float_unaligned);
    RUN_TEST(test_format_temperature_one_decimal);
    RUN_TEST(test_format_others_integer);
    RUN_TEST(test_format_negative_is_na);
    RUN_TEST(test_format_truncates_to_buffer);
    return UNITY_END();
}

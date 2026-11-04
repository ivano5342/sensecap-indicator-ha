/* Broker URL / IP / RSSI helpers (main/util/indicator_util.c). */
#include <string.h>

#include "unity.h"

#include "indicator_util.h"

void setUp(void) {}
void tearDown(void) {}

static void test_rssi_levels(void)
{
    TEST_ASSERT_EQUAL_INT(3, wifi_rssi_level_get(-30));
    TEST_ASSERT_EQUAL_INT(3, wifi_rssi_level_get(-65));
    TEST_ASSERT_EQUAL_INT(2, wifi_rssi_level_get(-66));
    TEST_ASSERT_EQUAL_INT(2, wifi_rssi_level_get(-87));
    TEST_ASSERT_EQUAL_INT(1, wifi_rssi_level_get(-88));
    TEST_ASSERT_EQUAL_INT(1, wifi_rssi_level_get(-120));
}

static void test_is_valid_ipv4(void)
{
    TEST_ASSERT_TRUE(is_valid_ipv4("192.168.1.10"));
    TEST_ASSERT_TRUE(is_valid_ipv4("0.0.0.0"));
    TEST_ASSERT_TRUE(is_valid_ipv4("255.255.255.255"));
    TEST_ASSERT_FALSE(is_valid_ipv4("256.1.1.1"));
    TEST_ASSERT_FALSE(is_valid_ipv4("01.2.3.4"));
    TEST_ASSERT_FALSE(is_valid_ipv4("1.2.3"));
    TEST_ASSERT_FALSE(is_valid_ipv4("1.2.3.4.5"));
    TEST_ASSERT_FALSE(is_valid_ipv4(" 1.2.3.4"));
    TEST_ASSERT_FALSE(is_valid_ipv4("homeassistant.local"));
    TEST_ASSERT_FALSE(is_valid_ipv4(""));
}

static void test_extract_ip_from_url(void)
{
    char ip[16];
    TEST_ASSERT_TRUE(extract_ip_from_url("mqtt://192.168.12.253:1883", ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("192.168.12.253", ip);
    TEST_ASSERT_TRUE(extract_ip_from_url("10.0.0.1", ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", ip);
    TEST_ASSERT_FALSE(extract_ip_from_url("mqtt://broker.emqx.io", ip, sizeof(ip)));

    char small[8];
    TEST_ASSERT_FALSE(extract_ip_from_url("mqtt://192.168.12.253", small, sizeof(small)));
}

static void test_assemble_broker_url(void)
{
    char url[64];
    assemble_broker_url("192.168.1.10", url, sizeof(url));
    TEST_ASSERT_EQUAL_STRING("mqtt://192.168.1.10:1883", url);

    char small[12];
    assemble_broker_url("192.168.1.10", small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("mqtt://192.", small);
}

static void test_ip_url_round_trip(void)
{
    char url[64], ip[16];
    assemble_broker_url("172.16.0.42", url, sizeof(url));
    TEST_ASSERT_TRUE(extract_ip_from_url(url, ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("172.16.0.42", ip);
}

static void test_normalize_broker_url(void)
{
    char out[64];
    TEST_ASSERT_TRUE(normalize_broker_url("192.168.1.10", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("mqtt://192.168.1.10", out);
    TEST_ASSERT_TRUE(normalize_broker_url("mqtt://192.168.1.10:1883", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("mqtt://192.168.1.10:1883", out);
    TEST_ASSERT_TRUE(normalize_broker_url("mqtts://broker.emqx.io", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("mqtts://broker.emqx.io", out);
    TEST_ASSERT_TRUE(normalize_broker_url("broker.emqx.io", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("mqtt://broker.emqx.io", out);

    TEST_ASSERT_FALSE(normalize_broker_url("", out, sizeof(out)));
    TEST_ASSERT_FALSE(normalize_broker_url(NULL, out, sizeof(out)));
    TEST_ASSERT_FALSE(normalize_broker_url("x", NULL, 4));

    char small[10];
    TEST_ASSERT_FALSE(normalize_broker_url("1.2.3.4", small, sizeof(small)));
    char exact[11]; /* "mqtt://1.2" + NUL */
    TEST_ASSERT_TRUE(normalize_broker_url("1.2", exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_STRING("mqtt://1.2", exact);
    TEST_ASSERT_FALSE(normalize_broker_url("1.2", small, sizeof(small)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rssi_levels);
    RUN_TEST(test_is_valid_ipv4);
    RUN_TEST(test_extract_ip_from_url);
    RUN_TEST(test_assemble_broker_url);
    RUN_TEST(test_ip_url_round_trip);
    RUN_TEST(test_normalize_broker_url);
    return UNITY_END();
}

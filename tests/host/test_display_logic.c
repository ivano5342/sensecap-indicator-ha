/* Display settings logic (main/display/display_logic.c). */
#include "unity.h"

#include "display_logic.h"

void setUp(void) {}
void tearDown(void) {}

static void test_brightness_clamp(void)
{
    TEST_ASSERT_EQUAL_INT(1, display_brightness_clamp(-5));
    TEST_ASSERT_EQUAL_INT(1, display_brightness_clamp(0));
    TEST_ASSERT_EQUAL_INT(1, display_brightness_clamp(1));
    TEST_ASSERT_EQUAL_INT(50, display_brightness_clamp(50));
    TEST_ASSERT_EQUAL_INT(99, display_brightness_clamp(99));
    TEST_ASSERT_EQUAL_INT(99, display_brightness_clamp(100));
    TEST_ASSERT_EQUAL_INT(99, display_brightness_clamp(255));
}

static void test_duty_matches_original_formulas(void)
{
    /* Original _lcd_bl_set: (uint32_t)(8192 - 1) * b / 100.0 after clamping.
     * Original _lcd_bl_init: (8192 - 1) * b / 100 (integer) after clamping. */
    for (int b = -20; b <= 300; b++) {
        int c = b > 99 ? 99 : (b < 1 ? 1 : b);
        uint32_t via_double = (uint32_t)((uint32_t)(8192 - 1) * c / 100.0);
        uint32_t via_int = (uint32_t)((8192 - 1) * c / 100);
        TEST_ASSERT_EQUAL_UINT32(via_double, display_brightness_to_duty(b));
        TEST_ASSERT_EQUAL_UINT32(via_int, display_brightness_to_duty(b));
    }
    TEST_ASSERT_EQUAL_UINT32(81, display_brightness_to_duty(1));
    TEST_ASSERT_EQUAL_UINT32(6552, display_brightness_to_duty(80));
    TEST_ASSERT_EQUAL_UINT32(8109, display_brightness_to_duty(99));
}

static void test_default_cfg(void)
{
    struct view_data_display cfg = {.brightness = 3, .sleep_mode_en = true, .sleep_mode_time_min = 9};
    display_cfg_default(&cfg);
    TEST_ASSERT_EQUAL_INT(80, cfg.brightness);
    TEST_ASSERT_FALSE(cfg.sleep_mode_en);
    TEST_ASSERT_EQUAL_INT(0, cfg.sleep_mode_time_min);
}

static void test_sleep_timeout(void)
{
    TEST_ASSERT_EQUAL_UINT64(0, display_sleep_timeout_us(false, 5));
    TEST_ASSERT_EQUAL_UINT64(0, display_sleep_timeout_us(true, 0));
    TEST_ASSERT_EQUAL_UINT64(60000000ULL, display_sleep_timeout_us(true, 1));
    TEST_ASSERT_EQUAL_UINT64(5ULL * 60 * 1000000, display_sleep_timeout_us(true, 5));
    /* No int overflow for large values (e.g. one day). */
    TEST_ASSERT_EQUAL_UINT64(1440ULL * 60 * 1000000, display_sleep_timeout_us(true, 1440));
}

static void test_sleep_minutes_parse(void)
{
    TEST_ASSERT_EQUAL_INT(0, display_sleep_minutes_parse(false, "10"));
    TEST_ASSERT_EQUAL_INT(0, display_sleep_minutes_parse(true, NULL));
    TEST_ASSERT_EQUAL_INT(0, display_sleep_minutes_parse(true, ""));
    TEST_ASSERT_EQUAL_INT(10, display_sleep_minutes_parse(true, "10"));
    TEST_ASSERT_EQUAL_INT(7, display_sleep_minutes_parse(true, "7min"));
    TEST_ASSERT_EQUAL_INT(0, display_sleep_minutes_parse(true, "abc"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_brightness_clamp);
    RUN_TEST(test_duty_matches_original_formulas);
    RUN_TEST(test_default_cfg);
    RUN_TEST(test_sleep_timeout);
    RUN_TEST(test_sleep_minutes_parse);
    return UNITY_END();
}

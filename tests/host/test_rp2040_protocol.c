/* ESP32S3 <-> RP2040 UART protocol (main/rp2040/rp2040_protocol.c + util/cobs.c). */
#include <string.h>

#include "unity.h"

#include "cobs.h"
#include "rp2040_protocol.h"
#include "sensor_format.h"

/* ── Recording sink ─────────────────────────────────────────────────────── */

#define MAX_PKTS 8

typedef struct {
    int count;
    size_t len[MAX_PKTS];
    uint8_t data[MAX_PKTS][RP2040_RX_BUF_SIZE];
    int oversize_count;
    size_t oversize_len;
} recorder_t;

static recorder_t rec;
static rp2040_rx_t rx;

static void rec_packet(void *ctx, uint8_t *p, size_t len)
{
    recorder_t *r = ctx;
    TEST_ASSERT_LESS_THAN(MAX_PKTS, r->count);
    r->len[r->count] = len;
    memcpy(r->data[r->count], p, len);
    r->count++;
}

static void rec_oversize(void *ctx, size_t len)
{
    recorder_t *r = ctx;
    r->oversize_count++;
    r->oversize_len = len;
}

static const rp2040_rx_sink_t sink = {rec_packet, rec_oversize, &rec};

void setUp(void)
{
    memset(&rec, 0, sizeof(rec));
    rp2040_rx_init(&rx);
}
void tearDown(void) {}

/* COBS-encode payload into out and append the 0x00 delimiter. */
static size_t frame(const uint8_t *payload, size_t len, uint8_t *out, size_t out_size)
{
    cobs_encode_result r = cobs_encode(out, out_size - 1, payload, len);
    TEST_ASSERT_EQUAL(COBS_ENCODE_OK, r.status);
    out[r.out_len] = 0x00;
    return r.out_len + 1;
}

static uint8_t xor_of(const uint8_t *p, size_t len)
{
    uint8_t x = 0;
    for (size_t i = 0; i < len; i++) {
        x ^= p[i];
    }
    return x;
}

/* ── Checksum / validation / classification ─────────────────────────────── */

static void test_xor_checksum(void)
{
    uint8_t ok[] = {0x12, 0x34, 0x12 ^ 0x34};
    uint8_t bad[] = {0x12, 0x34, 0x00};
    TEST_ASSERT_TRUE(rp2040_xor_checksum_ok(ok, sizeof(ok)));
    TEST_ASSERT_FALSE(rp2040_xor_checksum_ok(bad, sizeof(bad)));
    TEST_ASSERT_FALSE(rp2040_xor_checksum_ok(ok, 1));
    TEST_ASSERT_FALSE(rp2040_xor_checksum_ok(NULL, 3));
    uint8_t zero[] = {0x00, 0x00};
    TEST_ASSERT_TRUE(rp2040_xor_checksum_ok(zero, sizeof(zero)));
}

static void test_dynamic_detached(void)
{
    uint8_t p[3] = {PKT_TYPE_SENSOR_DETACHED, 0x10, 0};
    p[2] = xor_of(p, 2);
    TEST_ASSERT_TRUE(rp2040_dynamic_packet_is_valid(p, 3));
    TEST_ASSERT_EQUAL(RP2040_PKT_DYNAMIC_OK, rp2040_packet_classify(p, 3));

    p[2] ^= 0xFF;
    TEST_ASSERT_EQUAL(RP2040_PKT_DYNAMIC_MALFORMED, rp2040_packet_classify(p, 3));
}

static void test_dynamic_value(void)
{
    uint8_t p[7] = {PKT_TYPE_SENSOR_VALUE, 0x10, 1, 2, 3, 4, 0};
    p[6] = xor_of(p, 6);
    TEST_ASSERT_TRUE(rp2040_dynamic_packet_is_valid(p, 7));

    uint8_t q[8] = {PKT_TYPE_SENSOR_VALUE, 0x10, 1, 2, 3, 4, 5, 0};
    q[7] = xor_of(q, 7);
    TEST_ASSERT_FALSE(rp2040_dynamic_packet_is_valid(q, 8)); /* wrong length */
}

/* [type][id][cat][name_len][name...][unit_len][unit...][xor] */
static size_t build_attached(uint8_t *p, const char *name, const char *unit)
{
    size_t n = 0;
    p[n++] = PKT_TYPE_SENSOR_ATTACHED;
    p[n++] = PKT_SENSOR_ID_GROVE_BASE;
    p[n++] = PKT_SENSOR_CAT_TEMP;
    p[n++] = (uint8_t)strlen(name);
    memcpy(p + n, name, strlen(name));
    n += strlen(name);
    p[n++] = (uint8_t)strlen(unit);
    memcpy(p + n, unit, strlen(unit));
    n += strlen(unit);
    p[n] = xor_of(p, n);
    return n + 1;
}

static void test_dynamic_attached(void)
{
    uint8_t p[64];
    size_t n = build_attached(p, "AHT20", "C");
    TEST_ASSERT_TRUE(rp2040_dynamic_packet_is_valid(p, n));

    n = build_attached(p, "", "");
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_TRUE(rp2040_dynamic_packet_is_valid(p, n));

    /* name_len claims more bytes than present */
    n = build_attached(p, "AHT20", "C");
    p[3] = 40;
    p[n - 1] = xor_of(p, n - 1);
    TEST_ASSERT_FALSE(rp2040_dynamic_packet_is_valid(p, n));

    /* unit_len mismatch */
    n = build_attached(p, "AHT20", "C");
    p[4 + 5] = 2;
    p[n - 1] = xor_of(p, n - 1);
    TEST_ASSERT_FALSE(rp2040_dynamic_packet_is_valid(p, n));

    /* too short */
    uint8_t s[4] = {PKT_TYPE_SENSOR_ATTACHED, 0, 0, 0};
    s[3] = xor_of(s, 3);
    TEST_ASSERT_FALSE(rp2040_dynamic_packet_is_valid(s, 4));
}

static void test_classify_legacy_and_ignored(void)
{
    uint8_t legacy[5] = {PKT_TYPE_SENSOR_SCD41_CO2, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL(RP2040_PKT_LEGACY_SENSOR, rp2040_packet_classify(legacy, 5));
    /* Legacy packets are forwarded on len > 1; the sensor parser does the
     * stricter length check. */
    TEST_ASSERT_EQUAL(RP2040_PKT_LEGACY_SENSOR, rp2040_packet_classify(legacy, 2));
    TEST_ASSERT_EQUAL(RP2040_PKT_IGNORED, rp2040_packet_classify(legacy, 1));
    TEST_ASSERT_EQUAL(RP2040_PKT_IGNORED, rp2040_packet_classify(legacy, 0));
    TEST_ASSERT_EQUAL(RP2040_PKT_IGNORED, rp2040_packet_classify(NULL, 5));
}

static void test_classify_sen5x_collides_with_dynamic(void)
{
    /* PKT_TYPE_SENSOR_SEN5X_massConcentrationPm4p0 shares 0xB8 with
     * PKT_TYPE_SENSOR_ATTACHED: legacy-style SEN5x packets are treated as
     * (malformed) dynamic packets and dropped. */
    uint8_t p[5] = {PKT_TYPE_SENSOR_SEN5X_massConcentrationPm4p0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL(RP2040_PKT_DYNAMIC_MALFORMED, rp2040_packet_classify(p, 5));
}

/* ── Command framing ────────────────────────────────────────────────────── */

static void test_cmd_encode_no_payload(void)
{
    uint8_t out[RP2040_CMD_FRAME_MAX];
    int n = rp2040_cmd_frame_encode(PKT_TYPE_CMD_POWER_ON, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(3, n);
    const uint8_t expected[] = {0x02, PKT_TYPE_CMD_POWER_ON, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 3);
}

static void test_cmd_encode_payload_with_zeros_round_trips(void)
{
    uint32_t interval_ms = 1000; /* 0xE8 0x03 0x00 0x00 */
    uint8_t out[RP2040_CMD_FRAME_MAX];
    int n = rp2040_cmd_frame_encode(PKT_TYPE_CMD_COLLECT_INTERVAL, &interval_ms, sizeof(interval_ms),
                                    out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[n - 1]);
    for (int i = 0; i < n - 1; i++) {
        TEST_ASSERT_NOT_EQUAL(0x00, out[i]);
    }

    uint8_t dec[32];
    cobs_decode_result r = cobs_decode(dec, sizeof(dec), out, (size_t)n - 1);
    TEST_ASSERT_EQUAL(COBS_DECODE_OK, r.status);
    TEST_ASSERT_EQUAL_size_t(5, r.out_len);
    TEST_ASSERT_EQUAL_HEX8(PKT_TYPE_CMD_COLLECT_INTERVAL, dec[0]);
    TEST_ASSERT_EQUAL_MEMORY(&interval_ms, dec + 1, sizeof(interval_ms));
}

static void test_cmd_encode_limits(void)
{
    uint8_t payload[32] = {0};
    uint8_t out[RP2040_CMD_FRAME_MAX];
    TEST_ASSERT_GREATER_THAN(0, rp2040_cmd_frame_encode(0xA1, payload, 31, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, rp2040_cmd_frame_encode(0xA1, payload, 32, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, rp2040_cmd_frame_encode(0xA1, NULL, 0, out, 2));
    TEST_ASSERT_EQUAL_INT(-1, rp2040_cmd_frame_encode(0xA1, NULL, 0, NULL, 40));
    /* len > 0 with NULL payload sends just the command byte */
    TEST_ASSERT_EQUAL_INT(3, rp2040_cmd_frame_encode(0xA3, NULL, 4, out, sizeof(out)));
}

/* ── RX deframer ────────────────────────────────────────────────────────── */

static void test_rx_single_frame(void)
{
    const uint8_t pkt[] = {0xB2, 0x00, 0x00, 0x16, 0x44};
    uint8_t wire[16];
    size_t n = frame(pkt, sizeof(pkt), wire, sizeof(wire));

    rp2040_rx_feed(&rx, wire, n, &sink);
    TEST_ASSERT_EQUAL_INT(1, rec.count);
    TEST_ASSERT_EQUAL_size_t(sizeof(pkt), rec.len[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, rec.data[0], sizeof(pkt));
}

static void test_rx_multiple_frames_one_read(void)
{
    const uint8_t a[] = {0xB3, 1, 2, 3, 4};
    const uint8_t b[] = {0xB4, 0, 0, 0, 0};
    uint8_t wire[32];
    size_t n = frame(a, sizeof(a), wire, sizeof(wire));
    n += frame(b, sizeof(b), wire + n, sizeof(wire) - n);

    rp2040_rx_feed(&rx, wire, n, &sink);
    TEST_ASSERT_EQUAL_INT(2, rec.count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, rec.data[0], sizeof(a));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, rec.data[1], sizeof(b));
}

static void test_rx_frame_split_byte_by_byte(void)
{
    const uint8_t pkt[] = {0xB5, 0x00, 0x80, 0x3F, 0x00, 0x11};
    uint8_t wire[16];
    size_t n = frame(pkt, sizeof(pkt), wire, sizeof(wire));

    for (size_t i = 0; i < n; i++) {
        rp2040_rx_feed(&rx, &wire[i], 1, &sink);
        TEST_ASSERT_EQUAL_INT(i + 1 == n ? 1 : 0, rec.count);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, rec.data[0], sizeof(pkt));
}

static void test_rx_frame_split_across_reads(void)
{
    const uint8_t a[] = {0xB2, 9, 9, 9, 9};
    const uint8_t b[] = {0xB3, 8, 8, 8, 8};
    uint8_t wire[32];
    size_t n = frame(a, sizeof(a), wire, sizeof(wire));
    n += frame(b, sizeof(b), wire + n, sizeof(wire) - n);

    /* Cut in the middle of the second frame. */
    rp2040_rx_feed(&rx, wire, 9, &sink);
    TEST_ASSERT_EQUAL_INT(1, rec.count);
    rp2040_rx_feed(&rx, wire + 9, n - 9, &sink);
    TEST_ASSERT_EQUAL_INT(2, rec.count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, rec.data[1], sizeof(b));
}

static void test_rx_empty_frames_and_noise_ignored(void)
{
    const uint8_t zeros[] = {0x00, 0x00, 0x00};
    rp2040_rx_feed(&rx, zeros, sizeof(zeros), &sink);
    rp2040_rx_feed(&rx, NULL, 0, &sink);
    TEST_ASSERT_EQUAL_INT(0, rec.count);

    /* Invalid COBS (code byte points past the end) is dropped silently and
     * does not poison the next frame. */
    const uint8_t bad[] = {0x05, 0x11, 0x00};
    rp2040_rx_feed(&rx, bad, sizeof(bad), &sink);
    TEST_ASSERT_EQUAL_INT(0, rec.count);

    const uint8_t pkt[] = {0xB2, 1, 2, 3, 4};
    uint8_t wire[16];
    size_t n = frame(pkt, sizeof(pkt), wire, sizeof(wire));
    rp2040_rx_feed(&rx, wire, n, &sink);
    TEST_ASSERT_EQUAL_INT(1, rec.count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, rec.data[0], sizeof(pkt));
}

static void test_rx_oversize_fragment_dropped(void)
{
    uint8_t junk[300];
    memset(junk, 0x7E, sizeof(junk));

    rp2040_rx_feed(&rx, junk, sizeof(junk), &sink);
    TEST_ASSERT_EQUAL_INT(0, rec.oversize_count);
    rp2040_rx_feed(&rx, junk, sizeof(junk), &sink);
    TEST_ASSERT_EQUAL_INT(1, rec.oversize_count);
    TEST_ASSERT_EQUAL_size_t(600, rec.oversize_len);
    /* Historical behavior: the fragment restarts from the latest chunk. */
    TEST_ASSERT_EQUAL_size_t(300, rx.frag_len);

    /* Terminate the junk, then a good frame decodes normally. */
    const uint8_t z = 0x00;
    rp2040_rx_feed(&rx, &z, 1, &sink);
    TEST_ASSERT_EQUAL_size_t(0, rx.frag_len);
    int before = rec.count;

    const uint8_t pkt[] = {0xB4, 1, 2, 3, 4};
    uint8_t wire[16];
    size_t n = frame(pkt, sizeof(pkt), wire, sizeof(wire));
    rp2040_rx_feed(&rx, wire, n, &sink);
    TEST_ASSERT_EQUAL_INT(before + 1, rec.count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, rec.data[rec.count - 1], sizeof(pkt));
}

static void test_rx_null_sink_is_safe(void)
{
    const uint8_t pkt[] = {0xB2, 1, 2, 3, 4};
    uint8_t wire[16];
    size_t n = frame(pkt, sizeof(pkt), wire, sizeof(wire));
    rp2040_rx_feed(&rx, wire, n, NULL);
    TEST_ASSERT_EQUAL_size_t(0, rx.frag_len);
}

/* ── End to end: RP2040 float reading -> sensor type + value ────────────── */

static void test_end_to_end_sensor_reading(void)
{
    uint8_t pkt[5] = {PKT_TYPE_SENSOR_SHT41_HUMIDITY};
    float hum = 47.25f;
    memcpy(pkt + 1, &hum, sizeof(hum));
    uint8_t wire[16];
    size_t n = frame(pkt, sizeof(pkt), wire, sizeof(wire));

    rp2040_rx_feed(&rx, wire, n, &sink);
    TEST_ASSERT_EQUAL_INT(1, rec.count);
    TEST_ASSERT_EQUAL(RP2040_PKT_LEGACY_SENSOR, rp2040_packet_classify(rec.data[0], rec.len[0]));

    enum sensor_data_type type;
    float value;
    TEST_ASSERT_EQUAL(SENSOR_PKT_OK, sensor_pkt_decode(rec.data[0], rec.len[0], &type, &value));
    TEST_ASSERT_EQUAL(SHT41_SENSOR_HUMIDITY, type);
    TEST_ASSERT_EQUAL_FLOAT(47.25f, value);

    char buf[32];
    sensor_format_value(buf, sizeof(buf), type, value);
    TEST_ASSERT_EQUAL_STRING("47", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_xor_checksum);
    RUN_TEST(test_dynamic_detached);
    RUN_TEST(test_dynamic_value);
    RUN_TEST(test_dynamic_attached);
    RUN_TEST(test_classify_legacy_and_ignored);
    RUN_TEST(test_classify_sen5x_collides_with_dynamic);
    RUN_TEST(test_cmd_encode_no_payload);
    RUN_TEST(test_cmd_encode_payload_with_zeros_round_trips);
    RUN_TEST(test_cmd_encode_limits);
    RUN_TEST(test_rx_single_frame);
    RUN_TEST(test_rx_multiple_frames_one_read);
    RUN_TEST(test_rx_frame_split_byte_by_byte);
    RUN_TEST(test_rx_frame_split_across_reads);
    RUN_TEST(test_rx_empty_frames_and_noise_ignored);
    RUN_TEST(test_rx_oversize_fragment_dropped);
    RUN_TEST(test_rx_null_sink_is_safe);
    RUN_TEST(test_end_to_end_sensor_reading);
    return UNITY_END();
}

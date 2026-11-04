/**
 * @file rp2040_protocol.h
 *
 * Pure (hardware-independent) ESP32S3 <-> RP2040 UART protocol helpers:
 * packet type constants, packet validation/classification, command framing and
 * the COBS 0x00-delimited stream deframer.
 *
 * No ESP-IDF, FreeRTOS or UART driver dependencies; unit tested in tests/host/.
 * rp2040.c owns the UART task and dispatch. scripts/verify_rp2040_protocol.py
 * keeps the constants below in sync with rp2040/include/indicator_rp2040.hpp.
 */

#ifndef RP2040_PROTOCOL_H
#define RP2040_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum pkt_type_cmd
{
	PKT_TYPE_NONE = 0,
	PKT_TYPE_CMD_COLLECT_INTERVAL = 0xA0, // uin32_t
	PKT_TYPE_CMD_BEEP_ON = 0xA1, // uin32_t  ms: on time
	PKT_TYPE_CMD_BEEP_OFF = 0xA2,
	PKT_TYPE_CMD_SHUTDOWN = 0xA3,
	PKT_TYPE_CMD_POWER_ON = 0xA4,
	PKT_TYPE_CMD_RESCAN_GROVE = 0xA5,
};

enum pkt_type_data
{
	// Inner Sensor
	// PKT_TYPE_SENSOR_SCD41_TEMP      = 0xB0, // not accurate
	// PKT_TYPE_SENSOR_SCD41_HUMIDITY = 0xB1,
	PKT_TYPE_SENSOR_SCD41_CO2 = 0xB2, // float

	// SHT41 AHT
	PKT_TYPE_SENSOR_SHT41_TEMP = 0xB3,
	PKT_TYPE_SENSOR_SHT41_HUMIDITY = 0xB4, // float
	
	// Inner Sensor SGP40
	PKT_TYPE_SENSOR_SGP40_TVOC_INDEX = 0xB5, // float

	// Dynamic sensor registry packets.
	PKT_TYPE_SENSOR_ATTACHED = 0xB8,
	PKT_TYPE_SENSOR_DETACHED = 0xB9,
	PKT_TYPE_SENSOR_VALUE = 0xBA,

	// SEN5x
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm1p0 = 0xB6,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm2p5 = 0xB7,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm4p0 = 0xB8,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm10p0 = 0xB9,
	PKT_TYPE_SENSOR_SEN5X_ambientHumidity = 0xBA,
	PKT_TYPE_SENSOR_SEN5X_ambientTemperature = 0xBB,
	PKT_TYPE_SENSOR_SEN5X_vocIndex = 0xBC,
	PKT_TYPE_SENSOR_SEN5X_noxIndex = 0xBD,

	// SFA3X
	PKT_TYPE_SENSOR_SFA3X_HCHO = 0xBE,
	PKT_TYPE_SENSOR_SFA3X_HUMIDITY = 0xBF,
	PKT_TYPE_SENSOR_SFA3X_TEMP = 0xC0,
};

#define PKT_SENSOR_ID_AHT20_TEMP 0
#define PKT_SENSOR_ID_AHT20_HUMIDITY 1
#define PKT_SENSOR_ID_SCD41_CO2 2
#define PKT_SENSOR_ID_SGP40_VOC 3
#define PKT_SENSOR_ID_SCD41_TEMP 4
#define PKT_SENSOR_ID_SCD41_HUMIDITY 5
#define PKT_SENSOR_ID_GROVE_BASE 0x10

#define PKT_SENSOR_CAT_TEMP 0
#define PKT_SENSOR_CAT_HUMIDITY 1
#define PKT_SENSOR_CAT_CO2 2
#define PKT_SENSOR_CAT_VOC 3
#define PKT_SENSOR_CAT_LIGHT 4
#define PKT_SENSOR_CAT_PRESSURE 5
#define PKT_SENSOR_CAT_PM 6
#define PKT_SENSOR_CAT_NOX 7
#define PKT_SENSOR_CAT_UNKNOWN 255

/* Maximum command payload accepted by rp2040_cmd_frame_encode(). */
#define RP2040_CMD_PAYLOAD_MAX 31
/* Output buffer size that always fits an encoded command frame. */
#define RP2040_CMD_FRAME_MAX   40

/* Size of the RX fragment / decode buffers (matches the UART read buffer). */
#define RP2040_RX_BUF_SIZE     512

typedef enum {
	RP2040_PKT_IGNORED = 0,       /* empty or single-byte legacy packet */
	RP2040_PKT_LEGACY_SENSOR,     /* forward to _sensor_data_parse_handle() */
	RP2040_PKT_DYNAMIC_OK,        /* well-formed dynamic sensor registry packet */
	RP2040_PKT_DYNAMIC_MALFORMED, /* dynamic packet failing length/checksum */
} rp2040_pkt_class_t;

/* XOR of all bytes except the last must equal the last byte (len >= 2). */
bool rp2040_xor_checksum_ok(const uint8_t* p_data, size_t len);

/* Validate PKT_TYPE_SENSOR_ATTACHED / _DETACHED / _VALUE framing + checksum. */
bool rp2040_dynamic_packet_is_valid(const uint8_t* p_data, size_t len);

/* Decide what to do with one decoded packet. */
rp2040_pkt_class_t rp2040_packet_classify(const uint8_t* p_data, size_t len);

/*
 * Build a 0x00-terminated COBS frame for [cmd][payload...].
 * Returns the number of bytes to write (including the terminator), or -1 if
 * len > RP2040_CMD_PAYLOAD_MAX or the frame does not fit in out_size.
 */
int rp2040_cmd_frame_encode(uint8_t cmd, const void* p_data, uint8_t len, uint8_t* out,
							size_t out_size);

/* Receiver hooks invoked synchronously from rp2040_rx_feed(). */
typedef struct {
	/* One successfully COBS-decoded packet; buffer valid only during the call. */
	void (*on_packet)(void* ctx, uint8_t* p_data, size_t len);
	/* A fragment exceeded RP2040_RX_BUF_SIZE and was dropped (optional). */
	void (*on_oversize)(void* ctx, size_t len);
	void* ctx;
} rp2040_rx_sink_t;

/* Stream deframer state; bytes may arrive split across any number of reads. */
typedef struct {
	uint8_t frag[RP2040_RX_BUF_SIZE];
	size_t frag_len;
	uint8_t data[RP2040_RX_BUF_SIZE];
} rp2040_rx_t;

void rp2040_rx_init(rp2040_rx_t* rx);

/* Feed raw UART bytes; emits one on_packet per complete, decodable frame. */
void rp2040_rx_feed(rp2040_rx_t* rx, const uint8_t* buf, size_t len, const rp2040_rx_sink_t* sink);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* RP2040_PROTOCOL_H */

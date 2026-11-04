/**
 * @file rp2040_protocol.c
 *
 * Pure ESP32S3 <-> RP2040 protocol logic, split out of rp2040.c so it can be
 * unit tested on the host. Behavior is unchanged from the original inline code.
 */
#include "rp2040_protocol.h"

#include <string.h>

#include "cobs.h"

bool rp2040_xor_checksum_ok(const uint8_t* p_data, size_t len) {
	if(p_data == NULL || len < 2)
	{
		return false;
	}

	uint8_t checksum = 0;
	for(size_t i = 0; i < len - 1; i++)
	{
		checksum ^= p_data[i];
	}
	return checksum == p_data[len - 1];
}

bool rp2040_dynamic_packet_is_valid(const uint8_t* p_data, size_t len) {
	if(len < 2 || !rp2040_xor_checksum_ok(p_data, len))
	{
		return false;
	}

	switch(p_data[0])
	{
		case PKT_TYPE_SENSOR_ATTACHED: {
			if(len < 5)
			{
				return false;
			}
			uint8_t name_len = p_data[3];
			size_t unit_len_offset = 4U + name_len;
			if(unit_len_offset >= len - 1)
			{
				return false;
			}
			uint8_t unit_len = p_data[unit_len_offset];
			return unit_len_offset + 1U + unit_len == len - 1;
		}
		case PKT_TYPE_SENSOR_DETACHED:
			return len == 3;
		case PKT_TYPE_SENSOR_VALUE:
			return len == 7;
		default:
			return false;
	}
}

rp2040_pkt_class_t rp2040_packet_classify(const uint8_t* p_data, size_t len) {
	if(p_data == NULL || len < 1)
	{
		return RP2040_PKT_IGNORED;
	}

	switch(p_data[0])
	{
		case PKT_TYPE_SENSOR_ATTACHED:
		case PKT_TYPE_SENSOR_DETACHED:
		case PKT_TYPE_SENSOR_VALUE:
			return rp2040_dynamic_packet_is_valid(p_data, len) ? RP2040_PKT_DYNAMIC_OK
															   : RP2040_PKT_DYNAMIC_MALFORMED;
		default:
			return (len > 1) ? RP2040_PKT_LEGACY_SENSOR : RP2040_PKT_IGNORED;
	}
}

int rp2040_cmd_frame_encode(uint8_t cmd, const void* p_data, uint8_t len, uint8_t* out,
							size_t out_size) {
	uint8_t data[RP2040_CMD_PAYLOAD_MAX + 1] = {0};

	if(len > RP2040_CMD_PAYLOAD_MAX || out == NULL || out_size < 2)
	{
		return -1;
	}

	size_t index = 1;
	data[0] = cmd;

	if(len > 0 && p_data != NULL)
	{
		memcpy(&data[1], p_data, len);
		index += len;
	}

	/* Reserve one byte for the 0x00 frame delimiter. */
	cobs_encode_result ret = cobs_encode(out, out_size - 1, data, index);
	if(ret.status != COBS_ENCODE_OK)
	{
		return -1;
	}
	out[ret.out_len] = 0x00;
	return (int)(ret.out_len + 1);
}

void rp2040_rx_init(rp2040_rx_t* rx) {
	memset(rx, 0, sizeof(*rx));
}

void rp2040_rx_feed(rp2040_rx_t* rx, const uint8_t* buf, size_t len, const rp2040_rx_sink_t* sink) {
	const uint8_t* p_buf_start = buf;
	const uint8_t* p_buf_stop = buf + len;

	while(p_buf_start < p_buf_stop)
	{
		const uint8_t* p_buf_end = p_buf_start;
		while(p_buf_end < p_buf_stop && *p_buf_end != 0x00)
		{
			p_buf_end++;
		}

		size_t chunk_len = (size_t)(p_buf_end - p_buf_start);
		if(chunk_len > 0)
		{
			if(rx->frag_len + chunk_len > sizeof(rx->frag))
			{
				if(sink && sink->on_oversize)
				{
					sink->on_oversize(sink->ctx, rx->frag_len + chunk_len);
				}
				rx->frag_len = 0;
			}
			if(rx->frag_len + chunk_len <= sizeof(rx->frag))
			{
				memcpy(rx->frag + rx->frag_len, p_buf_start, chunk_len);
				rx->frag_len += chunk_len;
			}
		}

		if(p_buf_end < p_buf_stop) /* hit a 0x00 delimiter: frame complete */
		{
			if(rx->frag_len > 0)
			{
				memset(rx->data, 0, sizeof(rx->data));
				cobs_decode_result ret =
					cobs_decode(rx->data, sizeof(rx->data), rx->frag, rx->frag_len);
				if(ret.status == COBS_DECODE_OK && sink && sink->on_packet)
				{
					sink->on_packet(sink->ctx, rx->data, ret.out_len);
				}
			}
			rx->frag_len = 0;
		}

		p_buf_start = (p_buf_end < p_buf_stop) ? (p_buf_end + 1) : p_buf_stop; // next message
	}
}

/**
 * @file rp2040.c
 * @date  29 November 2023

 * @author Spencer Yan
 *
 * @copyright © 2023, Seeed Studio
 */
#include "rp2040.h"
#include "esp_log.h"
#include "rp2040_protocol.h"
#include "driver/uart.h"
#include "view_data.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "esp32_rp2040";

#define ESP32_RP2040_TXD				  (19)
#define ESP32_RP2040_RXD				  (20)
#define ESP32_RP2040_RTS				  (UART_PIN_NO_CHANGE)
#define ESP32_RP2040_CTS				  (UART_PIN_NO_CHANGE)

#define ESP32_COMM_PORT_NUM				  (2)
#define ESP32_COMM_BAUD_RATE			  (115200)
#define ESP32_RP2040_COMM_TASK_STACK_SIZE (1024 * 4)
#define BUF_SIZE						  (512)

_Static_assert(BUF_SIZE == RP2040_RX_BUF_SIZE, "RX deframer buffers must match the UART read buffer");

static uint8_t buf[BUF_SIZE];

/**
 * @brief sensor data parse
 * @attention should be implemented in indicator_sensor_model.c
 */
__attribute__((weak)) int _sensor_data_parse_handle(uint8_t* p_data, size_t len) {
	ESP_LOGI(TAG, "_sensor_data_parse_handle, please import the code from indicator_sensor_model");
	return -1;
}

/**
 * @brief send cmd to RP2040
 *
 * @param cmd
 * @param p_data
 * @param len
 * @return int
 */
static int _cmd_send(uint8_t cmd, void* p_data, uint8_t len) {
	uint8_t buf[RP2040_CMD_FRAME_MAX] = {0};

	int frame_len = rp2040_cmd_frame_encode(cmd, p_data, len, buf, sizeof(buf));
#if SENSOR_COMM_DEBUG
	ESP_LOGI(TAG, "encode len:%d", frame_len);
	for(int i = 0; i < frame_len; i++)
	{
		printf("0x%x ", buf[i]);
	}
	printf("\r\n");
#endif

	if(frame_len > 0)
	{
		return uart_write_bytes(ESP32_COMM_PORT_NUM, buf, frame_len);
	}
	return -1;
}

static void _packet_handle(void* ctx, uint8_t* p_data, size_t len) {
	(void)ctx;
#if SENSOR_COMM_DEBUG
	ESP_LOGI(TAG, "decode len:%d, type:0x%x  ", (int)len, p_data[0]);
	printf("decode: ");
	for(size_t i = 0; i < len; i++)
	{
		printf("0x%x ", p_data[i]);
	}
	printf("\r\n");
#endif

	switch(rp2040_packet_classify(p_data, len))
	{
		case RP2040_PKT_DYNAMIC_MALFORMED:
			ESP_LOGW(TAG, "drop malformed dynamic sensor packet type=0x%x len=%d", p_data[0],
					 (int)len);
			break;
		case RP2040_PKT_DYNAMIC_OK:
			ESP_LOGD(TAG, "dynamic sensor packet type=0x%x len=%d", p_data[0], (int)len);
			break;
		case RP2040_PKT_LEGACY_SENSOR:
			_sensor_data_parse_handle(p_data, len);
			break;
		case RP2040_PKT_IGNORED:
		default:
			break;
	}
}

static void _rx_oversize(void* ctx, size_t len) {
	(void)ctx;
	ESP_LOGW(TAG, "drop oversized COBS fragment len=%u", (unsigned)len);
}

static rp2040_rx_t s_rx;

static void esp32_rp2040_comm_task(void* arg) {
	uart_config_t uart_config = {
		.baud_rate = ESP32_COMM_BAUD_RATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	int intr_alloc_flags = 0;

	ESP_ERROR_CHECK(
		uart_driver_install(ESP32_COMM_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
	ESP_ERROR_CHECK(uart_param_config(ESP32_COMM_PORT_NUM, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(ESP32_COMM_PORT_NUM, ESP32_RP2040_TXD, ESP32_RP2040_RXD,
								 ESP32_RP2040_RTS, ESP32_RP2040_CTS));

	_cmd_send(PKT_TYPE_CMD_POWER_ON, NULL, 0);

	const rp2040_rx_sink_t sink = {
		.on_packet = _packet_handle,
		.on_oversize = _rx_oversize,
		.ctx = NULL,
	};
	rp2040_rx_init(&s_rx);

	while(1)
	{
		int len = uart_read_bytes(ESP32_COMM_PORT_NUM, buf, (BUF_SIZE - 1), pdMS_TO_TICKS(20));
#if SENSOR_COMM_DEBUG
		ESP_LOGI(TAG, "len:%d", len);
#endif
		if(len > 0)
		{
#if SENSOR_COMM_DEBUG
			printf("recv:  ");
			for(int i = 0; i < len; i++)
			{
				printf("0x%x ", buf[i]);
			}
			printf("\r\n");
#endif
			rp2040_rx_feed(&s_rx, buf, (size_t)len, &sink);
		}
	}
}

static void _sensor_shutdown(void) {
	int ret = 0;
	ret = _cmd_send(PKT_TYPE_CMD_SHUTDOWN, NULL, 0);
	if(ret <= 0)
	{
		ESP_LOGI(TAG, "sensor shutdown fail!. %d", ret);
	}
}
static void _commu_event_handler(void* handler_args, esp_event_base_t base, int32_t id,
								  void* event_data) {
	switch(id)
	{
		case VIEW_EVENT_SHUTDOWN:
			ESP_LOGI(TAG, "event: VIEW_EVENT_SHUTDOWN");
			_sensor_shutdown();
			break;
		default:
			break;
	}
}

void esp32_rp2040_init(void) {
	xTaskCreate(esp32_rp2040_comm_task, "esp32_rp2040_comm_task", ESP32_RP2040_COMM_TASK_STACK_SIZE,
				NULL, 5, NULL);
	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
															 VIEW_EVENT_SHUTDOWN,
															 _commu_event_handler, NULL, NULL));
}

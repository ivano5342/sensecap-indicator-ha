#include "overview_view.h"

#include "esp_log.h"
#include "lv_port.h"
#include "lvgl.h"
#include "nav.h"
#include "network_sensor_view.h"
#include "sen66_view.h"
#include "view_data.h"

#define OVERVIEW_MARGIN 4
#define OVERVIEW_CARD_H 200 /* AQI cards */
#define OVERVIEW_GAP    4
#define OVERVIEW_SEN66_W 234
#define OVERVIEW_SEN66_H 132
#define OVERVIEW_SEN66_COLS 2

static const char *TAG = "overview-view";

static const enum sensor_data_type s_sen66_types[] = {
	SEN66_SENSOR_PM2_5,
	SEN66_SENSOR_PM10_0,
	SEN66_SENSOR_CO2,
	SEN66_SENSOR_VOC,
};

void overview_view_init(void) {
	lv_port_sem_take();
	lv_obj_t *tile = nav_get_tile(NAV_TILE_OVERVIEW);
	if(tile == NULL)
	{
		lv_port_sem_give();
		ESP_LOGE(TAG, "Overview tile is not init");
		return;
	}

	/* The views that own these cards keep them updated. */
	network_sensor_view_add_aqi_cards(tile, OVERVIEW_MARGIN, OVERVIEW_CARD_H);
	int sen66_top = OVERVIEW_MARGIN + OVERVIEW_CARD_H + OVERVIEW_GAP;
	for(size_t i = 0; i < sizeof(s_sen66_types) / sizeof(s_sen66_types[0]); i++)
	{
		int col = i % OVERVIEW_SEN66_COLS;
		int row = i / OVERVIEW_SEN66_COLS;
		sen66_view_add_card(tile, s_sen66_types[i],
							OVERVIEW_MARGIN + col * (OVERVIEW_SEN66_W + OVERVIEW_GAP),
							sen66_top + row * (OVERVIEW_SEN66_H + OVERVIEW_GAP),
							OVERVIEW_SEN66_W, OVERVIEW_SEN66_H);
	}
	lv_port_sem_give();
}

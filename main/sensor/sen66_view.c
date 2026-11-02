#include "sen66_view.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "lv_port.h"
#include "lvgl.h"
#include "nav.h"
#include "view_data.h"

#define SEN66_CARD_WIDTH  150
#define SEN66_CARD_HEIGHT 130
#define SEN66_CARD_GAP    6
#define SEN66_GRID_LEFT   9
#define SEN66_GRID_TOP    70 /* below the settings gear */
#define SEN66_GRID_COLS   3

#define SEN66_STALE_AFTER_MS   30000
#define SEN66_STALE_CHECK_MS   1000
#define SEN66_HEADER_TEXT      "SEN66 Air Quality"
#define SEN66_HEADER_STALE     LV_SYMBOL_WARNING " STALE " LV_SYMBOL_WARNING
#define SEN66_COLOR_HEADER     0xFFFFFF
#define SEN66_COLOR_STALE      0xFF8000

LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_36);

static const char *TAG = "sen66-view";

typedef struct {
	enum sensor_data_type type;
	const char *name;
	const char *unit; /* the built-in fonts have no micro or superscript glyphs */
	const char *format;
	uint32_t color;
} sen66_card_spec_t;

static const sen66_card_spec_t s_card_specs[] = {
	{SEN66_SENSOR_PM1_0, "PM1.0", "ug/m3", "%.1f", 0x9FA8DA},
	{SEN66_SENSOR_PM2_5, "PM2.5", "ug/m3", "%.1f", 0x9FA8DA},
	{SEN66_SENSOR_PM4_0, "PM4.0", "ug/m3", "%.1f", 0x9FA8DA},
	{SEN66_SENSOR_PM10_0, "PM10", "ug/m3", "%.1f", 0x9FA8DA},
	{SEN66_SENSOR_VOC, "VOC", "index", "%.0f", 0xD76D46},
	{SEN66_SENSOR_NOX, "NOx", "index", "%.0f", 0xD76D46},
	{SEN66_SENSOR_CO2, "CO2", "ppm", "%.0f", 0x4F9E52},
	{SHT41_SENSOR_TEMP, "Temp", "\xC2\xB0" "C", "%.1f", 0xECBF41},
	{SHT41_SENSOR_HUMIDITY, "Humidity", "%", "%.0f", 0x52AAE5},
};

#define SEN66_CARD_COUNT (sizeof(s_card_specs) / sizeof(s_card_specs[0]))

static lv_obj_t *s_header;
static lv_obj_t *s_cards[SEN66_CARD_COUNT];
static lv_obj_t *s_values[SEN66_CARD_COUNT];

/* Second copies of some cards, on the overview page. NULL where there is no copy. */
static lv_obj_t *s_extra_cards[SEN66_CARD_COUNT];
static lv_obj_t *s_extra_values[SEN66_CARD_COUNT];

/* Guarded by the LVGL lock: the event task writes them, the LVGL timer reads them. */
static bool s_seen;
static bool s_stale;
static uint32_t s_last_packet_tick;

static bool sen66_type_is_shared(enum sensor_data_type type) {
	return type == SHT41_SENSOR_TEMP || type == SHT41_SENSOR_HUMIDITY;
}

static void sen66_set_stale(bool stale) {
	s_stale = stale;
	lv_label_set_text(s_header, stale ? SEN66_HEADER_STALE : SEN66_HEADER_TEXT);
	lv_obj_set_style_text_color(s_header, lv_color_hex(stale ? SEN66_COLOR_STALE : SEN66_COLOR_HEADER),
								LV_PART_MAIN | LV_STATE_DEFAULT);
	for(size_t i = 0; i < SEN66_CARD_COUNT; i++)
	{
		lv_obj_set_style_opa(s_cards[i], stale ? LV_OPA_40 : LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
		if(s_extra_cards[i] != NULL)
		{
			lv_obj_set_style_opa(s_extra_cards[i], stale ? LV_OPA_40 : LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
		}
	}
}

static void sen66_stale_timer_cb(lv_timer_t *timer) {
	LV_UNUSED(timer);
	if(s_seen && !s_stale && lv_tick_elaps(s_last_packet_tick) >= SEN66_STALE_AFTER_MS)
	{
		sen66_set_stale(true);
	}
}

static void sen66_on_sensor_data(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
	const struct view_data_sensor_data *data = event_data;
	if(id != VIEW_EVENT_SENSOR_DATA || data == NULL)
	{
		return;
	}

	bool shared = sen66_type_is_shared(data->sensor_type);
	const sen66_card_spec_t *spec = NULL;
	size_t index = 0;
	for(; index < SEN66_CARD_COUNT; index++)
	{
		if(s_card_specs[index].type == data->sensor_type)
		{
			spec = &s_card_specs[index];
			break;
		}
	}
	if(spec == NULL)
	{
		return;
	}

	char buf[16];
	snprintf(buf, sizeof(buf), spec->format, data->value);

	lv_port_sem_take();
	if(shared)
	{
		/*
		 * Temperature and humidity share packets with the SHT41 and AHT20, so they
		 * don't prove the SEN66 is alive. Only show them alongside live SEN66 data.
		 */
		if(s_seen && !s_stale)
		{
			lv_label_set_text(s_values[index], buf);
		}
	}
	else
	{
		s_seen = true;
		s_last_packet_tick = lv_tick_get();
		if(s_stale)
		{
			sen66_set_stale(false);
		}
		lv_label_set_text(s_values[index], buf);
		if(s_extra_values[index] != NULL)
		{
			lv_label_set_text(s_extra_values[index], buf);
		}
	}
	lv_port_sem_give();
}

static lv_obj_t *sen66_build_card(lv_obj_t *parent, const sen66_card_spec_t *spec, const char *name_text,
								  int x, int y, int w, int h, const lv_font_t *label_font,
								  const lv_font_t *value_font, lv_obj_t **value_out) {
	lv_color_t accent = lv_color_hex(spec->color);

	lv_obj_t *card = lv_obj_create(parent);
	lv_obj_set_size(card, w, h);
	lv_obj_set_pos(card, x, y);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(card, lv_color_hex(0x282828), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t *name = lv_label_create(card);
	lv_label_set_text(name, name_text);
	lv_obj_set_style_text_color(name, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(name, label_font, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 12);

	lv_obj_t *value = lv_label_create(card);
	lv_label_set_text(value, "N/A");
	lv_obj_set_style_text_color(value, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(value, value_font, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(value, LV_ALIGN_CENTER, 0, 2);

	lv_obj_t *unit = lv_label_create(card);
	lv_label_set_text(unit, spec->unit);
	lv_obj_set_style_text_color(unit, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(unit, label_font, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(unit, LV_ALIGN_BOTTOM_MID, 0, -12);

	*value_out = value;
	return card;
}

static void sen66_create_card(lv_obj_t *tile, size_t index) {
	const sen66_card_spec_t *spec = &s_card_specs[index];
	int col = index % SEN66_GRID_COLS;
	int row = index / SEN66_GRID_COLS;

	s_cards[index] = sen66_build_card(tile, spec, spec->name,
									  SEN66_GRID_LEFT + col * (SEN66_CARD_WIDTH + SEN66_CARD_GAP),
									  SEN66_GRID_TOP + row * (SEN66_CARD_HEIGHT + SEN66_CARD_GAP),
									  SEN66_CARD_WIDTH, SEN66_CARD_HEIGHT,
									  &lv_font_montserrat_18, &lv_font_montserrat_28, &s_values[index]);
}

void sen66_view_add_card(lv_obj_t *parent, enum sensor_data_type type, int x, int y, int w, int h) {
	if(parent == NULL)
	{
		return;
	}
	for(size_t i = 0; i < SEN66_CARD_COUNT; i++)
	{
		if(s_card_specs[i].type == type && s_extra_cards[i] == NULL)
		{
			/* There's no header on the page these cards go on, so name the sensor. */
			char name[32];
			snprintf(name, sizeof(name), "SEN66 %s", s_card_specs[i].name);
			s_extra_cards[i] = sen66_build_card(parent, &s_card_specs[i], name, x, y, w, h,
												&lv_font_montserrat_18, &lv_font_montserrat_36,
												&s_extra_values[i]);
			return;
		}
	}
	ESP_LOGE(TAG, "Can't add a card for sensor type %d", type);
}

void sen66_view_init(void) {
	lv_port_sem_take();
	lv_obj_t *tile = nav_get_tile(NAV_TILE_SEN66);
	if(tile == NULL)
	{
		lv_port_sem_give();
		ESP_LOGE(TAG, "SEN66 tile is not init");
		return;
	}

	s_header = lv_label_create(tile);
	lv_label_set_text(s_header, SEN66_HEADER_TEXT);
	lv_obj_set_style_text_color(s_header, lv_color_hex(SEN66_COLOR_HEADER), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(s_header, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(s_header, LV_ALIGN_TOP_MID, 0, 26);

	for(size_t i = 0; i < SEN66_CARD_COUNT; i++)
	{
		sen66_create_card(tile, i);
	}

	lv_timer_create(sen66_stale_timer_cb, SEN66_STALE_CHECK_MS, NULL);
	lv_port_sem_give();

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
															 VIEW_EVENT_SENSOR_DATA, sen66_on_sensor_data,
															 NULL, NULL));
}

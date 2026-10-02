#include "network_sensor_view.h"
#include "network_sensor_config.h"
#include "view_data.h"
#include "lv_port.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_80_digits);

static const char* TAG = "network-sensor-view";

static lv_obj_t* s_screen = NULL;
static lv_obj_t* s_header = NULL;
static lv_obj_t* s_labels[NETWORK_SENSOR_FIELD_COUNT];
static lv_obj_t* s_col_headers[NETWORK_SENSOR_COUNT];
static lv_obj_t* s_aqi_cards[NETWORK_SENSOR_COUNT];
static lv_obj_t* s_aqi_value_labels[NETWORK_SENSOR_COUNT];

/* Apply small-font style to a label */
static void _apply_small_style(lv_obj_t* lbl) {
    lv_obj_set_style_text_font(lbl, lv_theme_get_font_small(lbl), 0);
    lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* Unpack an RGB color packed as uint32_t → float */
static lv_color_t _unpack_color(float packed) {
    uint32_t rgb = *(uint32_t*)&packed;
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return lv_color_make(r, g, b);
}


/* Pulse the header text opacity while stale */
static void _header_pulse_cb(void* obj, int32_t v) {
    lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* Show or clear the stale state: header note plus dimmed AQI cards. Values stay as the last good reading. */
static void _on_network_sensor_stale_event(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data) {
    if (base != VIEW_EVENT_BASE || id != VIEW_EVENT_NETWORK_SENSOR_STALE || event_data == NULL) {
        return;
    }
    bool stale = ((struct view_data_network_sensor_stale*)event_data)->is_stale;

    lv_port_sem_take();
    if (s_header) {
        lv_obj_set_style_text_color(s_header, stale ? lv_color_hex(0xFF8000) : lv_color_hex(0xFFFFFF),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        /* 24 pt (~28 px line) fits between header y=4 and the cards at y=32 */
        lv_obj_set_style_text_font(s_header, stale ? &lv_font_montserrat_24 : lv_theme_get_font_normal(s_header),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_header, stale ? LV_SYMBOL_WARNING " STALE " LV_SYMBOL_WARNING : "Network Sensors");
        lv_obj_align(s_header, LV_ALIGN_TOP_MID, 0, stale ? 4 : 8);

        lv_anim_delete(s_header, _header_pulse_cb);
        lv_obj_set_style_text_opa(s_header, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        if (stale) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_header);
            lv_anim_set_exec_cb(&a, _header_pulse_cb);
            lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
            lv_anim_set_duration(&a, 600);
            lv_anim_set_reverse_duration(&a, 600);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        }
    }
    for (int c = 0; c < NETWORK_SENSOR_COUNT; c++) {
        if (s_aqi_cards[c]) {
            lv_obj_set_style_opa(s_aqi_cards[c], stale ? LV_OPA_40 : LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    lv_port_sem_give();
}

/* Event handler for network sensor updates */
static void _on_network_sensor_event(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data) {
    if (base != VIEW_EVENT_BASE || id != VIEW_EVENT_NETWORK_SENSOR_DATA) {
        return;
    }

    struct view_data_sensor_data* v = (struct view_data_sensor_data*)event_data;
    if (v == NULL) return;

    enum sensor_data_type type = v->sensor_type;

    // Handle AQI color events — update card background
    if (type == NETWORK_SENSOR_AQI_COLOR_A || type == NETWORK_SENSOR_AQI_COLOR_B) {
        int idx = (type == NETWORK_SENSOR_AQI_COLOR_A) ? NETWORK_SENSOR_A : NETWORK_SENSOR_B;
        lv_port_sem_take();
        if (s_aqi_cards[idx]) {
            lv_obj_set_style_bg_color(s_aqi_cards[idx], _unpack_color(v->value), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_port_sem_give();
        return;
    }

    // Handle AQI value events — update card value label
    if (type == NETWORK_SENSOR_PM25_AQI_A || type == NETWORK_SENSOR_PM25_AQI_B) {
        int idx = (type == NETWORK_SENSOR_PM25_AQI_A) ? NETWORK_SENSOR_A : NETWORK_SENSOR_B;
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", v->value);
        lv_port_sem_take();
        if (s_aqi_value_labels[idx]) lv_label_set_text(s_aqi_value_labels[idx], buf);
        lv_port_sem_give();
        return;
    }

    // Handle all other fields — update column labels
    for (int i = 0; network_sensor_field_map[i].json_key != NULL; i++) {
        if (network_sensor_field_map[i].sensor_type == type) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f %s", v->value, network_sensor_field_map[i].unit);

            lv_port_sem_take();
            if (s_labels[i]) lv_label_set_text(s_labels[i], buf);
            lv_port_sem_give();
            break;
        }
    }
}

void network_sensor_view_init(lv_obj_t* tile_parent, esp_event_loop_handle_t event_handle) {
    if (tile_parent == NULL) return;

    lv_port_sem_take();

    s_screen = lv_obj_create(tile_parent);
    lv_obj_set_size(s_screen, lv_obj_get_width(tile_parent), lv_obj_get_height(tile_parent));
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    int screen_w = lv_obj_get_width(tile_parent);
    int screen_h = lv_obj_get_height(tile_parent);

    // Title
    lv_obj_t* header = lv_label_create(s_screen);
    lv_label_set_text(header, "Network Sensors");
    lv_obj_set_style_text_color(header, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(header, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);
    s_header = header;

    // AQI Cards (top row, large)
    int card_w = (screen_w - 12) / 2;
    int card_h = 160;
    int card_y = 32;
    static const char* col_names[] = {"CH A", "CH B"};

    for (int c = 0; c < NETWORK_SENSOR_COUNT; c++) {
        lv_obj_t* card = lv_obj_create(s_screen);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, 4 + c * (card_w + 4), card_y);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xF5F5F5), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        s_aqi_cards[c] = card;

        // Combined title label (sensor name + "PM2.5 AQI", centered, black)
        lv_obj_t* title_lbl = lv_label_create(card);
        char title_text[64];
        snprintf(title_text, sizeof(title_text), "%s  PM2.5 AQI", col_names[c]);
        lv_label_set_text(title_lbl, title_text);
        lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(title_lbl, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        _apply_small_style(title_lbl);
        lv_obj_align(title_lbl, LV_ALIGN_BOTTOM_MID, 0, -12);

        // AQI value (large, centered, black)
        lv_obj_t* aqi_val = lv_label_create(card);
        lv_label_set_text(aqi_val, "--");
        lv_obj_set_style_text_color(aqi_val, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(aqi_val, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(aqi_val, &lv_font_montserrat_80_digits, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(aqi_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(aqi_val, LV_ALIGN_CENTER, 0, -1);
        s_aqi_value_labels[c] = aqi_val;
    }

    // Sensor columns below AQI cards
    int margin = 1;
    int col_w = (screen_w - margin * 3) / NETWORK_SENSOR_COUNT;
    int col_start_y = card_y + card_h + 12;
    int col_h = screen_h - col_start_y - 8;

    lv_obj_t* col_containers[NETWORK_SENSOR_COUNT];

    for (int c = 0; c < NETWORK_SENSOR_COUNT; c++) {
        lv_obj_t* col = lv_obj_create(s_screen);
        lv_obj_set_size(col, col_w, col_h);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(col, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_column(col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(col, margin + c * (col_w + margin), col_start_y);
        col_containers[c] = col;

        // Column header
        lv_obj_t* col_hdr = lv_label_create(col);
        lv_label_set_text(col_hdr, col_names[c]);
        lv_obj_set_style_text_color(col_hdr, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
        _apply_small_style(col_hdr);
        s_col_headers[c] = col_hdr;
    }

    // Create row containers for each field in the appropriate column
    for (int i = 0; network_sensor_field_map[i].json_key != NULL; i++) {
        const network_sensor_field_mapping_t* f = &network_sensor_field_map[i];
        int c = f->sensor_id;

        // Fields handled by the AQI cards — create row but hide it
        int is_card_field = (f->sensor_type == NETWORK_SENSOR_AQI_COLOR_A ||
                             f->sensor_type == NETWORK_SENSOR_AQI_COLOR_B ||
                             f->sensor_type == NETWORK_SENSOR_PM25_AQI_A ||
                             f->sensor_type == NETWORK_SENSOR_PM25_AQI_B);

        // Row container to hold label + value side by side
        lv_obj_t* row = lv_obj_create(col_containers[c]);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

        if (is_card_field) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            s_labels[i] = NULL;
            continue;
        }

        // Static label (e.g. "PM1.0:")
        lv_obj_t* static_lbl = lv_label_create(row);
        char static_text[64];
        snprintf(static_text, sizeof(static_text), "%s:", f->display_label);
        lv_label_set_text(static_lbl, static_text);
        lv_obj_set_style_text_color(static_lbl, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
        _apply_small_style(static_lbl);

        // Value label (updated at runtime)
        lv_obj_t* lbl = lv_label_create(row);
        char buf[32];
        snprintf(buf, sizeof(buf), "-- %s", f->unit);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        _apply_small_style(lbl);

        s_labels[i] = lbl;
    }

    lv_port_sem_give();

    // Register for network sensor events
    esp_event_handler_register_with(event_handle, VIEW_EVENT_BASE, VIEW_EVENT_NETWORK_SENSOR_DATA, _on_network_sensor_event, NULL);

    esp_event_handler_register_with(event_handle, VIEW_EVENT_BASE, VIEW_EVENT_NETWORK_SENSOR_STALE, _on_network_sensor_stale_event, NULL);

    ESP_LOGI(TAG, "network sensor view initialized");
}

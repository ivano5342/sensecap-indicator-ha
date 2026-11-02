#ifndef NETWORK_SENSOR_VIEW_H
#define NETWORK_SENSOR_VIEW_H

#include "lvgl.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the network sensor view and attach it to the tile view
 * @param tile_parent Parent lv_obj_t of the tile to add the screen to
 * @param event_handle Event handle to register view event handlers
 */
void network_sensor_view_init(lv_obj_t* tile_parent, esp_event_loop_handle_t event_handle);

/** Add a second pair of AQI cards, kept in step with the first. Call once, with the LVGL lock held.
 * @param parent Parent lv_obj_t, whose full width the cards span
 * @param y Top of the cards
 * @param card_h Height of the cards
 */
void network_sensor_view_add_aqi_cards(lv_obj_t* parent, int y, int card_h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // NETWORK_SENSOR_VIEW_H

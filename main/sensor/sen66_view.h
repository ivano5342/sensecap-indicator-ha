#ifndef SEN66_VIEW_H
#define SEN66_VIEW_H

#include "lvgl.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the SEN66 tile and start listening for sensor data. */
void sen66_view_init(void);

/**
 * Add a second card for a SEN66 value, kept in step with the one on the SEN66 tile.
 * Call at most once per type, after sen66_view_init(), with the LVGL lock held.
 */
void sen66_view_add_card(lv_obj_t *parent, enum sensor_data_type type, int x, int y, int w, int h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // SEN66_VIEW_H

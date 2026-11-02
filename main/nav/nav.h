#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Tile indices for the main swipeable screens */
#define NAV_TILE_OVERVIEW       0   /* network AQI and key SEN66 values (boot page) */
#define NAV_TILE_NETWORK_SENSOR 1   /* network PM sensors view */
#define NAV_TILE_SEN66          2   /* SEN66 air quality view */
#define NAV_TILE_HA_DATA        3   /* sensor data view */
#define NAV_TILE_HA_CTRL        4   /* switch control view */
#define NAV_TILE_HA_MIX         5   /* mixed sensors+switches view */
#define NAV_TILE_COUNT          6

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */

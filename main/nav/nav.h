#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Tile indices for the main swipeable screens */
#define NAV_TILE_NETWORK_SENSOR 0   /* network PM sensors view (boot page) */
#define NAV_TILE_HA_DATA        1   /* sensor data view */
#define NAV_TILE_HA_CTRL        2   /* switch control view */
#define NAV_TILE_HA_MIX         3   /* mixed sensors+switches view */
#define NAV_TILE_COUNT          4

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */

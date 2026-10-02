#include "indicator_enabler.h"

#include "nav.h"
#include "view_data.h"
#include "network_sensor_view.h"

extern int indicator_display_view_init(void);

int indicator_view_init(void) {
	nav_init();

#ifdef INDICATOR_DISPLAY_H
	indicator_display_view_init();
#endif

#ifdef SENSOR_H
	view_sensor_init();
#endif

#ifdef WIFI_H
	indicator_wifi_view_init();
#endif

#ifdef HA_H
	indicator_ha_view_init();
#endif

#ifdef NETWORK_SENSOR_VIEW_H
	network_sensor_view_init(nav_get_tile(NAV_TILE_NETWORK_SENSOR), view_event_handle);
#endif

#ifdef SETTINGS_H
	settings_view_init();
#endif

	return 0;
}

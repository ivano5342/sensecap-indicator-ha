#ifndef WIFI_CONNECT_SCREEN_H
#define WIFI_CONNECT_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_connect_screen wifi_connect_screen_t;

/* Show a connect dialog for an unconnected AP (modal overlay).
 * Returns NULL if one is already open. The dialog frees itself when a
 * button closes it, so don't keep the returned pointer; use
 * wifi_connect_screen_is_open() / wifi_connect_screen_dismiss_open(). */
wifi_connect_screen_t *wifi_connect_screen_show(const char *ssid, bool have_password);

/* Show a details dialog for the currently-connected AP (Delete/Cancel). */
wifi_connect_screen_t *wifi_details_screen_show(const char *ssid);

/* Dismiss programmatically (safe to call if already dismissed). */
void wifi_connect_screen_dismiss(wifi_connect_screen_t *s);

/* True while a connect or details dialog is open. */
bool wifi_connect_screen_is_open(void);

/* Dismiss whichever dialog is open; no-op if none. */
void wifi_connect_screen_dismiss_open(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_CONNECT_SCREEN_H */

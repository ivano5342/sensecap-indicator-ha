#pragma once
/* Simulator stub: no network interface exists, so the settings IP label shows
 * its "not connected" fallback. */
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef struct esp_netif_obj esp_netif_t;

typedef struct { uint32_t addr; } esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

#define IPSTR "%d.%d.%d.%d"
#define esp_ip4_addr_get_byte(ipaddr, idx) (((const uint8_t *)(&(ipaddr)->addr))[idx])
#define IP2STR(ipaddr) esp_ip4_addr_get_byte(ipaddr, 0), esp_ip4_addr_get_byte(ipaddr, 1), \
                       esp_ip4_addr_get_byte(ipaddr, 2), esp_ip4_addr_get_byte(ipaddr, 3)

static inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key) {
    (void)if_key; return NULL;
}
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip_info) {
    (void)netif; (void)ip_info; return ESP_FAIL;
}

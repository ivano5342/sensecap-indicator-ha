


#ifndef INDICATOR_UTIL_H
#define INDICATOR_UTIL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int wifi_rssi_level_get(int rssi);
bool isValidIP(const char *input);
bool isValidDomain(const char *input);

bool is_valid_ipv4(const char* ip_address);
bool extract_ip_from_url(const char* url, char* ip, size_t ip_size);
void assemble_broker_url(const char* ip_address, char* broker_url, size_t broker_url_size);
/* Prefix "mqtt://" unless input already has an mqtt:// or mqtts:// scheme.
 * Returns false on empty input or truncation. */
bool normalize_broker_url(const char* input, char* output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif

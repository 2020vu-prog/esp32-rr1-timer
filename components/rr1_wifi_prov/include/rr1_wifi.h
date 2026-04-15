#pragma once


void rr1WifiProv(void);
void nvs_dumprr1();
void nvs_set_rr1_apikey(char *api_key);
void nvs_get_rr1_apikey(char *out_value, size_t max_len);
void nvs_get_rr1_host(char *out_value, size_t max_len);	

extern char wifi_ip[20];
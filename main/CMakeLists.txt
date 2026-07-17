# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "rr1_ota.c" "umain.c"
                    INCLUDE_DIRS "."
                       REQUIRES wifi_provisioning
                    PRIV_REQUIRES esp_http_client app_update esp_https_ota
                                  esp_netif mbedtls nvs_flash esp_wifi esp_psram
				  rr1_wifi_prov
				  rr1_capture
				  rr1_messages
				  rr1_gps
				  esp_driver_tsens
				  esp_app_format
                    )

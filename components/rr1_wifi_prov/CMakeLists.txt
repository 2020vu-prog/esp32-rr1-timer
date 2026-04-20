idf_component_register(SRCS "app_main.c" "nvs_utils.c"
                       REQUIRES esp_wifi nvs_flash wifi_provisioning mbedtls
		       esp_driver_gpio
		       PRIV_REQUIRES rr1_blink
		       INCLUDE_DIRS "include")

idf_component_register(SRCS "app_main.c" "nvs_utils.c"
                       REQUIRES esp_wifi nvs_flash wifi_provisioning 
		       esp_driver_gpio
		       INCLUDE_DIRS "include")

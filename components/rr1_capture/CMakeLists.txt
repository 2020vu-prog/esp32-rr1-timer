idf_component_register(SRCS "quad_uint32.c" "timer_hist.c" "gps_xlate.c" "timer_mqtt.c" "timer_capture.c" "user_main.c" 
	INCLUDE_DIRS "include"

    REQUIRES esp_wifi mqtt 
    	esp_driver_mcpwm
    	rr1_messages
    PRIV_REQUIRES esp_wifi  esp_driver_gpio esp_driver_gptimer esp_event nvs_flash esp_psram
)



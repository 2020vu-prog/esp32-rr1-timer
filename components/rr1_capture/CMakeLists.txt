idf_component_register(SRCS "quad_uint32.c" "timer_hist.c" "gps_xlate.c" "timer_mqtt.c" "timer_capture.c" "user_main.c"  "timer_health.c"
	"timer_stdin.c" "get_mqtt_creds.c"
	"cpu_idle.c"
	"timer_i2c_motion.c"
	"timer_i2c_tap.c"	
	INCLUDE_DIRS "include" "../../build"

    REQUIRES esp_wifi mqtt 
    	esp_driver_mcpwm
    	rr1_messages
	rr1_wifi_prov
	rr1_blink
	rr1_pin_defs

    PRIV_REQUIRES esp_wifi 
     esp_driver_gpio 
     esp_driver_gptimer 
     esp_event 
     nvs_flash 
     esp_psram 
     esp_driver_tsens
     esp_http_client
     esp_driver_i2c
)



idf_component_register(SRCS "timer_blink.c" "rr1_blink.c"
                INCLUDE_DIRS "include"
		PRIV_REQUIRES esp_driver_gpio 
)
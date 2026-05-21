idf_component_register(SRCS "nmea_rr1_main.c" "nmea_rr1_uart.c"
                    INCLUDE_DIRS "include"
		PRIV_REQUIRES 
		esp_driver_uart
		rr1_pin_defs 
		    )

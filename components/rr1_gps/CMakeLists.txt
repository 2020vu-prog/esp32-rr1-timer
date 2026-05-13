idf_component_register(SRCS "rr1_gps.c" "nmea_example_main.c" "nmea_example_uart.c"
                    INCLUDE_DIRS "include"
		PRIV_REQUIRES esp_driver_uart 
		    )

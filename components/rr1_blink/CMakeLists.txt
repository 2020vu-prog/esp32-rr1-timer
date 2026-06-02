idf_component_register(SRCS "v12_blink.c" "timer_blink.c" "rr1_blink.c"
                INCLUDE_DIRS "include"
                PRIV_REQUIRES esp_driver_gpio esp_driver_gptimer
                rr1_pin_defs
)

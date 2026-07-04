idf_component_register(SRCS "timer_isr_main.c"
                       REQUIRES esp_driver_gpio esp_driver_gptimer
                       INCLUDE_DIRS ".")

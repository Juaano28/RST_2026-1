idf_component_register(
    SRCS
        "main.c"
        "library_led_c.c"
        "adc_module.c"
        "uart_module.c"
        "ntc_module.c"
        "button_led_module.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_ledc
        esp_driver_gpio
        esp_adc
        esp_driver_uart
)
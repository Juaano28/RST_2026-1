idf_component_register(
    SRCS "library_led_c.c" "ledc_basic_example_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_ledc esp_driver_gpio
)
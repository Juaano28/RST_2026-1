# CMakeLists.txt interno de la carpeta main
cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "main.c"
        "adc_module.c"
        "ntc_module.c"
        "control_system.c"
        "register.c"
        "rgb_led.c"
        "wifi_app.c"
        "http_server.c"
    INCLUDE_DIRS
        "."
    EMBED_FILES
        "jquery-3.3.1.min.js"
        "index.html"
        "app.css"
        "app.js"
    REQUIRES
        "esp_adc"
        "esp_http_server"
        "nvs_flash"
        "esp_wifi"
        "esp_event"
        "esp_netif"
        "esp_timer"
        "app_update"
        "esp_driver_gpio"
        "esp_driver_ledc"
)
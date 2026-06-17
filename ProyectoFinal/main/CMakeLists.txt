cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS 
        "main.c"
        "adc_module.c"
        "ntc_module.c"
        "wifi_app.c"
        "http_server.c"  
        "fan_alarm.c"
        "library_led_c.c"
        "servo_curtain.c"
        "schedule.c"
        "settings.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        esp_adc
        esp_http_server
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_driver_gpio
        app_update
        esp_timer
        lwip
        mqtt
        esp_driver_ledc
    EMBED_FILES
        "webpage/index.html"    # ◄── Verifica que estas rutas correspondan a tus archivos
        "webpage/app.js"
        "webpage/app.css"
        "webpage/favicon.ico"
        "webpage/jquery-3.3.1.min.js"
)
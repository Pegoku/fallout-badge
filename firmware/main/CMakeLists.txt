idf_component_register(
    SRCS
        "app_main.c"
        "badge_buttons.c"
        "badge_comms.c"
        "badge_display.c"
        "badge_hardware.c"
        "badge_storage.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES esp_event esp_netif esp_timer esp_wifi nvs_flash
)

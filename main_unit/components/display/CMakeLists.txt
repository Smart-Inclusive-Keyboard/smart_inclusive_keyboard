idf_component_register(
    SRCS
        "src/display.c"
        "src/display_ili9341.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board fonts theme driver esp_lcd esp_psram esp_timer esp_driver_ledc
)

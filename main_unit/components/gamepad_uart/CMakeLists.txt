idf_component_register(
    SRCS "src/gamepad_uart.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_timer esp_driver_uart
)

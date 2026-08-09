idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board display fonts theme kb_layout
        keyboard_ui gamepad_uart input_router
        hid narrator audio
        nvs_flash esp_timer
)

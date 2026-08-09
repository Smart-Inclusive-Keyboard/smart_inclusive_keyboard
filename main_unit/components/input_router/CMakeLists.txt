idf_component_register(
    SRCS "src/input_router.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES gamepad_uart keyboard_ui hid kb_layout narrator esp_timer
)

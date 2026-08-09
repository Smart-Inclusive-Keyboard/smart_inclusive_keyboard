idf_component_register(
    SRCS "src/keyboard_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES kb_layout
    PRIV_REQUIRES display theme fonts hid nvs_flash audio narrator
)

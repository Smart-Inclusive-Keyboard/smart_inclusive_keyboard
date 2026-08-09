idf_component_register(
    SRCS
        "src/board.c"
        "src/board_freenove_fnk0104a.c"
        "src/board_generic_s3.c"
        "src/board_generic_esp32.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_lcd esp_psram
)

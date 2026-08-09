idf_component_register(
    SRCS "src/audio.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_psram esp_codec_dev esp_driver_i2s esp_driver_i2c
)

idf_component_register(
    SRCS "main.c" "uart_gamepad.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp_timer
        esp_driver_ledc
        esp_driver_uart
        esp_lcd_jd9853
        esp_lcd_touch_axs5106
        espressif__esp_lvgl_port
        espressif__esp_lcd_touch
)

set(APP_SRCS
    "main.c"
    "board_waveshare_1_46b.c"
    "drivers/esp_lcd_spd2010/esp_lcd_spd2010.c"
)

set(APP_INCLUDES
    "."
    "drivers/esp_lcd_spd2010"
)

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS ${APP_INCLUDES}
    EMBED_FILES "../../../assets/loader.riv"
    REQUIRES rive_thorvg driver esp_lcd esp_timer
)

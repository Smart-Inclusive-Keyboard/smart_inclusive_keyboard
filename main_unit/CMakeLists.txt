cmake_minimum_required(VERSION 3.16)

# ESP-IDF Smart Keyboard.
#
# A re-implementation of the Linux "smart-keyboard" project as
# ESP-IDF firmware for ESP32-family boards. The first supported board
# is the Freenove FNK0104A; additional boards plug in by adding a
# Kconfig stanza and a per-board source file under components/board/.
#
# Build:
#     idf.py set-target esp32s3
#     idf.py menuconfig         # under "Smart Keyboard" -> select board, layout, theme
#     idf.py build flash monitor
#
# The audio / narrator components are compiled out unless the
# selected board reports CONFIG_BOARD_HAS_PSRAM && CONFIG_BOARD_HAS_SPEAKER
# and CONFIG_NARRATOR_ENABLE is set.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-smart-keyboard)

## BLE HID backend.
##
## Static registration: src/ble_hid.c is always in SRCS and the
## bt component is always in PRIV_REQUIRES. This keeps ESP-IDF's
## (>= 5.5) include-checker happy unconditionally and avoids the
## fragile interaction between Kconfig `select`s, CMake-level
## `if(CONFIG_...)` gates, and ESP-IDF's BUILD_COMPONENTS list.
##
## When CONFIG_SK_HID_TRANSPORT_BLE is not selected, the body of
## ble_hid.c is #if'd out, so the resulting TU is empty (no
## NimBLE headers preprocessed, no symbols defined). The hid.c
## facade dispatches to this backend only under the same CONFIG,
## so no link-time references remain.
idf_component_register(
    SRCS "src/ble_hid.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash bt
)

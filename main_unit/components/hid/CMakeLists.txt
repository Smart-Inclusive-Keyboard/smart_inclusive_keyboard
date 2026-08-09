## Transport-agnostic HID facade.
##
## Always depends on both backend components so the public hid.h
## can be included from any caller; the actual dispatch in hid.c
## is gated by CONFIG_SK_HID_TRANSPORT_* and only one backend's
## code is linked in.
idf_component_register(
    SRCS "src/hid.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ble_hid usb_hid
)

## USB HID backend.
##
## src/usb_hid.c includes <tinyusb.h> and <class/hid/hid_device.h>
## unconditionally. ESP-IDF >= 5.5's include-checker scans those
## #include directives without evaluating any surrounding #if, so we
## keep the source file out of the build entirely when the USB
## transport isn't selected. The unified hid.c facade already gates
## every call into this backend on CONFIG_SK_HID_TRANSPORT_USB, so
## no stub TU is required for the link to succeed.
set(srcs "")
set(reqs "")
if(CONFIG_SK_HID_TRANSPORT_USB)
    list(APPEND srcs "src/usb_hid.c")
    list(APPEND reqs esp_tinyusb)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${reqs}
)

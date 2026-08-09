# Smart Inclusive Keyboard

This project implements a specialized keyboard for users with limited
hand function, such as those sfuufering from cerebral palsy.

Theh keyboard consists of 3 ESP32 units connected together:

1. The [main unit](main_unit/) is an ESP32-S3 device with an LCD
display (Freenove FNK0104A), acting as a USB keyboard and mouse
towarrd the PC host. It displays a virtual keyboard on its screen and
allows the user to navigate and select the letters that are to be sent
to the host. The attached speaker pronounces the letters that user is
navigating, so that they don't need to look at the display all the
time. The main unit provides 5v power to the two input controllers
which are connected to it via UART interfaces:

2. The [touch input controller](input_controller), an ESP32-C6 device
with a small touchscreen (Waveshare ESP32-C6-Touch-LCD-1.47). The main
input gesture that it takes from the user is the vertical finger
sliding, allowing to navigate the virtual keyboard vertically or
horizontally. Also, upon a long touch, it switches between vertical
and horizontal modes. An attached vibration motor helps indicating the
mode switch. Also, the "Mouse" button toggles the controller between
keyboard and mouse modes, so that the user can use the same sliding
motions to control the mouse cursor movements vertically or
horizontally.

3. An additional generic ESP32 device (ESP32-C3 Super Mini utilized in
this build) is used for connecting the 7 buttons, as the touch input
device does not have enough free GPIO pins. It uses the same source
code as the touch controller, only without the display and touch
functions.


## Use of LLM

A large part of the code, especially the keyboard layouts and voice
narrator, are made with the use of Claude AI. As this is a
non-commercial and open source project, the author would never find
enough time to program it by hand.

If anyone finds any copyright infringement in the source code, the
original authors are welcome to contact me and negotiate a satisfying
solution.


## Acknowledgements

The 8x8 font is the [public-domain
font8x8](http://github.com/dhepper/font8x8). The larger 10x20 and
12x16 fonts are derived from
[Greybeard](https://github.com/flowchartsman/greybeard), a vector /
bitmap port of Uwe Waldmann's UW ttyp0 (MIT License).







## Copyright and license

This work is licensed under the MIT License.

Copyright (c) 2026 clackups@gmail.com

Fediverse: [@clackups@social.noleron.com](https://social.noleron.com/@clackups)

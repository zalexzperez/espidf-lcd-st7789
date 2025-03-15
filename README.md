## Intro
This is a continuation of the [LVGL slider](https://github.com/zalexzperez/LVGL_slider) repository, this time, using the ESP-IDF framework.
Based on FrankBJensen's working [code](https://forum.lvgl.io/t/gestures-are-slow-perceiving-only-detecting-one-of-5-10-tries/18515/62) at LVGL forums.
The goal of this project is to be able to extract the most performance out of my display, using the esp_lcd component along with LVGL. 

## Specifications
  - 172x320 SPI ST7789 based display
  - ESP32-S3 8MB flash with no PSRAM
  - Default menuconfig values except for increased MCU clock speed (240MHz)

## Changes from FrankBJensen's code
  - New GPIO pin definitions
  - Remove the LVGL code creating and updating a label
  - Add my own LVGL code made with EEZ Studio where a centered slider transitions from 0 to 100% with an animation
  - Fix inverted colors by sending the invert display command with `esp_lcd_panel_io_tx_param()` (INVON 0x21)

## Result
Unfortunately, there's still tearing in the slider knob:

![esp_lcd_st7789_tearing](https://github.com/user-attachments/assets/cbf28dd0-e4c6-48b0-b794-e08296ce0f0b)

### About those red bands
They are simply part of the background. 
However, my display's resolution is 172x320. If I set `#define DISP_VER_RES 172` (and also set it in EEZ Studio), the image doesn’t span the entire height, leaving a disabled area at the bottom with what seems power-up garbage. With FrankBJensen's UI, it looks like this:
![image](https://github.com/user-attachments/assets/39bbd574-205c-40db-af19-0c0fee7d5c35). 

This is certainly a huge problem, as I'm unable to design the UI properly if I need to set it to the default `240` value.


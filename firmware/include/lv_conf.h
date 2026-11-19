// LVGL 9.2 configuration for EasyDeck (T-Deck Plus, 320x240 RGB565, PSRAM).
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// memory: on the device every LVGL object goes to PSRAM first (src/hal/LvMem.cpp) - internal RAM
// is scarce with Bluetooth on and Wi-Fi needs it. The simulator uses the C library.
#ifdef EASYDECK_SIM
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#else
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CUSTOM
#endif
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

#define LV_USE_OS   LV_OS_NONE
#define LV_DEF_REFR_PERIOD  30
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_DRAW_SW 1

// Fonts: big, readable sizes
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

// Widgets we don't use
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_IMGFONT 1   // image glyphs: the pixel-art emoji (ui/fonts/pixemoji.c)
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPAN 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_ANIMIMG 0
#define LV_USE_LOTTIE 0

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0

// Map tiles: PNG files read straight from the SD card (S:/sd/tiles/z/x/y.png)
#define LV_USE_LODEPNG 1
#define LV_USE_SNAPSHOT 1   /* screenshots to SD (trackball + key) */
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'S'
#define LV_FS_STDIO_PATH ""
#define LV_FS_STDIO_CACHE_SIZE 0
#define LV_CACHE_DEF_SIZE (2 * 1024 * 1024)   // decoded tiles live in PSRAM
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 16

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0

#endif
#endif

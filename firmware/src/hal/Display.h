#pragma once
// ST7789 320x240 display via LovyanGFX, sharing the global SPI host with the
// SX1262 radio and SD card. No DMA: every flush completes before returning, so
// the bus is always free for the radio/SD on the same (main) task.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class TDeckDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
public:
  TDeckDisplay();
  bool start();                       // init panel, landscape, black screen
  void setBacklight(uint8_t level);   // 0 = off, 1..16
  uint8_t backlight() const { return _bl_level; }
private:
  uint8_t _bl_level = 0;
};

extern TDeckDisplay tft;

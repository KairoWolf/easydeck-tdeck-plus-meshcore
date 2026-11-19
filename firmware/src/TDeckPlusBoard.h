#pragma once
// Board support for the LilyGO T-Deck Plus (ESP32-S3FN16R8).
// Pin facts cross-checked against LilyGO's T-Deck utilities.h, Meshtastic's
// t-deck variant and MeshCore's lilygo_tdeck variant (see research report).

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

// ---- Power / buses ----
#define PIN_POWER_ON      10   // peripheral power rail: keyboard, SD, backlight, trackball, audio
#define PIN_I2C_SDA       18
#define PIN_I2C_SCL        8
#define PIN_SPI_SCK       40
#define PIN_SPI_MOSI      41
#define PIN_SPI_MISO      38

// ---- Display (ST7789, 320x240 landscape) ----
#define PIN_TFT_CS        12
#define PIN_TFT_DC        11
#define PIN_TFT_BL        42   // pulse-count dimmer (16 levels), not plain PWM

// ---- SD card ----
#define PIN_SD_CS         39

// ---- Input ----
#define KB_I2C_ADDR       0x55 // ESP32-C3 keyboard controller (polled, no IRQ)
#define TOUCH_I2C_ADDR    0x5D // GT911 (0x14 fallback)
#define PIN_TOUCH_INT     16
#define PIN_TB_UP          3
#define PIN_TB_DOWN       15
#define PIN_TB_LEFT        1
#define PIN_TB_RIGHT       2
#define PIN_TB_CLICK       0

// ---- Battery ----
#define PIN_VBAT_ADC       4

class TDeckPlusBoard : public ESP32Board {
public:
  void begin();

  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override { return "LilyGo T-Deck Plus"; }

  // 0..100 estimate from voltage (single-cell Li-ion, rough curve)
  int getBatteryPercent();
  static int percentFromMv(int mv);
  bool isCharging();   // heuristic: voltage above full-charge threshold

  // T-Deck Plus ships with either an L76K (9600 baud) or a u-blox M10 (38400).
  // MeshCore opens the GPS at 38400, so switch an L76K up to 38400 first.
  // Returns the baud the GPS was found at (0 = no GPS seen).
  uint32_t prepareGps();
  uint32_t gpsBaudFound() const { return _gps_baud; }
  void switchGpsBaud();   // tell an L76K at 9600 to go to 38400, then open Serial1 at 38400 (no probing)
private:
  uint32_t _gps_baud = 0;
public:

  // Hand control back to the SD-card Launcher (if installed). The Launcher's custom
  // bootloader boots the Launcher after a deep-sleep wake by default.
  void rebootToLauncher();
  bool isRunningUnderLauncher();
};

#include "Display.h"
#include "TDeckPlusBoard.h"

TDeckDisplay tft;

TDeckDisplay::TDeckDisplay() {
  {
    auto cfg = _bus.config();
    cfg.spi_host = SPI2_HOST;      // same host as Arduino's global SPI (FSPI) on ESP32-S3
    cfg.spi_mode = 0;
    cfg.freq_write = 40000000;
    cfg.freq_read = 16000000;
    cfg.spi_3wire = false;
    cfg.use_lock = true;
    cfg.dma_channel = 0;           // no DMA: keep bus ownership simple
    cfg.pin_sclk = PIN_SPI_SCK;
    cfg.pin_mosi = PIN_SPI_MOSI;
    cfg.pin_miso = PIN_SPI_MISO;
    cfg.pin_dc = PIN_TFT_DC;
    _bus.config(cfg);
    _panel.setBus(&_bus);
  }
  {
    auto cfg = _panel.config();
    cfg.pin_cs = PIN_TFT_CS;
    cfg.pin_rst = -1;
    cfg.pin_busy = -1;
    cfg.panel_width = 240;
    cfg.panel_height = 320;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.offset_rotation = 0;
    cfg.dummy_read_pixel = 8;
    cfg.readable = false;
    cfg.invert = true;
    cfg.rgb_order = false;
    cfg.dlen_16bit = false;
    cfg.bus_shared = true;         // SD card + LoRa on the same bus
    _panel.config(cfg);
  }
  setPanel(&_panel);
}

bool TDeckDisplay::start() {
  if (!init()) return false;
  setRotation(1);                  // landscape, keyboard at the bottom (verify on hardware)
  // Same gamma tweak Meshtastic uses for this panel
  startWrite();
  writeCommand(0x26);
  writeData(0x01);
  endWrite();
  fillScreen(TFT_BLACK);
  return true;
}

// The backlight driver (AW9364) is a 16-step pulse-count dimmer: each LOW->HIGH
// pulse steps brightness down one level, wrapping around. LilyGO's reference routine.
void TDeckDisplay::setBacklight(uint8_t value) {
  const uint8_t steps = 16;
  if (value > steps) value = steps;
  pinMode(PIN_TFT_BL, OUTPUT);
  if (value == 0) {
    digitalWrite(PIN_TFT_BL, LOW);
    delay(3);
    _bl_level = 0;
    return;
  }
  if (_bl_level == 0) {
    digitalWrite(PIN_TFT_BL, HIGH);
    _bl_level = steps;
    delayMicroseconds(30);
  }
  int from = steps - _bl_level;
  int to = steps - value;
  int num = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) {
    digitalWrite(PIN_TFT_BL, LOW);
    digitalWrite(PIN_TFT_BL, HIGH);
  }
  _bl_level = value;
}

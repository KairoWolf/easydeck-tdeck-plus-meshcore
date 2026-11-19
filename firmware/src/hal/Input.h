#pragma once
// Keyboard (ESP32-C3 @0x55, polled), trackball (4 hall sensors + click), and
// GT911 touch (polled over the shared Arduino Wire bus — no second I2C controller
// on the same pins, and no reliance on the INT pin whose polarity varies by batch).

#include <Arduino.h>

#include "core/Types.h"

struct TouchPoint {
  bool pressed;
  int16_t x, y;
};

class Input {
public:
  void begin();
  void poll();                       // call often from the main loop
  bool next(InputEvent& ev);         // pop one queued key/trackball event
  TouchPoint touch() const { return _touch; }
  bool hasTouch() const { return _touch_addr != 0; }
  bool hasKeyboard() const { return _kb_ok; }
  unsigned long lastActivity() const { return _last_activity; }
  void setKeyboardBacklight(uint8_t duty);   // 0..255

  // diagnostics
  uint32_t edgeCount(int dir) const;

private:
  void push(InputType t, char c = 0);
  void pollKeyboard();
  void pollTrackball();
  void pollTouch();
  bool gt911Read(uint16_t reg, uint8_t* buf, size_t len);
  bool gt911Write(uint16_t reg, uint8_t val);

  #define INPUT_Q 32
  InputEvent _q[INPUT_Q];
  uint8_t _q_head = 0, _q_tail = 0;

  bool _kb_ok = false;
  uint8_t _touch_addr = 0;
  TouchPoint _touch = { false, 0, 0 };
  unsigned long _next_kb = 0, _next_touch = 0, _last_activity = 0, _last_touch_data = 0;

  // trackball click state
  bool _btn_down = false;
  bool _btn_long_fired = false;
  bool _btn_sos_fired = false;
  bool _btn_combo = false;   // a key was pressed during this hold (screenshot)
  unsigned long _btn_since = 0;
  int32_t _tb_acc[4] = {0, 0, 0, 0};
  unsigned long _tb_last_emit = 0;
};

extern Input input;

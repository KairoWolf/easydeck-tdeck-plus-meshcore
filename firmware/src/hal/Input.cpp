#include "Input.h"
#include <Wire.h>
#include "TDeckPlusBoard.h"

Input input;

// ---- trackball edge counters (ISR) ----
static volatile uint32_t tb_edges[4] = {0, 0, 0, 0};   // up, down, left, right
static void IRAM_ATTR tb_up()    { tb_edges[0]++; }
static void IRAM_ATTR tb_down()  { tb_edges[1]++; }
static void IRAM_ATTR tb_left()  { tb_edges[2]++; }
static void IRAM_ATTR tb_right() { tb_edges[3]++; }

// Tuning (needs on-device validation): how many hall-sensor edges make one step,
// and the minimum time between steps.
#define TB_EDGES_PER_STEP   2
#define TB_MIN_STEP_MS      90
#define BTN_LONG_MS         800
#define BTN_SOS_MS          3000
#define KB_POLL_MS          15
#define TOUCH_POLL_MS       25

// Touch mapping for rotation 1 (LilyGO: swapXY + mirror Y, max 320x240)
#define TOUCH_SWAP_XY       1
#define TOUCH_MIRROR_X      0
#define TOUCH_MIRROR_Y      1

void Input::begin() {
  pinMode(PIN_TB_UP, INPUT_PULLUP);
  pinMode(PIN_TB_DOWN, INPUT_PULLUP);
  pinMode(PIN_TB_LEFT, INPUT_PULLUP);
  pinMode(PIN_TB_RIGHT, INPUT_PULLUP);
  pinMode(PIN_TB_CLICK, INPUT_PULLUP);
  attachInterrupt(PIN_TB_UP, tb_up, CHANGE);
  attachInterrupt(PIN_TB_DOWN, tb_down, CHANGE);
  attachInterrupt(PIN_TB_LEFT, tb_left, CHANGE);
  attachInterrupt(PIN_TB_RIGHT, tb_right, CHANGE);

  // The keyboard MCU needs ~500 ms after the power rail comes up.
  unsigned long t0 = millis();
  while (millis() - t0 < 600) {
    Wire.beginTransmission(KB_I2C_ADDR);
    if (Wire.endTransmission() == 0) { _kb_ok = true; break; }
    delay(50);
  }

  // GT911 at 0x5D, fallback 0x14
  const uint8_t addrs[2] = { 0x5D, 0x14 };
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { _touch_addr = a; break; }
  }
  if (_touch_addr) gt911Write(0x814E, 0);
  _last_activity = millis();
}

uint32_t Input::edgeCount(int dir) const { return tb_edges[dir & 3]; }

void Input::push(InputType t, char c) {
  uint8_t n = (_q_head + 1) % INPUT_Q;
  if (n == _q_tail) return;   // full: drop
  _q[_q_head] = { t, c };
  _q_head = n;
  _last_activity = millis();
}

bool Input::next(InputEvent& ev) {
  if (_q_tail == _q_head) return false;
  ev = _q[_q_tail];
  _q_tail = (_q_tail + 1) % INPUT_Q;
  return true;
}

void Input::setKeyboardBacklight(uint8_t duty) {
  if (!_kb_ok) return;
  Wire.beginTransmission(KB_I2C_ADDR);
  Wire.write(0x01);
  Wire.write(duty);
  Wire.endTransmission();
}

void Input::pollKeyboard() {
  if (!_kb_ok) return;
  if (Wire.requestFrom((uint8_t)KB_I2C_ADDR, (uint8_t)1) != 1) return;
  int c = Wire.read();
  if (c <= 0) return;
  if (c == 0x08) push(IN_BACKSPACE);
  else if (c == 0x0D || c == 0x0A) push(IN_ENTER);
  else if (c >= 0x20 && c < 0x7F) push(IN_CHAR, (char)c);
}

void Input::pollTrackball() {
  static uint32_t last[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; i++) {
    uint32_t now = tb_edges[i];
    _tb_acc[i] += (int32_t)(now - last[i]);
    last[i] = now;
  }
  unsigned long ms = millis();
  if (ms - _tb_last_emit >= TB_MIN_STEP_MS) {
    // emit the dominant direction only
    int best = -1;
    int32_t best_v = TB_EDGES_PER_STEP - 1;
    for (int i = 0; i < 4; i++) if (_tb_acc[i] > best_v) { best = i; best_v = _tb_acc[i]; }
    if (best >= 0) {
      static const InputType map[4] = { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT };
      push(map[best]);
      _tb_last_emit = ms;
      for (int i = 0; i < 4; i++) _tb_acc[i] = 0;
    }
  }
  // decay stray edges so a tiny wobble doesn't trigger a step much later
  static unsigned long last_decay = 0;
  if (ms - last_decay > 400) {
    last_decay = ms;
    for (int i = 0; i < 4; i++) _tb_acc[i] = 0;
  }

  // click / long-press (GPIO0, active LOW)
  bool down = digitalRead(PIN_TB_CLICK) == LOW;
  if (down && !_btn_down) {
    _btn_down = true;
    _btn_long_fired = false;
    _btn_sos_fired = false;
    _btn_since = ms;
  } else if (down && _btn_down && !_btn_long_fired && ms - _btn_since >= BTN_LONG_MS) {
    _btn_long_fired = true;
    push(IN_BACK);
  } else if (down && _btn_down && _btn_long_fired && !_btn_sos_fired && ms - _btn_since >= BTN_SOS_MS) {
    _btn_sos_fired = true;
    push(IN_SOS_HOLD);
  } else if (!down && _btn_down) {
    _btn_down = false;
    if (!_btn_long_fired && ms - _btn_since > 30) push(IN_SELECT);
  }
}

bool Input::gt911Read(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(_touch_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(_touch_addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool Input::gt911Write(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(_touch_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void Input::pollTouch() {
  if (!_touch_addr) return;
  uint8_t status;
  if (!gt911Read(0x814E, &status, 1)) return;
  if (!(status & 0x80)) {                 // no new data
    // GT911 reports ~every 10 ms while touched; silence means the finger lifted
    if (_touch.pressed && millis() - _last_touch_data > 120) _touch.pressed = false;
    return;
  }
  uint8_t n = status & 0x0F;
  if (n > 0) {
    uint8_t p[4];
    if (gt911Read(0x8150, p, 4)) {
      int16_t rx = p[0] | (p[1] << 8);
      int16_t ry = p[2] | (p[3] << 8);
      int16_t x = rx, y = ry;
      if (TOUCH_SWAP_XY) { x = ry; y = rx; }
      if (TOUCH_MIRROR_X) x = 319 - x;
      if (TOUCH_MIRROR_Y) y = 239 - y;
      _touch = { true, (int16_t)constrain(x, 0, 319), (int16_t)constrain(y, 0, 239) };
      _last_activity = millis();
      _last_touch_data = millis();
    }
  } else {
    _touch.pressed = false;
  }
  gt911Write(0x814E, 0);
}

void Input::poll() {
  unsigned long ms = millis();
  pollTrackball();
  if ((long)(ms - _next_kb) >= 0) { _next_kb = ms + KB_POLL_MS; pollKeyboard(); }
  if ((long)(ms - _next_touch) >= 0) { _next_touch = ms + TOUCH_POLL_MS; pollTouch(); }
}

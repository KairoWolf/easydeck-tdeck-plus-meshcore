#include "Sound.h"
#include <math.h>
#include <driver/i2s.h>

#define PIN_I2S_BCK   7
#define PIN_I2S_WS    5
#define PIN_I2S_DOUT  6
#define SAMPLE_RATE   16000
#define VOLUME        7000     // of 32767 (a rounded wave sounds quieter than a square one)
#define I2S_PORT      I2S_NUM_0

Sound sound;

void Sound::begin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 8;    // 8 x 512 samples = 256 ms at 16 kHz: survives a full-screen redraw
  cfg.dma_buf_len = 512;    // (the wake-up redraw when a message arrives) without crackling
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;   // silence when we stop feeding
  if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) return;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_I2S_BCK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  _ok = i2s_set_pin(I2S_PORT, &pins) == ESP_OK;
  i2s_zero_dma_buffer(I2S_PORT);
}

void Sound::queue(uint16_t hz, uint16_t ms) {
  if (_n < 8) _notes[_n++] = { hz, ms };
}

void Sound::chime() {
  if (!_ok) return;
  _n = 0; _cur = 0; _samples_left = 0;
  queue(587, 160); queue(0, 40); queue(784, 240); queue(0, 300);
  queue(587, 160); queue(0, 40); queue(784, 240);
}

void Sound::play(int tone) {
  if (!_ok) return;
  if (tone == TONE_CHIME) { chime(); return; }
  _n = 0; _cur = 0; _samples_left = 0;
  switch (tone) {
    case TONE_BEEP:  queue(880, 90); queue(0, 70); queue(880, 90); break;              // double beep
    case TONE_PING:  queue(1046, 60); queue(784, 140); break;                         // soft ping
    case TONE_TICK:  queue(660, 40); break;                                           // quiet tick (groups)
    case TONE_ALARM:                                                                  // siren: SOS
      for (int i = 0; i < 4; i++) { queue(880, 180); queue(622, 180); }
      break;
    default: chime();
  }
}

void Sound::click() {
  if (!_ok || _cur < _n) return;
  _n = 0; _cur = 0; _samples_left = 0;
  queue(440, 15);
}

void Sound::loop() {
  if (!_ok || _cur >= _n) return;
  int16_t buf[128];
  for (int chunk = 0; chunk < 32; chunk++) {   // fill all free DMA space (stops when it's full)
    if (_samples_left == 0) {
      if (_cur >= _n) return;
      _samples_left = (uint32_t)SAMPLE_RATE * _notes[_cur].ms / 1000;
      _note_total = _samples_left;
      _note_pos = 0;
      _phase = 0;
    }
    uint16_t hz = _notes[_cur].hz;
    int count = min((uint32_t)128, _samples_left);
    // Rounded wave (a sine pushed into soft clipping: warm but loud on the small speaker) with a
    // 4 ms fade in and out per note, so notes don't click. Phase runs on even if a write is short.
    static int16_t table[64];
    static bool init = false;
    if (!init) { for (int i = 0; i < 64; i++) { float v = 1.6f * sinf(i * 2.0f * (float)M_PI / 64); table[i] = (int16_t)(32767 * (v > 1 ? 1 : v < -1 ? -1 : v)); } init = true; }
    uint32_t step = hz ? (uint32_t)(((uint64_t)hz * 64 << 16) / SAMPLE_RATE) : 0;
    const uint32_t fade = SAMPLE_RATE * 4 / 1000;
    uint32_t ph = _phase;
    for (int i = 0; i < count; i++) {
      if (!hz) { buf[i] = 0; continue; }
      uint32_t pos = _note_pos + i, left = _note_total - pos;
      int32_t env = 256;
      if (pos < fade) env = (int32_t)(pos * 256 / fade);
      if (left < fade && (int32_t)(left * 256 / fade) < env) env = (int32_t)(left * 256 / fade);
      buf[i] = (int16_t)((int32_t)table[(ph >> 16) & 63] * VOLUME / 32767 * env / 256);
      ph += step;
    }
    size_t written = 0;
    i2s_write(I2S_PORT, buf, count * sizeof(int16_t), &written, 0);   // never block
    int done = written / sizeof(int16_t);
    if (done == 0) return;   // DMA full, try next loop
    _phase += step * done;
    _note_pos += done;
    _samples_left -= done;
    if (_samples_left == 0) _cur++;
  }
}

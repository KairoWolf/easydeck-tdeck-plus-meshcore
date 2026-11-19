#include "Sound.h"
#include <driver/i2s.h>

#define PIN_I2S_BCK   7
#define PIN_I2S_WS    5
#define PIN_I2S_DOUT  6
#define SAMPLE_RATE   16000
#define VOLUME        6000     // of 32767
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
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
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
  for (int chunk = 0; chunk < 4; chunk++) {
    if (_samples_left == 0) {
      if (_cur >= _n) return;
      _samples_left = (uint32_t)SAMPLE_RATE * _notes[_cur].ms / 1000;
      _phase = 0;
    }
    uint16_t hz = _notes[_cur].hz;
    int count = min((uint32_t)128, _samples_left);
    for (int i = 0; i < count; i++) {
      if (hz == 0) { buf[i] = 0; continue; }
      // square-ish wave with soft edges: cheap and loud enough on a small speaker
      uint32_t period = SAMPLE_RATE / hz;
      uint32_t p = (_phase + i) % period;
      int32_t v = (p < period / 2) ? VOLUME : -VOLUME;
      buf[i] = (int16_t)v;
    }
    size_t written = 0;
    i2s_write(I2S_PORT, buf, count * sizeof(int16_t), &written, 0);   // never block
    int done = written / sizeof(int16_t);
    if (done == 0) return;   // DMA full, try next loop
    _phase += done;
    _samples_left -= done;
    if (_samples_left == 0) _cur++;
  }
}

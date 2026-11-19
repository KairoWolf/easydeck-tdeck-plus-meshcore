#pragma once
// Simple non-blocking tones on the T-Deck's I2S amplifier (MAX98357A).
// Low, distinct pitches (older ears lose high frequencies first).
#include <Arduino.h>

enum { TONE_CHIME = 0, TONE_BEEP = 1, TONE_PING = 2, TONE_TICK = 3, TONE_ALARM = 4 };

class Sound {
public:
  void begin();
  void chime();          // new message: two rising notes, played twice
  void play(int tone);   // TONE_* below
  void click();          // very short tick
  void loop();           // feeds the I2S DMA without blocking
  bool ok() const { return _ok; }
  bool busy() const { return _ok && _cur < _n; }   // tones still queued / playing
private:
  void queue(uint16_t hz, uint16_t ms);
  struct Note { uint16_t hz, ms; };
  Note _notes[8];
  int _n = 0, _cur = 0;
  uint32_t _samples_left = 0;
  uint32_t _phase = 0;        // 16.16 fixed-point position in the wave table
  uint32_t _note_total = 0;   // samples in the current note (for the fade in/out)
  uint32_t _note_pos = 0;
  bool _ok = false;
};

extern Sound sound;

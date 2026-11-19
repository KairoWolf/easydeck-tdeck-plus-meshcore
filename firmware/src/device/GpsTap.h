#pragma once
// A transparent Stream between the GPS UART and MeshCore's MicroNMEALocationProvider.
// MeshCore reads through it as usual; every byte it reads is also parsed here, so EasyDeck
// gets what MeshCore's (private) parser doesn't expose: speed, course, HDOP, fix quality and
// the satellites in view (GSV) for a sky plot. No change to MeshCore.
#include <Arduino.h>
#include "core/Model.h"

class GpsTap : public Stream {
public:
  explicit GpsTap(Stream& s) : _s(s) {}

  int available() override { return _s.available(); }
  int peek() override { return _s.peek(); }
  int read() override {
    int c = _s.read();
    if (c >= 0) feed((char)c);
    return c;
  }
  size_t write(uint8_t b) override { return _s.write(b); }
  size_t write(const uint8_t* b, size_t n) override { return _s.write(b, n); }
  int availableForWrite() override { return _s.availableForWrite(); }
  void flush() override { _s.flush(); }

  void detail(GpsDetail& out);   // snapshot for the UI
  uint32_t sentences() const { return _sentences; }

private:
  void feed(char c);
  void parseLine();
  void parseGsv(char sys, char** f, int n);
  void parseRmc(char** f, int n);
  void parseGga(char** f, int n);
  void parseGsa(char** f, int n);

  Stream& _s;
  char _line[100];
  uint8_t _len = 0;
  uint32_t _sentences = 0;

  // latest values
  bool _valid = false;
  double _lat = 0, _lon = 0;
  float _alt = 0, _speed_kmh = 0, _course = 0, _hdop = 0;
  uint8_t _used = 0, _fix_q = 0, _fix_type = 0;
  uint32_t _fix_ms = 0;

  struct Sat { uint8_t prn, elev, snr; uint16_t az; char sys; uint32_t at; };
  Sat _sats[GPS_MAX_SATS] = {};
  uint8_t _nsat = 0;
};

extern GpsTap gps_tap;

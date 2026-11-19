#include "GpsTap.h"
#include <stdlib.h>
#include <string.h>

void GpsTap::feed(char c) {
  if (c == '$') { _len = 0; _line[_len++] = c; return; }
  if (_len == 0) return;
  if (c == '\r' || c == '\n') {
    _line[_len] = 0;
    parseLine();
    _len = 0;
    return;
  }
  if (_len < sizeof(_line) - 1) _line[_len++] = c;
  else _len = 0;   // overlong: drop
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

// NMEA ddmm.mmmm + hemisphere -> degrees
static double coord(const char* v, const char* hemi) {
  if (!v || !*v) return 0;
  double raw = atof(v);
  int deg = (int)(raw / 100);
  double d = deg + (raw - deg * 100) / 60.0;
  if (hemi && (*hemi == 'S' || *hemi == 'W')) d = -d;
  return d;
}

void GpsTap::parseLine() {
  // verify "*hh" checksum
  char* star = strrchr(_line, '*');
  if (!star || star - _line < 7 || hexv(star[1]) < 0 || hexv(star[2]) < 0) return;
  uint8_t sum = 0;
  for (char* p = _line + 1; p < star; p++) sum ^= (uint8_t)*p;
  if (sum != (uint8_t)(hexv(star[1]) * 16 + hexv(star[2]))) return;
  *star = 0;
  _sentences++;

  char* f[24];
  int n = 0;
  for (char* p = _line; n < 24; ) {   // split in place, keeping empty fields
    f[n++] = p;
    char* comma = strchr(p, ',');
    if (!comma) break;
    *comma = 0;
    p = comma + 1;
  }
  const char* id = f[0] + 1;   // "GPRMC", "BDGSV", ...
  if (strlen(id) < 5) return;
  const char* type = id + 2;
  char sys = id[1] == 'P' ? 'G' : id[1] == 'L' ? 'R' : id[1] == 'A' ? 'E' : (id[0] == 'B' || id[1] == 'B') ? 'C' : id[1] == 'N' ? 'N' : '?';
  if (!strcmp(type, "GSV")) parseGsv(sys, f, n);
  else if (!strcmp(type, "RMC")) parseRmc(f, n);
  else if (!strcmp(type, "GGA")) parseGga(f, n);
  else if (!strcmp(type, "GSA")) parseGsa(f, n);
}

void GpsTap::parseRmc(char** f, int n) {
  if (n < 9) return;
  _valid = f[2][0] == 'A';
  if (_valid) {
    _lat = coord(f[3], f[4]);
    _lon = coord(f[5], f[6]);
    _speed_kmh = atof(f[7]) * 1.852f;
    if (*f[8]) _course = atof(f[8]);
    _fix_ms = millis();
  }
}

void GpsTap::parseGga(char** f, int n) {
  if (n < 10) return;
  _fix_q = atoi(f[6]);
  _used = atoi(f[7]);
  if (*f[8]) _hdop = atof(f[8]);
  if (*f[9]) _alt = atof(f[9]);
}

void GpsTap::parseGsa(char** f, int n) {
  if (n < 3) return;
  int t = atoi(f[2]);
  if (t > _fix_type || millis() - _fix_ms > 3000) _fix_type = t;   // several GSA (one per system) per second
}

void GpsTap::parseGsv(char sys, char** f, int n) {
  // $xxGSV,total,msgno,inview,[prn,elev,az,snr]x(1..4)[,signal]
  uint32_t now = millis();
  for (int i = 4; i + 3 < n; i += 4) {
    if (!*f[i]) continue;
    int prn = atoi(f[i]);
    if (prn <= 0 || prn > 255) continue;
    int k = 0;
    for (; k < _nsat; k++) if (_sats[k].prn == prn && _sats[k].sys == sys) break;
    if (k == _nsat) {
      if (_nsat < GPS_MAX_SATS) _nsat++;
      else {   // replace the stalest
        k = 0;
        for (int j = 1; j < GPS_MAX_SATS; j++) if (_sats[j].at < _sats[k].at) k = j;
      }
    }
    Sat& s = _sats[k];
    s.prn = (uint8_t)prn; s.sys = sys;
    s.elev = (uint8_t)atoi(f[i + 1]);
    s.az = (uint16_t)atoi(f[i + 2]);
    s.snr = *f[i + 3] ? (uint8_t)atoi(f[i + 3]) : 0;
    s.at = now;
  }
}

void GpsTap::detail(GpsDetail& o) {
  uint32_t now = millis();
  o.valid = _valid && now - _fix_ms < 5000;
  o.lat = _lat; o.lon = _lon;
  o.alt_m = _alt; o.speed_kmh = _speed_kmh; o.course_deg = _course; o.hdop = _hdop;
  o.sats_used = _used;
  o.fix_type = _fix_type;
  o.fix_age_s = _fix_ms ? (now - _fix_ms) / 1000 : 0xFFFFFFFF;
  o.sentences = _sentences;
  o.n = 0;
  for (int i = 0; i < _nsat; i++) {
    if (now - _sats[i].at > 15000) continue;   // not in view any more
    GpsSat& d = o.sats[o.n++];
    d.prn = _sats[i].prn; d.elev = _sats[i].elev; d.az = _sats[i].az; d.snr = _sats[i].snr; d.sys = _sats[i].sys;
  }
  o.sats_view = o.n;
}

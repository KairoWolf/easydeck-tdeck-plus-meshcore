#include "Sos.h"
#include "App.h"
#include "Widgets.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

void Sos::begin(App* app, Model* m) {
  _app = app;
  _m = m;
  if (active()) _next_ms = lv_tick_get() + 5000;   // resume after reboot: send an update soon
}

bool Sos::active() const { return _m && _m->ui().sos_active; }
int Sos::seq() const { return _m->ui().sos_seq; }
uint32_t Sos::startedAt() const { return _m->ui().sos_started; }

int Sos::secondsToNext() const {
  if (!active()) return 0;
  int32_t d = (int32_t)(_next_ms - lv_tick_get());
  return d > 0 ? d / 1000 : 0;
}

int Sos::contacts(std::vector<ContactView>& out) {
  out.clear();
  for (int i = 0; i < _m->contactCount(); i++) {
    ContactView c;
    if (_m->contactAt(i, c) && c.favorite && c.type == 1) out.push_back(c);
  }
  return (int)out.size();
}

bool Sos::emergencyChannel(ChannelView* out) {
  for (int i = 0; i < _m->channelCount(); i++) {
    ChannelView ch;
    if (_m->channelAt(i, ch) && strcasecmp(ch.name, "#emergency") == 0) { if (out) *out = ch; return true; }
  }
  return false;
}

void Sos::position(char* out, int n) {
  DeviceStatus st = _m->status();
  char t[12] = "";
  if (st.clock_valid) ui::clockTime(st.now, _m->tzOffset(), t, sizeof(t));
  GpsDetail* g = new GpsDetail();   // satellite table: keep it off the stack
  _m->gpsDetail(*g);
  bool live = g->valid;
  delete g;
  if (st.gps_fix && live) snprintf(out, n, "%.5f,%.5f %s", st.lat, st.lon, t);
  else if (st.gps_fix) snprintf(out, n, "%.5f,%.5f (last fix) %s", st.lat, st.lon, t);
  else snprintf(out, n, "no GPS fix %s", t);
}

void Sos::broadcast(const char* text, bool to_emergency) {
  std::vector<ContactView> cs;
  contacts(cs);
  for (auto& c : cs) _m->sendText(ConvKey::direct(c.key), text);
  ChannelView ch;
  if (to_emergency && emergencyChannel(&ch)) _m->sendText(ConvKey::channel(ch.key), text);
}

void Sos::sendUpdate() {
  UiSettings& u = _m->ui();
  if (u.sos_seq < 250) u.sos_seq++;
  _m->saveUi();
  char pos[48], text[LOG_TEXT_MAX];
  position(pos, sizeof(pos));
  snprintf(text, sizeof(text), "SOS! %s needs help. %s bat %d%% #%d", _m->nodeName(), pos, _m->status().battery_pct, u.sos_seq);
  broadcast(text, u.sos_post_emergency);
  // cadence: every 2 min for 10 min, then every 10 min, then every 30 min after an hour
  // by update count, not wall clock (the clock may jump when GPS gets a fix mid-SOS):
  // 5 x 2 min (first 10 min), 5 x 10 min (to ~1 h), then every 30 min
  uint32_t interval = u.sos_seq < 5 ? 120 : u.sos_seq < 10 ? 600 : 1800;
  _next_ms = lv_tick_get() + interval * 1000;
}

void Sos::start() {
  UiSettings& u = _m->ui();
  if (u.sos_active) return;
  u.sos_active = 1;
  u.sos_seq = 0;
  u.sos_started = _m->status().now;
  if (!_m->status().gps_enabled) _m->setGps(true);   // keep the GPS powered during SOS
  sendUpdate();
}

void Sos::cancel() {
  UiSettings& u = _m->ui();
  if (!u.sos_active) return;
  char pos[48], text[LOG_TEXT_MAX];
  position(pos, sizeof(pos));
  snprintf(text, sizeof(text), "SOS CANCELLED - %s is OK. %s", _m->nodeName(), pos);
  broadcast(text, u.sos_post_emergency);
  u.sos_active = 0;
  _m->saveUi();
}

void Sos::checkIn() {
  char pos[48], text[LOG_TEXT_MAX];
  position(pos, sizeof(pos));
  snprintf(text, sizeof(text), "OK check-in from %s. %s bat %d%%", _m->nodeName(), pos, _m->status().battery_pct);
  broadcast(text, false);
}

void Sos::loop() {
  if (!active()) return;
  // keep the elapsed timer right if the clock gets set (jumps) during the SOS
  uint32_t tick = lv_tick_get();
  if (!_ref_tick || tick - _ref_tick >= 2000) {
    uint32_t now = _m->status().now;
    if (_ref_tick) {
      uint32_t expect = _ref_now + (tick - _ref_tick) / 1000;
      int32_t jump = (int32_t)(now - expect);
      if (jump > 60 || jump < -60) { _m->ui().sos_started += jump; _m->saveUi(); }
    }
    _ref_now = now; _ref_tick = tick;
  }
  if ((int32_t)(tick - _next_ms) >= 0) sendUpdate();
}

bool Sos::isSosText(const char* t) {
  if (*t == 0x07) t++;   // optional BEL ring marker
  return strncasecmp(t, "SOS!", 4) == 0;
}

// "<lat>,<lon>" or "<lat>, <lon>" where both numbers have >= 3 decimals (so "Room 5, 3.5" or
// "back at 10, 20 mins" are not mistaken for places).
static bool coordNum(const char*& p, double& v) {
  const char* s = p;
  if (*p == '-') p++;
  int ip = 0, dp = 0;
  while (*p >= '0' && *p <= '9') { p++; ip++; }
  if (*p != '.' || ip == 0 || ip > 3) return false;
  p++;
  while (*p >= '0' && *p <= '9') { p++; dp++; }
  if (dp < 3) return false;
  v = strtod(s, nullptr);
  return true;
}

bool Sos::parseLocation(const char* t, double& lat, double& lon) {
  for (const char* p = t; *p; p++) {
    if (!(*p == '-' || (*p >= '0' && *p <= '9')) || (p != t && p[-1] != ' ')) continue;
    const char* q = p;
    double a, b;
    if (!coordNum(q, a) || *q != ',') continue;
    q++;
    if (*q == ' ') q++;
    if (!coordNum(q, b)) continue;
    if (*q >= '0' && *q <= '9') continue;
    if (a >= -90 && a <= 90 && b >= -180 && b <= 180 && !(a == 0 && b == 0)) { lat = a; lon = b; return true; }
  }
  return false;
}

bool Sos::isLocText(const char* t) { return strncmp(t, "LOC ", 4) == 0; }

// Garmin-style tracking: send my position to favourites on an interval, only when I've moved
// more than 50 m, plus an hourly heartbeat so they know I'm still alive.
void Sos::shareLoop() {
  uint16_t mins = kShareMinutes[_m->ui().share_loc % NUM_SHARE];
  if (!mins || active()) return;   // SOS has its own, faster cadence
  uint32_t now = lv_tick_get();
  if (_share_ms && now - _share_ms < mins * 60000UL) return;
  DeviceStatus st = _m->status();
  if (!st.gps_fix) return;
  double dy = (st.lat - _share_lat) * 111320.0, dx = (st.lon - _share_lon) * 111320.0 * cos(st.lat * M_PI / 180.0);
  bool moved = !_share_ms || dx * dx + dy * dy > 50.0 * 50.0;
  bool beat = !_share_beat_ms || now - _share_beat_ms > 3600000UL;
  _share_ms = now;
  if (!moved && !beat) return;
  _share_lat = st.lat; _share_lon = st.lon; _share_beat_ms = now;
  char pos[48], text[LOG_TEXT_MAX];
  position(pos, sizeof(pos));
  snprintf(text, sizeof(text), "LOC %s bat %d%%", pos, st.battery_pct);
  std::vector<ContactView> cs;
  contacts(cs);
  for (auto& c : cs) _m->sendText(ConvKey::direct(c.key), text);
}

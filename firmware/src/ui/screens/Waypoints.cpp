// Saved places (waypoints) and a navigate-to screen. The T-Deck has no compass, so the arrow
// is heading-up only while you're moving (GPS course); standing still it's north-up.
#include "Common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static double distM(double la1, double lo1, double la2, double lo2) {
  const double R = 6371000.0, d2r = M_PI / 180.0;
  double dla = (la2 - la1) * d2r, dlo = (lo2 - lo1) * d2r;
  double a = sin(dla / 2) * sin(dla / 2) + cos(la1 * d2r) * cos(la2 * d2r) * sin(dlo / 2) * sin(dlo / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}
static double bearingDeg(double la1, double lo1, double la2, double lo2) {
  const double d2r = M_PI / 180.0;
  double y = sin((lo2 - lo1) * d2r) * cos(la2 * d2r);
  double x = cos(la1 * d2r) * sin(la2 * d2r) - sin(la1 * d2r) * cos(la2 * d2r) * cos((lo2 - lo1) * d2r);
  return fmod(atan2(y, x) / d2r + 360.0, 360.0);
}
static void fmtDist(double d, char* b, int n) {
  if (d < 1000) snprintf(b, n, "%d m", (int)d);
  else if (d < 10000) snprintf(b, n, "%.1f km", d / 1000.0);
  else snprintf(b, n, "%d km", (int)(d / 1000.0));
}

// ---------------------------------------------------------------- navigate

#define MOVING_KMH 2.0f
#define ARRIVED_M  15.0

class NavigateScreen : public HeaderScreen {
public:
  NavigateScreen(const char* name, double lat, double lon) : _lat(lat), _lon(lon) { snprintf(_name, sizeof(_name), "%s", name); }

  void build() override {
    buildHeader(_name);
    lv_obj_t* c = content();
    int h = contentH();
    _r = (h - 12) / 2;
    if (_r > 78) _r = 78;
    _dial = ui::box(c);
    lv_obj_set_size(_dial, 2 * _r, 2 * _r);
    lv_obj_set_pos(_dial, 10, (h - 2 * _r) / 2);
    lv_obj_set_style_radius(_dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_dial, T::surface, 0);
    lv_obj_set_style_bg_opa(_dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_dial, 2, 0);
    lv_obj_set_style_border_color(_dial, T::surface2, 0);
    lv_obj_remove_flag(_dial, LV_OBJ_FLAG_SCROLLABLE);
    _north = ui::label(_dial, "N", T::f14b, T::accent_text);
    _arrow = lv_line_create(_dial);
    lv_obj_set_style_line_width(_arrow, 6, 0);
    lv_obj_set_style_line_color(_arrow, T::accent, 0);
    lv_obj_set_style_line_rounded(_arrow, true, 0);
    _hub = ui::box(_dial);
    lv_obj_set_size(_hub, 10, 10);
    lv_obj_set_style_radius(_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_hub, T::text, 0);
    lv_obj_set_style_bg_opa(_hub, LV_OPA_COVER, 0);
    lv_obj_center(_hub);

    int x = 2 * _r + 24;
    _dist = ui::label(c, "-", T::f32b, T::text);
    lv_obj_set_pos(_dist, x, 6);
    _brg = ui::label(c, "", T::f16b, T::accent_text);
    lv_obj_set_pos(_brg, x, 46);
    _eta = ui::label(c, "", T::f14, T::muted);
    lv_obj_set_pos(_eta, x, 72);
    _mode = ui::label(c, "", T::f12, T::faint);
    lv_label_set_long_mode(_mode, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_mode, SCR_W - x - 8);
    lv_obj_set_pos(_mode, x, 100);
    tick();
  }

  // Arrow pointing at `deg` (0 = up). Shaft from the centre plus a chevron head.
  void setArrow(double deg) {
    double a = deg * M_PI / 180.0;
    double cx = _r, cy = _r, L = _r - 16, hw = 14;
    double tx = cx + L * sin(a), ty = cy - L * cos(a);                 // tip
    double bx = cx - (L * 0.55) * sin(a), by = cy + (L * 0.55) * cos(a);  // tail
    double px = -cos(a), py = -sin(a);                                  // perpendicular
    double hx = tx - 22 * sin(a), hy = ty + 22 * cos(a);                // head base centre
    _pts[0] = { (lv_value_precise_t)bx, (lv_value_precise_t)by };
    _pts[1] = { (lv_value_precise_t)tx, (lv_value_precise_t)ty };
    _pts[2] = { (lv_value_precise_t)(hx + hw * px), (lv_value_precise_t)(hy + hw * py) };
    _pts[3] = { (lv_value_precise_t)tx, (lv_value_precise_t)ty };
    _pts[4] = { (lv_value_precise_t)(hx - hw * px), (lv_value_precise_t)(hy - hw * py) };
    lv_line_set_points(_arrow, _pts, 5);
  }

  void tick() override {
    GpsDetail g;
    m->gpsDetail(g);
    DeviceStatus st = m->status();
    bool fix = g.valid || st.gps_fix;
    double la = g.valid ? g.lat : st.lat, lo = g.valid ? g.lon : st.lon;
    char b[64];
    if (!fix) {
      ui::setText(_dist, "--");
      ui::setText(_brg, st.gps_enabled ? "Waiting for GPS" : "GPS is off");
      ui::setText(_eta, "");
      ui::setText(_mode, st.gps_enabled ? "Go outside with a view of the sky." : "Turn GPS on in the GPS screen.");
      lv_obj_add_flag(_arrow, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(_north, LV_ALIGN_TOP_MID, 0, 4);
      return;
    }
    lv_obj_remove_flag(_arrow, LV_OBJ_FLAG_HIDDEN);
    double d = distM(la, lo, _lat, _lon), brg = bearingDeg(la, lo, _lat, _lon);
    bool moving = g.valid && g.speed_kmh >= MOVING_KMH;
    double up = moving ? g.course_deg : 0;   // what "up" on the dial means
    setArrow(brg - up);
    double na = -up * M_PI / 180.0;          // where north is on the dial
    lv_obj_align(_north, LV_ALIGN_CENTER, (int)((_r - 12) * sin(na)), (int)(-(_r - 12) * cos(na)));
    if (d < ARRIVED_M) {
      ui::setText(_dist, "Here");
      ui::setText(_brg, "You've arrived");
      if (!_arrived) { _arrived = true; m->playSound(SOUND_MESSAGE); }
    } else {
      _arrived = false;
      fmtDist(d, b, sizeof(b));
      ui::setText(_dist, b);
      snprintf(b, sizeof(b), "%d\xC2\xB0 %s", (int)brg, ui::compassPoint(brg));
      ui::setText(_brg, b);
    }
    if (moving) {
      int mins = (int)(d / 1000.0 / g.speed_kmh * 60.0 + 0.5);
      if (mins < 60) snprintf(b, sizeof(b), "%.1f km/h \xC2\xB7 ~%d min", g.speed_kmh, mins);
      else snprintf(b, sizeof(b), "%.1f km/h \xC2\xB7 ~%dh%02d", g.speed_kmh, mins / 60, mins % 60);
    } else snprintf(b, sizeof(b), "standing still");
    ui::setText(_eta, b);
    ui::setText(_mode, moving ? "Arrow follows your direction of travel."
                                    : "North-up. Walk a few steps and the arrow turns to your direction (no compass in the T-Deck).");
  }

  void onInput(const InputEvent& e) override {
    if (e.type == IN_CHAR && (e.ch == 'm' || e.ch == 'M')) { app->push(makeMapScreenAt(_lat, _lon, _name)); return; }
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE || e.type == IN_SELECT) app->pop();
  }

private:
  char _name[24];
  double _lat, _lon;
  int _r = 70;
  bool _arrived = false;
  lv_obj_t *_dial = nullptr, *_north = nullptr, *_arrow = nullptr, *_hub = nullptr;
  lv_obj_t *_dist = nullptr, *_brg = nullptr, *_eta = nullptr, *_mode = nullptr;
  lv_point_precise_t _pts[5];
};

Screen* makeNavigateScreen(const char* name, double lat, double lon) { return new NavigateScreen(name, lat, lon); }

// ---------------------------------------------------------------- waypoints list

class WaypointsScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Saved places");
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.onActivate = [this](int i) { act(i); };
    fill();
  }
  void onShow() override { if (_built) fill(); _built = true; }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    DeviceStatus st = m->status();
    char sub[48];
    snprintf(sub, sizeof(sub), "%s", st.gps_fix ? "" : "needs a GPS fix");
    _list.rowSetting(IC_PLUS, T::accent, "Save where I am", sub, false);
    int n = m->waypointCount();
    for (int i = 0; i < n; i++) {
      Waypoint w;
      if (!m->waypointAt(i, w)) continue;
      if (st.gps_fix) ui::distanceText(st.lat, st.lon, w.lat, w.lon, sub, sizeof(sub));
      else snprintf(sub, sizeof(sub), "%.4f, %.4f", w.lat, w.lon);
      _list.rowSetting(IC_PIN, lv_color_hex(0xFF8A65), w.name, sub, true);
    }
    if (n == 0) _list.addHeader("TIP: ON THE MAP, PRESS W TO SAVE A SPOT");
    _list.select(sel > 0 && sel < _list.count() ? sel : 0, false);
  }

  void act(int i) {
    if (i == 0) {
      DeviceStatus st = m->status();
      if (!st.gps_fix) { app->toast(IC_SATELLITE, "No GPS fix yet"); return; }
      double la = st.lat, lo = st.lon;
      char def[20];
      snprintf(def, sizeof(def), "Place %d", m->waypointCount() + 1);
      app->push(makeTextEntryScreen("Name this place", "e.g. Camp, Car, Water tank", def, 19, [this, la, lo](const char* name) {
        Waypoint w = {};
        snprintf(w.name, sizeof(w.name), "%s", name);
        w.lat = la; w.lon = lo; w.at = m->status().now;
        if (m->addWaypoint(w)) app->toast(IC_PIN, "Place saved"); else app->toast(IC_ERROR, "List full - delete one first");
        fill();
      }));
      return;
    }
    int wi = i - 1;
    Waypoint w;
    if (!m->waypointAt(wi, w)) return;
    std::string name = w.name;
    double la = w.lat, lo = w.lon;
    app->push(makeChoiceScreen(w.name, { { "Navigate", "arrow + distance" }, { "Show on map", "" }, { "Delete", "" } }, -1,
      [this, wi, name, la, lo](int k) {
        if (k == 0) app->push(makeNavigateScreen(name.c_str(), la, lo));
        else if (k == 1) app->push(makeMapScreenAt(la, lo, name.c_str()));
        else { m->removeWaypoint(wi); app->toast(IC_TRASH, "Place deleted"); fill(); }
      }));
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }

private:
  FocusList _list;
  bool _built = false;
};

Screen* makeWaypointsScreen() { return new WaypointsScreen(); }

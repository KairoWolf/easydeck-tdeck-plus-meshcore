// Offline map: slippy-map PNG tiles from the SD card (/tiles/z/x/y.png, the same layout
// Ripple uses), with you and your contacts plotted. Roll to pan, +/- to zoom, C to centre.
#include "Common.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "../MapView.h"
using namespace mapv;
#define MIN_Z 3
#define MAX_Z 17

class MapScreen : public HeaderScreen {
public:
  MapScreen() {}
  MapScreen(double lat, double lon, const char* label) : _fixed(true), _plat(lat), _plon(lon) {
    snprintf(_plabel, sizeof(_plabel), "%s", label ? label : "");
  }
  void build() override {
    buildHeader("Map");
    _view = content();
    lv_obj_set_style_bg_color(_view, T::dark() ? lv_color_hex(0x1A1A1A) : lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_bg_opa(_view, LV_OPA_COVER, 0);
    lv_obj_add_flag(_view, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(_view, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // zoom / info overlay (top-right of the header)
    _info = ui::label(_header, "", T::f12, T::accent_text);
    lv_obj_align(_info, LV_ALIGN_RIGHT_MID, -8, 0);

    DeviceStatus st = m->status();
    if (_fixed) { _lat = _plat; _lon = _plon; }
    else if (st.gps_fix) { _lat = st.lat; _lon = st.lon; }
    else {
      bool found = false;
      for (int i = 0; i < m->contactCount() && !found; i++) {
        ContactView c;
        if (m->contactAt(i, c) && c.has_location) { _lat = c.lat; _lon = c.lon; found = true; }
      }
      if (!found) { _lat = -31.9110; _lon = 152.4630; }   // Taree, MidCoast NSW
    }
    _z = 12;
    render();
  }

  bool tileExists(int z, int x, int y) { return tileOk(m, z, x, y); }

  void marker(int px, int py, const char* name, const uint8_t* key, bool me) {
    if (px < -40 || px > SCR_W + 40 || py < -20 || py > _vh + 20) return;
    lv_obj_t* d = ui::box(_view);
    int s = me ? 12 : 10;
    lv_obj_set_size(d, s, s);
    lv_obj_set_pos(d, px - s / 2, py - s / 2);
    bool tac = T::style() == STYLE_TACTICAL;
    lv_obj_set_style_radius(d, tac && !me ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, me ? lv_color_hex(0x2F80ED) : (tac ? T::accent : T::avatarColor(key)), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_border_color(d, me ? lv_color_white() : lv_color_black(), 0);
    if (!me && name) {
      lv_obj_t* l = ui::label(_view, name, T::f12, lv_color_white());
      lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
      lv_obj_set_style_pad_hor(l, 3, 0);
      lv_obj_set_pos(l, px + 7, py - 8);
    }
  }

  void render() {
    lv_obj_clean(_view);
    _vh = contentH();
    double cx = lon2x(_lon, _z), cy = lat2y(_lat, _z);
    double ox = cx - SCR_W / 2.0, oy = cy - _vh / 2.0;   // world pixel at the view's top-left
    int tx0 = (int)floor(ox / TILE), ty0 = (int)floor(oy / TILE);
    int tx1 = (int)floor((ox + SCR_W - 1) / TILE), ty1 = (int)floor((oy + _vh - 1) / TILE);
    int have = 0, need = 0;
    int n = 1 << _z;
    for (int ty = ty0; ty <= ty1; ty++) {
      for (int tx = tx0; tx <= tx1; tx++) {
        if (ty < 0 || ty >= n) continue;
        int wx = ((tx % n) + n) % n;
        int px = (int)(tx * TILE - ox), py = (int)(ty * TILE - oy);
        need++;
        if (!tileExists(_z, wx, ty)) continue;
        have++;
        char p[128];
        snprintf(p, sizeof(p), "%s/%d/%d/%d.png", m->tileRoot(), _z, wx, ty);
        lv_obj_t* img = lv_image_create(_view);
        lv_image_set_src(img, p);
        lv_obj_set_pos(img, px, py);
        lv_obj_set_size(img, TILE, TILE);
      }
    }
    if (have == 0) {
      lv_obj_t* l = ui::label(_view, "No map tiles here.\nPress D to download this area over Wi-Fi,\nor copy /tiles/<zoom>/<x>/<y>.png to the SD card.", T::small, T::muted);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    }

    // breadcrumb track (my recorded path)
    {
      std::vector<TrackPoint> pts(1500);   // large: lands in PSRAM on the device
      int n = m->trackLoad(pts.data(), 1500);
      _line_pts.clear();
      for (int i = 0; i < n; i++) {
        int px = (int)(lon2x(pts[i].lon, _z) - ox), py = (int)(lat2y(pts[i].lat, _z) - oy);
        if (!_line_pts.empty()) {   // skip points closer than 3 px to keep the line light
          const lv_point_precise_t& b = _line_pts.back();
          if (abs(px - (int)b.x) < 3 && abs(py - (int)b.y) < 3) continue;
        }
        _line_pts.push_back({ (lv_value_precise_t)px, (lv_value_precise_t)py });
      }
      if (_line_pts.size() >= 2) {
        lv_obj_t* ln = lv_line_create(_view);
        lv_line_set_points(ln, _line_pts.data(), _line_pts.size());
        lv_obj_set_style_line_width(ln, 3, 0);
        lv_obj_set_style_line_color(ln, T::style() == STYLE_TACTICAL ? T::accent : lv_color_hex(0xE53935), 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_set_pos(ln, 0, 0);
      }
    }

    // contacts with a known position
    for (int i = 0; i < m->contactCount(); i++) {
      ContactView c;
      if (!m->contactAt(i, c) || !c.has_location) continue;
      marker((int)(lon2x(c.lon, _z) - ox), (int)(lat2y(c.lat, _z) - oy), c.name, c.key, false);
    }
    DeviceStatus st = m->status();
    if (st.gps_fix) marker((int)(lon2x(st.lon, _z) - ox), (int)(lat2y(st.lat, _z) - oy), nullptr, nullptr, true);
    if (_fixed) {   // the place we were opened for (e.g. an SOS position)
      mapv::Pin p = { _plat, _plon, lv_color_hex(0xFF3B30), true, _plabel };
      int px = (int)(lon2x(_plon, _z) - ox), py = (int)(lat2y(_plat, _z) - oy);
      if (px > -20 && px < SCR_W + 20 && py > -20 && py < _vh + 20) {
        markerPin(px, py, p);
      }
    }

    // crosshair at the centre
    lv_obj_t* ch = ui::label(_view, IC_CROSSHAIR, &icons_16, T::style() == STYLE_TACTICAL ? T::accent : lv_color_hex(0x2F80ED));
    lv_obj_align(ch, LV_ALIGN_CENTER, 0, 0);

    char b[48];
    snprintf(b, sizeof(b), "z%d  %.4f, %.4f", _z, _lat, _lon);
    lv_label_set_text(_info, b);
  }

  void pan(int dx, int dy) {
    double cx = lon2x(_lon, _z) + dx, cy = lat2y(_lat, _z) + dy;
    _lon = x2lon(cx, _z);
    _lat = y2lat(cy, _z);
    if (_lat > 85) _lat = 85;
    if (_lat < -85) _lat = -85;
    render();
  }

  void zoom(int d) {
    int z = _z + d;
    if (z < MIN_Z || z > MAX_Z) return;
    _z = z;
    render();
  }

  void onInput(const InputEvent& e) override {
    const int step = 64;
    switch (e.type) {
      case IN_UP: pan(0, -step); break;
      case IN_DOWN: pan(0, step); break;
      case IN_LEFT: pan(-step, 0); break;
      case IN_RIGHT: pan(step, 0); break;
      case IN_SELECT: case IN_ENTER: zoom(1); break;
      case IN_BACKSPACE: app->pop(); break;
      case IN_CHAR:
        if (e.ch == '+' || e.ch == '=' || e.ch == 'i' || e.ch == 'I') zoom(1);
        else if (e.ch == '-' || e.ch == 'o' || e.ch == 'O') zoom(-1);
        else if (e.ch == 'd' || e.ch == 'D') app->push(makeOfflineMapsScreenAt(_lat, _lon));   // download around here
        else if (e.ch == 'c' || e.ch == 'C') {
          DeviceStatus st = m->status();
          if (st.gps_fix) { _lat = st.lat; _lon = st.lon; render(); }
          else app->toast(IC_SATELLITE, "No GPS fix yet");
        }
        break;
      default: break;
    }
  }

  void onContacts() override { render(); }

  void markerPin(int px, int py, const mapv::Pin& p) {
    lv_obj_t* d = ui::box(_view);
    lv_obj_set_size(d, 14, 14);
    lv_obj_set_pos(d, px - 7, py - 7);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, p.color, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_border_color(d, lv_color_white(), 0);
    if (p.label && *p.label) {
      lv_obj_t* l = ui::label(_view, p.label, T::f12, lv_color_white());
      lv_obj_set_style_bg_color(l, p.color, 0);
      lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
      lv_obj_set_style_pad_hor(l, 3, 0);
      lv_obj_set_pos(l, px + 9, py - 8);
    }
  }

private:
  bool _fixed = false;
  double _plat = 0, _plon = 0;
  char _plabel[24] = "";
  lv_obj_t* _view = nullptr;
  lv_obj_t* _info = nullptr;
  std::vector<lv_point_precise_t> _line_pts;
  double _lat = 0, _lon = 0;
  int _z = 12;
  int _vh = 0;
};

Screen* makeMapScreen() { return new MapScreen(); }
Screen* makeMapScreenAt(double lat, double lon, const char* label) { return new MapScreen(lat, lon, label); }

#include "Widgets.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

namespace ui {

lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* f, lv_color_t c) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  return l;
}

void initials(const char* name, char out[4]) {
  int n = 0;
  bool start = true;
  for (const char* p = name; *p && n < 2; p++) {
    unsigned char ch = (unsigned char)*p;
    if (ch == ' ' || ch == '-' || ch == '_' || ch == '.') { start = true; continue; }
    if (start && (isalnum(ch))) { out[n++] = toupper(ch); start = false; }
    else start = false;
  }
  if (n == 0) out[n++] = '?';
  out[n] = 0;
}

lv_obj_t* avatar(lv_obj_t* parent, const char* name, const uint8_t* key, int size, bool group) {
  lv_obj_t* a = box(parent);
  lv_obj_set_size(a, size, size);
  lv_color_t fg = lv_color_white();
  if (T::round_avatars) {
    lv_obj_set_style_radius(a, group ? size / 3 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(a, T::avatarColor(key), 0);
  } else {   // tactical: square tag, coloured initials on a dark tile
    lv_obj_set_style_radius(a, 2, 0);
    lv_obj_set_style_bg_color(a, T::surface2, 0);
    lv_obj_set_style_border_width(a, 1, 0);
    lv_obj_set_style_border_color(a, T::border, 0);
    fg = group ? T::accent : T::avatarColor(key);
  }
  lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
  const lv_font_t* f = size >= 48 ? T::title : (size >= 40 ? T::f16b : (size >= 26 ? T::f14b : T::f12));
  lv_obj_t* l;
  if (group) {
    l = label(a, IC_HASH, size >= 40 ? &icons_24 : (size >= 28 ? &icons_20 : &icons_14), fg);
  } else {
    char ini[4];
    initials(name, ini);
    l = label(a, ini, f, fg);
  }
  lv_obj_center(l);
  return a;
}

lv_obj_t* badge(lv_obj_t* parent, int count) {
  lv_obj_t* b = box(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, T::easy() ? 28 : 20);
  lv_obj_set_style_min_width(b, T::easy() ? 28 : 20, 0);
  lv_obj_set_style_radius(b, T::style() == STYLE_TACTICAL ? 2 : LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, T::accent, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(b, 6, 0);
  char t[8];
  snprintf(t, sizeof(t), "%d", count > 99 ? 99 : count);
  lv_obj_t* l = label(b, t, T::easy() ? T::f16b : T::f12, T::on_accent);
  lv_obj_center(l);
  return b;
}

lv_obj_t* iconTile(lv_obj_t* parent, const char* icon, lv_color_t color, int size) {
  lv_obj_t* t = box(parent);
  lv_obj_set_size(t, size, size);
  lv_obj_set_style_radius(t, size / 4, 0);
  lv_obj_set_style_bg_color(t, color, 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_20, 0);
  lv_obj_t* l = label(t, icon, size >= 40 ? &icons_24 : (size >= 30 ? &icons_20 : &icons_16), color);
  lv_obj_center(l);
  return t;
}

static void toLocal(uint32_t t, int tz, struct tm* out) {
  time_t lt = (time_t)t + tz * 3600;
  gmtime_r(&lt, out);
}

bool sameDay(uint32_t a, uint32_t b, int tz) {
  return ((int64_t)a + tz * 3600) / 86400 == ((int64_t)b + tz * 3600) / 86400;
}

void clockTime(uint32_t t, int tz, char* out, int n) {
  struct tm a;
  toLocal(t, tz, &a);
  snprintf(out, n, "%02d:%02d", a.tm_hour, a.tm_min);
}

static const char* kMon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static const char* kDay[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

void dateText(uint32_t t, int tz, char* out, int n) {
  static const char* kDayLong[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  struct tm a;
  toLocal(t, tz, &a);
  snprintf(out, n, "%s %d %s", kDayLong[a.tm_wday % 7], a.tm_mday, kMon[a.tm_mon % 12]);
}

void dayLabel(uint32_t t, uint32_t now, int tz, char* out, int n) {
  int64_t da = ((int64_t)t + tz * 3600) / 86400, dn = ((int64_t)now + tz * 3600) / 86400;
  struct tm a;
  toLocal(t, tz, &a);
  if (da == dn) snprintf(out, n, "Today");
  else if (dn - da == 1) snprintf(out, n, "Yesterday");
  else if (dn - da < 7) snprintf(out, n, "%s", kDay[a.tm_wday]);
  else snprintf(out, n, "%d %s", a.tm_mday, kMon[a.tm_mon]);
}

void relTime(uint32_t t, uint32_t now, int tz, char* out, int n) {
  if (t == 0) { out[0] = 0; return; }
  if (now >= t && now - t < 60) { snprintf(out, n, "now"); return; }
  if (now >= t && now - t < 3600) { snprintf(out, n, "%um", (unsigned)((now - t) / 60)); return; }
  if (sameDay(t, now, tz)) { clockTime(t, tz, out, n); return; }
  dayLabel(t, now, tz, out, n);
}

const char* compassPoint(double deg) {
  static const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
  double d = fmod(deg, 360.0);
  if (d < 0) d += 360.0;
  return dirs[(int)((d + 22.5) / 45.0) % 8];
}

void distanceText(double lat1, double lon1, double lat2, double lon2, char* out, int n) {
  const double R = 6371000.0, d2r = M_PI / 180.0;
  double dlat = (lat2 - lat1) * d2r, dlon = (lon2 - lon1) * d2r;
  double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1 * d2r) * cos(lat2 * d2r) * sin(dlon / 2) * sin(dlon / 2);
  double d = 2 * R * atan2(sqrt(a), sqrt(1 - a));
  double y = sin(dlon) * cos(lat2 * d2r);
  double x = cos(lat1 * d2r) * sin(lat2 * d2r) - sin(lat1 * d2r) * cos(lat2 * d2r) * cos(dlon);
  double brg = fmod(atan2(y, x) / d2r + 360.0, 360.0);
  const char* dir = compassPoint(brg);
  if (d < 1000) snprintf(out, n, "%d m %s", (int)d, dir);
  else if (d < 10000) snprintf(out, n, "%.1f km %s", d / 1000.0, dir);
  else snprintf(out, n, "%d km %s", (int)(d / 1000.0), dir);
}

void oneLine(lv_obj_t* l, int w) {
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, w);
  lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
}

static void anim_y_cb(void* obj, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)obj, v, 0); }
static void anim_opa_cb(void* obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)obj, v, 0); }

void animIn(lv_obj_t* obj, uint32_t delay) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_duration(&a, 200);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_start(&a);
  lv_anim_set_values(&a, 8, 0);
  lv_anim_set_exec_cb(&a, anim_y_cb);
  lv_anim_start(&a);
}

} // namespace ui

// ------------------------------------------------------------ FocusList

void FocusList::create(lv_obj_t* parent, int x, int y, int w, int h) {
  _cont = lv_obj_create(parent);
  lv_obj_remove_style_all(_cont);
  lv_obj_set_pos(_cont, x, y);
  lv_obj_set_size(_cont, w, h);
  lv_obj_set_flex_flow(_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_hor(_cont, T::pad, 0);
  lv_obj_set_style_pad_ver(_cont, T::easy() ? 4 : 2, 0);
  lv_obj_set_style_pad_row(_cont, T::easy() ? 2 : 0, 0);
  lv_obj_set_scroll_dir(_cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(_cont, LV_SCROLLBAR_MODE_OFF);
  _rows.clear();
  _sel = 0;
  _n_children = 0;
}

void FocusList::clear() {
  lv_obj_clean(_cont);
  _rows.clear();
  _n_children = 0;
}

lv_obj_t* FocusList::addRow(int height) {
  lv_obj_t* r = lv_obj_create(_cont);
  lv_obj_remove_style_all(r);
  lv_obj_set_width(r, lv_pct(100));
  lv_obj_set_height(r, height > 0 ? height : T::row_h);
  lv_obj_set_style_radius(r, T::radius, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(r, T::easy() ? T::surface2 : T::select_bg, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_FOCUSED);
  if (T::style() == STYLE_TACTICAL && !T::easy()) {   // crisp amber outline
    lv_obj_set_style_border_width(r, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(r, T::select_border, LV_STATE_FOCUSED);
  }
  lv_obj_set_style_bg_color(r, T::surface2, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  if (T::easy()) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(r, T::accent, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_border_width(r, 3, LV_STATE_FOCUSED);
  }
  lv_obj_set_style_pad_hor(r, T::easy() ? 8 : 6, 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(r, clickCb, LV_EVENT_CLICKED, this);
  int idx = (int)_rows.size();
  lv_obj_set_user_data(r, (void*)(intptr_t)idx);
  _rows.push_back(r);
  if (animate) ui::animIn(r, (_n_children < 8 ? _n_children : 8) * 30);
  _n_children++;
  if (idx == _sel) lv_obj_add_state(r, LV_STATE_FOCUSED);
  return r;
}

void FocusList::addHeader(const char* text) {
  char buf[64];
  bool tac = T::style() == STYLE_TACTICAL;
  int i = 0;
  bool start = true;
  for (; text[i] && i < (int)sizeof(buf) - 1; i++) {   // Tactical: UPPERCASE, Modern: Title case
    char c = text[i];
    buf[i] = tac ? toupper((unsigned char)c) : (start ? toupper((unsigned char)c) : tolower((unsigned char)c));
    start = c == ' ' || c == '&';
  }
  buf[i] = 0;
  if (!tac && strstr(text, "#")) strncpy(buf, text, sizeof(buf) - 1);   // keep channel names as typed
  lv_obj_t* l = ui::label(_cont, buf, T::caption, T::accent_text);
  lv_obj_set_style_pad_top(l, _n_children ? (T::easy() ? 10 : 6) : 2, 0);
  lv_obj_set_style_pad_left(l, 6, 0);
  lv_obj_set_style_pad_bottom(l, 2, 0);
  if (animate) ui::animIn(l, (_n_children < 8 ? _n_children : 8) * 30);
  _n_children++;
}

void FocusList::setEmpty(const char* icon, const char* title, const char* sub) {
  lv_obj_t* b = ui::box(_cont);
  lv_obj_set_size(b, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b, 6, 0);
  ui::label(b, icon, &icons_32, T::faint);
  ui::label(b, title, T::body_semi, T::text);
  lv_obj_t* s = ui::label(b, sub, T::small, T::muted);
  lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s, lv_pct(90));
  lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
  if (animate) ui::animIn(b, 0);
}

void FocusList::select(int i, bool anim) {
  if (_rows.empty()) { _sel = 0; return; }
  if (i < 0) i = 0;
  if (i >= (int)_rows.size()) i = (int)_rows.size() - 1;
  if (_sel >= 0 && _sel < (int)_rows.size()) lv_obj_remove_state(_rows[_sel], LV_STATE_FOCUSED);
  _sel = i;
  lv_obj_add_state(_rows[_sel], LV_STATE_FOCUSED);
  lv_obj_scroll_to_view(_rows[_sel], anim ? LV_ANIM_ON : LV_ANIM_OFF);
  if (onFocus) onFocus(_sel);
}

bool FocusList::handleNav(InputType t) {
  switch (t) {
    case IN_UP: move(-1); return true;
    case IN_DOWN: move(1); return true;
    case IN_SELECT: case IN_ENTER: activate(); return true;
    default: return false;
  }
}

void FocusList::clickCb(lv_event_t* e) {
  FocusList* self = (FocusList*)lv_event_get_user_data(e);
  lv_obj_t* r = (lv_obj_t*)lv_event_get_current_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(r);
  self->select(idx);
  self->activate();
}

static lv_color_t styleIcon(lv_color_t c) {
  // Tactical is monochrome: every icon amber except red danger/SOS ones
  if (T::style() == STYLE_TACTICAL && !lv_color_eq(c, T::danger) && !lv_color_eq(c, lv_color_hex(0xD92D20))) return T::accent;
  return c;
}

lv_obj_t* FocusList::rowSetting(const char* icon, lv_color_t icon_color, const char* title, const char* value, bool chevron) {
  icon_color = styleIcon(icon_color);
  bool easy = T::easy();
  if (!easy) {   // compact Ripple-style row: small icon, label, cyan value
    lv_obj_t* r = addRow(T::row_h);
    lv_obj_t* ic = ui::label(r, icon, T::icon, icon_color);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    int x = 22;
    int right = chevron ? 14 : 0;
    if (chevron) {
      lv_obj_t* ch = ui::label(r, IC_RIGHT, &icons_12, T::faint);
      lv_obj_align(ch, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_t* t = ui::label(r, title, T::body, T::text);
    int avail = SCR_W - 2 * T::pad - 12 - x - right;
    int vw = 0;
    if (value && *value) {
      lv_obj_t* v = ui::label(r, value, T::body, T::accent_text);
      lv_obj_update_layout(v);
      vw = lv_obj_get_width(v);
      if (vw > avail * 3 / 5) vw = avail * 3 / 5;
      ui::oneLine(v, vw);
      lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_align(v, LV_ALIGN_RIGHT_MID, -right - 2, 0);
      vw += 8;
    }
    ui::oneLine(t, avail - vw);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    return r;
  }
  bool two_line = easy && value && *value;
  lv_obj_t* r = addRow(two_line ? 76 : (easy ? 64 : (T::row_h - 6)));
  int tile = easy ? 40 : 30;
  lv_obj_t* ic = ui::iconTile(r, icon, icon_color, tile);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  int right = 0;
  if (chevron) {
    lv_obj_t* ch = ui::label(r, IC_RIGHT, &icons_16, T::faint);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, 0, 0);
    right = 20;
  }
  lv_obj_t* t = ui::label(r, title, T::body, T::text);
  int avail = SCR_W - 2 * T::pad - 16 - tile - 10 - right;
  if (two_line) {   // easy mode: value under the title, nothing truncated side by side
    ui::oneLine(t, avail);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, tile + 10, 8);
    lv_obj_t* v = ui::label(r, value, T::small, T::accent_text);
    ui::oneLine(v, avail);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, tile + 10, -8);
    return r;
  }
  int vw = 0;
  if (value && *value) {
    lv_obj_t* v = ui::label(r, value, T::small, T::muted);
    lv_obj_update_layout(v);
    vw = lv_obj_get_width(v);
    if (vw > avail / 2) vw = avail / 2;
    ui::oneLine(v, vw);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -right, 0);
    vw += 8;
  }
  ui::oneLine(t, avail - vw);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, tile + 10, 0);
  return r;
}

lv_obj_t* FocusList::rowToggle(const char* icon, lv_color_t icon_color, const char* title, bool on) {
  icon_color = styleIcon(icon_color);
  if (!T::easy()) {   // compact: "[ON]" / "[OFF]" value like Ripple, but keep a real switch for state
    lv_obj_t* r = addRow(T::row_h);
    lv_obj_t* ic = ui::label(r, icon, T::icon, icon_color);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* t = ui::label(r, title, T::body, T::text);
    ui::oneLine(t, SCR_W - 2 * T::pad - 12 - 22 - 44);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_t* sw = lv_switch_create(r);
    lv_obj_set_size(sw, 34, 18);
    if (T::style() == STYLE_TACTICAL) {
      lv_obj_set_style_radius(sw, 0, LV_PART_MAIN);
      lv_obj_set_style_radius(sw, 0, LV_PART_INDICATOR);
      lv_obj_set_style_radius(sw, 0, LV_PART_KNOB);
    }
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(sw, T::border, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, T::accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
  }
  lv_obj_t* r = addRow(T::easy() ? 64 : (T::row_h - 6));
  int tile = T::easy() ? 40 : 30;
  lv_obj_t* ic = ui::iconTile(r, icon, icon_color, tile);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* t = ui::label(r, title, T::body, T::text);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, tile + 10, 0);
  lv_obj_t* sw = lv_switch_create(r);
  lv_obj_set_size(sw, T::easy() ? 56 : 40, T::easy() ? 30 : 22);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);   // the row handles clicks
  lv_obj_set_style_bg_color(sw, T::border, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, T::accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  return sw;
}

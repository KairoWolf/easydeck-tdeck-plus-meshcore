#pragma once
#include <lvgl.h>
#include <functional>
#include <vector>
#include "Theme.h"

namespace ui {
  lv_obj_t* box(lv_obj_t* parent);   // unstyled, non-scrolling container
  // Set label text, drawing flags and combined emoji (family, rainbow flag ...) as one picture.
// Use this for anything that can hold names or message text.
void setText(lv_obj_t* label, const char* txt);
lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* f, lv_color_t c);
  lv_obj_t* avatar(lv_obj_t* parent, const char* name, const uint8_t* key, int size, bool group = false);
  lv_obj_t* badge(lv_obj_t* parent, int count);
  lv_obj_t* iconTile(lv_obj_t* parent, const char* icon, lv_color_t color, int size);
  void initials(const char* name, char out[4]);
  // "now", "5m", "14:05", "Yesterday", "Mon", "12 Mar"
  void relTime(uint32_t t, uint32_t now, int tz_hours, char* out, int n);
  void clockTime(uint32_t t, int tz_hours, char* out, int n);
  void dateText(uint32_t t, int tz_hours, char* out, int n);   // "Friday 20 Nov"
  void dayLabel(uint32_t t, uint32_t now, int tz_hours, char* out, int n);
  bool sameDay(uint32_t a, uint32_t b, int tz_hours);
  // "2.4 km NE"
  void distanceText(double lat1, double lon1, double lat2, double lon2, char* out, int n);
  const char* compassPoint(double deg);   // "N", "NE", ... for a bearing in degrees
  void animIn(lv_obj_t* obj, uint32_t delay);    // fade + slide up a little
  // single line, truncated with ... at width w
  void oneLine(lv_obj_t* label, int w);
}

// Vertical scrolling list driven by trackball/keys (manual focus) and touch.
class FocusList {
public:
  void create(lv_obj_t* parent, int x, int y, int w, int h);
  void clear();
  lv_obj_t* addRow(int height = 0);              // focusable, clickable row container
  void addHeader(const char* text);              // section label, not focusable
  void setEmpty(const char* icon, const char* title, const char* sub);
  int  count() const { return (int)_rows.size(); }
  int  selected() const { return _sel; }
  lv_obj_t* row(int i) { return (i >= 0 && i < (int)_rows.size()) ? _rows[i] : nullptr; }
  void select(int i, bool anim = true);
  void move(int d) { select(_sel + d); }
  void activate() { if (_sel >= 0 && _sel < count() && onActivate) onActivate(_sel); }
  bool handleNav(InputType t);                   // up/down/select/enter -> true if consumed
  lv_obj_t* obj() { return _cont; }
  std::function<void(int)> onActivate;
  std::function<void(int)> onFocus;
  bool animate = true;

  // standard rows
  lv_obj_t* rowSetting(const char* icon, lv_color_t icon_color, const char* title, const char* value, bool chevron = true);
  lv_obj_t* rowToggle(const char* icon, lv_color_t icon_color, const char* title, bool on);   // returns the switch
private:
  static void clickCb(lv_event_t* e);
  lv_obj_t* _cont = nullptr;
  std::vector<lv_obj_t*> _rows;
  int _sel = 0;
  int _n_children = 0;
};

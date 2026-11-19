// People: favourites first, then everyone heard on the mesh.
#include "Common.h"
#include <algorithm>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <array>
#include <ctype.h>
#include <string>

class PeoplePage : public Page {
public:
  void build(lv_obj_t* parent, int w, int h) override {
    _list.create(parent, 0, 0, w, h);
    _list.onActivate = [this](int i) {
      if (i == 0) {
        m->advertise(true);
        app->toast(m->status().clock_valid ? IC_RADIO : IC_CLOCK, m->status().clock_valid ? "Advert sent to the mesh" : "Sent - but set the clock (GPS/phone) or it may be ignored", 3500);
        return;
      }
      int k = i - 1;
      if (k >= 0 && k < (int)_keys.size()) app->push(makeContactScreen(_keys[k].data()));
    };
    fill();
  }

  // Ripple-style single-line row: type icon/avatar, name, right: distance/age, star
  void compactRow(const ContactView& c, const DeviceStatus& st) {
    lv_obj_t* r = _list.addRow(T::row_h);
    if (c.type == 1) {
      lv_obj_t* a = ui::avatar(r, c.name, c.key, 20, false);
      lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
      lv_obj_t* ic = ui::label(r, c.type == 2 ? IC_ANTENNA : c.type == 3 ? IC_USERS : IC_SIGNAL, &icons_16,
                               c.type == 2 ? lv_color_hex(0xFF8A65) : lv_color_hex(0xBA68C8));
      lv_obj_align(ic, LV_ALIGN_LEFT_MID, 2, 0);
    }
    char right[48] = "", when[16] = "";
    if (c.last_heard && st.clock_valid) ui::relTime(c.last_heard, st.now, m->tzOffset(), when, sizeof(when));
    if (c.has_location && st.gps_fix) {
      char d[24];
      ui::distanceText(st.lat, st.lon, c.lat, c.lon, d, sizeof(d));
      snprintf(right, sizeof(right), "%s", d);
    } else if (when[0]) {
      snprintf(right, sizeof(right), "%s", when);
    }
    char hx[8];
    snprintf(hx, sizeof(hx), "%02X", c.key[0]);
    lv_obj_t* hl = ui::label(r, hx, T::f12, T::faint);
    lv_obj_align(hl, LV_ALIGN_RIGHT_MID, 0, 0);
    int rw = 22;
    if (c.favorite) {
      lv_obj_t* stl = ui::label(r, IC_STAR, &icons_12, T::warning);
      lv_obj_align(stl, LV_ALIGN_RIGHT_MID, -rw, 0);
      rw += 16;
    }
    if (right[0]) {
      lv_obj_t* rl = ui::label(r, right, T::f12, T::accent_text);
      lv_obj_align(rl, LV_ALIGN_RIGHT_MID, -rw, 0);
      lv_obj_update_layout(rl);
      rw += lv_obj_get_width(rl) + 8;
    }
    lv_obj_t* nm = ui::label(r, c.name, T::body, T::text);
    ui::oneLine(nm, SCR_W - 2 * T::pad - 12 - 28 - rw);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 28, 0);
    std::array<uint8_t, LOG_KEY_LEN> k;
    memcpy(k.data(), c.key, LOG_KEY_LEN);
    _keys.push_back(k);
    _names.push_back(c.name);
  }

  void personRow(const ContactView& c, const DeviceStatus& st) {
    if (!T::easy()) { compactRow(c, st); return; }
    _names.push_back(c.name);
    lv_obj_t* r = _list.addRow();
    int av = T::avatar;
    lv_obj_t* a = ui::avatar(r, c.name, c.key, av, false);
    lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
    if (c.type != 1) {   // repeater / room / sensor: icon instead of initials
      lv_obj_clean(a);
      lv_obj_t* ic = ui::label(a, c.type == 2 ? IC_ANTENNA : c.type == 3 ? IC_USERS : IC_SIGNAL, &icons_20, lv_color_white());
      lv_obj_center(ic);
    }
    int x = av + 10;
    int text_w = SCR_W - 2 * T::pad - 16 - x - (c.favorite ? 24 : 0);
    lv_obj_t* nm = ui::label(r, c.name, T::body_semi, T::text);
    ui::oneLine(nm, text_w);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, x, T::easy() ? 8 : 6);

    char sub[80], when[24] = "";
    if (c.last_heard && st.clock_valid) ui::relTime(c.last_heard, st.now, m->tzOffset(), when, sizeof(when));
    const char* kind = c.type == 2 ? "Repeater" : c.type == 3 ? "Room" : c.type == 4 ? "Sensor" : nullptr;
    if (c.has_location && st.gps_fix) {
      char d[32];
      ui::distanceText(st.lat, st.lon, c.lat, c.lon, d, sizeof(d));
      snprintf(sub, sizeof(sub), "%s%s%s", d, when[0] ? " \xC2\xB7 " : "", when);
    } else if (when[0]) {
      snprintf(sub, sizeof(sub), "%s%sHeard %s", kind ? kind : "", kind ? " \xC2\xB7 " : "", when);
    } else {
      snprintf(sub, sizeof(sub), "%s", kind ? kind : "Not heard yet");
    }
    lv_obj_t* s = ui::label(r, sub, T::small, T::muted);
    ui::oneLine(s, text_w);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, x, T::easy() ? -8 : -6);
    if (c.favorite) {
      lv_obj_t* st_ = ui::label(r, IC_STAR, T::icon, T::warning);
      lv_obj_align(st_, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    std::array<uint8_t, LOG_KEY_LEN> k;
    memcpy(k.data(), c.key, LOG_KEY_LEN);
    _keys.push_back(k);
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _keys.clear();
    _names.clear();
    DeviceStatus st = m->status();

    // action row
    if (T::easy()) {
      lv_obj_t* r = _list.addRow();
      lv_obj_t* tile = ui::iconTile(r, IC_RADIO, T::accent, T::avatar);
      lv_obj_align(tile, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* t = ui::label(r, "Announce yourself", T::body_semi, T::accent_text);
      lv_obj_align(t, LV_ALIGN_TOP_LEFT, T::avatar + 10, 8);
      lv_obj_t* s = ui::label(r, "Let people on the mesh find you", T::small, T::muted);
      lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, T::avatar + 10, -8);
    } else {
      _list.rowSetting(IC_RADIO, lv_color_hex(0xF5B83D), "Advertise to mesh", "", false);
    }

    std::vector<ContactView> favs, people, other;
    bool all = m->ui().show_all_contacts && !T::easy();   // easy mode: people only
    for (int i = 0; i < m->contactCount(); i++) {
      ContactView c;
      if (!m->contactAt(i, c)) continue;
      if (c.favorite) favs.push_back(c);
      else if (c.type == 1) people.push_back(c);
      else if (all) other.push_back(c);
    }
    auto byName = [](const ContactView& a, const ContactView& b) { return strcasecmp(a.name, b.name) < 0; };
    auto byRecent = [](const ContactView& a, const ContactView& b) { return a.last_heard > b.last_heard; };
    std::sort(favs.begin(), favs.end(), byName);
    std::sort(people.begin(), people.end(), byRecent);
    std::sort(other.begin(), other.end(), byRecent);

    if (!favs.empty()) { _list.addHeader(T::easy() ? "FAVOURITES" : "Favourites"); for (auto& c : favs) personRow(c, st); }
    if (!people.empty()) { _list.addHeader(T::easy() ? "PEOPLE" : "Companions"); for (auto& c : people) personRow(c, st); }
    if (!other.empty()) { _list.addHeader(T::easy() ? "REPEATERS & ROOMS" : "Repeaters & rooms"); for (auto& c : other) personRow(c, st); }
    if (favs.empty() && people.empty() && other.empty()) {
      lv_obj_t* hint = ui::label(_list.obj(), "Nobody yet. Contacts appear when their device advertises nearby. Check Heard and Signal if nothing shows up.", T::small, T::muted);
      lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(hint, lv_pct(100));
      lv_obj_set_style_pad_all(hint, 10, 0);
    }
    _list.select(sel, false);
    _list.animate = false;
  }

  bool onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return true;
    if (e.type == IN_CHAR && isalpha((unsigned char)e.ch) && !_names.empty()) {   // A-Z jump (like Ripple)
      int n = (int)_names.size(), cur = _list.selected() - 1;
      for (int k = 1; k <= n; k++) {
        int i = ((cur + k) % n + n) % n;
        if (tolower((unsigned char)_names[i][0]) == tolower((unsigned char)e.ch)) { _list.select(i + 1); return true; }
      }
      return true;
    }
    return false;
  }
  void onContacts() override { _dirty = true; }
  void onShow() override { fill(); }
  void tick() override { if (_dirty) { _dirty = false; fill(); } }

private:
  FocusList _list;
  std::vector<std::array<uint8_t, LOG_KEY_LEN>> _keys;
  std::vector<std::string> _names;
  bool _dirty = false;
};

Page* makePeoplePage() { return new PeoplePage(); }

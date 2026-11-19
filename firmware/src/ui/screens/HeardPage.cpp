// Heard: every node whose advert we received recently (like MeshOS "Heard").
#include "Common.h"
#include <stdio.h>
Screen* makeDiscoverScreen();

class HeardPage : public Page {
public:
  void build(lv_obj_t* parent, int w, int h) override {
    _list.create(parent, 0, 0, w, h);
    _list.onActivate = [this](int i) {
      if (i == 0) { app->push(makeDiscoverScreen()); return; }
      i -= 1;
      if (i < 0 || i >= _n) return;
      ContactView c;
      if (m->findContact(_heard[i].key, c)) app->push(makeContactScreen(_heard[i].key));
      else app->toast(IC_INFO, "Not in contacts (auto-add may be off)");
    };
    fill();
  }
  void fill() {
    int sel = _list.selected();
    _list.clear();
    _n = m->recentlyHeard(_heard, 16);
    DeviceStatus st = m->status();
    _list.rowSetting(IC_ANTENNA, lv_color_hex(0xFF8A65), "Discover nearby repeaters", "", true);
    for (int i = 0; i < _n; i++) {
      HeardView& hv = _heard[i];
      lv_obj_t* r = _list.addRow(T::easy() ? 64 : T::row_h);
      lv_obj_t* a = ui::avatar(r, hv.name, hv.key, T::easy() ? 40 : 20, false);
      lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
      char right[40], when[16] = "";
      if (st.clock_valid) ui::relTime(hv.when, st.now, m->tzOffset(), when, sizeof(when));
      if (hv.hops == 0) snprintf(right, sizeof(right), "direct  %s", when);
      else snprintf(right, sizeof(right), "%d hop%s  %s", hv.hops, hv.hops == 1 ? "" : "s", when);
      lv_obj_t* rl = ui::label(r, right, T::easy() ? T::small : T::f12, T::accent_text);
      lv_obj_align(rl, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_update_layout(rl);
      int x = T::easy() ? 50 : 28;
      lv_obj_t* nm = ui::label(r, hv.name, T::body, T::text);
      ui::oneLine(nm, SCR_W - 2 * T::pad - 12 - x - lv_obj_get_width(rl) - 8);
      lv_obj_align(nm, LV_ALIGN_LEFT_MID, x, 0);
    }
    if (_n == 0) {
      lv_obj_t* h = ui::label(_list.obj(), "Nothing heard yet. Adverts from nearby nodes and repeaters show up here. Try Discover to ping repeaters in range.", T::small, T::muted);
      lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(h, lv_pct(100));
      lv_obj_set_style_pad_all(h, 8, 0);
    }
    _list.select(sel, false);
    _list.animate = false;
  }
  bool onInput(const InputEvent& e) override { return _list.handleNav(e.type); }
  void onContacts() override { _dirty = true; }
  void tick() override { if (_dirty) { _dirty = false; fill(); } }
private:
  FocusList _list;
  HeardView _heard[16];
  int _n = 0;
  bool _dirty = false;
};

Page* makeHeardPage() { return new HeardPage(); }

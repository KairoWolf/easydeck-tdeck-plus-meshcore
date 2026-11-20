// First-run onboarding: Welcome -> Name -> Radio preset (or More options: settings packs) -> Style -> Done.
#include "Common.h"
#include <stdio.h>
#include <string.h>

extern "C" const lv_image_dsc_t easydeck_logo_96;

class SetupScreen : public Screen {
public:
  bool fullscreen() override { return true; }

  void build() override {
    _page = ui::box(root);
    lv_obj_set_size(_page, SCR_W, SCR_H);
    _dots = ui::box(root);
    lv_obj_set_size(_dots, SCR_W, 18);
    lv_obj_set_pos(_dots, 0, SCR_H - 18);
    show(0);
  }

  void dots() {
    lv_obj_clean(_dots);
    if (_step == 0) return;
    const int n = 4, w = 6, gap = 6;
    int total = n * w + (n - 1) * gap + 10;
    int x = (SCR_W - total) / 2;
    for (int i = 1; i <= n; i++) {
      lv_obj_t* d = ui::box(_dots);
      int dw = i == _step ? w + 10 : w;
      lv_obj_set_size(d, dw, 6);
      lv_obj_set_pos(d, x, 4);
      lv_obj_set_style_radius(d, 3, 0);
      lv_obj_set_style_bg_color(d, i == _step ? T::accent : T::border, 0);
      lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
      x += dw + gap;
    }
  }

  lv_obj_t* heading(const char* t, const char* sub, int y) {
    lv_obj_t* h = ui::label(_page, t, T::title, T::text);
    lv_obj_set_pos(h, 16, y);
    if (sub) {
      lv_obj_t* s = ui::label(_page, sub, T::small, T::muted);
      lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(s, SCR_W - 32);
      lv_obj_set_pos(s, 16, y + 28);
      ui::animIn(s, 60);
    }
    ui::animIn(h, 0);
    return h;
  }

  lv_obj_t* button(const char* text, int y) {
    lv_obj_t* b = ui::box(_page);
    lv_obj_set_size(b, SCR_W - 48, 38);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, T::style() == STYLE_TACTICAL ? 0 : 20, 0);
    lv_obj_set_style_bg_color(b, T::accent, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { ((SetupScreen*)lv_event_get_user_data(e))->next(); }, LV_EVENT_CLICKED, this);
    lv_obj_t* l = ui::label(b, text, T::f16b, T::on_accent);
    lv_obj_center(l);
    ui::animIn(b, 160);
    return b;
  }

  static void popIn(lv_obj_t* o, uint32_t delay) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, 64, 256);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_duration(&a, 420);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&a, [](void* ob, int32_t v) { lv_obj_set_style_transform_scale((lv_obj_t*)ob, v, 0); });
    lv_anim_start(&a);
  }

  lv_obj_t* logo(lv_obj_t* parent, const char* icon, lv_color_t bg, int size, int y) {
    lv_obj_t* c = ui::box(parent);
    lv_obj_set_size(c, size, size);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(c, T::style() == STYLE_TACTICAL ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, bg, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    if (T::shadows) {
      lv_obj_set_style_shadow_color(c, bg, 0);
      lv_obj_set_style_shadow_width(c, 30, 0);
      lv_obj_set_style_shadow_opa(c, LV_OPA_40, 0);
    }
    lv_obj_set_style_transform_pivot_x(c, size / 2, 0);
    lv_obj_set_style_transform_pivot_y(c, size / 2, 0);
    lv_obj_t* i = ui::label(c, icon, &icons_48, T::on_accent);
    lv_obj_center(i);
    popIn(c, 0);
    return c;
  }

  void show(int step) {
    _step = step;
    lv_obj_clean(_page);
    _ta = nullptr;
    dots();
    switch (step) {
      case 0: {
        {   // the EasyDeck mark (tools/gen_logo.py)
          lv_obj_t* im = lv_image_create(_page);
          lv_image_set_src(im, &easydeck_logo_96);
          lv_obj_align(im, LV_ALIGN_TOP_MID, 0, 8);
          lv_obj_set_style_transform_pivot_x(im, 48, 0);
          lv_obj_set_style_transform_pivot_y(im, 48, 0);
          popIn(im, 0);
        }
        lv_obj_t* t = ui::label(_page, "EasyDeck", T::hero, T::text);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 110);
        ui::animIn(t, 120);
        lv_obj_t* s = ui::label(_page, "Off-grid messaging for everyone", T::small, T::muted);
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 150);
        ui::animIn(s, 180);
        button("Get started", 186);
        break;
      }
      case 1: {
        heading("What's your name?", "People see this when you message them.", 22);
        _ta = lv_textarea_create(_page);
        lv_textarea_set_one_line(_ta, true);
        lv_textarea_set_max_length(_ta, 24);
        lv_textarea_set_placeholder_text(_ta, "Your name");
        lv_obj_set_width(_ta, SCR_W - 32);
        lv_obj_set_pos(_ta, 16, 96);
        lv_obj_set_style_text_font(_ta, T::body, 0);
        lv_obj_set_style_text_color(_ta, T::text, 0);
        lv_obj_set_style_text_color(_ta, T::faint, LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_bg_color(_ta, T::surface, 0);
        lv_obj_set_style_border_color(_ta, T::accent, 0);
        lv_obj_set_style_border_width(_ta, 2, 0);
        lv_obj_set_style_radius(_ta, 12, 0);
        lv_obj_set_style_pad_hor(_ta, 12, 0);
        lv_obj_set_style_pad_ver(_ta, 10, 0);
        lv_obj_set_style_bg_color(_ta, T::accent, LV_PART_CURSOR);
        lv_obj_add_state(_ta, LV_STATE_FOCUSED);
        if (_name[0]) lv_textarea_set_text(_ta, _name);
        ui::animIn(_ta, 100);
        button("Continue", 160);
        break;
      }
      case 2: {
        heading("Your area", "Pick the radio preset your local mesh uses.", 10);
        _list.create(_page, 0, 66, SCR_W, SCR_H - 20 - 66);
        for (int i = 0; i < kNumRadioPresets; i++) {
          lv_obj_t* r = _list.addRow(T::easy() ? T::row_h : 38);
          lv_obj_t* t = ui::label(r, kRadioPresets[i].name, T::body_semi, T::text);
          lv_obj_align(t, LV_ALIGN_TOP_LEFT, 4, T::easy() ? 7 : 2);
          lv_obj_t* s = ui::label(r, kRadioPresets[i].where, T::easy() ? T::small : T::f12, T::muted);
          lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 4, T::easy() ? -7 : -3);
        }
        {   // More options: settings packs (built in + from GitHub)
          lv_obj_t* r = _list.addRow(T::easy() ? T::row_h : 38);
          lv_obj_t* t = ui::label(r, "More options", T::body_semi, T::accent_text);
          lv_obj_align(t, LV_ALIGN_TOP_LEFT, 4, T::easy() ? 7 : 2);
          lv_obj_t* s = ui::label(r, "Settings packs, e.g. MidCoast NSW: radio + channels", T::easy() ? T::small : T::f12, T::muted);
          lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 4, T::easy() ? -7 : -3);
        }
        int cur = findPreset(m->radio());
        _list.select(cur >= 0 ? cur : 0, false);
        _list.onActivate = [this](int i) {
          if (i == kNumRadioPresets) { app->push(makePacksScreen([this]() { next(); })); return; }
          m->setRadio(kRadioPresets[i]); next();
        };
        break;
      }
      case 3: {
        heading("Your home screen", nullptr, 10);   // not a "setup" choice: what you see every day
        static const char* names[2] = { "Full menu", "Easy menu" };
        static const char* subs[2] = { "Everything + shortcuts", "Big list, family first" };
        static const char* icons[2] = { IC_CHATS, IC_HEART };
        const int w = (SCR_W - 16 * 2 - 10) / 2;
        for (int i = 0; i < 2; i++) {
          _cards[i] = ui::box(_page);
          lv_obj_set_size(_cards[i], w, 140);
          lv_obj_set_pos(_cards[i], 16 + i * (w + 10), 50);
          lv_obj_set_style_radius(_cards[i], T::style() == STYLE_TACTICAL ? 2 : 16, 0);
          lv_obj_set_style_bg_color(_cards[i], T::surface, 0);
          lv_obj_set_style_bg_opa(_cards[i], LV_OPA_COVER, 0);
          lv_obj_set_style_border_color(_cards[i], T::border, 0);
          lv_obj_set_style_border_width(_cards[i], 1, 0);
          lv_obj_set_style_border_color(_cards[i], T::accent, LV_STATE_FOCUSED);
          lv_obj_set_style_border_width(_cards[i], 3, LV_STATE_FOCUSED);
          lv_obj_set_style_bg_color(_cards[i], T::surface2, LV_STATE_FOCUSED);
          lv_obj_add_flag(_cards[i], LV_OBJ_FLAG_CLICKABLE);
          lv_obj_set_user_data(_cards[i], (void*)(intptr_t)i);
          lv_obj_add_event_cb(_cards[i], [](lv_event_t* e) {
            SetupScreen* s = (SetupScreen*)lv_event_get_user_data(e);
            s->pickStyle((int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e)));
            s->next();
          }, LV_EVENT_CLICKED, this);
          lv_obj_t* ic = ui::label(_cards[i], icons[i], &icons_48, i ? lv_color_hex(0xF06292) : T::accent);
          lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);
          lv_obj_t* t = ui::label(_cards[i], names[i], i ? T::f20b : T::f16b, T::text);
          lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 78);
          lv_obj_t* s = ui::label(_cards[i], subs[i], T::f12, T::muted);
          lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -10);
          ui::animIn(_cards[i], 80 + i * 60);
        }
        pickStyle(m->ui().easy_mode ? 1 : 0);
        break;
      }
      case 4: {
        logo(_page, IC_CHECK, T::accent, 68, 16);
        char t[64];
        snprintf(t, sizeof(t), "You're all set, %s", _name);
        lv_obj_t* h = ui::label(_page, t, T::title, T::text);
        ui::oneLine(h, SCR_W - 24);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 96);
        ui::animIn(h, 120);
        lv_obj_t* s = ui::label(_page, "We've announced you to the mesh.\nFriends nearby will see you soon.", T::small, T::muted);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 128);
        ui::animIn(s, 180);
        button("Start", 180);
        break;
      }
    }
  }

  void pickStyle(int i) {
    _style = i;
    for (int k = 0; k < 2; k++) {
      if (k == i) lv_obj_add_state(_cards[k], LV_STATE_FOCUSED);
      else lv_obj_remove_state(_cards[k], LV_STATE_FOCUSED);
    }
  }

  void next() {
    switch (_step) {
      case 0: show(1); break;
      case 1: {
        const char* t = lv_textarea_get_text(_ta);
        while (*t == ' ') t++;
        if (!*t) { app->toast(IC_EDIT, "Type your name first"); return; }
        snprintf(_name, sizeof(_name), "%s", t);
        m->setNodeName(_name);
        show(2);
        break;
      }
      case 2: show(3); break;
      case 3:
        m->ui().easy_mode = _style;
        if (!m->status().gps_enabled) m->setGps(true);   // T-Deck Plus has GPS: gives location and the clock
        m->advertise(true);
        show(4);
        break;
      default:
        m->ui().setup_done = 1;
        m->saveUi();
        app->rebuildAll();   // applies easy mode theme + opens home
    }
  }

  void onInput(const InputEvent& e) override {
    switch (_step) {
      case 1:
        if (e.type == IN_CHAR) lv_textarea_add_char(_ta, e.ch);
        else if (e.type == IN_BACKSPACE) lv_textarea_delete_char(_ta);
        else if (e.type == IN_ENTER || e.type == IN_SELECT) next();
        break;
      case 2:
        if (!_list.handleNav(e.type) && (e.type == IN_BACKSPACE || e.type == IN_BACK)) show(1);
        break;
      case 3:
        if (e.type == IN_LEFT || e.type == IN_UP) pickStyle(0);
        else if (e.type == IN_RIGHT || e.type == IN_DOWN) pickStyle(1);
        else if (e.type == IN_ENTER || e.type == IN_SELECT) next();
        break;
      default:
        if (e.type == IN_ENTER || e.type == IN_SELECT) next();
    }
  }

private:
  lv_obj_t* _page = nullptr;
  lv_obj_t* _dots = nullptr;
  lv_obj_t* _ta = nullptr;
  lv_obj_t* _cards[2] = { nullptr, nullptr };
  FocusList _list;
  int _step = 0;
  int _style = 0;
  char _name[32] = "";
};

Screen* makeSetupScreen() { return new SetupScreen(); }

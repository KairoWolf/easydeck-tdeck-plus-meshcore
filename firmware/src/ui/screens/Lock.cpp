// Lock screen: clock, unread count, PIN/passphrase entry. SOS stays reachable (hold the
// trackball 3 s - handled by App before this screen sees the input). Too many wrong tries
// lock entry for a while; "?" offers erasing the device if the PIN is forgotten.
#include "Common.h"
#include <stdio.h>
#include <string.h>
#include <string>

#define PIN_MAX 16
#define TRIES_BEFORE_WAIT 5

class LockScreen : public Screen {
public:
  bool fullscreen() override { return true; }
  bool isLock() override { return true; }
  bool lockSafe() override { return true; }

  void build() override {
    lv_obj_set_style_bg_color(root, T::bg, 0);
    _clock = ui::label(root, "", T::hero, T::text);
    lv_obj_align(_clock, LV_ALIGN_TOP_MID, 0, 22);
    _date = ui::label(root, "", T::small, T::muted);
    lv_obj_align(_date, LV_ALIGN_TOP_MID, 0, 22 + lv_font_get_line_height(T::hero) + 2);
    _batt = ui::label(root, "", T::caption, T::muted);
    lv_obj_align(_batt, LV_ALIGN_TOP_RIGHT, -8, 4);
    _unread = ui::label(root, "", T::small, T::accent_text);
    lv_obj_align(_unread, LV_ALIGN_TOP_MID, 0, 22 + lv_font_get_line_height(T::hero) + 22);

    OwnerInfo oi; m->ownerInfo(oi);
    _has_info = oi.owner[0] || oi.ice[0] || oi.medical[0];
    int dy = _has_info ? -16 : 0;   // room for the owner line at the bottom
    _field_y = 56 + dy;
    lv_obj_t* ic = ui::label(root, IC_LOCK, T::icon, T::muted);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, 22 + dy);
    _field = ui::box(root);
    lv_obj_set_size(_field, 200, 36);
    lv_obj_align(_field, LV_ALIGN_CENTER, 0, _field_y);
    lv_obj_set_style_radius(_field, T::style() == STYLE_TACTICAL ? 0 : 18, 0);
    lv_obj_set_style_bg_color(_field, T::surface, 0);
    lv_obj_set_style_bg_opa(_field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_field, 2, 0);
    lv_obj_set_style_border_color(_field, T::accent, 0);
    _dots = ui::label(_field, "", T::f20b, T::text);
    lv_obj_center(_dots);
    _hint = ui::label(root, "Type your PIN, then Enter", T::small, T::muted);
    lv_obj_align(_hint, LV_ALIGN_CENTER, 0, 88 + dy);
    if (_has_info) {   // like a phone's medical ID: readable without the PIN
      char b[96];
      if (oi.owner[0]) snprintf(b, sizeof(b), "Owner: %s  \xC2\xB7  roll up = emergency info", oi.owner);
      else snprintf(b, sizeof(b), "Roll the trackball up for emergency info");
      lv_obj_t* ol = ui::label(root, b, T::caption, T::accent_text);
      ui::oneLine(ol, SCR_W - 16);
      lv_obj_align(ol, LV_ALIGN_BOTTOM_MID, 0, -22);
    }
    lv_obj_t* sos = ui::label(root, "Hold the trackball 3 s for SOS  \xC2\xB7  roll down: forgot PIN", T::caption, T::faint);
    lv_obj_align(sos, LV_ALIGN_BOTTOM_MID, 0, -6);
    tick();
  }

  void tick() override {
    DeviceStatus st = m->status();
    char b[40];
    if (st.clock_valid) {
      ui::clockTime(st.now, m->tzOffset(), b, sizeof(b));
      ui::setText(_clock, b);
      ui::dateText(st.now, m->tzOffset(), b, sizeof(b));
      ui::setText(_date, b);
    } else { ui::setText(_clock, "EasyDeck"); ui::setText(_date, ""); }
    snprintf(b, sizeof(b), "%s%d%%", st.charging ? "+" : "", st.battery_pct);
    ui::setText(_batt, b);
    int n = m->totalUnread();
    if (n) snprintf(b, sizeof(b), "%d new message%s", n, n == 1 ? "" : "s"); else b[0] = 0;
    ui::setText(_unread, b);
    if (_wait_until) {
      int32_t left = (int32_t)(_wait_until - lv_tick_get());
      if (left <= 0) { _wait_until = 0; setHint("Type your PIN, then Enter", false); }
      else { snprintf(b, sizeof(b), "Too many tries - wait %ds", (int)(left / 1000) + 1); setHint(b, true); }
    }
  }

  void setHint(const char* t, bool bad) {
    ui::setText(_hint, t);
    lv_obj_set_style_text_color(_hint, bad ? T::danger : T::muted, 0);
  }

  void redraw() {
    std::string d;
    for (size_t i = 0; i < _pin.size(); i++) d += i ? " \xE2\x80\xA2" : "\xE2\x80\xA2";   // bullets
    ui::setText(_dots, d.c_str());
  }

  void shake() {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _field);
    lv_anim_set_values(&a, -10, 10);
    lv_anim_set_duration(&a, 60);
    lv_anim_set_playback_duration(&a, 60);
    lv_anim_set_repeat_count(&a, 3);
    lv_obj_set_user_data(_field, (void*)(intptr_t)_field_y);   // remembered for the animation callbacks
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) {
      lv_obj_align((lv_obj_t*)o, LV_ALIGN_CENTER, v, (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)o)); });
    lv_anim_set_user_data(&a, _field);
    lv_anim_set_completed_cb(&a, [](lv_anim_t* an) {
      lv_obj_t* f = (lv_obj_t*)lv_anim_get_user_data(an);
      lv_obj_align(f, LV_ALIGN_CENTER, 0, (int)(intptr_t)lv_obj_get_user_data(f)); });
    lv_anim_start(&a);
  }

  void submit() {
    // Settings trims spaces around a new PIN (text entry does), so trim here the same way
    while (!_pin.empty() && _pin.back() == ' ') _pin.pop_back();
    while (!_pin.empty() && _pin.front() == ' ') _pin.erase(0, 1);
    if (_pin.empty()) { redraw(); return; }
    if (app->checkPin(_pin.c_str())) {
      App* ap = app;
      ap->unlocked();
      ap->pop();
      return;
    }
    _pin.clear();
    redraw();
    shake();
    m->playSound(SOUND_CLICK);
    if (++_wrong % TRIES_BEFORE_WAIT == 0) {   // 30 s, then 60 s, 120 s ...
      uint32_t secs = 30u << (_wrong / TRIES_BEFORE_WAIT - 1 > 4 ? 4 : _wrong / TRIES_BEFORE_WAIT - 1);
      _wait_until = lv_tick_get() + secs * 1000;
      tick();
    } else setHint("Wrong PIN", true);
  }

  void onInput(const InputEvent& e) override {
    // trackball up/down, not letters: a PIN may start with any character
    if (e.type == IN_UP && _has_info) { showInfo(); return; }   // works during the wrong-PIN wait too
    if (_wait_until) return;
    switch (e.type) {
      case IN_DOWN: if (_pin.empty()) forgot(); return;
      case IN_CHAR:
        if (_pin.size() < PIN_MAX && e.ch >= ' ') { _pin += e.ch; redraw(); }
        break;
      case IN_BACKSPACE: if (!_pin.empty()) { _pin.pop_back(); redraw(); } break;
      case IN_ENTER: case IN_SELECT: submit(); break;
      default: break;
    }
  }

  void showInfo() {
    OwnerInfo oi; m->ownerInfo(oi);
    char t[220];
    snprintf(t, sizeof(t), "Owner: %s\n\nEmergency contact: %s\n\nMedical: %s",
             oi.owner[0] ? oi.owner : "-", oi.ice[0] ? oi.ice : "-", oi.medical[0] ? oi.medical : "-");
    app->push(makeInfoScreen("Emergency info", t));
  }

  void forgot() {
    app->push(makeConfirmScreen("Forgot PIN",
      "The only way past a forgotten PIN is to erase this EasyDeck: identity, contacts, channels, messages and settings. Maps on the SD card stay.",
      "Erase EasyDeck", true, [this]() { m->factoryReset(); }));
  }

private:
  lv_obj_t *_batt = nullptr;
  lv_obj_t *_clock = nullptr, *_date = nullptr, *_unread = nullptr, *_field = nullptr, *_dots = nullptr, *_hint = nullptr;
  std::string _pin;
  bool _has_info = false;
  int _field_y = 56;
  int _wrong = 0;
  uint32_t _wait_until = 0;
};

Screen* makeLockScreen() { return new LockScreen(); }

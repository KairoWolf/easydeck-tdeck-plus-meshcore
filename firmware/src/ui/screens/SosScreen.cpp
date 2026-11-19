// SOS: idle (start / check-in / settings), arming countdown, active, and the
// full-screen alarm shown when someone else's SOS arrives.
#include "Common.h"
#include "ui/Sos.h"
#include <stdio.h>
#include <string.h>

static const lv_color_t kRed = lv_color_hex(0xD92D20);
static const lv_color_t kRedDark = lv_color_hex(0x3A0A08);

class SosScreen : public HeaderScreen {
public:
  void build() override { show(); }

  void show() {
    lv_obj_clean(root);
    _header = nullptr; _content = nullptr;
    _mode = app->sos().active() ? 2 : (_count > 0 ? 1 : 0);
    if (_mode == 1) buildArming();
    else if (_mode == 2) buildActive();
    else buildIdle();
    app->refreshStatus();
  }

  // ---------------------------------------------------------------- idle
  void buildIdle() {
    lv_obj_set_style_bg_color(root, T::bg, 0);
    buildHeader("SOS");
    lv_obj_t* c = content();
    std::vector<ContactView> cs;
    int n = app->sos().contacts(cs);

    lv_obj_t* hero = ui::box(c);
    lv_obj_set_size(hero, SCR_W, 74);
    lv_obj_t* btn = ui::box(hero);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_radius(btn, T::style() == STYLE_TACTICAL ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, kRed, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    if (T::shadows) {
      lv_obj_set_style_shadow_color(btn, kRed, 0);
      lv_obj_set_style_shadow_width(btn, 18, 0);
      lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    }
    lv_obj_t* si = ui::label(btn, IC_SIREN, &icons_32, lv_color_white());
    lv_obj_center(si);
    char who[96];
    if (n == 0) snprintf(who, sizeof(who), "No SOS contacts yet. Star people in Contacts to add them.");
    else snprintf(who, sizeof(who), "Alerts go to %d favourite%s%s.", n, n == 1 ? "" : "s",
                  m->ui().sos_post_emergency ? " and #emergency" : "");
    lv_obj_t* t = ui::label(hero, "Emergency alert", T::body_semi, T::text);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 84, 12);
    lv_obj_t* s = ui::label(hero, who, T::f12, n ? T::muted : T::warning);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, SCR_W - 96);
    lv_obj_update_layout(t);
    lv_obj_align_to(s, t, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);   // below the title whatever its font size

    _list.create(c, 0, 74, SCR_W, contentH() - 74 - 16);
    _list.rowSetting(IC_SIREN, kRed, "Start SOS", "10 s countdown", false);
    _list.rowSetting(IC_OK_CIRCLE, T::success, "Send \"I'm OK\" check-in", "", false);
    _sw = _list.rowToggle(IC_HASH, lv_color_hex(0xBA68C8), "Also post to #emergency", m->ui().sos_post_emergency);
    _list.rowSetting(IC_STAR, T::warning, "SOS contacts", n ? std::to_string(n).c_str() : "none", true);
    _list.select(0, false);
    _list.onActivate = [this](int i) {
      if (i == 0) { _count = 10; show(); }
      else if (i == 1) {
        app->sos().checkIn();
        app->toast(IC_OK_CIRCLE, "Check-in sent");
      } else if (i == 2) {
        UiSettings& u = m->ui();
        u.sos_post_emergency = !u.sos_post_emergency;
        if (u.sos_post_emergency && !app->sos().emergencyChannel()) m->joinChannel("#emergency");
        m->saveUi();
        if (u.sos_post_emergency) lv_obj_add_state(_sw, LV_STATE_CHECKED); else lv_obj_remove_state(_sw, LV_STATE_CHECKED);
      } else if (i == 3) app->push(makePageScreen(PAGE_CONTACTS));
    };
    lv_obj_t* nb = ui::box(c);
    lv_obj_set_size(nb, SCR_W, 16);
    lv_obj_align(nb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nb, T::bg, 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_COVER, 0);
    lv_obj_t* note = ui::label(nb, "Does not contact 000. Delivery depends on the mesh.", T::f12, T::faint);
    lv_obj_center(note);
  }

  // ---------------------------------------------------------------- arming
  void buildArming() {
    lv_obj_set_style_bg_color(root, kRed, 0);
    lv_obj_t* t = ui::label(root, "SOS in", T::title, lv_color_white());
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, STATUS_H + 14);
    _num = ui::label(root, "10", T::f32b, lv_color_white());
    lv_obj_set_style_transform_pivot_x(_num, 20, 0);
    lv_obj_align(_num, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t* h = ui::label(root, "Press any key to cancel", T::body, lv_color_white());
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -20);
    _ring = lv_arc_create(root);
    lv_obj_set_size(_ring, 120, 120);
    lv_obj_align(_ring, LV_ALIGN_CENTER, 0, -6);
    lv_arc_set_bg_angles(_ring, 0, 360);
    lv_arc_set_rotation(_ring, 270);
    lv_arc_set_range(_ring, 0, 100);
    lv_arc_set_value(_ring, 100);
    lv_obj_remove_style(_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(_ring, lv_color_hex(0x7A1A12), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_ring, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(_ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_ring, 8, LV_PART_INDICATOR);
    _arm_ms = lv_tick_get();
    m->playSound(SOUND_CLICK);
  }

  // ---------------------------------------------------------------- active
  void buildActive() {
    lv_obj_set_style_bg_color(root, kRedDark, 0);
    lv_obj_t* bar = ui::box(root);
    lv_obj_set_size(bar, SCR_W, 34);
    lv_obj_set_pos(bar, 0, STATUS_H);
    lv_obj_set_style_bg_color(bar, kRed, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_t* ic = ui::label(bar, IC_SIREN, &icons_20, lv_color_white());
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t* t = ui::label(bar, "SOS ACTIVE", T::f16b, lv_color_white());
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 34, 0);
    _elapsed = ui::label(bar, "", T::f16b, lv_color_white());
    lv_obj_align(_elapsed, LV_ALIGN_RIGHT_MID, -8, 0);
    // pulse the bar
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, LV_OPA_60, LV_OPA_COVER);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, v, 0); });
    lv_anim_start(&a);

    _info = ui::label(root, "", T::f12, lv_color_hex(0xFFD6D2));
    ui::oneLine(_info, SCR_W - 16);
    lv_obj_set_pos(_info, 8, STATUS_H + 38);

    _list.create(root, 0, STATUS_H + 54, SCR_W, SCR_H - STATUS_H - 54);
    fillActive();
    tick();
  }

  void fillActive() {
    _list.clear();
    std::vector<ContactView> cs;
    app->sos().contacts(cs);
    _status.clear();
    for (auto& c : cs) {
      lv_obj_t* r = _list.addRow(T::row_h);
      lv_obj_t* a = ui::avatar(r, c.name, c.key, 20, false);
      lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* nm = ui::label(r, c.name, T::body, lv_color_white());
      ui::oneLine(nm, 170);
      lv_obj_align(nm, LV_ALIGN_LEFT_MID, 28, 0);
      lv_obj_t* st = ui::label(r, "", T::f12, lv_color_hex(0xFFD6D2));
      lv_obj_align(st, LV_ALIGN_RIGHT_MID, 0, 0);
      _status.push_back({ ConvKey::direct(c.key), st });
    }
    lv_obj_t* cancel = _list.rowSetting(IC_X, lv_color_white(), "I'm safe - cancel SOS", "", false);
    (void)cancel;
    _list.select(_status.empty() ? 0 : (int)_status.size(), false);
    _list.onActivate = [this](int i) {
      if (i == (int)_status.size()) {
        app->push(makeConfirmScreen("Cancel SOS", "Send \"SOS cancelled - I'm OK\" to everyone who got your SOS?",
          "Yes, I'm safe", false, [this]() { app->sos().cancel(); app->toast(IC_OK_CIRCLE, "SOS cancelled"); }));
      } else if (i >= 0 && i < (int)_status.size()) app->openChat(_status[i].key);
    };
  }

  void tick() override {
    if (_mode == 2) {
      if (!app->sos().active()) { _count = 0; show(); return; }
      DeviceStatus st = m->status();
      uint32_t el = st.now > app->sos().startedAt() ? st.now - app->sos().startedAt() : 0;
      char b[96];
      snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
      lv_label_set_text(_elapsed, b);
      int nx = app->sos().secondsToNext();
      snprintf(b, sizeof(b), "Update #%d sent \xC2\xB7 next in %d:%02d \xC2\xB7 %s \xC2\xB7 bat %d%%", app->sos().seq(),
               nx / 60, nx % 60, st.gps_fix ? "GPS fix" : "no GPS fix", st.battery_pct);
      lv_label_set_text(_info, b);
      for (auto& s : _status) {   // delivery state of the last message to each contact
        uint32_t seqs[1];
        LogRecord r;
        const char* txt = "-";
        if (m->conversationSeqs(s.key, seqs, 1) == 1 && m->getMessage(seqs[0], r) && r.dir == DIR_OUT)
          txt = r.status == ST_DELIVERED ? IC_CHECK2 " delivered" : r.status == ST_FAILED ? IC_ERROR " not delivered" : IC_CLOCK " sending";
        lv_label_set_text(s.lbl, txt);
      }
    }
  }

  void onShow() override { if (_mode == 2 && !app->sos().active()) { _count = 0; show(); } }

  void loopArming() {
    uint32_t el = lv_tick_get() - _arm_ms;
    int left = 10 - (int)(el / 1000);
    if (left != _count && left >= 0) {
      _count = left;
      char b[4];
      snprintf(b, sizeof(b), "%d", left);
      lv_label_set_text(_num, b);
      m->playSound(SOUND_CLICK);
    }
    lv_arc_set_value(_ring, 100 - (int)(el / 100));
    if (el >= 10000) {
      _count = 0;
      app->sos().start();
      m->playSound(SOUND_ALERT);
      show();
    }
  }

  void onInput(const InputEvent& e) override {
    if (_mode == 1) { _count = 0; show(); app->toast(IC_X, "SOS cancelled"); return; }   // any key aborts
    if (_list.handleNav(e.type)) return;
    if (_mode == 0 && (e.type == IN_LEFT || e.type == IN_BACKSPACE)) app->pop();
  }

  bool armed() const { return _mode == 1; }
  void abandonArming() {   // screen closed (e.g. trackball held) during the countdown = cancel
    if (_mode != 1) return;
    _mode = 0; _count = 0;
    app->toast(IC_X, "SOS cancelled");
  }
  void frame() { if (_mode == 1) loopArming(); }

private:
  struct St { ConvKey key; lv_obj_t* lbl; };
  FocusList _list;
  std::vector<St> _status;
  lv_obj_t *_sw = nullptr, *_num = nullptr, *_ring = nullptr, *_elapsed = nullptr, *_info = nullptr;
  int _mode = 0, _count = 0;
  uint32_t _arm_ms = 0;
  friend Screen* makeSosScreen();
};

// The arming countdown needs sub-second updates; drive it from an LVGL timer.
static void arm_timer_cb(lv_timer_t* t) {
  SosScreen* s = (SosScreen*)lv_timer_get_user_data(t);
  s->frame();
}

class SosScreenTimed : public SosScreen {
public:
  ~SosScreenTimed() override { if (_t) lv_timer_delete(_t); }
  void onHide() override {   // widgets are being deleted: the 100 ms timer must not touch them again
    if (_t) { lv_timer_delete(_t); _t = nullptr; }
    abandonArming();
  }
  void build() override {
    SosScreen::build();
    _t = lv_timer_create(arm_timer_cb, 100, (SosScreen*)this);
  }
private:
  lv_timer_t* _t = nullptr;
};

Screen* makeSosScreen() { return new SosScreenTimed(); }

// ---------------------------------------------------------------- incoming SOS alarm

class SosAlarmScreen : public Screen {
public:
  explicit SosAlarmScreen(const LogRecord& r) : _r(r) {}
  bool showsChat(const ConvKey& k) override { return false; }
  bool isSosAlarmFor(const ConvKey& k) override { return _r.conv() == k; }
  void build() override {
    lv_obj_set_style_bg_color(root, kRed, 0);
    lv_obj_t* ic = ui::label(root, IC_SIREN, &icons_48, lv_color_white());
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, STATUS_H + 4);
    char t[64];
    snprintf(t, sizeof(t), "SOS from %s", _r.name[0] ? _r.name : "unknown");
    lv_obj_t* h = ui::label(root, t, T::f20b, lv_color_white());
    ui::oneLine(h, SCR_W - 16);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, STATUS_H + 56);
    double lat, lon;
    char where[64] = "No position in message";
    DeviceStatus st = m->status();
    if (Sos::parseLocation(_r.text, lat, lon)) {
      if (st.gps_fix) ui::distanceText(st.lat, st.lon, lat, lon, where, sizeof(where));
      else snprintf(where, sizeof(where), "%.5f, %.5f", lat, lon);
    }
    lv_obj_t* w = ui::label(root, where, T::f16b, lv_color_hex(0xFFE3E0));
    lv_obj_align(w, LV_ALIGN_TOP_MID, 0, STATUS_H + 82);
    lv_obj_t* body = ui::label(root, _r.text, T::f12, lv_color_hex(0xFFE3E0));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, SCR_W - 24);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, STATUS_H + 104);

    _list.create(root, 0, SCR_H - 66, SCR_W, 66);
    _list.rowSetting(IC_CHECK, lv_color_white(), "Acknowledge (tell them you saw it)", "", false);
    _list.rowSetting(IC_CHAT, lv_color_white(), "Open chat", "", false);
    _list.select(0, false);
    _list.onActivate = [this](int i) {
      if (i == 0) {
        char a[LOG_TEXT_MAX];
        snprintf(a, sizeof(a), "SOS ACK from %s - I've seen your SOS.", m->nodeName());
        m->sendText(_r.conv(), a);
        app->toast(IC_CHECK, "Acknowledged");
      }
      ConvKey k = _r.conv();
      App* ap = app;
      ap->pop();
      if (i == 1) ap->openChat(k);
    };
    m->playSound(SOUND_ALERT);
    _last_ring = lv_tick_get();
  }
  void tick() override {   // keep ringing until acknowledged
    if (lv_tick_get() - _last_ring > 4000) { m->playSound(SOUND_ALERT); _last_ring = lv_tick_get(); }
  }
  void onInput(const InputEvent& e) override { _list.handleNav(e.type); }
private:
  LogRecord _r;
  FocusList _list;
  uint32_t _last_ring = 0;
};

Screen* makeSosAlarmScreen(const LogRecord& r) { return new SosAlarmScreen(r); }

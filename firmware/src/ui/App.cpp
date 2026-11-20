#include "App.h"
#include "Widgets.h"
#include "SetupFile.h"
#include "screens/Common.h"
#include <stdio.h>
#include <math.h>

#include <src/lvgl_private.h>   // display->scr_to_load (see pop)
#define GRAVE_MS 600

void App::begin(Model* model) {
  _m = model;
  _sos.begin(this, _m);
  int imported = SetupFile::importIfPresent(_m);   // easydeck-setup.txt on the SD card
  applyTheme();
  buildChrome();
  _last_activity = lv_tick_get();
  if (_m->ui().setup_done) openHome();
  else replaceAll(makeSetupScreen());
  setPower(PWR_ON);
  if (_m->ui().setup_done) lockNow();   // no-op without a PIN
  if (imported) {
    char b[64];
    snprintf(b, sizeof(b), "Setup loaded from SD card (%d settings)", imported);
    toast(IC_CHECK, b, 4000);
    _m->advertise(true);
  }
  if (_m->crashedLastBoot()) {
    char b[80];
    snprintf(b, sizeof(b), "Restarted after %s%s", _m->lastResetReason(), _sos.active() ? " - SOS still active" : "");
    toast(IC_ALERT, b, 5000);
    if (*_m->crashDetails() && !_sos.active()) {   // what crashed: helps fixing it
      char t[260];
      snprintf(t, sizeof(t), "EasyDeck restarted after a crash.\n\n%s\n\nThe full report is in /easydeck/crash.txt on the SD card - "
               "please send it (or a photo of this screen) with your bug report.", _m->crashDetails());
      push(makeInfoScreen("Crash report", t));
    }
  }
}

// ------------------------------------------------------------ chrome

void App::buildChrome() {
  lv_obj_t* top = lv_layer_top();
  lv_obj_clean(top);

  _status = ui::box(top);
  lv_obj_set_size(_status, SCR_W, STATUS_H);
  lv_obj_set_style_bg_color(_status, T::bg, 0);
  lv_obj_set_style_bg_opa(_status, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(_status, 8, 0);
  _st_time = ui::label(_status, "", T::f12, T::text);
  lv_obj_align(_st_time, LV_ALIGN_LEFT_MID, 0, 0);
  _st_sos = ui::label(_status, IC_SIREN " SOS", T::f12, lv_color_hex(0xFF5A5F));
  lv_obj_align(_st_sos, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(_st_sos, LV_OBJ_FLAG_HIDDEN);
  _st_icons = ui::box(_status);
  lv_obj_set_size(_st_icons, 170, STATUS_H);   // fixed width, right-aligned: a content-sized box clipped the GPS/BT icons
  lv_obj_set_flex_flow(_st_icons, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_st_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(_st_icons, 6, 0);
  lv_obj_align(_st_icons, LV_ALIGN_RIGHT_MID, 0, 0);

  _toast = ui::box(top);
  lv_obj_set_size(_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(_toast, 8, 0);
  lv_obj_set_style_max_width(_toast, SCR_W - 24, 0);
  bool tac = T::style() == STYLE_TACTICAL;
  lv_obj_set_style_radius(_toast, tac ? 0 : 17, 0);
  lv_obj_set_style_bg_color(_toast, tac ? T::accent : (T::dark() ? lv_color_hex(0xF1F5F9) : lv_color_hex(0x0F172A)), 0);
  lv_obj_set_style_bg_opa(_toast, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(_toast, 14, 0);
  if (T::shadows) { lv_obj_set_style_shadow_width(_toast, 16, 0); lv_obj_set_style_shadow_opa(_toast, LV_OPA_40, 0); }
  _toast_lbl = ui::label(_toast, "", T::f14b, tac ? T::on_accent : (T::dark() ? lv_color_hex(0x0B1220) : lv_color_hex(0xF1F5F9)));
  lv_label_set_long_mode(_toast_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_toast_lbl, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(_toast_lbl, SCR_W - 52, 0);
  lv_obj_set_style_text_align(_toast_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_toast, LV_ALIGN_BOTTOM_MID, 0, -44);   // clear of compose bars and bottom rows
  lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);

  _banner = nullptr;
  refreshStatus();
}

void App::refreshStatus() {
  if (!_status) return;
  Screen* s = _stack.empty() ? nullptr : _stack.back();
  if (s && s->fullscreen()) lv_obj_add_flag(_status, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(_status, LV_OBJ_FLAG_HIDDEN);

  if (_sos.active()) lv_obj_remove_flag(_st_sos, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(_st_sos, LV_OBJ_FLAG_HIDDEN);
  DeviceStatus st = _m->status();
  char t[16] = "";
  if (st.clock_valid) ui::clockTime(st.now, _m->tzOffset(), t, sizeof(t));
  else snprintf(t, sizeof(t), "%s", _m->nodeName());
  lv_label_set_text(_st_time, t);

  lv_obj_clean(_st_icons);
  if (_desk) ui::label(_st_icons, IC_MOON, &icons_14, T::muted);   // desk mode: quiet
  if (st.gps_enabled) ui::label(_st_icons, IC_SATELLITE, &icons_14, st.gps_fix ? T::accent_text : T::faint);
  if (st.bt_enabled) ui::label(_st_icons, IC_BT, &icons_14, st.bt_connected ? T::accent_text : T::faint);
  const char* batt = st.charging ? IC_BATT_CHG : st.battery_pct > 70 ? IC_BATT_FULL : st.battery_pct > 30 ? IC_BATT_MED : IC_BATT_LOW;
  lv_color_t bc = (st.battery_pct <= 15 && !st.charging) ? T::danger : T::text;
  char pct[24];
  snprintf(pct, sizeof(pct), "%s %d%%", batt, st.battery_pct);
  ui::label(_st_icons, pct, T::f12, bc);
}

void App::toast(const char* icon, const char* text, uint32_t ms) {
  char b[128];
  if (icon && *icon) snprintf(b, sizeof(b), "%s  %s", icon, text);
  else snprintf(b, sizeof(b), "%s", text);
  lv_label_set_text(_toast_lbl, b);
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(_toast, LV_ALIGN_BOTTOM_MID, 0, -44);   // clear of compose bars and bottom rows
  lv_obj_move_foreground(_toast);
  ui::animIn(_toast, 0);
  _toast_until = lv_tick_get() + ms;
}

// Incoming-message banner: slides down from the status bar, tap to open the chat.
void App::showBanner(const LogRecord& r) {
  if (_banner) { lv_obj_delete(_banner); _banner = nullptr; }
  _banner = ui::box(lv_layer_top());
  lv_obj_set_size(_banner, SCR_W - 12, T::easy() ? 76 : 56);
  lv_obj_set_pos(_banner, 6, STATUS_H + 2);
  lv_obj_set_style_radius(_banner, T::style() == STYLE_TACTICAL ? 0 : 14, 0);
  lv_obj_set_style_bg_color(_banner, T::surface2, 0);
  lv_obj_set_style_bg_opa(_banner, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_banner, T::style() == STYLE_TACTICAL ? T::accent : T::border, 0);
  lv_obj_set_style_border_width(_banner, 1, 0);
  if (T::shadows) { lv_obj_set_style_shadow_width(_banner, 20, 0); lv_obj_set_style_shadow_opa(_banner, LV_OPA_50, 0); }
  lv_obj_set_style_pad_hor(_banner, 10, 0);
  lv_obj_add_flag(_banner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(_banner, bannerClickCb, LV_EVENT_CLICKED, this);

  char title[64];
  bool group = r.kind == KIND_CHANNEL;
  if (group) {
    ChannelView ch;
    if (_m->findChannel(r.key, ch)) snprintf(title, sizeof(title), "%s \xC2\xB7 %s", r.name[0] ? r.name : "?", ch.name);
    else snprintf(title, sizeof(title), "%s", r.name);
  } else {
    snprintf(title, sizeof(title), "%s", r.name);
  }
  int av = T::easy() ? 48 : 34;
  lv_obj_t* a = ui::avatar(_banner, group ? title : r.name, group ? r.key : r.key, av, group);
  lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* t = ui::label(_banner, title, T::body_semi, T::text);
  ui::oneLine(t, SCR_W - 12 - 20 - av - 10);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, av + 10, T::easy() ? 8 : 6);
  if (_locked) {   // no previews (not even who it's from) on the lock screen
    snprintf(title, sizeof(title), "New message");
    lv_label_set_text(t, title);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_t* p = ui::label(_banner, _locked ? "Unlock to read it" : r.text, T::small, T::muted);
  ui::oneLine(p, SCR_W - 12 - 20 - av - 10);
  lv_obj_align(p, LV_ALIGN_BOTTOM_LEFT, av + 10, T::easy() ? -8 : -6);

  // slide down
  lv_anim_t an;
  lv_anim_init(&an);
  lv_anim_set_var(&an, _banner);
  lv_anim_set_values(&an, -70, 0);
  lv_anim_set_duration(&an, 260);
  lv_anim_set_path_cb(&an, T::style() == STYLE_TACTICAL ? lv_anim_path_ease_out : lv_anim_path_overshoot);
  lv_anim_set_exec_cb(&an, [](void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); });
  lv_anim_start(&an);

  _banner_key = r.conv();
  _banner_until = lv_tick_get() + 4000;
}

void App::bannerClickCb(lv_event_t* e) {
  App* self = (App*)lv_event_get_user_data(e);
  ConvKey k = self->_banner_key;
  self->_banner_until = lv_tick_get();   // close on next loop
  if (!self->_locked) self->openChat(k);
}

// ------------------------------------------------------------ navigation

void App::reap() {
  uint32_t now = lv_tick_get();
  for (size_t i = 0; i < _graveyard.size(); ) {
    if (now - _graveyard[i].at > GRAVE_MS) {
      delete _graveyard[i].s;
      _graveyard.erase(_graveyard.begin() + i);
    } else i++;
  }
}

void App::push(Screen* s, bool anim) {
  if (_locked && !s->lockSafe()) {   // nothing but SOS and dialogs above the lock screen
    delete s;
    toast(IC_LOCK, "Unlock first");
    return;
  }
  s->app = this;
  s->m = _m;
  s->root = lv_obj_create(NULL);
  lv_obj_remove_style_all(s->root);
  lv_obj_set_size(s->root, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s->root, T::bg, 0);
  lv_obj_set_style_bg_opa(s->root, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s->root, LV_OBJ_FLAG_SCROLLABLE);
  _stack.push_back(s);
  s->build();
  if (anim && _stack.size() > 1) lv_screen_load_anim(s->root, LV_SCR_LOAD_ANIM_MOVE_LEFT, T::ANIM_SCREEN, 0, false);
  else lv_screen_load(s->root);
  refreshStatus();
  s->onShow();
}

void App::pop(bool anim) {
  if (_stack.size() <= 1) return;
  Screen* s = _stack.back();
  _stack.pop_back();
  s->onHide();
  Screen* top = _stack.back();
  if (lv_screen_active() == top->root) {
    // s was pushed with an animation that hasn't started yet (push + pop in one loop pass):
    // LVGL would ignore our load (top is still active) and later show the freed screen.
    // Cancel the pending load and delete s right away.
    lv_display_t* d = lv_display_get_default();
    lv_anim_delete(s->root, NULL);
    lv_anim_delete(top->root, NULL);
    lv_obj_set_pos(top->root, 0, 0);
    if (d->scr_to_load == s->root) d->scr_to_load = NULL;
    lv_obj_delete(s->root);
  }
  else if (anim) lv_screen_load_anim(top->root, LV_SCR_LOAD_ANIM_MOVE_RIGHT, T::ANIM_SCREEN, 0, true);   // deletes s->root after
  else { lv_screen_load(top->root); lv_obj_delete(s->root); }
  _graveyard.push_back({ s, lv_tick_get() });
  refreshStatus();
  top->onShow();
}

void App::replaceAll(Screen* s) {
  std::vector<Screen*> old = _stack;
  _stack.clear();
  push(s, false);
  for (Screen* o : old) {
    o->onHide();
    lv_obj_delete(o->root);
    _graveyard.push_back({ o, lv_tick_get() });
  }
}

void App::openHome() {
  replaceAll(_m->ui().easy_mode ? makeEasyHome() : makeMainScreen(0));
}

void App::doRebuild() {
  _rebuild = false;
  applyTheme();
  buildChrome();
  openHome();
}

void App::openChat(const ConvKey& k) {
  // don't stack the same chat twice
  if (!_stack.empty() && _stack.back()->showsChat(k)) return;
  push(makeChatScreen(k));
}

bool App::isViewingChat(const ConvKey& k) {
  return _power != PWR_OFF && !_stack.empty() && _stack.back()->showsChat(k);
}

// ------------------------------------------------------------ events

void App::onMessage(const LogRecord& r, bool appended) {
  for (Screen* s : _stack) s->onMessage(r, appended);
  if (appended && r.dir == DIR_IN && (r.flags & LOG_FLAG_SOS)) {   // someone else's SOS: full-screen alarm
    setPower(PWR_ON);
    _last_activity = lv_tick_get();
    if (_stack.back()->isSosAlarmFor(r.conv())) pop(false);   // newer update replaces the alarm, no pile-up
    push(makeSosAlarmScreen(r));
    refreshStatus();
    return;
  }
  // desk mode keeps channel chatter quiet (counted as unread); direct messages and SOS still alert
  bool muted = r.kind == KIND_CHANNEL && (_m->channelMuted(r.key) || _desk);   // (SOS is handled above, never muted)
  if (appended && r.dir == DIR_IN && !r.isRead() && !(r.flags & LOG_FLAG_SILENT) && !muted) {
    if (_power == PWR_OFF) _m->notifyFlash();
    if (_power != PWR_ON) setPower(PWR_ON);
    _last_activity = lv_tick_get();
    showBanner(r);
    _m->playSound(r.kind == KIND_CHANNEL ? SOUND_GROUP : SOUND_MESSAGE);
  }
  if (appended) refreshStatus();
}

void App::onContactsChanged() {
  for (Screen* s : _stack) s->onContacts();
}

// ------------------------------------------------------------ input & power

void App::setPower(int p) {
  _power = p;
  UiSettings& u = _m->ui();
  if (p == PWR_ON) { _m->setBacklight(u.brightness); _last_key = lv_tick_get(); keyLight(true); }   // lit for a moment on wake
  else if (p == PWR_DIM) { _m->setBacklight(u.brightness > 3 ? 3 : 1); keyLight(false); }
  else { _m->setBacklight(0); keyLight(false); lockNow(); }   // lock while the screen is off
}

static uint32_t pinHashOf(const char* pin) {
  uint32_t h = 2166136261UL;
  for (const char* p = "EasyDeck-lock:"; *p; p++) { h ^= (uint8_t)*p; h *= 16777619UL; }
  for (const char* p = pin; *p; p++) { h ^= (uint8_t)*p; h *= 16777619UL; }
  return h ? h : 1;
}
bool App::pinSet() const { const uint8_t* p = _m->ui().pin_hash; return p[0] | p[1] | p[2] | p[3]; }
bool App::checkPin(const char* pin) const {
  uint32_t h = pinHashOf(pin);
  return memcmp(&h, _m->ui().pin_hash, 4) == 0;
}
void App::setPin(const char* pin) {
  uint32_t h = (pin && *pin) ? pinHashOf(pin) : 0;
  memcpy(_m->ui().pin_hash, &h, 4);
  _m->saveUi();
}
void App::lockNow() {
  if (!pinSet() || _locked || _stack.empty()) return;
  if (_stack.back()->keepAwake()) return;   // never hide an SOS alarm behind the lock
  _locked = true;
  push(makeLockScreen(), false);
}

void App::touched() {
  _last_activity = lv_tick_get();
  if (_power != PWR_ON) setPower(PWR_ON);
}

void App::input(const InputEvent& e) {
  _last_activity = lv_tick_get();
  if (_power == PWR_OFF) {   // first key only wakes - except the 3 s SOS hold (pocketed device)
    setPower(PWR_ON);
    if (e.type != IN_SOS_HOLD) return;
  }
  if (_power == PWR_DIM) setPower(PWR_ON);
  if (_stack.empty()) return;

  if (_banner && !_locked && (e.type == IN_SELECT || e.type == IN_ENTER) && lv_tick_get() < _banner_until) {
    ConvKey k = _banner_key;
    _banner_until = lv_tick_get();
    openChat(k);
    return;
  }
  if (e.type == IN_CHAR || e.type == IN_ENTER || e.type == IN_BACKSPACE) { _last_key = lv_tick_get(); if (!_kb_lit) keyLight(true); }
  if (e.type == IN_SCREENSHOT) { screenshot(); return; }
  if (e.type == IN_SOS_HOLD) {   // 3 s hold from anywhere: straight to SOS (countdown still applies)
    _m->playSound(SOUND_CLICK);
    push(makeSosScreen());
    return;
  }
  if (e.type == IN_BACK) {
    if (_stack.size() > 1 && !_stack.back()->isLock()) { pop(); return; }
  }
  _stack.back()->onInput(e);
}

// Announce ourselves automatically: once the clock is trustworthy after boot (adverts with a
// stale clock get ignored by peers), then on the chosen interval.
void App::autoAdvert() {
  UiSettings& u = _m->ui();
  uint16_t hours = kAutoAdvertHours[u.auto_advert % NUM_AUTO_ADVERT];
  if (!hours || !u.setup_done) return;
  uint32_t now = lv_tick_get();
  if (!_adverted_this_boot) {
    if (!_m->status().clock_valid || now < 20000) return;   // let GPS/phone set the clock first
    _adverted_this_boot = true;
    _m->advertise(true);
    _next_advert_ms = now + hours * 3600000UL;
    return;
  }
  if ((int32_t)(now - _next_advert_ms) >= 0) {
    _m->advertise(true);
    _next_advert_ms = now + hours * 3600000UL;
  }
}

// Breadcrumb track: a point when we've moved >= 15 m, at most every 10 s (like Ripple / Garmin).
void App::recordTrack() {
  if (!_m->ui().track_on) return;
  uint32_t now = lv_tick_get();
  if (now - _trk_ms < 10000) return;
  DeviceStatus st = _m->status();
  if (!st.gps_fix) return;
  double dlat = (st.lat - _trk_lat) * 111320.0;
  double dlon = (st.lon - _trk_lon) * 111320.0 * cos(st.lat * M_PI / 180.0);
  if (_trk_ms && dlat * dlat + dlon * dlon < 15.0 * 15.0) return;
  _trk_lat = st.lat; _trk_lon = st.lon; _trk_ms = now;
  TrackPoint p = { (float)st.lat, (float)st.lon, st.now };
  _m->trackAppend(p);
}

// Screenshot: render the active screen and the top layer (status bar, banners, toasts) with
// lv_snapshot, blend them and write a 24-bit BMP to the SD card. Buffers are large, so they
// come from the heap (PSRAM on the device).
void App::screenshot() {
  lv_draw_buf_t* base = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
  if (!base) { toast(IC_ERROR, "Screenshot failed (memory)"); return; }
  lv_draw_buf_t* top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
  const int w = base->header.w, h = base->header.h;
  const int row = (w * 3 + 3) & ~3;
  const size_t size = 54 + (size_t)row * h;
  uint8_t* bmp = (uint8_t*)lv_malloc(size);
  if (!bmp) { lv_draw_buf_destroy(base); if (top) lv_draw_buf_destroy(top); toast(IC_ERROR, "Screenshot failed (memory)"); return; }
  memset(bmp, 0, 54);
  auto put32 = [&](int o, uint32_t v) { bmp[o] = v; bmp[o + 1] = v >> 8; bmp[o + 2] = v >> 16; bmp[o + 3] = v >> 24; };
  bmp[0] = 'B'; bmp[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
  bmp[26] = 1; bmp[28] = 24; put32(34, row * h);
  for (int y = 0; y < h; y++) {
    const uint8_t* sp = base->data + (size_t)y * base->header.stride;
    const uint8_t* tp = (top && y < (int)top->header.h) ? top->data + (size_t)y * top->header.stride : nullptr;
    uint8_t* dp = bmp + 54 + (size_t)(h - 1 - y) * row;   // BMP rows are bottom-up, BGR
    for (int x = 0; x < w; x++) {
      int b = sp[x * 4], g = sp[x * 4 + 1], r = sp[x * 4 + 2];
      if (tp && x < (int)top->header.w) {
        int a = tp[x * 4 + 3];
        if (a) { b = (tp[x * 4] * a + b * (255 - a)) / 255; g = (tp[x * 4 + 1] * a + g * (255 - a)) / 255; r = (tp[x * 4 + 2] * a + r * (255 - a)) / 255; }
      }
      dp[x * 3] = b; dp[x * 3 + 1] = g; dp[x * 3 + 2] = r;
    }
  }
  lv_draw_buf_destroy(base);
  if (top) lv_draw_buf_destroy(top);
  // next free number: remembered in the settings, so normally this is a single existence check
  UiSettings& u = _m->ui();
  int no = u.shot_no[0] | (u.shot_no[1] << 8);
  if (no < 1 || no > 9999) no = 1;
  char path[48];
  snprintf(path, sizeof(path), "/screenshots/easydeck-%04d.bmp", no);
  for (int tries = 0; tries < 50 && _m->fileExists(path); tries++) {   // bounded (loop watchdog)
    if (++no > 9999) no = 1;
    snprintf(path, sizeof(path), "/screenshots/easydeck-%04d.bmp", no);
  }
  if (_m->fileExists(path)) { lv_free(bmp); toast(IC_ERROR, "Screenshot folder full - clear /screenshots"); return; }
  bool ok = _m->writeFile(path, bmp, size);
  lv_free(bmp);
  if (ok) {
    char b[48]; snprintf(b, sizeof(b), "Screenshot %04d saved", no); toast(IC_CHECK, b);
    if (++no > 9999) no = 1;
    u.shot_no[0] = no & 0xFF; u.shot_no[1] = no >> 8;
    _m->saveUi();
  }
  else toast(IC_ERROR, _m->hasSd() ? "Couldn't save the screenshot" : "Insert an SD card for screenshots");
}

void App::advertSent() { _advert_report_ms = lv_tick_get() + 20000; }

void App::loop() {
  uint32_t now = lv_tick_get();
  reap();
  autoAdvert();
  recordTrack();
  if (_rebuild) doRebuild();

  _sos.loop();
  _sos.shareLoop();
  if (_advert_report_ms && (int32_t)(now - _advert_report_ms) >= 0) {
    _advert_report_ms = 0;
    bool act; uint32_t age; int n;
    _m->advertEcho(act, age, n);
    char b[96];
    if (n > 0) snprintf(b, sizeof(b), "Your advert was repeated by %d repeater%s", n, n == 1 ? "" : "s");
    else snprintf(b, sizeof(b), "No repeater re-sent your advert. Check the radio preset, antenna, or move higher.");
    toast(n > 0 ? IC_CHECK : IC_ALERT, b, n > 0 ? 3500 : 6000);
  }
  deskLoop(now);
  if (_kb_lit && now - _last_key > 8000) keyLight(false);   // keyboard light: only while typing
  uint32_t to = kScreenTimeoutSec[_m->ui().screen_timeout % NUM_SCREEN_TIMEOUTS] * 1000UL;
  if (_desk && (to == 0 || to > 20000)) to = 20000;            // desk mode: screen off sooner
  if (to && !_stack.empty() && _stack.back()->keepAwake()) _last_activity = now;   // SOS alarm: stay on
  if (to) {
    uint32_t idle = now - _last_activity;
    if (_power == PWR_ON && idle > to) setPower(PWR_DIM);
    if (_power == PWR_DIM && idle > to + 8000 && !_sos.active()) setPower(PWR_OFF);   // SOS keeps the screen dimly on
  }

  if (_toast_until && (int32_t)(now - _toast_until) >= 0) {
    _toast_until = 0;
    lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  }
  if (_banner && (int32_t)(now - _banner_until) >= 0) {
    lv_obj_delete(_banner);
    _banner = nullptr;
  }
  if ((int32_t)(now - _next_tick) >= 0) {
    _next_tick = now + 500;
    if (!_stack.empty()) _stack.back()->tick();
  }
  if ((int32_t)(now - _next_status) >= 0) {
    _next_status = now + 5000;
    refreshStatus();
  }
}

// ------------------------------------------------------------ power helpers

void App::keyLight(bool on) {
  on = on && _m->ui().kb_light && _power == PWR_ON;
  if (on == _kb_lit) return;
  _kb_lit = on;
  _m->setKeyboardLight(on);
}

// Desk mode: on a charger (and, in the "not moving" setting, after 30 min without moving or
// typing) the T-Deck stays on the mesh but keeps quiet. Moving (GPS > 5 km/h or > 150 m from
// where it settled) or unplugging ends it straight away.
void App::deskLoop(uint32_t now) {
  uint8_t mode = _m->ui().desk_mode & DESK_MASK;
  if (!mode) { if (_desk) { _desk = false; refreshStatus(); } return; }
  if (_desk_check && now - _desk_check < 5000) return;
  _desk_check = now ? now : 1;
  DeviceStatus st = _m->status();
  if (st.charging) { if (!_ext_since) _ext_since = now ? now : 1; } else _ext_since = 0;
  bool moving = false;
  GpsDetail* g = new GpsDetail();   // satellite table: off the stack
  _m->gpsDetail(*g);
  if (g->valid) {
    if (g->speed_kmh > 5.0f) moving = true;
    if (_desk && (_desk_lat != 0 || _desk_lon != 0)) {
      double dy = (g->lat - _desk_lat) * 111320.0, dx = (g->lon - _desk_lon) * 111320.0 * cos(g->lat * M_PI / 180.0);
      if (dx * dx + dy * dy > 150.0 * 150.0) moving = true;
    }
  }
  if (moving || now - _last_key < 60000) _still_since = 0;
  else if (!_still_since) _still_since = now ? now : 1;
  bool want = (_ext_since && now - _ext_since > 60000 && !moving) ||
              (mode == 2 && _still_since && now - _still_since > 30UL * 60000UL);
  if (want && !_desk) {
    _desk = true;
    _desk_lat = g->valid ? g->lat : 0; _desk_lon = g->valid ? g->lon : 0;
    if (_power == PWR_ON) toast(IC_HOME, "Desk mode: quiet, still on the mesh", 3500);
    refreshStatus();
  } else if (!want && _desk) {
    _desk = false;
    if (moving) { setPower(PWR_ON); toast(IC_NAV, "On the move: desk mode off", 3500); }
    refreshStatus();
  }
  delete g;
}

void App::applyTheme() {
  T::apply(_m->ui());
  if (themeAccent(_m->ui().theme) == ACCENT_CUSTOM) {
    char* buf = (char*)malloc(1024);
    if (buf) { if (_m->readTextFile(THEME_FILE, buf, 1024)) T::applyCustom(buf); free(buf); }
  }
}

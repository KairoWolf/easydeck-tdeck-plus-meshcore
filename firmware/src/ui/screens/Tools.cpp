// Signal (radio diagnostics), packet log and GPS screens.
#include "Common.h"
Screen* makePacketLogScreen();
Screen* makeDiscoverScreen();
#include <stdio.h>

static lv_obj_t* statPanel(lv_obj_t* parent, int h) {
  lv_obj_t* p = ui::box(parent);
  lv_obj_set_size(p, SCR_W - 8, h);
  lv_obj_set_pos(p, 4, 4);
  lv_obj_set_style_radius(p, 6, 0);
  lv_obj_set_style_bg_color(p, T::surface, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(p, 6, 0);
  return p;
}

// two-column "label  value" text block
static lv_obj_t* kv(lv_obj_t* parent, int x, int y, const char* k) {
  lv_obj_t* kl = ui::label(parent, k, T::f12, T::muted);
  lv_obj_set_pos(kl, x, y);
  lv_obj_t* vl = ui::label(parent, "-", T::f14b, T::accent_text);
  lv_obj_set_pos(vl, x, y + 13);
  return vl;
}

class SignalScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Signal");
    lv_obj_t* c = content();
    const int ph = 112;
    lv_obj_t* p = statPanel(c, ph);
    const int cw = (SCR_W - 20) / 3;
    _rx = kv(p, 0, 0, "RX packets");     _tx = kv(p, cw, 0, "TX packets");    _err = kv(p, 2 * cw, 0, "RX errors");
    _rssi = kv(p, 0, 34, "Last RSSI");   _snr = kv(p, cw, 34, "Last SNR");    _nf = kv(p, 2 * cw, 34, "Noise floor");
    _radio = ui::label(p, "", T::f12, T::muted);
    ui::oneLine(_radio, SCR_W - 20);
    lv_obj_set_pos(_radio, 0, 72);
    _scope = ui::label(p, "", T::f12, T::muted);
    ui::oneLine(_scope, SCR_W - 20);
    lv_obj_set_pos(_scope, 0, 86);

    _list.create(c, 0, ph + 6, SCR_W, contentH() - ph - 6);
    _list.rowSetting(IC_RADIO, lv_color_hex(0xF5B83D), "Advert to whole mesh", "", false);
    _list.rowSetting(IC_RADIO, lv_color_hex(0x66BB6A), "Advert nearby only", "", false);
    _list.rowSetting(IC_HASH, lv_color_hex(0xBA68C8), "Send \"test\" to #test", "", false);
    _list.rowSetting(IC_RADAR, lv_color_hex(0x26C6DA), "Packet log", "", true);
    _list.rowSetting(IC_ANTENNA, lv_color_hex(0xFF8A65), "Discover nearby repeaters", "", true);
    _list.rowSetting(IC_SLIDERS, lv_color_hex(0x9AA4B0), "Radio settings", "", true);
    _list.select(0, false);
    _list.onActivate = [this](int i) { act(i); };
    tick();
  }

  void act(int i) {
    if (i == 0) { m->advertise(true); app->toast(m->status().clock_valid ? IC_RADIO : IC_CLOCK, m->status().clock_valid ? "Advert sent (flood)" : "Sent - but set the clock first or it may be ignored", 3500); }
    else if (i == 1) { m->advertise(false); app->toast(IC_RADIO, "Advert sent (zero hop)"); }
    else if (i == 2) {
      // find or join #test, then send a test message; local bots reply with the path
      for (int k = 0; k < m->channelCount(); k++) {
        ChannelView ch;
        if (m->channelAt(k, ch) && strcasecmp(ch.name, "#test") == 0) {
          m->sendText(ConvKey::channel(ch.key), "test");
          app->openChat(ConvKey::channel(ch.key));
          return;
        }
      }
      if (m->joinChannel("#test")) { app->toast(IC_HASH, "Joined #test - press again to send"); }
    } else if (i == 3) app->push(makePacketLogScreen());
    else if (i == 4) app->push(makeDiscoverScreen());
    else if (i == 5) app->push(makePageScreen(PAGE_SETTINGS));
  }

  void tick() override {
    RadioStats r = m->radioStats();
    RadioSettings rs = m->radio();
    char b[64];
    snprintf(b, sizeof(b), "%lu", (unsigned long)r.rx); lv_label_set_text(_rx, b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)r.tx); lv_label_set_text(_tx, b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)r.rx_errors); lv_label_set_text(_err, b);
    if (r.rx) { snprintf(b, sizeof(b), "%.0f dBm", r.last_rssi); lv_label_set_text(_rssi, b);
                snprintf(b, sizeof(b), "%.1f dB", r.last_snr); lv_label_set_text(_snr, b); }
    snprintf(b, sizeof(b), "%d dBm", r.noise_floor); lv_label_set_text(_nf, b);
    int pi = findPreset(rs);
    snprintf(b, sizeof(b), "%s  %.3f MHz  BW%.1f  SF%d  CR%d  %ddBm", pi >= 0 ? kRadioPresets[pi].name : "Custom",
             rs.freq, rs.bw, rs.sf, rs.cr, rs.tx_power);
    lv_label_set_text(_radio, b);
    snprintf(b, sizeof(b), "Scope: %s   Uptime: %lum", *m->regionScope() ? m->regionScope() : "none (unscoped)",
             (unsigned long)(r.uptime_s / 60));
    lv_label_set_text(_scope, b);
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }

private:
  FocusList _list;
  lv_obj_t *_rx, *_tx, *_err, *_rssi, *_snr, *_nf, *_radio, *_scope;
};

Screen* makeSignalScreen() { return new SignalScreen(); }

class GpsScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("GPS");
    lv_obj_t* c = content();
    const int ph = 84;
    lv_obj_t* p = statPanel(c, ph);
    const int cw = (SCR_W - 20) / 3;
    _state = kv(p, 0, 0, "Status");   _sats = kv(p, cw, 0, "Satellites");   _time = kv(p, 2 * cw, 0, "Clock");
    _pos = kv(p, 0, 34, "Position");
    lv_obj_set_width(_pos, SCR_W - 20);

    _list.create(c, 0, ph + 6, SCR_W, contentH() - ph - 6);
    DeviceStatus st = m->status();
    _sw_gps = _list.rowToggle(IC_SATELLITE, lv_color_hex(0x26C6DA), "GPS receiver", st.gps_enabled);
    _sw_adv = _list.rowToggle(IC_PIN, lv_color_hex(0x66BB6A), "Include position in adverts", m->advertLocation());
    _sw_trk = _list.rowToggle(IC_NAV, lv_color_hex(0xF5B83D), "Record my track", m->ui().track_on);
    _list.rowSetting(IC_USERS, lv_color_hex(0x6E8BFF), "Share with favourites", kShareNames[m->ui().share_loc % NUM_SHARE], true);
    _list.rowSetting(IC_TRASH, T::danger, "Clear track", "", false);
    _list.rowSetting(IC_INFO, lv_color_hex(0x9AA4B0), "About location sharing", "", true);
    _list.select(0, false);
    _list.onActivate = [this](int i) {
      if (i == 0) {
        bool on = !m->status().gps_enabled;
        m->setGps(on);
        if (on) lv_obj_add_state(_sw_gps, LV_STATE_CHECKED); else lv_obj_remove_state(_sw_gps, LV_STATE_CHECKED);
      } else if (i == 1) {
        bool on = !m->advertLocation();
        m->setAdvertLocation(on);
        if (on) lv_obj_add_state(_sw_adv, LV_STATE_CHECKED); else lv_obj_remove_state(_sw_adv, LV_STATE_CHECKED);
      } else if (i == 2) {
        m->ui().track_on = !m->ui().track_on;
        m->saveUi();
        if (m->ui().track_on) lv_obj_add_state(_sw_trk, LV_STATE_CHECKED); else lv_obj_remove_state(_sw_trk, LV_STATE_CHECKED);
        if (m->ui().track_on && !m->status().gps_enabled) m->setGps(true);
      } else if (i == 3) {
        std::vector<ChoiceItem> items;
        for (int k = 0; k < NUM_SHARE; k++) items.push_back({ kShareNames[k], k == 2 ? "Recommended" : "" });
        app->push(makeChoiceScreen("Share location", items, m->ui().share_loc % NUM_SHARE, [this](int k) {
          m->ui().share_loc = k; m->saveUi();
          if (k && !m->status().gps_enabled) m->setGps(true);
          app->pop(); app->push(makeGpsScreen());   // refresh values
        }, "Your favourites get your position (only when you've moved, plus once an hour). They see you on their map."));
      } else if (i == 4) {
        app->push(makeConfirmScreen("Clear track", "Delete your recorded track?", "Delete", true,
          [this]() { m->trackClear(); app->toast(IC_TRASH, "Track cleared"); }));
      } else {
        app->push(makeInfoScreen("Location sharing",
          "Adverts: when on, everyone who hears your advert sees where you are.\n\n"
          "Per contact: open a contact and turn on \"Can see my location\" to let only that person ask for it.\n\n"
          "The GPS needs a clear view of the sky for its first fix (can take a few minutes)."));
      }
    };
    tick();
  }

  void tick() override {
    DeviceStatus st = m->status();
    char b[64];
    lv_label_set_text(_state, !st.gps_enabled ? "Off" : st.gps_fix ? "Fix" : "Searching");
    lv_obj_set_style_text_color(_state, !st.gps_enabled ? T::muted : st.gps_fix ? T::success : T::warning, 0);
    snprintf(b, sizeof(b), "%d", st.gps_sats); lv_label_set_text(_sats, b);
    if (st.clock_valid) ui::clockTime(st.now, m->tzOffset(), b, sizeof(b)); else snprintf(b, sizeof(b), "not set");
    lv_label_set_text(_time, b);
    if (st.gps_fix) snprintf(b, sizeof(b), "%.5f, %.5f", st.lat, st.lon); else snprintf(b, sizeof(b), "-");
    lv_label_set_text(_pos, b);
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }

private:
  FocusList _list;
  lv_obj_t *_state, *_sats, *_time, *_pos, *_sw_gps, *_sw_adv, *_sw_trk;
};

Screen* makeGpsScreen() { return new GpsScreen(); }

// ---------------------------------------------------------------- packet log

class PacketLogScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Packet log");
    _sub = ui::label(_header, "", T::f12, T::muted);
    lv_obj_align(_sub, LV_ALIGN_RIGHT_MID, -8, 0);
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.animate = false;
    fill();
  }
  static const char* typeName(uint8_t t) {
    switch (t) {
      case 0: return "REQ";   case 1: return "RESP";  case 2: return "MSG";   case 3: return "ACK";
      case 4: return "ADVERT"; case 5: return "GROUP"; case 6: return "GDATA"; case 7: return "ANON";
      case 8: return "PATH";  case 9: return "TRACE"; case 10: return "MULTI"; case 11: return "CTRL";
      default: return "RAW";
    }
  }
  void fill() {
    PacketView p[40];
    int n = m->packetLog(p, 40);
    if (n == _last_n && n && p[0].ago_s == _last_age) return;   // nothing new
    _last_n = n;
    _last_age = n ? p[0].ago_s : 0;
    int sel = _list.selected();
    _list.clear();
    for (int i = 0; i < n; i++) {
      lv_obj_t* r = _list.addRow(24);
      char a[16], b[64];
      if (p[i].ago_s < 60) snprintf(a, sizeof(a), "%lus", (unsigned long)p[i].ago_s);
      else snprintf(a, sizeof(a), "%lum", (unsigned long)(p[i].ago_s / 60));
      lv_obj_t* al = ui::label(r, a, T::f12, T::faint);
      lv_obj_set_width(al, 30);
      lv_obj_align(al, LV_ALIGN_LEFT_MID, 0, 0);
      lv_color_t tc = p[i].type == 4 ? T::success : p[i].type == 2 ? T::accent_text : p[i].type == 5 ? lv_color_hex(0xBA68C8) : T::text;
      lv_obj_t* tl = ui::label(r, typeName(p[i].type), T::f12, tc);
      lv_obj_align(tl, LV_ALIGN_LEFT_MID, 34, 0);
      snprintf(b, sizeof(b), "%s%d%s  %dB  %.1fdB  %.0f", p[i].flood ? "flood " : "direct ", p[i].hops,
               p[i].scoped ? "*" : "", p[i].len, p[i].snr, p[i].rssi);
      lv_obj_t* bl = ui::label(r, b, T::f12, T::muted);
      lv_obj_align(bl, LV_ALIGN_LEFT_MID, 90, 0);
    }
    if (n) {
      lv_obj_t* hint = ui::label(_list.obj(), "hops  size  SNR  RSSI dBm   * = region-scoped", T::f12, T::faint);
      lv_obj_set_style_pad_left(hint, 6, 0);
    }
    if (n == 0) _list.setEmpty(IC_RADAR, "Nothing received yet", "Every packet your radio hears shows up here. If this stays empty while others transmit nearby, the radio or preset is the problem.");
    _list.select(sel, false);
    RadioStats rs = m->radioStats();
    char s[32];
    snprintf(s, sizeof(s), "RX %lu  TX %lu", (unsigned long)rs.rx, (unsigned long)rs.tx);
    lv_label_set_text(_sub, s);
  }
  void tick() override { fill(); }
  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
private:
  FocusList _list;
  lv_obj_t* _sub = nullptr;
  int _last_n = -1;
  uint32_t _last_age = 0;
};

Screen* makePacketLogScreen() { return new PacketLogScreen(); }

// ---------------------------------------------------------------- discover

class DiscoverScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Discover");
    _sub = ui::label(_header, "", T::f12, T::muted);
    lv_obj_align(_sub, LV_ALIGN_RIGHT_MID, -8, 0);
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.animate = false;
    _list.onActivate = [this](int i) { if (i == 0) scan(); };
    scan();
  }
  void scan() {
    if (m->discover()) { _start = lv_tick_get(); app->toast(IC_RADAR, "Scanning for nearby repeaters..."); }
    else app->toast(IC_ERROR, "Couldn't send (radio busy)");
    _last = -1;
    fill();
  }
  void fill() {
    DiscoveredView d[16];
    int n = m->discovered(d, 16);
    if (n == _last && lv_tick_get() - _start > 12000) return;
    _last = n;
    int sel = _list.selected();
    _list.clear();
    _list.rowSetting(IC_REFRESH, lv_color_hex(0x26C6DA), "Scan again", "", false);
    for (int i = 0; i < n; i++) {
      lv_obj_t* r = _list.addRow(T::row_h);
      lv_obj_t* ic = ui::label(r, d[i].type == 4 ? IC_SIGNAL : IC_ANTENNA, &icons_16, T::style() == STYLE_TACTICAL ? T::accent : lv_color_hex(0xFF8A65));
      lv_obj_align(ic, LV_ALIGN_LEFT_MID, 2, 0);
      char nm[40], q[40];
      if (d[i].name[0]) snprintf(nm, sizeof(nm), "%s", d[i].name);
      else snprintf(nm, sizeof(nm), "Node %02X%02X%02X", d[i].key[0], d[i].key[1], d[i].key[2]);
      snprintf(q, sizeof(q), "in %.1f  out %.1f dB", d[i].snr_us, d[i].snr_them);
      lv_obj_t* ql = ui::label(r, q, T::f12, d[i].snr_us > 0 ? T::success : d[i].snr_us > -7 ? T::warning : T::danger);
      lv_obj_align(ql, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_update_layout(ql);
      lv_obj_t* nl = ui::label(r, nm, T::body, T::text);
      ui::oneLine(nl, SCR_W - 2 * T::pad - 12 - 28 - lv_obj_get_width(ql) - 8);
      lv_obj_align(nl, LV_ALIGN_LEFT_MID, 28, 0);
    }
    bool scanning = lv_tick_get() - _start < 12000;
    if (n == 0) {
      lv_obj_t* h = ui::label(_list.obj(), scanning ? "Listening for replies..." :
        "No repeater answered. You may be out of range of the mesh, on a different radio preset, or the repeaters have discovery turned off.",
        T::small, T::muted);
      lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(h, lv_pct(100));
      lv_obj_set_style_pad_all(h, 8, 0);
    }
    _list.select(sel, false);
    char s[32];
    snprintf(s, sizeof(s), scanning ? "scanning... %d found" : "%d found", n);
    lv_label_set_text(_sub, s);
  }
  void tick() override { fill(); }
  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
private:
  FocusList _list;
  lv_obj_t* _sub = nullptr;
  uint32_t _start = 0;
  int _last = -1;
};

Screen* makeDiscoverScreen() { return new DiscoverScreen(); }

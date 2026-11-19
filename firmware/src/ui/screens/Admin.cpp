#include <vector>
// Repeater / room administration: log in, live status, and a CLI terminal.
#include "Common.h"
#include <stdio.h>
#include <string.h>

static const char* stateText(AdminState s) {
  switch (s) {
    case ADM_PENDING: return "Logging in...";
    case ADM_GUEST: return "Logged in (guest)";
    case ADM_ADMIN: return "Logged in (admin)";
    case ADM_FAILED: return "Login failed / no reply";
    default: return "Not logged in";
  }
}

// ---------------------------------------------------------------- terminal

class TerminalScreen : public HeaderScreen {
public:
  explicit TerminalScreen(const uint8_t* key) { memcpy(_key, key, LOG_KEY_LEN); }
  void build() override {
    ContactView c;
    char t[48];
    snprintf(t, sizeof(t), "%s", m->findContact(_key, c) ? c.name : "Terminal");
    buildHeader(t, "CLI");
    lv_obj_t* cont = content();
    const int in_h = 30;
    _log = ui::box(cont);
    lv_obj_set_size(_log, SCR_W, contentH() - in_h);
    lv_obj_add_flag(_log, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_log, LV_DIR_VER);
    lv_obj_set_style_pad_all(_log, 6, 0);
    lv_obj_set_flex_flow(_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_log, 2, 0);

    lv_obj_t* bar = ui::box(cont);
    lv_obj_set_size(bar, SCR_W, in_h);
    lv_obj_set_pos(bar, 0, contentH() - in_h);
    lv_obj_set_style_bg_color(bar, T::surface, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_t* pr = ui::label(bar, ">", T::f14b, T::accent_text);
    lv_obj_align(pr, LV_ALIGN_LEFT_MID, 6, 0);
    _ta = lv_textarea_create(bar);
    lv_textarea_set_one_line(_ta, true);
    lv_textarea_set_max_length(_ta, 120);
    lv_textarea_set_placeholder_text(_ta, "ver, clock, neighbors, get radio ...");
    lv_obj_set_size(_ta, SCR_W - 24, in_h);
    lv_obj_set_pos(_ta, 18, 0);
    lv_obj_set_style_bg_opa(_ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_ta, 0, 0);
    lv_obj_set_style_text_font(_ta, T::f14, 0);
    lv_obj_set_style_text_color(_ta, T::text, 0);
    lv_obj_set_style_text_color(_ta, T::faint, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(_ta, T::accent, LV_PART_CURSOR);
    lv_obj_add_state(_ta, LV_STATE_FOCUSED);
    _shown = -1;
    tick();
  }
  std::vector<CliLine> _lines = std::vector<CliLine>(40);   // heap: too big for the loop stack
  void tick() override {
    CliLine* lines = _lines.data();
    int n = m->cliLines(_key, lines, 40);
    if (n == _shown) return;
    _shown = n;
    lv_obj_clean(_log);
    if (n == 0) ui::label(_log, "Type a command and press Enter. You need to be logged in as admin for most commands.", T::f12, T::faint);
    for (int i = 0; i < n; i++) {
      char b[LOG_TEXT_MAX + 4];
      snprintf(b, sizeof(b), "%s%s", lines[i].out ? "> " : "", lines[i].text);
      lv_obj_t* l = ui::label(_log, b, T::f14, lines[i].out ? T::accent_text : T::text);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(l, SCR_W - 12);
    }
    lv_obj_update_layout(_log);
    lv_obj_scroll_to_y(_log, LV_COORD_MAX, LV_ANIM_OFF);
  }
  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_CHAR: lv_textarea_add_char(_ta, e.ch); break;
      case IN_BACKSPACE:
        if (!*lv_textarea_get_text(_ta)) { app->pop(); return; }
        lv_textarea_delete_char(_ta);
        break;
      case IN_ENTER: case IN_SELECT: {
        const char* t = lv_textarea_get_text(_ta);
        if (!*t) return;
        m->sendCli(_key, t);
        lv_textarea_set_text(_ta, "");
        tick();
        break;
      }
      case IN_UP: lv_obj_scroll_by_bounded(_log, 0, 40, LV_ANIM_ON); break;
      case IN_DOWN: lv_obj_scroll_by_bounded(_log, 0, -40, LV_ANIM_ON); break;
      default: break;
    }
  }
private:
  uint8_t _key[LOG_KEY_LEN];
  lv_obj_t* _log = nullptr;
  lv_obj_t* _ta = nullptr;
  int _shown = -1;
};

// ---------------------------------------------------------------- admin

class AdminScreen : public HeaderScreen {
public:
  explicit AdminScreen(const uint8_t* key) { memcpy(_key, key, LOG_KEY_LEN); }
  void build() override {
    ContactView c;
    bool have = m->findContact(_key, c);
    buildHeader(have ? c.name : "Repeater", c.type == 3 ? "Room" : "Repeater");
    lv_obj_t* cont = content();
    _panel = ui::box(cont);
    lv_obj_set_size(_panel, SCR_W - 8, 84);
    lv_obj_set_pos(_panel, 4, 4);
    lv_obj_set_style_bg_color(_panel, T::surface, 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_panel, T::radius, 0);
    lv_obj_set_style_pad_all(_panel, 6, 0);
    _state = ui::label(_panel, "", T::f14b, T::accent_text);
    lv_obj_set_pos(_state, 0, 0);
    _stats = ui::label(_panel, "", T::f12, T::text);
    lv_label_set_long_mode(_stats, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_stats, SCR_W - 20);
    lv_obj_set_pos(_stats, 0, 20);

    _list.create(cont, 0, 92, SCR_W, contentH() - 92);
    bool room = have && c.type == 3;
    _list.rowSetting(IC_SHIELD, lv_color_hex(0x6E8BFF), room ? "Join with password" : "Log in as admin", "password", true);
    _list.rowSetting(IC_USER, lv_color_hex(0x9AA4B0), room ? "Join (no password)" : "Log in as guest", room ? "public room" : "no password", false);
    _list.rowSetting(IC_REFRESH, lv_color_hex(0x26C6DA), "Get status", "", false);
    _list.rowSetting(IC_KEYBOARD, lv_color_hex(0xF5B83D), "Terminal (CLI)", "", true);
    _list.select(0, false);
    _list.onActivate = [this](int i) {
      if (i == 0) {
        app->push(makeTextEntryScreen("Admin login", "Repeater admin password", "", 15,
          [this](const char* pw) { if (m->adminLogin(_key, pw)) app->toast(IC_SHIELD, "Logging in..."); }));
      } else if (i == 1) {
        if (m->adminLogin(_key, "")) app->toast(IC_USER, "Logging in as guest...");
      } else if (i == 2) {
        if (m->requestStatus(_key)) app->toast(IC_REFRESH, "Status requested...");
        else app->toast(IC_ERROR, "Couldn't send");
      } else if (i == 3) {
        app->push(new TerminalScreen(_key));
      }
    };
    tick();
  }
  void tick() override {
    lv_label_set_text(_state, stateText(m->adminState(_key)));
    RepeaterStatus s;
    if (!m->getStatus(_key, s)) {
      lv_label_set_text(_stats, "No status yet. Log in (guest is usually enough), then Get status.");
      return;
    }
    char b[256];
    uint32_t up = s.uptime_secs;
    snprintf(b, sizeof(b),
      "Battery %.2fV   Up %lud %luh   Queue %u\n"
      "RX %lu (flood %lu)   TX %lu (flood %lu)\n"
      "Noise %ddBm   Last %ddBm / %.1fdB   Air %lus",
      s.batt_mv / 1000.0f, (unsigned long)(up / 86400), (unsigned long)((up / 3600) % 24), s.tx_queue,
      (unsigned long)s.pkts_recv, (unsigned long)s.recv_flood, (unsigned long)s.pkts_sent, (unsigned long)s.sent_flood,
      s.noise_floor, s.last_rssi, s.last_snr, (unsigned long)s.air_secs);
    lv_label_set_text(_stats, b);
  }
  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
private:
  uint8_t _key[LOG_KEY_LEN];
  FocusList _list;
  lv_obj_t *_panel, *_state, *_stats;
};

Screen* makeAdminScreen(const uint8_t* key) { return new AdminScreen(key); }

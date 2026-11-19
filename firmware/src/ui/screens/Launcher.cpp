// Home: MeshOS-style app grid (standard mode). Easy mode uses EasyHome instead.
#include "Common.h"
#include <stdio.h>

struct AppTile { const char* icon; const char* name; uint32_t color; char key; };
static const AppTile kTiles[] = {
  { IC_CHAT,      "Chats",     0x3CD6E8, 'c' },
  { IC_USERS,     "Contacts",  0x6E8BFF, 'o' },
  { IC_HASH,      "Groups",    0xBA68C8, 'g' },
  { IC_MAP,       "Map",       0x66BB6A, 'm' },
  { IC_RADAR,     "Heard",     0x26C6DA, 'h' },
  { IC_ANTENNA,   "Signal",    0xFF8A65, 'r' },
  { IC_RADIO,     "Advertise", 0xF5B83D, 'a' },
  { IC_SATELLITE, "GPS",       0x4DB6AC, 'p' },
  { IC_SIREN,     "SOS",       0xFF5A5F, 'x' },
  { IC_OK_CIRCLE, "I'm OK",    0x3DDC84, 'k' },
  { IC_SETTINGS,  "Settings",  0x9AA4B0, 's' },
  { IC_INFO,      "Help",      0x8D9EFF, '?' },
};
#define N_TILES (int)(sizeof(kTiles) / sizeof(kTiles[0]))
#define COLS 4

class Launcher : public Screen {
public:
  void build() override {
    const int rows = (N_TILES + COLS - 1) / COLS;
    const int top = STATUS_H, bottom = SCR_H - INFOBAR_H;
    const int cw = SCR_W / COLS, ch = (bottom - top) / rows;
    for (int i = 0; i < N_TILES; i++) {
      lv_obj_t* t = ui::box(root);
      lv_obj_set_size(t, cw - 4, ch - 4);
      lv_obj_set_pos(t, (i % COLS) * cw + 2, top + (i / COLS) * ch + 2);
      lv_obj_set_style_radius(t, T::style() == STYLE_TACTICAL ? 0 : 10, 0);
      lv_obj_set_style_bg_color(t, T::select_bg, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_STATE_FOCUSED);
      if (T::style() == STYLE_TACTICAL) {
        lv_obj_set_style_border_width(t, 1, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(t, T::select_border, LV_STATE_FOCUSED);
      }
      lv_obj_set_style_bg_color(t, T::surface2, LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(t, (void*)(intptr_t)i);
      lv_obj_add_event_cb(t, [](lv_event_t* e) {
        Launcher* s = (Launcher*)lv_event_get_user_data(e);
        int i = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
        s->select(i);
        s->open(i);
      }, LV_EVENT_CLICKED, this);
      bool sos = kTiles[i].key == 'x';
      lv_color_t icol = T::style() == STYLE_TACTICAL ? (sos ? T::danger : T::accent) : lv_color_hex(kTiles[i].color);
      lv_obj_t* ic = ui::label(t, kTiles[i].icon, &icons_24, icol);
      lv_obj_align(ic, LV_ALIGN_CENTER, 0, -8);
      lv_obj_t* nm = ui::label(t, kTiles[i].name, T::f12, T::text);
      lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -3);
      _tiles[i] = t;
    }
    // info bar: who / preset / scope
    _info = ui::label(root, "", T::f12, T::muted);
    ui::oneLine(_info, SCR_W - 12);
    lv_obj_set_style_text_align(_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_info, LV_ALIGN_BOTTOM_MID, 0, -2);
    select(_sel);
    refresh();
  }

  void refresh() {
    if (_badge) { lv_obj_delete(_badge); _badge = nullptr; }
    int n = m->totalUnread();
    if (n > 0) {
      _badge = ui::badge(_tiles[0], n);
      lv_obj_set_height(_badge, 16);
      lv_obj_set_style_min_width(_badge, 16, 0);
      lv_obj_set_style_pad_hor(_badge, 4, 0);
      lv_obj_align(_badge, LV_ALIGN_TOP_RIGHT, -8, 4);
    }
    DeviceStatus st = m->status();
    if (!st.clock_valid) {   // the #1 reason adverts get ignored
      lv_label_set_text(_info, st.gps_enabled ? "Clock not set - waiting for GPS (go outside) or set it in Settings"
                                              : "Clock not set - turn on GPS or set it in Settings");
      lv_obj_set_style_text_color(_info, T::warning, 0);
      return;
    }
    lv_obj_set_style_text_color(_info, T::muted, 0);
    RadioSettings r = m->radio();
    int pi = findPreset(r);
    char b[96];
    snprintf(b, sizeof(b), "%s  \xC2\xB7  %s%s%s", m->nodeName(), pi >= 0 ? kRadioPresets[pi].name : "Custom radio",
             *m->regionScope() ? "  \xC2\xB7  " : "", m->regionScope());
    lv_label_set_text(_info, b);
  }

  void select(int i) {
    if (i < 0 || i >= N_TILES) return;
    lv_obj_remove_state(_tiles[_sel], LV_STATE_FOCUSED);
    _sel = i;
    lv_obj_add_state(_tiles[_sel], LV_STATE_FOCUSED);
  }

  void open(int i) {
    switch (kTiles[i].key) {
      case 'c': app->push(makePageScreen(PAGE_CHATS)); break;
      case 'o': app->push(makePageScreen(PAGE_CONTACTS)); break;
      case 'g': app->push(makeGroupsScreen()); break;
      case 'm': app->push(makeMapScreen()); break;
      case 'h': app->push(makePageScreen(PAGE_HEARD)); break;
      case 'r': app->push(makeSignalScreen()); break;
      case 'p': app->push(makeGpsScreen()); break;
      case 's': app->push(makePageScreen(PAGE_SETTINGS)); break;
      case 'x': app->push(makeSosScreen()); break;
      case 'a':
        app->push(makeChoiceScreen("Advertise", {
            { "Whole mesh", "Flood via all repeaters" },
            { "Nearby only", "Direct neighbours, no repeats" } }, 0,
          [this](int c) {
            m->advertise(c == 0);
            if (c == 0) app->advertSent();
            if (!m->status().clock_valid) app->toast(IC_CLOCK, "Advert sent, but the clock isn't set - others may ignore it. Turn on GPS or set the time.", 4500);
            else app->toast(IC_RADIO, c == 0 ? "Advert sent to the mesh" : "Advert sent nearby");
          }));
        break;
      case 'k':
        app->push(makeConfirmScreen("I'm OK", "Send an \"I'm OK\" check-in with your location to your favourites?",
          "Send check-in", false, [this]() { app->sos().checkIn(); app->toast(IC_OK_CIRCLE, "Check-in sent"); }));
        break;
      case '?':
        app->push(makeInfoScreen("Help",
          "Trackball: move. Press: open. Hold: back.\n"
          "Home keys: C chats, O contacts, G groups, M map, H heard, R signal, A advertise, P GPS, W saved places, X SOS, K I'm OK, S settings.\n\n"
          "Chats: type and press Enter to send. Backspace on an empty box goes back. In the chat list, / searches all messages and $ jumps to the next unread.\n\n"
          "Screenshot: hold the trackball and press any key (saved to /screenshots on the SD card).\n\n"
          "Map: roll to pan, + / - to zoom, C to centre on you. Uses map tiles in /tiles on the SD card.\n\n"
          "Nobody showing up? Open Signal: RX packets should climb when others transmit. Make sure the clock is set "
          "(GPS or phone) and that you use the same radio preset as your local repeaters.\n\n"
          "SOS: hold the trackball for 3 seconds from any screen (or press X on home). After a 10 s countdown it sends your position to your favourite contacts every few minutes until you cancel. It does not contact 000."));
        break;
    }
  }

  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_UP: if (_sel >= COLS) select(_sel - COLS); break;
      case IN_DOWN: if (_sel + COLS < N_TILES) select(_sel + COLS); break;
      case IN_LEFT: if (_sel > 0) select(_sel - 1); break;
      case IN_RIGHT: if (_sel < N_TILES - 1) select(_sel + 1); break;
      case IN_SELECT: case IN_ENTER: open(_sel); break;
      case IN_CHAR:
        if (e.ch == 'w' || e.ch == 'W') { app->push(makeWaypointsScreen()); break; }   // keys without a tile
        for (int i = 0; i < N_TILES; i++) if (e.ch == kTiles[i].key || e.ch == kTiles[i].key - 32) { select(i); open(i); }
        break;
      default: break;
    }
  }
  void onMessage(const LogRecord& r, bool appended) override { refresh(); }
  void onShow() override { refresh(); }
  void tick() override { if (++_ticks % 10 == 0) refresh(); }   // every ~5 s (clock / GPS state)

private:
  lv_obj_t* _tiles[N_TILES];
  lv_obj_t* _badge = nullptr;
  lv_obj_t* _info = nullptr;
  int _sel = 0;
  int _ticks = 0;
};

Screen* makeMainScreen(int tab) { (void)tab; return new Launcher(); }

// Contact details, Groups, Device check.
#include "Common.h"
#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------ Contact

class ContactScreen : public HeaderScreen {
public:
  explicit ContactScreen(const uint8_t* key) { memcpy(_key, key, LOG_KEY_LEN); }

  void build() override {
    ContactView c;
    if (!m->findContact(_key, c)) { memset(&c, 0, sizeof(c)); strcpy(c.name, "Unknown"); }
    buildHeader("Contact");
    lv_obj_t* cont = content();

    // hero: big avatar + name + details
    lv_obj_t* hero = ui::box(cont);
    lv_obj_set_size(hero, SCR_W, 76);
    lv_obj_t* av = ui::avatar(hero, c.name, c.key, 56, false);
    lv_obj_align(av, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t* nm = ui::label(hero, c.name, T::title, T::text);
    ui::oneLine(nm, SCR_W - 96);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 84, 12);
    DeviceStatus st = m->status();
    char sub[80], when[24] = "";
    if (c.last_heard && st.clock_valid) ui::relTime(c.last_heard, st.now, m->tzOffset(), when, sizeof(when));
    char hops[20] = "";
    if (c.hops != 0xFF) snprintf(hops, sizeof(hops), " \xC2\xB7 %d hop%s", c.hops, c.hops == 1 ? "" : "s");
    if (c.has_location && st.gps_fix) {
      char d[32];
      ui::distanceText(st.lat, st.lon, c.lat, c.lon, d, sizeof(d));
      snprintf(sub, sizeof(sub), "%s%s%s", d, when[0] ? " \xC2\xB7 " : "", when);
    } else {
      snprintf(sub, sizeof(sub), "%s%s%s", when[0] ? "Heard " : "Not heard yet", when, hops);
    }
    lv_obj_t* s = ui::label(hero, sub, T::small, T::muted);
    ui::oneLine(s, SCR_W - 96);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 84, -14);

    _list.create(cont, 0, 76, SCR_W, contentH() - 76);
    _is_node = c.type != 1;
    _acts.clear();
    auto row = [this](int id, const char* ic, lv_color_t col, const char* t, const char* v, bool chev) {
      _list.rowSetting(ic, col, t, v, chev); _acts.push_back(id);
    };
    if (_is_node) row(A_ADMIN, IC_SHIELD, lv_color_hex(0x6E8BFF), c.type == 3 ? "Room admin & status" : "Admin & status", "", true);
    row(A_CHAT, IC_CHAT, T::accent, "Send message", "", true);
    if (c.has_location) {
      row(A_NAV, IC_NAV, lv_color_hex(0x66BB6A), "Find them", "arrow + distance", true);
      row(A_MAP, IC_MAP, lv_color_hex(0x26C6DA), "Show on map", "", true);
    }
    _fav = _list.rowToggle(IC_STAR, T::warning, "Favourite", c.favorite); _acts.push_back(A_FAV);
    _share = _list.rowToggle(IC_PIN, lv_color_hex(0x26C6DA), "Can see my location", c.share_location); _acts.push_back(A_SHARE);
    char note[CONTACT_NOTE_MAX + 1];
    m->contactNote(_key, note);
    if (!note[0]) row(A_NOTE, IC_EDIT, lv_color_hex(0x9AA4B0), "Add a note", "only on this device", true);
    else {   // show the whole note (wrapped over two lines)
      lv_obj_t* r = _list.addRow(T::easy() ? T::row_h + 16 : 44);
      lv_obj_t* ic = ui::label(r, IC_EDIT, T::icon, lv_color_hex(0x9AA4B0));
      lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* nt = ui::label(r, note, T::small, T::text);
      lv_label_set_long_mode(nt, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(nt, SCR_W - 2 * T::pad - 40);
      lv_obj_set_style_max_height(nt, 2 * lv_font_get_line_height(T::small), 0);
      lv_obj_align(nt, LV_ALIGN_LEFT_MID, 30, 0);
      _acts.push_back(A_NOTE);
    }
    row(A_ASK, IC_LOCATE, lv_color_hex(0x6E8BFF), "Ask for their location", "", false);
    {
      PathHop hops[8];
      int nh = m->contactPath(_key, hops, 8);
      char rv[24];
      if (nh < 0) snprintf(rv, sizeof(rv), "not learned yet");
      else if (nh == 0) snprintf(rv, sizeof(rv), "direct");
      else snprintf(rv, sizeof(rv), "%d repeater%s", nh, nh == 1 ? "" : "s");
      row(A_ROUTE, IC_NAV, lv_color_hex(0x26C6DA), "Route", rv, true);
    }
    row(A_RESET, IC_REFRESH, lv_color_hex(0x9AA4B0), "Reset route", c.hops == 0xFF ? "flood" : "learned", false);
    row(A_SHARECONTACT, IC_RADIO, lv_color_hex(0xF5B83D), "Share contact nearby", "", false);
    row(A_REMOVE, IC_TRASH, T::danger, "Remove contact", "", false);
    _list.select(0, false);
    _list.onActivate = [this](int i) { act(i); };
  }

  enum { A_ADMIN, A_CHAT, A_NAV, A_MAP, A_FAV, A_SHARE, A_NOTE, A_ASK, A_ROUTE, A_RESET, A_SHARECONTACT, A_REMOVE };
  std::vector<int> _acts;

  void act(int i) {
    if (i < 0 || i >= (int)_acts.size()) return;
    ContactView c;
    bool have = m->findContact(_key, c);
    switch (_acts[i]) {
      case A_ADMIN: app->push(makeAdminScreen(_key)); break;
      case A_CHAT: app->openChat(ConvKey::direct(_key)); break;
      case A_NAV: if (have && c.has_location) app->push(makeNavigateScreen(c.name, c.lat, c.lon)); break;
      case A_MAP: if (have && c.has_location) app->push(makeMapScreenAt(c.lat, c.lon, c.name)); break;
      case A_FAV:
        if (!have) return;
        m->setFavorite(_key, !c.favorite);
        if (!c.favorite) lv_obj_add_state(_fav, LV_STATE_CHECKED); else lv_obj_remove_state(_fav, LV_STATE_CHECKED);
        break;
      case A_SHARE:
        if (!have) return;
        m->setShareLocation(_key, !c.share_location);
        if (!c.share_location) lv_obj_add_state(_share, LV_STATE_CHECKED); else lv_obj_remove_state(_share, LV_STATE_CHECKED);
        app->toast(IC_PIN, !c.share_location ? "They can now see your location" : "Location sharing off");
        break;
      case A_NOTE: {
        char cur[CONTACT_NOTE_MAX + 1];
        m->contactNote(_key, cur);
        uint8_t k[LOG_KEY_LEN];
        memcpy(k, _key, LOG_KEY_LEN);
        auto editNote = [this, k, cur]() {
          app->push(makeTextEntryScreen("Note", "Private note about this contact, e.g. \"Dad's car radio\". Only stored here.",
                                        cur, CONTACT_NOTE_MAX, [this, k](const char* t) {
            if (!m->setContactNote(k, t)) app->toast(IC_ERROR, "Too many notes");
            app->pop(); app->push(makeContactScreen(k));   // refresh
          }, "note"));
        };
        if (!cur[0]) { editNote(); break; }
        app->push(makeChoiceScreen("Note", { { "Edit note", "" }, { "Delete note", "" } }, -1, [this, k, editNote](int ch) {
          if (ch == 0) editNote();
          else { m->setContactNote(k, ""); app->toast(IC_TRASH, "Note deleted"); app->pop(); app->push(makeContactScreen(k)); }
        }));
        break;
      }
      case A_ROUTE: app->push(makeRouteScreen(_key, have ? c.name : "")); break;
      case A_ASK:
        if (m->requestLocation(_key)) app->toast(IC_LOCATE, "Asked for their location...");
        else app->toast(IC_ERROR, "Couldn't send the request");
        break;
      case A_RESET:
        if (m->resetRoute(_key)) app->toast(IC_REFRESH, "Route reset - next message will flood");
        break;
      case A_SHARECONTACT:
        if (m->shareContact(_key)) app->toast(IC_RADIO, "Contact shared with nearby nodes");
        else app->toast(IC_ERROR, "Their advert isn't stored yet");
        break;
      case A_REMOVE: {
        uint8_t k[LOG_KEY_LEN];
        memcpy(k, _key, LOG_KEY_LEN);
        app->push(makeConfirmScreen("Remove contact", "Remove this contact? They will come back if they announce themselves again.",
          "Remove", true, [this, k]() { m->removeContact(k); app->pop(); app->toast(IC_TRASH, "Contact removed"); }));
        break;
      }
    }
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }

private:
  uint8_t _key[LOG_KEY_LEN];
  bool _is_node = false;
  FocusList _list;
  lv_obj_t* _fav = nullptr;
  lv_obj_t* _share = nullptr;
};

Screen* makeContactScreen(const uint8_t* key) { return new ContactScreen(key); }

// ------------------------------------------------------------ Groups

// NSW Mesh / MidCoast conventions (nswmesh.au/meshcore/channels, mesh.nah.rocks): scope 0 = default,
// 1 = unscoped (state-wide flood), 2 = the named region.
static const struct { const char* name; const char* what; int mode; const char* scope; } kSuggested[] = {
  { "#test", "Check your signal (bots reply)", 2, "au-nsw-tro" },
  { "#nsw", "NSW-wide chat", 2, "au-nsw" },
  { "#midnorthcoast", "Mid North Coast", 0, "" },
  { "#weather", "Weather & conditions", 2, "au-nsw-tro" },
  { "#emergency", "Emergencies only", 1, "" },
  { "#alert", "Fire & situation alerts", 1, "" },
};

class GroupsScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Groups");
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.onActivate = [this](int i) { act(i); };
    fill();
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _acts.clear();
    _list.addHeader("JOINED");
    for (int i = 0; i < m->channelCount(); i++) {
      ChannelView ch;
      if (!m->channelAt(i, ch)) continue;
      lv_obj_t* r = _list.addRow(T::row_h);
      lv_obj_t* a = ui::avatar(r, ch.name, ch.key, T::easy() ? T::avatar : 22, true);
      lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* t = ui::label(r, ch.name, T::body_semi, T::text);
      lv_obj_align(t, LV_ALIGN_LEFT_MID, (T::easy() ? T::avatar : 22) + 8, 0);
      lv_obj_t* x = ui::label(r, IC_RIGHT, &icons_16, T::faint);
      lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);
      if (m->channelMuted(ch.key)) {
        lv_obj_t* mu = ui::label(r, IC_MUTE, &icons_16, T::faint);
        lv_obj_align(mu, LV_ALIGN_RIGHT_MID, -22, 0);
      }
      Act a2; a2.type = 0; memcpy(a2.key, ch.key, LOG_KEY_LEN); a2.name = ch.name;
      _acts.push_back(a2);
    }
    _list.addHeader("SUGGESTED NEAR YOU");
    for (auto& sg : kSuggested) {
      bool joined = false;
      for (int i = 0; i < m->channelCount(); i++) {
        ChannelView ch;
        if (m->channelAt(i, ch) && strcasecmp(ch.name, sg.name) == 0) joined = true;
      }
      if (joined) continue;
      bool easy = T::easy();
      lv_obj_t* r = _list.addRow(easy ? T::row_h : 38);
      lv_obj_t* tile = ui::iconTile(r, IC_PLUS, T::accent, easy ? T::avatar : 24);
      lv_obj_align(tile, LV_ALIGN_LEFT_MID, 0, 0);
      int x = (easy ? T::avatar : 24) + 8;
      lv_obj_t* t = ui::label(r, sg.name, T::body_semi, T::text);
      lv_obj_align(t, LV_ALIGN_TOP_LEFT, x, easy ? 8 : 2);
      lv_obj_t* s = ui::label(r, sg.what, easy ? T::small : T::f12, T::muted);
      lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, x, easy ? -8 : -3);
      Act a2; a2.type = 1; a2.name = sg.name; a2.smode = sg.mode; a2.sname = sg.scope;
      _acts.push_back(a2);
    }
    _list.addHeader(T::easy() ? "MORE" : "More");
    _list.rowSetting(IC_HASH, lv_color_hex(0xBA68C8), "Join a #hashtag group", "public", true);
    { Act a3; a3.type = 2; _acts.push_back(a3); }
    _list.rowSetting(IC_SHIELD, lv_color_hex(0x6E8BFF), "Create private group", "secret key", true);
    { Act a4; a4.type = 3; _acts.push_back(a4); }
    _list.rowSetting(IC_SHIELD, lv_color_hex(0x9AA4B0), "Join private group", "name + key", true);
    { Act a5; a5.type = 4; _acts.push_back(a5); }
    _list.select(sel, false);
    _list.animate = false;
  }

  void act(int i) {
    if (i < 0 || i >= (int)_acts.size()) return;
    Act a = _acts[i];
    if (a.type == 0) {
      bool priv = a.name.size() && a.name[0] != '#' && a.name != "Public";
      char sc[32];
      int sm = m->channelScope(a.key, sc);
      char scope_txt[48];
      if (sm == 1) snprintf(scope_txt, sizeof(scope_txt), "none (all repeaters)");
      else if (sm == 2) snprintf(scope_txt, sizeof(scope_txt), "%s", sc);
      else snprintf(scope_txt, sizeof(scope_txt), "default (%s)", *m->regionScope() ? m->regionScope() : "none");
      bool muted = m->channelMuted(a.key);
      std::vector<ChoiceItem> opts = { { "Open chat", "" }, { "Region scope", scope_txt },
                                       { muted ? "Turn alerts on" : "Mute alerts", muted ? "muted: no sound or pop-ups" : "still counts unread" },
                                       { "Leave group", "" } };
      if (priv) opts.push_back({ "Show secret key", "to invite others" });
      app->push(makeChoiceScreen(a.name.c_str(), opts, 0, [this, a](int c) {
        if (c == 0) app->openChat(ConvKey::channel(a.key));
        else if (c == 1) {
          std::vector<ChoiceItem> sopts = { { "Use default", "Settings > Region scope" }, { "None", "every repeater passes it on" },
                                            { "au-nsw-tro", "MidCoast" }, { "au-nsw", "All of NSW" }, { "au", "All of Australia" } };
          char cur_name[32];
          int cm = m->channelScope(a.key, cur_name);
          int cur = cm == 1 ? 1 : cm == 2 ? (!strcmp(cur_name, "au-nsw-tro") ? 2 : !strcmp(cur_name, "au-nsw") ? 3 : !strcmp(cur_name, "au") ? 4 : 0) : 0;
          app->push(makeChoiceScreen("Region scope", sopts, cur, [this, a](int k) {
            static const char* names[] = { "", "", "au-nsw-tro", "au-nsw", "au" };
            m->setChannelScope(a.key, k == 0 ? 0 : (k == 1 ? 1 : 2), names[k]);
            app->toast(IC_GLOBE, "Scope saved for this group");
          }, "Which repeaters should pass on messages you send to this group?"));
        }
        else if (c == 2) {
          bool now = !m->channelMuted(a.key);
          if (!m->setChannelMuted(a.key, now)) { app->toast(IC_ERROR, "Too many muted groups"); return; }
          app->toast(now ? IC_MUTE : IC_BELL, now ? "Group muted" : "Alerts on");
          fill();
        }
        else if (c == 3) { m->leaveChannel(a.key); m->setChannelMuted(a.key, false); app->toast(IC_CHECK, "Left group"); fill(); }
        else {
          char hex[33], t[200];
          if (m->channelSecretHex(a.key, hex)) {
            snprintf(t, sizeof(t), "Group: %s\n\nSecret key:\n%.8s %.8s\n%.8s %.8s\n\nOthers join with Groups > Join private group, using exactly this name and key.",
                     a.name.c_str(), hex, hex + 8, hex + 16, hex + 24);
            app->push(makeInfoScreen("Secret key", t));
          }
        }
      }));
    } else if (a.type == 1) {
      if (m->joinChannel(a.name.c_str())) {
        for (int i = 0; i < m->channelCount(); i++) {   // apply the local convention for this group's scope
          ChannelView ch;
          if (m->channelAt(i, ch) && strcasecmp(ch.name, a.name.c_str()) == 0 && a.smode) m->setChannelScope(ch.key, a.smode, a.sname.c_str());
        }
        app->toast(IC_CHECK, "Joined"); fill();
      }
      else app->toast(IC_ERROR, "Couldn't join (group list full?)");
    } else if (a.type == 3) {
      app->push(makeTextEntryScreen("Private group", "Name for the new group (e.g. Family)", "", 24, [this](const char* name) {
        char hex[33], t[220];
        if (!m->createPrivateChannel(name, hex)) { app->toast(IC_ERROR, "Couldn't create (group list full?)"); return; }
        snprintf(t, sizeof(t), "Created \"%s\".\n\nSecret key:\n%.8s %.8s\n%.8s %.8s\n\nShare the name and key with the people you want in the group. Only they can read it.",
                 name, hex, hex + 8, hex + 16, hex + 24);
        app->push(makeInfoScreen("Private group", t));
        fill();
      }));
    } else if (a.type == 4) {
      app->push(makeTextEntryScreen("Join private group", "Group name (exactly as given to you)", "", 24, [this](const char* name) {
        std::string nm = name;
        app->push(makeTextEntryScreen("Secret key", "32 characters (spaces are ignored)", "", 40, [this, nm](const char* hex) {
          if (m->joinPrivateChannel(nm.c_str(), hex)) { app->toast(IC_CHECK, "Joined"); fill(); }
          else app->toast(IC_ERROR, "Key must be 32 hex characters (0-9, a-f)");
        }));
      }));
    } else {
      app->push(makeTextEntryScreen("Join a group", "Type the group name, e.g. #taree. Everyone using the same name can read it.", "#", 30,
        [this](const char* t) {
          std::string n = t;
          if (n[0] != '#') n = "#" + n;
          for (auto& ch : n) ch = (char)tolower(ch);
          if (m->joinChannel(n.c_str())) { app->toast(IC_CHECK, "Joined"); fill(); }
          else app->toast(IC_ERROR, "Couldn't join that group");
        }));
    }
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
  void onShow() override { fill(); }

private:
  struct Act { int type; uint8_t key[LOG_KEY_LEN]; std::string name; int smode = 0; std::string sname; };
  FocusList _list;
  std::vector<Act> _acts;
};

Screen* makeGroupsScreen() { return new GroupsScreen(); }

// ------------------------------------------------------------ Device check

class DeviceCheckScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Device check");
    _scroll = content();
    lv_obj_add_flag(_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_scroll, LV_DIR_VER);
    lv_obj_set_flex_flow(_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_scroll, 6, 0);
    lv_obj_set_style_pad_row(_scroll, 1, 0);
    _rows = ui::box(_scroll);
    lv_obj_set_size(_rows, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(_rows, LV_FLEX_FLOW_COLUMN);
    _lbl = ui::label(_scroll, "", T::f12, T::muted);
    lv_obj_set_width(_lbl, SCR_W - 16);
    lv_label_set_long_mode(_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(_lbl, 6, 0);
    tick();
  }
  void onInput(const InputEvent& e) override {
    static const char* names[] = { "-", "ball up", "ball down", "ball left", "ball right", "ball press", "ball hold", "", "Backspace", "Enter", "ball hold 3s (SOS)" };
    if (e.type == IN_CHAR) snprintf(_last, sizeof(_last), "key '%c'", e.ch);
    else snprintf(_last, sizeof(_last), "%s", names[e.type]);
    if (e.type == IN_SELECT) m->playSound(SOUND_MESSAGE);
    if (e.type == IN_UP) lv_obj_scroll_by_bounded(_scroll, 0, 40, LV_ANIM_ON);
    if (e.type == IN_DOWN) lv_obj_scroll_by_bounded(_scroll, 0, -40, LV_ANIM_ON);
    tick();
  }
  void tick() override {
    SelfTestItem items[16];
    int n = m->selfTest(items, 16);
    lv_obj_clean(_rows);
    for (int i = 0; i < n; i++) {
      lv_obj_t* r = ui::box(_rows);
      lv_obj_set_size(r, lv_pct(100), 18);
      static const char* tags[] = { "PASS", "CHECK", "FAIL" };
      lv_color_t c = items[i].result == TEST_PASS ? T::success : items[i].result == TEST_WARN ? T::warning : T::danger;
      lv_obj_t* t = ui::label(r, tags[items[i].result], T::f12, c);
      lv_obj_set_pos(t, 0, 0);
      lv_obj_t* nm = ui::label(r, items[i].name, T::f12, T::text);
      lv_obj_set_pos(nm, 46, 0);
      lv_obj_t* d = ui::label(r, items[i].detail, T::f12, T::muted);
      ui::oneLine(d, SCR_W - 16 - 120);
      lv_obj_set_pos(d, 120, 0);
    }
    DeviceStatus s = m->status();
    char b[300];
    snprintf(b, sizeof(b),
      "Last input: %s  (roll, press, type to test)\n"
      "Ball edges U%lu D%lu L%lu R%lu \xC2\xB7 contacts %d \xC2\xB7 messages %lu\n"
      "Last reset: %s\n%s",
      _last, (unsigned long)m->inputEdges(0), (unsigned long)m->inputEdges(1), (unsigned long)m->inputEdges(2),
      (unsigned long)m->inputEdges(3), s.contacts, (unsigned long)s.messages_stored,
      *m->lastResetReason() ? m->lastResetReason() : "power on", m->diagnostics());
    lv_label_set_text(_lbl, b);
  }
private:
  lv_obj_t* _scroll = nullptr;
  lv_obj_t* _rows = nullptr;
  lv_obj_t* _lbl = nullptr;
  char _last[24] = "(none)";
};

Screen* makeDeviceCheckScreen() { return new DeviceCheckScreen(); }

// ---------------------------------------------------------------- route (path) view
class RouteScreen : public HeaderScreen {
public:
  RouteScreen(const uint8_t* key, const char* name) { memcpy(_key, key, LOG_KEY_LEN); snprintf(_name, sizeof(_name), "%s", name); }
  void build() override {
    buildHeader("Route");
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.onActivate = [this](int i) { if (i == _ping_row) ping(); };
    fill();
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _ping_row = -1;
    PathHop hops[16];
    int n = m->contactPath(_key, hops, 16);
    _names.clear();
    if (n < 0) {
      _list.setEmpty(IC_NAV, "No route learned yet",
                     "Messages flood through every repeater until one gets a reply. The route is learned automatically.");
      return;
    }
    stop(IC_USER, "You", "this T-Deck");
    bool unsure = false;
    for (int i = 0; i < n; i++) {
      char hx[12] = "", who[40];
      for (int b = 0; b < hops[i].hash_len; b++) snprintf(hx + 2 * b, sizeof(hx) - 2 * b, "%02X", hops[i].hash[b]);
      if (!hops[i].name[0]) snprintf(who, sizeof(who), "Repeater %s", hx);
      else if (hops[i].matches > 1) { snprintf(who, sizeof(who), "%s?", hops[i].name); unsure = true; }
      else snprintf(who, sizeof(who), "%s", hops[i].name);
      _names.push_back(who);
      stop(IC_ANTENNA, who, hx);
    }
    stop(IC_PIN, _name[0] ? _name : "Contact", n == 0 ? "direct - no repeaters" : "");
    if (n > 0) {   // ping: out through the repeaters and back
      _ping_row = _list.count();
      TraceResult t = m->traceResult();
      _list.rowSetting(IC_RADAR, lv_color_hex(0x26C6DA), t.state == 1 ? "Pinging..." : "Ping this route",
                       "every hop, both ways", false);
      if (t.state == 2) showResult(t);
      else if (t.state == 3) _list.addHeader("NO ANSWER - A REPEATER MAY BE DOWN OR OUT OF RANGE");
      else if (t.state == 4) _list.addHeader("COULDN'T SEND THE PING");
    }
    if (unsure) _list.addHeader("? = SHORT ID SHARED BY SEVERAL REPEATERS");
    _list.select(sel >= 0 && sel < _list.count() ? sel : 0, false);
  }

  // links in order: You -> H1 -> ... -> Hn -> Hn-1 -> ... -> H1 -> You
  void showResult(const TraceResult& t) {
    std::vector<std::string> seq;
    seq.push_back("You");
    for (auto& h : _names) seq.push_back(h);
    for (int i = (int)_names.size() - 2; i >= 0; i--) seq.push_back(_names[i]);
    seq.push_back("You");
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "ROUND TRIP %.1f S", t.rtt_ms / 1000.0f);
    _list.addHeader(hdr);
    for (int i = 0; i < t.hops && i + 1 < (int)seq.size(); i++) {
      char title[64], v[24];
      snprintf(title, sizeof(title), "%s > %s", seq[i].c_str(), seq[i + 1].c_str());   // (no arrow glyph in the fonts)
      float snr = t.snr4[i] / 4.0f;
      snprintf(v, sizeof(v), "%.1f dB %s", snr, snr >= 5 ? "good" : snr >= 0 ? "ok" : "weak");
      // >= 5 dB solid, 0..5 ok, below 0 weak (LoRa still decodes down to about -10 dB at SF9)
      lv_color_t c = snr >= 5 ? T::success : snr >= 0 ? T::warning : T::danger;
      lv_obj_t* r = _list.rowSetting(IC_SIGNAL, c, title, v, false);
      (void)r;
    }
  }

  void ping() {
    if (!m->startTrace(_key)) app->toast(IC_ERROR, "Couldn't send the ping");
    _was = 1;
    fill();
  }
  void tick() override {
    int st = m->traceResult().state;
    if (st != _was) { _was = st; fill(); }   // waiting -> done / no answer
  }

  void stop(const char* ic, const char* title, const char* value) {
    _list.rowSetting(ic, T::accent, title, value, false);
  }
  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
private:
  uint8_t _key[LOG_KEY_LEN];
  char _name[32];
  FocusList _list;
  std::vector<std::string> _names;
  int _ping_row = -1, _was = 0;
};

Screen* makeRouteScreen(const uint8_t* key, const char* name) { return new RouteScreen(key, name); }

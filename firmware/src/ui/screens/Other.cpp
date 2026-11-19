// Contact details, Groups, Device check.
#include "Common.h"
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
    if (_is_node) _list.rowSetting(IC_SHIELD, lv_color_hex(0x6E8BFF), c.type == 3 ? "Room admin & status" : "Admin & status", "", true);
    _list.rowSetting(IC_CHAT, T::accent, "Send message", "", true);
    _fav = _list.rowToggle(IC_STAR, T::warning, "Favourite", c.favorite);
    _share = _list.rowToggle(IC_PIN, lv_color_hex(0x26C6DA), "Can see my location", c.share_location);
    _list.rowSetting(IC_LOCATE, lv_color_hex(0x6E8BFF), "Ask for their location", "", false);
    _list.rowSetting(IC_REFRESH, lv_color_hex(0x9AA4B0), "Reset route", c.hops == 0xFF ? "flood" : "learned", false);
    _list.rowSetting(IC_RADIO, lv_color_hex(0xF5B83D), "Share contact nearby", "", false);
    _list.rowSetting(IC_TRASH, T::danger, "Remove contact", "", false);
    _list.select(0, false);
    _list.onActivate = [this](int i) { act(i); };
  }

  void act(int i) {
    if (_is_node) {
      if (i == 0) { app->push(makeAdminScreen(_key)); return; }
      i -= 1;
    }
    ContactView c;
    bool have = m->findContact(_key, c);
    switch (i) {
      case 0: app->openChat(ConvKey::direct(_key)); break;
      case 1:
        if (!have) return;
        m->setFavorite(_key, !c.favorite);
        if (!c.favorite) lv_obj_add_state(_fav, LV_STATE_CHECKED); else lv_obj_remove_state(_fav, LV_STATE_CHECKED);
        break;
      case 2:
        if (!have) return;
        m->setShareLocation(_key, !c.share_location);
        if (!c.share_location) lv_obj_add_state(_share, LV_STATE_CHECKED); else lv_obj_remove_state(_share, LV_STATE_CHECKED);
        app->toast(IC_PIN, !c.share_location ? "They can now see your location" : "Location sharing off");
        break;
      case 3:
        if (m->requestLocation(_key)) app->toast(IC_LOCATE, "Asked for their location...");
        else app->toast(IC_ERROR, "Couldn't send the request");
        break;
      case 4:
        if (m->resetRoute(_key)) app->toast(IC_REFRESH, "Route reset - next message will flood");
        break;
      case 5:
        if (m->shareContact(_key)) app->toast(IC_RADIO, "Contact shared with nearby nodes");
        else app->toast(IC_ERROR, "Their advert isn't stored yet");
        break;
      case 6: {
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
      std::vector<ChoiceItem> opts = { { "Open chat", "" }, { "Region scope", scope_txt }, { "Leave group", "" } };
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
        else if (c == 2) { m->leaveChannel(a.key); app->toast(IC_CHECK, "Left group"); fill(); }
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

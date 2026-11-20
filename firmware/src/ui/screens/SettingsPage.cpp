// Settings: grouped, iOS-style list with icon tiles, values and toggles.
#include "Common.h"
#include "core/Version.h"
#include "../TextInput.h"
#include <string>
#include "ui/SetupFile.h"
#include <stdio.h>

// Sections: the top level lists them, each opens its own page (the rows used to be one long list).
enum { SEC_TOP = 0, SEC_YOU, SEC_RADIO, SEC_CONTACTS, SEC_PRIVACY, SEC_DISPLAY, SEC_POWER, SEC_SOUND, SEC_PHONE, SEC_MAPS, SEC_SYSTEM, SEC_N };
static const char* const kSecNames[SEC_N] = { "Settings", "You & safety", "Radio", "Contacts & channels", "Privacy",
                                               "Display", "Power", "Sound & typing", "Phone & GPS", "Maps & Wi-Fi", "System" };
Screen* makeSettingsSection(int section);
Screen* makeWifiScreen(std::function<void()> done, int target);

class SettingsPage : public Page {
public:
  explicit SettingsPage(int section = SEC_TOP) : _section(section) {}
  void build(lv_obj_t* parent, int w, int h) override {
    _list.create(parent, 0, 0, w, h);
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_actions.size() && _actions[i]) _actions[i](); };
    fill();
  }

  void add(lv_obj_t* row, std::function<void()> fn) { (void)row; _actions.push_back(fn); }
  void sec(int id) { _cur = id; }   // the rows that follow belong to this section
  bool here() const { return _cur == _section; }
  void setting(const char* icon, lv_color_t c, const char* title, const char* value, std::function<void()> fn) {
    if (!here()) return;
    add(_list.rowSetting(icon, c, title, value), fn);
  }
  void toggle(const char* icon, lv_color_t c, const char* title, bool on, std::function<bool()> fn) {
    if (!here()) return;
    lv_obj_t* sw = _list.rowToggle(icon, c, title, on);
    _actions.push_back([sw, fn]() {
      bool now_on = fn();
      if (now_on) lv_obj_add_state(sw, LV_STATE_CHECKED); else lv_obj_remove_state(sw, LV_STATE_CHECKED);
    });
  }

  // Owner & medical info: three text fields, shown on the lock screen without the PIN
  void editOwner() {
    OwnerInfo oi; m->ownerInfo(oi);
    std::vector<ChoiceItem> it = {
      { "Owner", oi.owner[0] ? oi.owner : "your name" },
      { "Emergency contact", oi.ice[0] ? oi.ice : "name and phone" },
      { "Medical notes", oi.medical[0] ? oi.medical : "allergies, conditions, blood type" },
    };
    app->push(makeChoiceScreen("Owner & medical", it, -1, [this](int k) {
      OwnerInfo cur; m->ownerInfo(cur);
      static const char* titles[] = { "Owner", "Emergency contact", "Medical notes" };
      static const char* prompts[] = { "Shown on the lock screen so a finder can return it.",
                                       "Who to call, e.g. \"Sam (wife) 0412 345 678\".",
                                       "For first responders, e.g. \"Diabetic, allergic to penicillin, O+\"." };
      char* field = k == 0 ? cur.owner : k == 1 ? cur.ice : cur.medical;
      int maxlen = k == 0 ? 31 : k == 1 ? 39 : 99;
      int kk = k;
      app->push(makeTextEntryScreen(titles[k], prompts[k], field, maxlen, [this, kk](const char* t) {
        OwnerInfo o; m->ownerInfo(o);
        char* f = kk == 0 ? o.owner : kk == 1 ? o.ice : o.medical;
        size_t cap = kk == 0 ? sizeof(o.owner) : kk == 1 ? sizeof(o.ice) : sizeof(o.medical);
        snprintf(f, cap, "%s", strcmp(t, "-") ? t : "");   // "-" clears it
        m->setOwnerInfo(o);
        app->toast(IC_CHECK, "Saved");
        fill();
      }, "type - to clear"));
    }, "Readable from the lock screen without your PIN (roll the trackball up there), like a phone's medical ID."));
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _actions.clear();
    UiSettings& u = m->ui();
    DeviceStatus st = m->status();
    char buf[64];

    const lv_color_t blue = lv_color_hex(0x6E8BFF), orange = lv_color_hex(0xFF8A65), purple = lv_color_hex(0xBA68C8),
                     teal = lv_color_hex(0x26C6DA), yellow = lv_color_hex(0xF5B83D), green = lv_color_hex(0x66BB6A),
                     pink = lv_color_hex(0xF06292), grey = lv_color_hex(0x9AA4B0), red = T::danger;

    if (_section == SEC_TOP) {
      _cur = SEC_TOP;
      RadioSettings r0 = m->radio();
      int p0 = findPreset(r0);
      char rs0[48];
      snprintf(rs0, sizeof(rs0), "%s", p0 >= 0 ? kRadioPresets[p0].name : "Custom");
      static const char* pw[] = { "Normal", "Battery saver", "Maximum saving" };
      char ch0[32];
      snprintf(ch0, sizeof(ch0), "%d channels", m->channelCount());
      struct { int id; const char* ic; lv_color_t c; const char* sub; } cats[] = {
        { SEC_YOU, IC_USER, blue, m->nodeName() },
        { SEC_RADIO, IC_RADIO, green, rs0 },
        { SEC_CONTACTS, IC_USERS, blue, ch0 },
        { SEC_PRIVACY, IC_SHIELD, green, m->telemetryMode(true) == TM_DENY ? "nobody can locate you" : m->telemetryMode(true) == TM_ALL ? "contacts can locate you" : "people you choose" },
        { SEC_DISPLAY, IC_SUN, yellow, kThemeNames[themeStyle(u.theme)] },
        { SEC_POWER, IC_BATT, green, pw[u.battery_saver % 3] },
        { SEC_SOUND, IC_VOLUME, pink, u.sound ? "sounds on" : "sounds off" },
        { SEC_PHONE, IC_PHONE, teal, st.bt_enabled ? "Bluetooth on" : "Bluetooth off" },
        { SEC_MAPS, IC_MAP, green, *m->wifiSsid() ? m->wifiSsid() : "offline maps" },
        { SEC_SYSTEM, IC_SETTINGS, grey, "EasyDeck " EASYDECK_VERSION_STR },
      };
      for (auto& c : cats) {
        int id = c.id;
        add(_list.rowSetting(c.ic, c.c, kSecNames[id], c.sub), [this, id]() { app->push(makeSettingsSection(id)); });
      }
      _list.select(sel >= 0 && sel < _list.count() ? sel : 0, false);
      _list.animate = false;
      return;
    }

    sec(SEC_YOU);
    setting(IC_USER, blue, "Name", m->nodeName(), [this]() {
      app->push(makeTextEntryScreen("Your name", "This is what other people see.", m->nodeName(), 24,
        [this](const char* t) { m->setNodeName(t); m->advertise(true); app->toast(IC_CHECK, "Name saved"); fill(); }));
    });

    sec(SEC_RADIO);
    RadioSettings rs = m->radio();
    int pi = findPreset(rs);
    {
      char cv[40];
      if (pi < 0) snprintf(cv, sizeof(cv), "%.3f / %g / SF%d", rs.freq, rs.bw, rs.sf);
      setting(IC_RADIO, green, "Radio preset", pi >= 0 ? kRadioPresets[pi].name : cv, [this]() {
        std::vector<ChoiceItem> items;
        for (int i = 0; i < kNumRadioPresets; i++) items.push_back({ kRadioPresets[i].name, kRadioPresets[i].where });
        items.push_back({ "Custom...", "your own frequency, bandwidth, SF and CR" });
        int cur = findPreset(m->radio());
        app->push(makeChoiceScreen("Radio preset", items, cur >= 0 ? cur : kNumRadioPresets, [this](int i) {
          if (i < kNumRadioPresets) { m->setRadio(kRadioPresets[i]); app->toast(IC_RADIO, kRadioPresets[i].name); fill(); return; }
          RadioSettings r = m->radio();
          char init[48];
          snprintf(init, sizeof(init), "%.3f, %g, %d, %d", r.freq, r.bw, r.sf, r.cr);
          app->push(makeTextEntryScreen("Custom radio", "MHz, kHz, SF, CR - e.g. 915.075, 125, 9, 5", init, 40, [this](const char* t) {
            RadioSettings r = m->radio();
            float f, bw; int sf, cr;
            if (sscanf(t, "%f , %f , %d , %d", &f, &bw, &sf, &cr) != 4 || f < 137 || f > 1020 || bw < 7 || bw > 500 || sf < 5 || sf > 12 || cr < 5 || cr > 8) {
              app->toast(IC_ERROR, "Use: 915.075, 125, 9, 5");
              return;
            }
            r.freq = f; r.bw = bw; r.sf = (uint8_t)sf; r.cr = (uint8_t)cr;
            m->setRadioCustom(r);
            app->toast(IC_RADIO, "Custom radio saved");
            fill();
          }));
        }, "Everyone near you must use the same settings."));
      });
    }
    {
      static const int8_t pw[] = { 22, 20, 17, 14, 10, 5, 2 };
      char tv[16];
      snprintf(tv, sizeof(tv), "%d dBm", rs.tx_power);
      setting(IC_ANTENNA, green, "Transmit power", tv, [this]() {
        std::vector<ChoiceItem> it;
        int cur = 0;
        for (int i = 0; i < 7; i++) {
          char b[16]; snprintf(b, sizeof(b), "%d dBm", pw[i]);
          it.push_back({ b, i == 0 ? "maximum (T-Deck Plus)" : i == 5 ? "short range, saves battery" : "" });
          if (pw[i] == m->radio().tx_power) cur = i;
        }
        app->push(makeChoiceScreen("Transmit power", it, cur, [this](int k) {
          RadioSettings r = m->radio(); r.tx_power = pw[k]; m->setRadioCustom(r); fill();
        }, "Lower power saves battery but reaches fewer repeaters. Check your country's limit."));
      });
    }
    {
      char hv[16];
      snprintf(hv, sizeof(hv), "%d byte%s", m->pathHashBytes(), m->pathHashBytes() == 1 ? "" : "s");
      setting(IC_HASH, green, "Path hash size", hv, [this]() {
        app->push(makeChoiceScreen("Path hash size", {
            { "1 byte", "MeshCore default, most compatible" },
            { "2 bytes", "NSW Mesh recommendation: fewer mix-ups" },
            { "3 bytes", "" } },
          m->pathHashBytes() - 1, [this](int k) { m->setPathHashBytes(k + 1); fill(); },
          "How repeaters are identified in the path of messages you send. Bigger sizes avoid two "
          "repeaters looking the same, but leave less room for long paths. Needs MeshCore 1.14+ repeaters."));
      });
    }

    sec(SEC_RADIO);   // settings packs set the radio + channels for an area
    setting(IC_MAP, teal, "Settings packs", "for your area", [this]() {
      app->push(makePacksScreen([this]() { fill(); }));
    });
    sec(SEC_CONTACTS);
    setting(IC_RADIO, orange, "Auto advert", kAutoAdvertNames[u.auto_advert % NUM_AUTO_ADVERT], [this]() {
      std::vector<ChoiceItem> items;
      for (int i = 0; i < NUM_AUTO_ADVERT; i++) items.push_back({ kAutoAdvertNames[i], i == 3 ? "Recommended" : "" });
      app->push(makeChoiceScreen("Auto advert", items, m->ui().auto_advert,
        [this](int i) { m->ui().auto_advert = i; m->saveUi(); fill(); },
        "Announce yourself to the mesh automatically (after the clock is set, then on this schedule)."));
    });
    snprintf(buf, sizeof(buf), "%d joined", m->channelCount());
    setting(IC_HASH, purple, "Channels", buf, [this]() { app->push(makeGroupsScreen()); });
    {
      static const char* modes[] = { "Everyone", "People only", "People + repeaters", "Only when I add" };
      static const char* hops[] = { "Any distance", "Direct only", "Up to 1 hop", "Up to 2 hops", "Up to 3 hops" };
      AutoAddCfg aa = m->autoAdd();
      int hi = aa.max_hops <= 4 ? aa.max_hops : 0;
      setting(IC_USER_PLUS, blue, "New contacts", modes[aa.mode % 4], [this]() {
        app->push(makeChoiceScreen("New contacts", {
            { "Everyone", "every node heard is added" }, { "People only", "no repeaters, rooms or sensors" },
            { "People + repeaters", "also repeaters and rooms" }, { "Only when I add", "add them from Heard" } },
          m->autoAdd().mode % 4, [this](int k) { AutoAddCfg c = m->autoAdd(); c.mode = k; m->setAutoAdd(c); fill(); },
          "Which nodes are added to your contacts automatically when their advert is heard."));
      });
      setting(IC_RADAR, blue, "Add from", hops[hi], [this]() {
        app->push(makeChoiceScreen("Add from", {
            { "Any distance", "" }, { "Direct only", "heard without a repeater" }, { "Up to 1 hop", "" },
            { "Up to 2 hops", "" }, { "Up to 3 hops", "" } },
          m->autoAdd().max_hops <= 4 ? m->autoAdd().max_hops : 0,
          [this](int k) { AutoAddCfg c = m->autoAdd(); c.max_hops = k; m->setAutoAdd(c); fill(); },
          "On a busy mesh, only adding nearby nodes keeps your contact list useful."));
      });
      toggle(IC_REFRESH, blue, "Replace oldest when full", aa.overwrite, [this]() {
        AutoAddCfg c = m->autoAdd(); c.overwrite = !c.overwrite; m->setAutoAdd(c); return c.overwrite;
      });
    }
    sec(SEC_PRIVACY);
    {
      // Who can ask this T-Deck for its position / battery (the phone apps' "Locate" / telemetry).
      // Menu order: chosen people, everyone, nobody.
      static const char* names[] = { "Nobody", "People I choose", "All my contacts" };
      static const uint8_t order[] = { TM_CHOSEN, TM_ALL, TM_DENY };
      auto pick = [this](bool loc) {
        int cur = 0;
        for (int i = 0; i < 3; i++) if (order[i] == m->telemetryMode(loc)) cur = i;
        app->push(makeChoiceScreen(loc ? "Who can locate me" : "Battery & status", {
            { "People I choose", loc ? "per contact: Can see my location" : "people you share your location with" },
            { "All my contacts", "anyone saved in your contacts" },
            { "Nobody", loc ? "your position is never sent on request" : "also blocks location requests" } },
          cur, [this, loc](int k) { m->setTelemetryMode(loc, order[k]); fill(); },
          loc ? "When someone presses Locate on you, your T-Deck answers with its GPS position. "
                "SOS, I'm OK and Share with favourites still send your position."
              : "Your battery level, sent when someone requests your status."));
      };
      setting(IC_LOCATE, green, "Who can locate me", names[m->telemetryMode(true)], [pick]() { pick(true); });
      setting(IC_BATT_MED, green, "Battery & status", names[m->telemetryMode(false)], [pick]() { pick(false); });
    }

    sec(SEC_SYSTEM);
    {
      char bk[48];
      m->backupInfo(bk, sizeof(bk));
      setting(IC_REFRESH, green, "Back up to SD card", bk, [this]() {
        app->push(makeChoiceScreen("Back up to SD card", { { "On", "recommended" }, { "Off", "" } },
          m->ui().backup_off ? 1 : 0, [this](int k) { m->ui().backup_off = k; m->saveUi(); fill(); },
          "Keeps a copy of your identity, contacts, channels and settings in /easydeck/backup on the SD card. "
          "After a reinstall EasyDeck restores it automatically, so you keep the same identity. "
          "Anyone with the card could copy your identity - turn off if you share the card."));
      });
    }
    sec(SEC_MAPS);
    setting(IC_MAP, green, "Offline maps", "download your area", [this]() { app->push(makeOfflineMapsScreen()); });
    setting(IC_WIFI, blue, "Wi-Fi network", *m->wifiSsid() ? m->wifiSsid() : "not set", [this]() { app->push(makeWifiScreen([this]() { fill(); }, Model::WIFI_FOR_MAPS)); });
    setting(IC_GLOBE, teal, "Download via", m->useMirror() ? "mesh.nah.rocks" : "directly", [this]() {
      app->push(makeChoiceScreen("Download via", {
          { "mesh.nah.rocks (Sydney)", "fast in Australia; direct if it's down" },
          { "Directly", "map server / GitHub" } },
        m->useMirror() ? 0 : 1, [this](int k) { m->setUseMirror(k == 0); fill(); },
        "For map tiles (Thunderforest) and software updates."));
    });

    sec(SEC_DISPLAY);
    toggle(IC_HEART, pink, "Easy menu", u.easy_mode, [this]() {
      m->ui().easy_mode = !m->ui().easy_mode;
      m->saveUi();
      app->rebuildAll();
      return (bool)m->ui().easy_mode;
    });
    setting(IC_TYPE, yellow, "Style", kThemeNames[themeStyle(u.theme)], [this]() {
      app->push(makeChoiceScreen("Style", {
          { "Tactical", "Black & amber, condensed type, sharp edges" },
          { "Modern", "Navy & blue, rounded, colourful icons" },
          { "Modern light", "Bright, for daylight" } }, themeStyle(m->ui().theme),
        [this](int i) { m->ui().theme = themeMake(i, themeAccent(m->ui().theme)); m->saveUi(); app->rebuildAll(); }));
    });
    {
      uint8_t ac = themeAccent(u.theme);
      const char* cur = ac == ACCENT_CUSTOM ? "Custom (theme file)" : ac < NUM_ACCENTS ? kAccentNames[ac] : kAccentNames[0];
      setting(IC_SLIDERS, yellow, "Colour", cur, [this]() {
        std::vector<ChoiceItem> it;
        for (int i = 0; i < NUM_ACCENTS; i++) it.push_back({ kAccentNames[i], i == 0 ? "amber for Tactical, blue for Modern" : "" });
        it.push_back({ "Custom (theme file)", "your own colours: /easydeck/theme.txt on the SD card" });
        uint8_t a0 = themeAccent(m->ui().theme);
        app->push(makeChoiceScreen("Colour", it, a0 == ACCENT_CUSTOM ? NUM_ACCENTS : (a0 < NUM_ACCENTS ? a0 : 0), [this](int k) {
          uint8_t acc = k < NUM_ACCENTS ? (uint8_t)k : (uint8_t)ACCENT_CUSTOM;
          if (acc == ACCENT_CUSTOM) {
            char probe[8];
            if (!m->readTextFile(THEME_FILE, probe, sizeof(probe))) {   // no file yet: write one with the current colours
              char t[640];
              auto hx = [](lv_color_t c) { return (unsigned long)(lv_color_to_u32(c) & 0xFFFFFF); };
              snprintf(t, sizeof(t),
                "# EasyDeck colours. Edit the #RRGGBB values, save, then pick Colour > Custom again (or restart).\n"
                "# Lines you delete keep the style's own colour.\n"
                "bg=#%06lX\nsurface=#%06lX\nsurface2=#%06lX\nborder=#%06lX\ntext=#%06lX\nmuted=#%06lX\nfaint=#%06lX\n"
                "accent=#%06lX\nbubble_mine=#%06lX\nbubble_theirs=#%06lX\ndanger=#%06lX\n",
                hx(T::bg), hx(T::surface), hx(T::surface2), hx(T::border), hx(T::text), hx(T::muted), hx(T::faint),
                hx(T::accent), hx(T::bubble_mine), hx(T::bubble_theirs), hx(T::danger));
              if (m->writeTextFile(THEME_FILE, t)) app->toast(IC_EDIT, "Wrote /easydeck/theme.txt - edit it on a computer", 4000);
              else { app->toast(IC_ERROR, "Insert an SD card for a theme file"); return; }
            }
          }
          m->ui().theme = themeMake(themeStyle(m->ui().theme), acc);
          m->saveUi();
          app->rebuildAll();
        }, "Highlights, your message bubbles and selections."));
      });
    }
    setting(IC_TYPE, blue, "Text size", u.text_size ? "Large" : "Normal", [this]() {
      app->push(makeChoiceScreen("Text size", { { "Normal", "" }, { "Large", "" } }, m->ui().text_size,
        [this](int i) { m->ui().text_size = i; m->saveUi(); app->rebuildAll(); }));
    });
    static const uint8_t lv[4] = { 3, 7, 11, 16 };
    static const char* lvn[4] = { "Low", "Medium", "High", "Max" };
    int bi = 0;
    for (int i = 0; i < 4; i++) if (u.brightness >= lv[i]) bi = i;
    setting(IC_SUN, orange, "Brightness", lvn[bi], [this, bi]() {
      app->push(makeChoiceScreen("Brightness", { { "Low", "" }, { "Medium", "" }, { "High", "" }, { "Max", "" } }, bi,
        [this](int i) { m->ui().brightness = lv[i]; m->saveUi(); m->setBacklight(lv[i]); fill(); }));
    });
    setting(IC_CLOCK, grey, "Screen off after", kScreenTimeoutNames[u.screen_timeout % NUM_SCREEN_TIMEOUTS], [this]() {
      std::vector<ChoiceItem> items;
      for (int i = 0; i < NUM_SCREEN_TIMEOUTS; i++) items.push_back({ kScreenTimeoutNames[i], "" });
      app->push(makeChoiceScreen("Screen off after", items, m->ui().screen_timeout,
        [this](int i) { m->ui().screen_timeout = i; m->saveUi(); fill(); }));
    });

    sec(SEC_POWER);
    {
      static const char* pnames[] = { "Normal", "Battery saver", "Maximum saving" };
      setting(IC_BATT, green, "Power use", pnames[u.battery_saver % 3], [this]() {
        app->push(makeChoiceScreen("Power use", {
            { "Normal", "smooth; idles between frames" },
            { "Battery saver", "sleeps between radio packets when the screen is off" },
            { "Maximum saving", "also: Bluetooth off, dimmer, screen off after 15 s, slower UI" } },
          m->ui().battery_saver % 3, [this](int k) {
            UiSettings& uu = m->ui();
            uu.battery_saver = k;
            if (k == 2) {   // the whole bundle; each part can still be changed afterwards
              m->setBluetooth(false);
              uu.screen_timeout = 0;
              if (uu.brightness > 6) uu.brightness = 6;
              uu.kb_light = 0;
            }
            m->saveUi();
            app->setPower(0);   // apply brightness / CPU speed now
            fill();
            if (k >= 1) app->push(makeInfoScreen("Power use",
              "With the screen off for 15 s the T-Deck sleeps between radio packets. Messages still arrive "
              "and wake it; press the trackball to wake it (keys don't).\n\n"
              "It only sleeps while Bluetooth is off and no USB cable is plugged in. It stays awake during "
              "an SOS, a download, location sharing or track recording."));
          }, "The radio always keeps listening. Normal is already lighter than before."));
      });
      static const char* dnames[] = { "Off", "On a charger", "Charger or not moving" };
      setting(IC_MOON, green, "Desk mode", dnames[(u.desk_mode & DESK_MASK) % 3], [this]() {
        app->push(makeChoiceScreen("Desk mode", {
            { "Off", "" },
            { "On a charger", "after a minute on power" },
            { "Charger or not moving", "also after 30 min without moving or typing" } },
          (m->ui().desk_mode & DESK_MASK) % 3, [this](int k) { m->ui().desk_mode = (m->ui().desk_mode & ~DESK_MASK) | k; m->saveUi(); fill(); },
          "Desk mode keeps the T-Deck on the mesh but quiet: channel messages don't beep or light the "
          "screen (direct messages and SOS still do) and the screen turns off sooner. Moving with GPS or "
          "unplugging ends it automatically."));
      });
    }
    sec(SEC_SOUND);
    toggle(IC_VOLUME, green, "Sounds", u.sound, [this]() {
      m->ui().sound = !m->ui().sound; m->saveUi();
      if (m->ui().sound) m->playSound(SOUND_MESSAGE);
      return (bool)m->ui().sound;
    });
    {
      static const char* tones[] = { "Chime", "Double beep", "Soft ping" };
      static const char* gtones[] = { "Same as messages", "Short tick", "Silent" };
      setting(IC_BELL, green, "Message tone", tones[u.tone % NUM_TONES], [this]() {
        app->push(makeChoiceScreen("Message tone", { { "Chime", "two notes, twice" }, { "Double beep", "short and sharp" }, { "Soft ping", "gentle" } },
          m->ui().tone % NUM_TONES, [this](int k) { m->ui().tone = k; m->saveUi(); m->playSound(SOUND_MESSAGE); fill(); },
          "SOS alarms always use a loud siren, even with sounds off."));
      });
      setting(IC_HASH, green, "Channel messages", gtones[u.group_tone % NUM_GROUP_TONES], [this]() {
        app->push(makeChoiceScreen("Channel messages", { { "Same as messages", "" }, { "Short tick", "quieter for busy channels" }, { "Silent", "pop-up only" } },
          m->ui().group_tone % NUM_GROUP_TONES, [this](int k) { m->ui().group_tone = k; m->saveUi(); m->playSound(SOUND_GROUP); fill(); },
          "Mute a single channel from Channels instead if only one is noisy."));
      });
    }
    toggle(IC_KEYBOARD, grey, "Keyboard light", u.kb_light, [this]() {
      m->ui().kb_light = !m->ui().kb_light; m->saveUi(); m->setKeyboardLight(m->ui().kb_light);
      return (bool)m->ui().kb_light;
    });

    sec(SEC_PHONE);
    toggle(IC_BT, blue, "Bluetooth", st.bt_enabled, [this]() {
      bool on = !m->status().bt_enabled; m->setBluetooth(on); return on;
    });
    snprintf(buf, sizeof(buf), "PIN %06lu", (unsigned long)m->blePin());
    setting(IC_SMARTPHONE, teal, "Connect a phone", buf, [this]() {
      char t[420];
      snprintf(t, sizeof(t),
        "Use the MeshCore app (or the EasyDeck app) to read and send messages from your phone.\n\n"
        "1. Turn on Bluetooth on your phone.\n"
        "2. In the app, connect to\n    EasyDeck-%s\n"
        "3. Enter the PIN\n    %06lu\n\n"
        "Messages you send from the phone also appear here.",
        m->nodeName(), (unsigned long)m->blePin());
      app->push(makeInfoScreen("Connect a phone", t));
    });
    toggle(IC_SATELLITE, green, "GPS", st.gps_enabled, [this]() {
      bool on = !m->status().gps_enabled; m->setGps(on); return on;
    });

    sec(SEC_SYSTEM);
    {
      char tb[32];
      if (st.clock_valid) { char d[16], t2[8]; ui::dayLabel(st.now, st.now, m->tzOffset(), d, sizeof(d)); ui::clockTime(st.now, m->tzOffset(), t2, sizeof(t2)); snprintf(tb, sizeof(tb), "%s %s \xC2\xB7 %s", d, t2, m->clockSource()); }
      else snprintf(tb, sizeof(tb), "Not set");
      setting(IC_CLOCK, st.clock_valid ? lv_color_hex(0x66BB6A) : T::warning, "Date & time", tb, [this]() {
        app->push(makeTextEntryScreen("Date & time", "Local time as YYYY-MM-DD HH:MM. Usually not needed: GPS, a connected phone, the internet (whenever Wi-Fi is on) or 3 repeaters on the mesh set it automatically.", "", 16,
          [this](const char* t) {
            int Y, M, D, h, mi;
            if (sscanf(t, "%d-%d-%d %d:%d", &Y, &M, &D, &h, &mi) != 5 || Y < 2025 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59) {
              app->toast(IC_ERROR, "Use the format 2026-11-19 14:05");
              return;
            }
            // days since epoch (civil from days, Howard Hinnant)
            int y = Y - (M <= 2);
            int era = (y >= 0 ? y : y - 399) / 400;
            unsigned yoe = (unsigned)(y - era * 400);
            unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
            unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            long days = era * 146097L + (long)doe - 719468L;
            uint32_t unix_local = (uint32_t)(days * 86400L + h * 3600 + mi * 60);
            m->setTime(unix_local - m->tzOffset() * 3600);
            app->toast(IC_CHECK, "Clock set");
            fill();
          }, "2026-11-19 14:05"));
      });
    }
    if (m->tzSetting() == TZ_AU_EAST) snprintf(buf, sizeof(buf), "AU East (auto)");
    else snprintf(buf, sizeof(buf), "UTC%+d", m->tzOffset());
    setting(IC_GLOBE, blue, "Time zone", buf, [this]() {
      std::vector<ChoiceItem> items;
      items.push_back({ "NSW, VIC, ACT, TAS", "AEST/AEDT, daylight saving automatic" });
      for (int h = -12; h <= 14; h++) {
        char b[16];
        snprintf(b, sizeof(b), "UTC%+d", h);
        const char* note = h == 10 ? "AEST (NSW, QLD, VIC winter)" : h == 11 ? "AEDT (NSW, VIC summer)" :
                           h == 8 ? "AWST (WA)" : h == 9 ? "ACST approx. (SA, NT)" : "";
        items.push_back({ b, note });
      }
      int cur = m->tzSetting() == TZ_AU_EAST ? 0 : m->tzSetting() + 13;
      app->push(makeChoiceScreen("Time zone", items, cur, [this](int i) { m->setTzOffset(i == 0 ? TZ_AU_EAST : i - 13); fill(); }));
    });
    sec(SEC_CONTACTS);
    toggle(IC_ANTENNA, grey, "Show repeaters", u.show_all_contacts, [this]() {
      m->ui().show_all_contacts = !m->ui().show_all_contacts; m->saveUi(); return (bool)m->ui().show_all_contacts;
    });
    sec(SEC_SYSTEM);
    setting(IC_DOWNLOAD, grey, "Software update", "EasyDeck " EASYDECK_VERSION_STR, [this]() { app->push(makeUpdateScreen()); });
    setting(IC_ACTIVITY_ICON(), grey, "Device check", "", [this]() { app->push(makeDeviceCheckScreen()); });
    if (st.under_launcher) {
      setting(IC_HOME, grey, "App menu", "Launcher", [this]() {
        app->push(makeConfirmScreen("App menu", "Leave EasyDeck and open the Launcher?", "Open Launcher", true,
          [this]() { m->rebootToLauncher(); }));
      });
    }
    setting(IC_EDIT, grey, "Save setup to SD card", "", [this]() {
      if (SetupFile::exportTo(m)) app->push(makeInfoScreen("Setup saved",
          "Saved to easydeck-setup-export.txt on the SD card.\n\nTo prepare another T-Deck: copy it to that device's SD card, "
          "rename it to easydeck-setup.txt and restart. Edit the name= line first.\n\nIt contains your private channel keys."));
      else app->toast(IC_ERROR, "No SD card");
    });
    sec(SEC_YOU);
    {
      OwnerInfo oi; m->ownerInfo(oi);
      setting(IC_LIFEBUOY, red, "Owner & medical info", oi.owner[0] || oi.ice[0] || oi.medical[0] ? "set" : "not set", [this]() { editOwner(); });
    }
    setting(IC_LOCK, red, "Screen lock", app->pinSet() ? "PIN on" : "Off", [this]() {
      auto setNew = [this]() {
        app->push(makeTextEntryScreen("New PIN", "4 or more characters. You'll need it every time the screen wakes. SOS works without it.",
                                      "", 16, [this](const char* p1) {
          if (strlen(p1) < 4) { app->toast(IC_ALERT, "At least 4 characters"); return; }
          std::string first = p1;
          app->push(makeTextEntryScreen("Repeat PIN", "Type it once more", "", 16, [this, first](const char* p2) {
            if (first != p2) { app->toast(IC_ERROR, "Didn't match - try again"); return; }
            app->setPin(first.c_str());
            app->toast(IC_LOCK, "Screen lock on");
            fill();
          }, nullptr, true));
        }, nullptr, true));
      };
      if (!app->pinSet()) { setNew(); return; }
      app->push(makeTextEntryScreen("Current PIN", "Enter your PIN to change or remove the lock", "", 16, [this, setNew](const char* cur) {
        if (!app->checkPin(cur)) { app->toast(IC_ERROR, "Wrong PIN"); return; }
        app->push(makeChoiceScreen("Screen lock", { { "Change PIN", "" }, { "Turn lock off", "" } }, -1, [this, setNew](int k) {
          if (k == 0) setNew();
          else { app->setPin(nullptr); app->toast(IC_LOCK, "Screen lock off"); fill(); }
        }));
      }, nullptr, true));
    });
    sec(SEC_SOUND);
    setting(IC_CHAT, grey, "Emoji shortcodes", "type :heart:", [this]() {
      std::string t = "Type a code in a message and it turns into the emoji. Smileys like :) ;) :( :D <3 work too, and two spaces end a sentence.\n\n";
      char e[8];
      for (int i = 0; i < shortcodeCount(); i++) { t += shortcodeEmoji(i, e); t += " :"; t += shortcodeName(i); t += ":\n"; }
      app->push(makeInfoScreen("Emoji shortcodes", t.c_str()));
    });
    sec(SEC_SYSTEM);
    setting(IC_INFO, grey, "About", "", [this]() {
      char id[16], t[480];
      m->nodeId(id, sizeof(id));
      DeviceStatus s2 = m->status();
      RadioSettings r = m->radio();
      snprintf(t, sizeof(t),
        "EasyDeck " EASYDECK_VERSION_STR "\nOpen-source MeshCore firmware for the LilyGO T-Deck Plus.\n\n"
        "Name: %s\nID: %s\n\nRadio: %.3f MHz \xC2\xB7 %.1f kHz \xC2\xB7 SF%d \xC2\xB7 CR%d\n\n"
        "Messages stored: %lu (%s)\nContacts: %d\n\nBuilt on MeshCore (MIT). Barlow & Ubuntu fonts, Material Design Icons.",
        m->nodeName(), id, r.freq, r.bw, r.sf, r.cr,
        (unsigned long)s2.messages_stored, s2.sd_card ? "SD card" : "internal", s2.contacts);
      app->push(makeInfoScreen("About", t));
    });
    setting(IC_TRASH, red, "Reset EasyDeck", "", [this]() {
      app->push(makeConfirmScreen("Reset", "Erase your identity, contacts, channels, messages and settings on this device? This cannot be undone.",
        "Erase everything", true, [this]() { m->factoryReset(); }));
    });

    _list.select(sel, false);
    _list.animate = false;
  }

  static const char* IC_ACTIVITY_ICON() { return IC_SLIDERS; }

  bool onInput(const InputEvent& e) override { return _list.handleNav(e.type); }
  void onShow() override { fill(); }

private:
  FocusList _list;
  std::vector<std::function<void()>> _actions;
  int _section = SEC_TOP, _cur = SEC_TOP;
};

Page* makeSettingsPage() { return new SettingsPage(); }

// One section on its own page (header = section name), back to the overview with Back.
class SettingsSectionScreen : public HeaderScreen {
public:
  explicit SettingsSectionScreen(int s) : _s(s) {}
  ~SettingsSectionScreen() override { delete _page; }
  void build() override {
    buildHeader(kSecNames[_s]);
    _page = new SettingsPage(_s);
    _page->app = app;
    _page->m = m;
    _page->build(content(), SCR_W, contentH());
  }
  void onInput(const InputEvent& e) override {
    if (_page->onInput(e)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
  void onShow() override { _page->onShow(); }
  void tick() override { _page->tick(); }
private:
  int _s;
  SettingsPage* _page = nullptr;
};

Screen* makeSettingsSection(int section) { return new SettingsSectionScreen(section); }

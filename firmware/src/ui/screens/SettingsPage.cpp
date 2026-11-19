// Settings: grouped, iOS-style list with icon tiles, values and toggles.
#include "Common.h"
#include "../TextInput.h"
#include <string>
#include "ui/SetupFile.h"
#include <stdio.h>

class SettingsPage : public Page {
public:
  void build(lv_obj_t* parent, int w, int h) override {
    _list.create(parent, 0, 0, w, h);
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_actions.size() && _actions[i]) _actions[i](); };
    fill();
  }

  void add(lv_obj_t* row, std::function<void()> fn) { (void)row; _actions.push_back(fn); }
  void setting(const char* icon, lv_color_t c, const char* title, const char* value, std::function<void()> fn) {
    add(_list.rowSetting(icon, c, title, value), fn);
  }
  void toggle(const char* icon, lv_color_t c, const char* title, bool on, std::function<bool()> fn) {
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

    _list.addHeader("YOU");
    setting(IC_USER, blue, "Name", m->nodeName(), [this]() {
      app->push(makeTextEntryScreen("Your name", "This is what other people see.", m->nodeName(), 24,
        [this](const char* t) { m->setNodeName(t); m->advertise(true); app->toast(IC_CHECK, "Name saved"); fill(); }));
    });

    _list.addHeader("MESH");
    RadioSettings rs = m->radio();
    int pi = findPreset(rs);
    setting(IC_RADIO, green, "Radio preset", pi >= 0 ? kRadioPresets[pi].name : "Custom", [this]() {
      std::vector<ChoiceItem> items;
      for (int i = 0; i < kNumRadioPresets; i++) items.push_back({ kRadioPresets[i].name, kRadioPresets[i].where });
      app->push(makeChoiceScreen("Radio preset", items, findPreset(m->radio()),
        [this](int i) { m->setRadio(kRadioPresets[i]); app->toast(IC_RADIO, kRadioPresets[i].name); fill(); },
        "Everyone near you must use the same preset."));
    });
    setting(IC_GLOBE, teal, "Region scope", *m->regionScope() ? m->regionScope() : "Off", [this]() {
      std::vector<ChoiceItem> items = {
        { "Off", "Send to every repeater" },
        { "au-nsw-tro", "MidCoast (Taree, Forster...)" },
        { "au-nsw", "All of NSW" },
        { "au", "All of Australia" },
        { "Other...", "Type a scope name" },
      };
      const char* cur = m->regionScope();
      int ci = !*cur ? 0 : !strcmp(cur, "au-nsw-tro") ? 1 : !strcmp(cur, "au-nsw") ? 2 : !strcmp(cur, "au") ? 3 : 4;
      app->push(makeChoiceScreen("Region scope", items, ci, [this](int i) {
        if (i == 4) {
          app->push(makeTextEntryScreen("Region scope", "Scope name, e.g. au-nsw-syd", m->regionScope(), 30,
            [this](const char* t) { m->setRegionScope(t); fill(); }));
          return;
        }
        static const char* v[] = { "", "au-nsw-tro", "au-nsw", "au" };
        m->setRegionScope(v[i]);
        fill();
      }, "Repeaters only pass on messages for regions they serve. Match your local repeaters."));
    });
    setting(IC_RADIO, orange, "Auto advert", kAutoAdvertNames[u.auto_advert % NUM_AUTO_ADVERT], [this]() {
      std::vector<ChoiceItem> items;
      for (int i = 0; i < NUM_AUTO_ADVERT; i++) items.push_back({ kAutoAdvertNames[i], i == 3 ? "Recommended" : "" });
      app->push(makeChoiceScreen("Auto advert", items, m->ui().auto_advert,
        [this](int i) { m->ui().auto_advert = i; m->saveUi(); fill(); },
        "Announce yourself to the mesh automatically (after the clock is set, then on this schedule)."));
    });
    snprintf(buf, sizeof(buf), "%d joined", m->channelCount());
    setting(IC_HASH, purple, "Groups", buf, [this]() { app->push(makeGroupsScreen()); });
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

    setting(IC_MAP, green, "Offline maps", *m->wifiSsid() ? m->wifiSsid() : "download over Wi-Fi", [this]() { app->push(makeOfflineMapsScreen()); });

    _list.addHeader("DISPLAY");
    toggle(IC_HEART, pink, "Easy mode", u.easy_mode, [this]() {
      m->ui().easy_mode = !m->ui().easy_mode;
      m->saveUi();
      app->rebuildAll();
      return (bool)m->ui().easy_mode;
    });
    setting(IC_TYPE, yellow, "Style", kThemeNames[u.theme % NUM_THEMES], [this]() {
      app->push(makeChoiceScreen("Style", {
          { "Tactical", "Black & amber, condensed type, sharp edges" },
          { "Modern", "Navy & blue, rounded, colourful icons" },
          { "Modern light", "Bright, for daylight" } }, m->ui().theme % NUM_THEMES,
        [this](int i) { m->ui().theme = i; m->saveUi(); app->rebuildAll(); }));
    });
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

    _list.addHeader("SOUND & LIGHTS");
    toggle(IC_BATT, green, "Battery saver", u.battery_saver, [this]() {
      UiSettings& uu = m->ui();
      uu.battery_saver = !uu.battery_saver;
      m->saveUi();
      if (uu.battery_saver) app->push(makeInfoScreen("Battery saver",
        "When the screen has been off for 15 s, the T-Deck sleeps between radio packets. Messages still "
        "arrive and wake it. Press the trackball to wake it (keys don't).\n\n"
        "It only sleeps while Bluetooth is OFF (Settings > Bluetooth) and no USB cable is plugged in. "
        "Turning GPS off saves the most.\n"
        "It stays awake during an SOS, a map download, location sharing or track recording."));
      return (bool)uu.battery_saver;
    });
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
      setting(IC_HASH, green, "Group messages", gtones[u.group_tone % NUM_GROUP_TONES], [this]() {
        app->push(makeChoiceScreen("Group messages", { { "Same as messages", "" }, { "Short tick", "quieter for busy groups" }, { "Silent", "pop-up only" } },
          m->ui().group_tone % NUM_GROUP_TONES, [this](int k) { m->ui().group_tone = k; m->saveUi(); m->playSound(SOUND_GROUP); fill(); },
          "Mute a single group from Groups instead if only one is noisy."));
      });
    }
    toggle(IC_KEYBOARD, grey, "Keyboard light", u.kb_light, [this]() {
      m->ui().kb_light = !m->ui().kb_light; m->saveUi(); m->setKeyboardLight(m->ui().kb_light);
      return (bool)m->ui().kb_light;
    });

    _list.addHeader("PHONE & LOCATION");
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

    _list.addHeader("SYSTEM");
    {
      char tb[32];
      if (st.clock_valid) { char d[16], t2[8]; ui::dayLabel(st.now, st.now, m->tzOffset(), d, sizeof(d)); ui::clockTime(st.now, m->tzOffset(), t2, sizeof(t2)); snprintf(tb, sizeof(tb), "%s %s", d, t2); }
      else snprintf(tb, sizeof(tb), "Not set");
      setting(IC_CLOCK, st.clock_valid ? lv_color_hex(0x66BB6A) : T::warning, "Date & time", tb, [this]() {
        app->push(makeTextEntryScreen("Date & time", "Local time as YYYY-MM-DD HH:MM. GPS or a connected phone also sets it automatically.", "", 16,
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
    snprintf(buf, sizeof(buf), "UTC%+d", m->tzOffset());
    setting(IC_GLOBE, blue, "Time zone", buf, [this]() {
      std::vector<ChoiceItem> items;
      for (int h = -12; h <= 14; h++) {
        char b[16];
        snprintf(b, sizeof(b), "UTC%+d", h);
        const char* note = h == 10 ? "AEST (NSW, QLD, VIC winter)" : h == 11 ? "AEDT (NSW, VIC summer)" :
                           h == 8 ? "AWST (WA)" : h == 9 ? "ACST approx. (SA, NT)" : "";
        items.push_back({ b, note });
      }
      app->push(makeChoiceScreen("Time zone", items, m->tzOffset() + 12, [this](int i) { m->setTzOffset(i - 12); fill(); }));
    });
    toggle(IC_ANTENNA, grey, "Show repeaters", u.show_all_contacts, [this]() {
      m->ui().show_all_contacts = !m->ui().show_all_contacts; m->saveUi(); return (bool)m->ui().show_all_contacts;
    });
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
          "rename it to easydeck-setup.txt and restart. Edit the name= line first.\n\nIt contains your private group keys."));
      else app->toast(IC_ERROR, "No SD card");
    });
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
    setting(IC_CHAT, grey, "Emoji shortcodes", "type :heart:", [this]() {
      std::string t = "Type a code in a message and it turns into the emoji. Smileys like :) ;) :( :D <3 work too, and two spaces end a sentence.\n\n";
      char e[8];
      for (int i = 0; i < shortcodeCount(); i++) { t += shortcodeEmoji(i, e); t += " :"; t += shortcodeName(i); t += ":\n"; }
      app->push(makeInfoScreen("Emoji shortcodes", t.c_str()));
    });
    setting(IC_INFO, grey, "About", "", [this]() {
      char id[16], t[480];
      m->nodeId(id, sizeof(id));
      DeviceStatus s2 = m->status();
      RadioSettings r = m->radio();
      snprintf(t, sizeof(t),
        "EasyDeck " EASYDECK_VERSION_STR "\nOpen-source MeshCore firmware for the LilyGO T-Deck Plus.\n\n"
        "Name: %s\nID: %s\n\nRadio: %.3f MHz \xC2\xB7 %.1f kHz \xC2\xB7 SF%d \xC2\xB7 CR%d\nScope: %s\n\n"
        "Messages stored: %lu (%s)\nContacts: %d\n\nBuilt on MeshCore (MIT). Barlow & Ubuntu fonts, Material Design Icons.",
        m->nodeName(), id, r.freq, r.bw, r.sf, r.cr, *m->regionScope() ? m->regionScope() : "off",
        (unsigned long)s2.messages_stored, s2.sd_card ? "SD card" : "internal", s2.contacts);
      app->push(makeInfoScreen("About", t));
    });
    setting(IC_TRASH, red, "Reset EasyDeck", "", [this]() {
      app->push(makeConfirmScreen("Reset", "Erase your identity, contacts, groups, messages and settings on this device? This cannot be undone.",
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
};

Page* makeSettingsPage() { return new SettingsPage(); }

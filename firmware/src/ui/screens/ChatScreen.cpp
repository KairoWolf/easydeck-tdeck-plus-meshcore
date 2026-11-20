// Chat: bubbles with day separators, delivery ticks, group sender names, compose bar.
#include "Common.h"
#include "../PixEmoji.h"
#include <vector>
#include <string>
#include "../TextInput.h"
#include "../Sos.h"
#include <stdio.h>
#include <string.h>

#define MAX_BUBBLES 50     // history loaded when opening a chat
#define MAX_TRACK   120    // bubbles whose status can update live

class ChatScreen : public HeaderScreen {
public:
  explicit ChatScreen(const ConvKey& k) : _key(k) {}

  bool showsChat(const ConvKey& k) override { return k == _key; }

  // "2.3 km NE · heard 5 min ago" (distance needs both positions)
  void contactSub(const ContactView& c, const DeviceStatus& st, char* sub, int n) {
    char when[24] = "";
    if (c.last_heard && st.clock_valid) ui::relTime(c.last_heard, st.now, m->tzOffset(), when, sizeof(when));
    if (c.has_location && st.gps_fix) {
      char d[32];
      ui::distanceText(st.lat, st.lon, c.lat, c.lon, d, sizeof(d));
      snprintf(sub, n, "%s%s%s", d, when[0] ? " \xC2\xB7 heard " : "", when);
    } else if (when[0]) snprintf(sub, n, "Heard %s", when);
    else snprintf(sub, n, "Direct message");
  }

  // Ripple-style: opening a chat quietly asks for the person's position so the title shows a
  // fresh distance. Kept cheap: people only, known route (no flood), we have a fix, their
  // position is > 10 min old, and at most once per 10 min per contact.
  void autoLocate(const ContactView& c, const DeviceStatus& st) {
    if (c.type != 1 || c.hops == 0xFF || !st.gps_fix) return;
    if (c.has_location && st.clock_valid && c.location_time && st.now - c.location_time < 600) return;
    static struct { uint8_t key[LOG_KEY_LEN]; uint32_t ms; } recent[8] = {};
    uint32_t now = lv_tick_get();
    int slot = 0;
    for (int i = 0; i < 8; i++) {
      if (!memcmp(recent[i].key, c.key, LOG_KEY_LEN)) {
        if (recent[i].ms && now - recent[i].ms < 600000UL) return;
        slot = i; break;
      }
      if (recent[i].ms < recent[slot].ms) slot = i;   // else reuse the oldest
    }
    if (m->requestLocation(c.key)) { memcpy(recent[slot].key, c.key, LOG_KEY_LEN); recent[slot].ms = now ? now : 1; }
  }

  void onContacts() override {   // e.g. their position just arrived: refresh the distance
    if (_key.kind != KIND_DIRECT) return;
    ContactView c;
    if (!m->findContact(_key.id, c)) return;
    char sub[64];
    contactSub(c, m->status(), sub, sizeof(sub));
    setSubtitle(sub);
  }

  void build() override {
    { ContactView rc; _is_room = _key.kind == KIND_DIRECT && m->findContact(_key.id, rc) && rc.type == 3; }
    bool group = _key.kind == KIND_CHANNEL;
    char title[48] = "", sub[64] = "";
    DeviceStatus st = m->status();
    if (group) {
      ChannelView ch;
      snprintf(title, sizeof(title), "%s", m->findChannel(_key.id, ch) ? ch.name : "Channel");
      char sc[32];
      if (m->channelScope(_key.id, sc) == 2) snprintf(sub, sizeof(sub), "Channel \xC2\xB7 %s", sc);
      else snprintf(sub, sizeof(sub), "Channel");
    } else {
      ContactView c;
      if (m->findContact(_key.id, c)) {
        snprintf(title, sizeof(title), "%s", c.name);
        contactSub(c, st, sub, sizeof(sub));
        autoLocate(c, st);
      } else {
        snprintf(title, sizeof(title), "Unknown");
        snprintf(sub, sizeof(sub), "Not in your contacts");
      }
    }
    lv_obj_t* slot = nullptr;
    buildHeader(title, sub, &slot);
    lv_obj_t* av = ui::avatar(slot, title, _key.id, 30, group);
    lv_obj_center(av);

    const int compose_h = T::easy() ? 56 : 46;
    lv_obj_t* c = content();
    _msgs = ui::box(c);
    lv_obj_set_size(_msgs, SCR_W, contentH() - compose_h);
    lv_obj_add_flag(_msgs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_msgs, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_msgs, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(_msgs, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(_msgs, 8, 0);
    lv_obj_set_style_pad_ver(_msgs, 6, 0);

    buildCompose(c, contentH() - compose_h, compose_h);

    uint32_t seqs[MAX_BUBBLES];
    int n = m->conversationSeqs(_key, seqs, MAX_BUBBLES);
    _last_day = 0; _last_dir = -1; _last_name[0] = 0;
    for (int i = 0; i < n; i++) {
      LogRecord r;
      if (m->getMessage(seqs[i], r) && !(r.flags & LOG_FLAG_SILENT)) addBubble(r, false);
    }
    if (n == 0) {
      _empty = ui::box(_msgs);
      lv_obj_set_size(_empty, lv_pct(100), lv_pct(100));
      lv_obj_set_flex_flow(_empty, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_row(_empty, 4, 0);
      ui::label(_empty, group ? IC_HASH : IC_CHAT, &icons_32, T::faint);
      lv_obj_t* l = ui::label(_empty, group ? "Say something to the channel" : "Say hi!", T::body_semi, T::text);
      (void)l;
      lv_obj_t* s = ui::label(_empty, group ? "Everyone in this channel can read it." : "Messages are end-to-end encrypted.",
                              T::small, T::muted);
      lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    }
    m->markRead(_key);
    lv_obj_update_layout(_msgs);
    lv_obj_scroll_to_y(_msgs, LV_COORD_MAX, LV_ANIM_OFF);
  }

  void buildCompose(lv_obj_t* parent, int y, int h) {
    lv_obj_t* bar = ui::box(parent);
    lv_obj_set_size(bar, SCR_W, h);
    lv_obj_set_pos(bar, 0, y);
    lv_obj_set_style_bg_color(bar, T::bg, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    const int btn = h - 12;
    _ta = lv_textarea_create(bar);
    lv_textarea_set_one_line(_ta, true);
    lv_textarea_set_max_length(_ta, m->maxTextLen(_key));
    char ph[48];
    if (_key.kind == KIND_CHANNEL) { ChannelView ch; snprintf(ph, sizeof(ph), "Message %s", m->findChannel(_key.id, ch) ? ch.name : "channel"); }
    else if (T::easy()) snprintf(ph, sizeof(ph), "Type a message");
    else snprintf(ph, sizeof(ph), "Message  \xC2\xB7  roll right: quick reply");
    lv_textarea_set_placeholder_text(_ta, ph);
    snprintf(_ph, sizeof(_ph), "%s", ph);
    lv_obj_set_size(_ta, SCR_W - 16 - btn - 8, btn);
    lv_obj_align(_ta, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_font(_ta, T::body, 0);
    lv_obj_set_style_text_color(_ta, T::text, 0);
    lv_obj_set_style_text_color(_ta, T::faint, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(_ta, T::surface2, 0);
    lv_obj_set_style_bg_opa(_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_ta, 0, 0);
    lv_obj_set_style_radius(_ta, T::style() == STYLE_TACTICAL ? 0 : btn / 2, 0);
    if (T::style() == STYLE_TACTICAL) { lv_obj_set_style_border_width(_ta, 1, 0); lv_obj_set_style_border_color(_ta, T::border, 0); }
    lv_obj_set_style_pad_hor(_ta, 14, 0);
    lv_obj_set_style_pad_ver(_ta, (btn - lv_font_get_line_height(T::body)) / 2, 0);
    lv_obj_set_style_bg_color(_ta, T::accent, LV_PART_CURSOR);
    lv_obj_set_style_border_color(_ta, T::accent, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(_ta, 500, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_state(_ta, LV_STATE_FOCUSED);

    _send = ui::box(bar);
    lv_obj_set_size(_send, btn, btn);
    lv_obj_align(_send, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_radius(_send, T::style() == STYLE_TACTICAL ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_send, T::accent, 0);
    lv_obj_set_style_bg_opa(_send, LV_OPA_COVER, 0);
    lv_obj_add_flag(_send, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_send, sendCb, LV_EVENT_CLICKED, this);
    lv_obj_t* si = ui::label(_send, IC_SEND, T::easy() ? &icons_24 : &icons_20, T::on_accent);
    lv_obj_center(si);

    _counter = ui::label(parent, "", T::caption, T::muted);
    lv_obj_align(_counter, LV_ALIGN_BOTTOM_RIGHT, -12, -h - 2);
    updateCompose();
  }

  // Insert a typed character, then apply shortcodes (":heart:"), smileys and double-space.
  // The radio limit is in bytes (emoji are 3-4 bytes), so don't trust LVGL's char limit.
  void typeChar(char ch) {
    const char* t = lv_textarea_get_text(_ta);
    if ((int)pixemoji_unpacked_len(t) >= m->maxTextLen(_key)) { app->toast(IC_ALERT, "Message is full"); return; }
    int32_t chars = 0;   // UTF-8 characters (LVGL's cursor counts characters, not bytes)
    for (const char* p = t; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) chars++;
    bool at_end = lv_textarea_get_cursor_pos(_ta) >= chars;
    lv_textarea_add_char(_ta, ch);
    TextEdit ed;
    if (at_end && textEditFor(lv_textarea_get_text(_ta), ch, ed)) {
      for (int i = 0; i < ed.del_bytes; i++) lv_textarea_delete_char(_ta);   // only ASCII is removed
      lv_textarea_add_text(_ta, ed.insert);
    }
    updateCompose();
  }

  void pickEmoji() {   // emoji grid -> insert at the cursor
    app->push(makeEmojiScreen([this](const char* em) {
      if ((int)(pixemoji_unpacked_len(lv_textarea_get_text(_ta)) + pixemoji_unpacked_len(em)) > m->maxTextLen(_key)) { app->toast(IC_ALERT, "Message is full"); return; }
      lv_textarea_add_text(_ta, em);
      updateCompose();
    }));
  }

  // Ready-made messages: one roll + one press to answer (easier than typing on the tiny keyboard)
  void quickReply() {
    static const char* kQuick[] = { "OK \xF0\x9F\x91\x8D", "On my way", "Running late", "Call me when you can",
                                    "Where are you?", "I'm home safe", "Yes", "No" };
    std::vector<ChoiceItem> it;
    // a place mentioned in this chat (newest first): open it on the map
    Place pl;
    bool have_place = newestPlace(pl);
    if (have_place) {
      char sub[64];
      snprintf(sub, sizeof(sub), "%s: %.4f, %.4f", pl.who, pl.lat, pl.lon);
      it.push_back({ "Show on map", sub });
    }
    bool can_here = liveFix();
    if (can_here) it.push_back({ "I'm here (send my position)", "" });
    it.push_back({ "\xF0\x9F\x98\x80  Emoji...", "" });
    for (const char* q : kQuick) it.push_back({ q, "" });
    app->push(makeChoiceScreen("Quick reply", it, -1, [this, have_place, pl, can_here](int i) {
      static const char* kQ[] = { "OK \xF0\x9F\x91\x8D", "On my way", "Running late", "Call me when you can",
                                  "Where are you?", "I'm home safe", "Yes", "No" };
      if (have_place && i-- == 0) { app->push(makeMapScreenAt(pl.lat, pl.lon, pl.who)); return; }
      if (can_here && i-- == 0) {
        DeviceStatus s = m->status();
        if (!liveFix()) { app->toast(IC_SATELLITE, "No GPS fix"); return; }
        char t[48];
        snprintf(t, sizeof(t), "I'm here: %.5f, %.5f", s.lat, s.lon);
        if (m->sendText(_key, t)) m->playSound(SOUND_SENT); else app->toast(IC_ERROR, "Couldn't send");
        return;
      }
      if (i == 0) { pickEmoji(); return; }
      i -= 1;
      if (i < 0 || i >= (int)(sizeof(kQ) / sizeof(kQ[0]))) return;
      if (m->sendText(_key, kQ[i])) m->playSound(SOUND_SENT);
      else app->toast(IC_ERROR, "Couldn't send");
    }, "Messages are sent straight away. Emoji go into your message."));
  }

  static void sendCb(lv_event_t* e) { ((ChatScreen*)lv_event_get_user_data(e))->send(); }

  // Coordinates in messages ("Spot: -31.9, 152.4", "I'm here: ...", SOS, LOC beacons)
  struct Place { double lat, lon; char who[24]; };
  void placeFrom(const LogRecord& r, Place& p) {
    snprintf(p.who, sizeof(p.who), "%s", r.dir == DIR_OUT ? "You" : (r.name[0] ? r.name : "Them"));
  }
  bool newestPlace(Place& p) {   // newest messages of this chat, newest first
    uint32_t seqs[MAX_BUBBLES];
    int n = m->conversationSeqs(_key, seqs, MAX_BUBBLES);
    for (int i = n - 1; i >= 0; i--) {
      LogRecord r;
      if (m->getMessage(seqs[i], r) && Sos::parseLocation(r.text, p.lat, p.lon)) { placeFrom(r, p); return true; }
    }
    return false;
  }
  bool liveFix() {   // a current GPS fix (status().gps_fix only means "a position is known")
    GpsDetail* g = new GpsDetail();
    m->gpsDetail(*g);
    bool v = g->valid && m->status().gps_fix;
    delete g;
    return v;
  }
  static void bubbleCb(lv_event_t* e) {   // touch a message: select it and show what you can do
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    uint32_t seq = (uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
    for (int i = 0; i < self->_n; i++) if (self->_seq[i] == seq) { self->selectMsg(i); break; }
    self->messageMenu(seq);
  }

  // ---- message selection: roll up on an empty message box, then press for actions
  void selectMsg(int i) {
    if (_sel >= 0 && _sel < _n && _bubble[_sel]) lv_obj_set_style_outline_width(_bubble[_sel], 0, 0);
    _sel = i;
    if (_sel < 0 || _sel >= _n || !_bubble[_sel]) { _sel = -1; lv_textarea_set_placeholder_text(_ta, _ph); updateCompose(); return; }
    lv_obj_set_style_outline_color(_bubble[_sel], T::accent, 0);
    lv_obj_set_style_outline_width(_bubble[_sel], 2, 0);
    lv_obj_set_style_outline_pad(_bubble[_sel], 1, 0);
    lv_obj_scroll_to_view_recursive(_bubble[_sel], LV_ANIM_ON);
    lv_textarea_set_placeholder_text(_ta, "Press: reply, details  \xC2\xB7  roll down: back");
  }

  static int hopNames(Model* m, const LogRecord& r, std::string& out) {
    PathHop hops[8];
    int n = m->messageRepeaters(r, hops, 8);
    for (int i = 0; i < n; i++) {
      char b[40];
      if (hops[i].name[0]) snprintf(b, sizeof(b), "%s%s", hops[i].name, hops[i].matches > 1 ? "?" : "");
      else {
        snprintf(b, sizeof(b), "%02X", hops[i].hash[0]);
        for (int k = 1; k < hops[i].hash_len; k++) snprintf(b + strlen(b), sizeof(b) - strlen(b), "%02X", hops[i].hash[k]);
      }
      if (i) out += r.dir == DIR_OUT ? ", " : " > ";
      out += b;
    }
    return n;
  }

  void messageMenu(uint32_t seq) {
    LogRecord r;
    if (!m->getMessage(seq, r)) return;
    std::vector<ChoiceItem> items;
    std::vector<int> acts;   // 0 reply, 1 details, 2 map, 3 send again, 4 quote
    bool theirs = r.dir == DIR_IN;
    if (theirs && r.kind == KIND_CHANNEL && r.name[0]) { char t[48]; snprintf(t, sizeof(t), "Reply to %s", r.name); items.push_back({ t, "starts your message with @[name]" }); acts.push_back(0); }
    items.push_back({ "Quote", "puts their words in your message" }); acts.push_back(4);
    {
      char sub[64];
      if (r.dir == DIR_OUT && r.kind == KIND_CHANNEL) snprintf(sub, sizeof(sub), "heard from %d repeater%s", r.path_len == 0xFF ? 0 : r.path_len, r.path_len == 1 ? "" : "s");
      else if (theirs && r.path_len != 0xFF && r.path_len) snprintf(sub, sizeof(sub), "%d hop%s", r.path_len & 0x3F, (r.path_len & 0x3F) == 1 ? "" : "s");
      else snprintf(sub, sizeof(sub), "time, route, signal");
      items.push_back({ "Details", sub }); acts.push_back(1);
    }
    double la, lo;
    if (Sos::parseLocation(r.text, la, lo)) { items.push_back({ "Show on map", "" }); acts.push_back(2); }
    if (r.dir == DIR_OUT && r.status == ST_FAILED) { items.push_back({ "Send again", "" }); acts.push_back(3); }
    app->push(makeChoiceScreen("Message", items, -1, [this, seq, acts](int k) {
      if (k < 0 || k >= (int)acts.size()) return;
      LogRecord r;
      if (!m->getMessage(seq, r)) return;
      switch (acts[k]) {
        case 0: {
          char t[48]; snprintf(t, sizeof(t), "@[%s] ", r.name);
          setCompose(t); selectMsg(-1); updateCompose(); break;
        }
        case 4: {
          char t[LOG_TEXT_MAX + 8]; snprintf(t, sizeof(t), "\"%.40s%s\" ", r.text, strlen(r.text) > 40 ? "..." : "");
          setCompose(t); selectMsg(-1); updateCompose(); break;
        }
        case 1: app->push(makeInfoScreen("Message details", details(r).c_str())); break;
        case 2: { Place p; if (Sos::parseLocation(r.text, p.lat, p.lon)) { placeFrom(r, p); app->push(makeMapScreenAt(p.lat, p.lon, p.who)); } break; }
        case 3: if (m->sendText(_key, r.text)) app->toast(IC_REFRESH, "Sending again"); break;
      }
    }, nullptr));
  }

  std::string details(const LogRecord& r) {
    std::string t;
    char b[96];
    DeviceStatus st = m->status();
    uint32_t ts = r.dir == DIR_OUT ? r.sender_ts : r.stored_at;
    if (st.clock_valid && ts > 1700000000UL) {
      char d[16], c[8]; ui::dayLabel(ts, st.now, m->tzOffset(), d, sizeof(d)); ui::clockTime(ts, m->tzOffset(), c, sizeof(c));
      snprintf(b, sizeof(b), "%s %s %s\n", r.dir == DIR_OUT ? "Sent" : "Received", d, c); t += b;
    }
    if (r.dir == DIR_IN && r.kind == KIND_CHANNEL && r.name[0]) { snprintf(b, sizeof(b), "From: %s\n", r.name); t += b; }
    if (r.dir == DIR_OUT) {
      static const char* sts[] = { "-", "Sending...", "Sent", "Delivered", "Not delivered" };   // index = LogStatus
      snprintf(b, sizeof(b), "Status: %s%s\n", r.status < sizeof(sts) / sizeof(sts[0]) ? sts[r.status] : "?", r.origin == ORIGIN_PHONE ? " (from the phone)" : ""); t += b;
      if (r.kind == KIND_DIRECT && r.attempts > 1) { snprintf(b, sizeof(b), "Attempts: %d\n", r.attempts); t += b; }
      if (r.kind == KIND_CHANNEL) {
        int n = r.path_len == 0xFF ? 0 : r.path_len;
        snprintf(b, sizeof(b), "\nHeard from %d repeater%s%s\n", n, n == 1 ? "" : "s", n ? ":" : " (yet)"); t += b;
        std::string h;
        if (hopNames(m, r, h)) t += "  " + h + "\n";
        else if (n) t += "  (names are kept for messages from 0.52 on)\n";
      }
    } else {
      if (r.path_len == 0xFF || r.path_len == 0) t += "Route: direct\n";
      else { snprintf(b, sizeof(b), "Route: %d hop%s\n", r.path_len & 0x3F, (r.path_len & 0x3F) == 1 ? "" : "s"); t += b; }
      { std::string h; if (hopNames(m, r, h)) t += "  via " + h + "\n"; }
      if (r.snr4) { snprintf(b, sizeof(b), "Signal of the last hop: %.1f dB\n", r.snr4 / 4.0f); t += b; }
    }
    snprintf(b, sizeof(b), "\n%d characters", (int)strlen(r.text)); t += b;
    return t;
  }

  void updateCompose() {
    int len = (int)pixemoji_unpacked_len(lv_textarea_get_text(_ta));   // bytes on air
    int left = m->maxTextLen(_key) - len;
    lv_obj_set_style_bg_opa(_send, len ? LV_OPA_COVER : LV_OPA_40, 0);
    if (left <= 30) {
      char b[12];
      snprintf(b, sizeof(b), "%d", left);
      ui::setText(_counter, b);
      lv_obj_set_style_text_color(_counter, left <= 10 ? T::danger : T::muted, 0);
      lv_obj_remove_flag(_counter, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_counter, LV_OBJ_FLAG_HIDDEN);
    }
  }

  void metaText(const LogRecord& r, char* out, int n) {
    char t[16] = "";
    uint32_t ts = r.dir == DIR_OUT ? r.sender_ts : r.stored_at;
    if (m->status().clock_valid && ts > 1700000000UL) ui::clockTime(ts, m->tzOffset(), t, sizeof(t));
    if (r.dir == DIR_OUT) {
      const char* ic = r.status == ST_DELIVERED ? IC_CHECK2 : r.status == ST_FAILED ? IC_ERROR :
                       r.status == ST_PENDING ? IC_CLOCK : IC_CHECK;
      const char* via = r.origin == ORIGIN_PHONE ? IC_SMARTPHONE " " : "";
      if (r.status == ST_FAILED) snprintf(out, n, "%sNot delivered %s", via, ic);
      else if (r.kind == KIND_CHANNEL && r.path_len != 0xFF && r.path_len > 0)   // repeaters heard re-sending it
        snprintf(out, n, "%s%s%s%s  " IC_RADIO " %d", via, t, t[0] ? " " : "", ic, r.path_len);
      else snprintf(out, n, "%s%s%s%s", via, t, t[0] ? " " : "", ic);
    } else {
      // incoming: time + how it got here (hops / signal), like Ripple
      char via[24] = "";
      if (r.origin == ORIGIN_RADIO) {
        if (r.path_len == 0xFF || r.path_len == 0) {
          if (r.snr4) snprintf(via, sizeof(via), "direct %.0fdB", r.snr4 / 4.0f);
          else snprintf(via, sizeof(via), "direct");
        }
        else snprintf(via, sizeof(via), "%d hop%s", r.path_len & 0x3F, (r.path_len & 0x3F) == 1 ? "" : "s");
      }
      snprintf(out, n, "%s%s%s", t, (t[0] && via[0]) ? "  " : "", via);
    }
  }

  void addBubble(const LogRecord& r, bool animate) {
    if (_empty) { lv_obj_delete(_empty); _empty = nullptr; }
    DeviceStatus st = m->status();
    uint32_t ts = r.dir == DIR_OUT ? r.sender_ts : r.stored_at;
    bool mine = r.dir == DIR_OUT;

    // day separator
    if (st.clock_valid && ts > 1700000000UL && (_last_day == 0 || !ui::sameDay(ts, _last_day, m->tzOffset()))) {
      char d[24];
      ui::dayLabel(ts, st.now, m->tzOffset(), d, sizeof(d));
      lv_obj_t* sep = ui::box(_msgs);
      lv_obj_set_size(sep, lv_pct(100), LV_SIZE_CONTENT);
      lv_obj_set_style_pad_ver(sep, 6, 0);
      lv_obj_t* pill = ui::box(sep);
      lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_style_pad_hor(pill, 10, 0);
      lv_obj_set_style_pad_ver(pill, 2, 0);
      lv_obj_set_style_radius(pill, 10, 0);
      lv_obj_set_style_bg_color(pill, T::surface, 0);
      lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
      ui::label(pill, d, T::caption, T::muted);
      lv_obj_center(pill);
      _last_day = ts;
      _last_dir = -1;
    }

    bool same_run = _last_dir == r.dir && (mine || strncmp(_last_name, r.name, LOG_NAME_MAX) == 0);
    lv_obj_t* wrap = ui::box(_msgs);
    lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(wrap, same_run ? 2 : 8, 0);

    lv_obj_t* b = ui::box(wrap);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, SCR_W * 3 / 4, 0);
    lv_obj_set_style_radius(b, T::style() == STYLE_TACTICAL ? 2 : (T::easy() ? 18 : 14), 0);
    lv_obj_set_style_bg_color(b, mine ? T::bubble_mine : T::bubble_theirs, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 6, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 1, 0);
    lv_obj_set_align(b, mine ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT);
    if (!mine && !T::dark()) { lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, T::border, 0); }
    if (r.flags & LOG_FLAG_SOS) { lv_obj_set_style_bg_color(b, T::danger, 0); }
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);   // touch a message: reply, details, map...
    lv_obj_set_user_data(b, (void*)(uintptr_t)r.seq);
    lv_obj_add_event_cb(b, bubbleCb, LV_EVENT_CLICKED, this);

    if (!mine && (r.kind == KIND_CHANNEL || _is_room) && r.name[0] && !same_run) {
      uint8_t h[LOG_KEY_LEN] = {0};
      for (int i = 0; r.name[i] && i < LOG_NAME_MAX; i++) h[i % LOG_KEY_LEN] ^= (uint8_t)r.name[i] * 31 + i;
      lv_obj_t* nm = ui::label(b, r.name, T::f12, T::avatarColor(h));
      (void)nm;
    }
    lv_obj_t* t = ui::label(b, r.text, T::body, mine ? T::bubble_mine_text : T::bubble_theirs_text);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(t, SCR_W * 3 / 4 - 20, 0);

    char meta[48];
    metaText(r, meta, sizeof(meta));
    lv_obj_t* ml = ui::label(b, meta, T::f12, mine ? T::bubble_mine_text : T::muted);
    lv_obj_set_style_opa(ml, mine ? LV_OPA_70 : LV_OPA_COVER, 0);
    lv_obj_set_align(ml, LV_ALIGN_RIGHT_MID);
    lv_obj_set_style_align(ml, LV_ALIGN_RIGHT_MID, 0);
    if (r.dir == DIR_OUT && r.status == ST_FAILED) { lv_obj_set_style_text_color(ml, T::dark() ? lv_color_hex(0x5A0A0A) : lv_color_hex(0x7A0000), 0); }
    if (!meta[0]) lv_obj_add_flag(ml, LV_OBJ_FLAG_HIDDEN);

    if (animate) {
      // new bubbles slide in from their side
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, b);
      lv_anim_set_values(&a, mine ? 40 : -40, 0);
      lv_anim_set_duration(&a, 220);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
      lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); });
      lv_anim_start(&a);
      lv_obj_set_style_opa(b, LV_OPA_TRANSP, 0);
      lv_obj_fade_in(b, 180, 0);
    }

    if (_n < MAX_TRACK) {   // remember where each bubble's status line is, for live updates
      _seq[_n] = r.seq;
      _meta[_n] = ml;
      _bubble[_n] = b;
      _n++;
    }
    _last_dir = r.dir;
    strncpy(_last_name, r.name, LOG_NAME_MAX - 1);
    if (ts > 1700000000UL) _last_day = ts;
  }

  // Enter on an empty box: resend the most recent message that wasn't delivered
  bool resendFailed() {
    for (int i = _n - 1; i >= 0; i--) {
      LogRecord r;
      if (!m->getMessage(_seq[i], r) || r.dir != DIR_OUT) continue;
      if (r.status != ST_FAILED) return false;   // only the latest outgoing one
      if (m->sendText(_key, r.text)) app->toast(IC_REFRESH, "Sending again");
      return true;
    }
    return false;
  }

  // The box holds packed emoji (one code per flag/sequence, so they draw and delete as one);
  // what's sent is the real Unicode sequence.
  void setCompose(const char* t) {
    std::string b(strlen(t) + 1, '\0');
    pixemoji_pack(t, &b[0], b.size());
    lv_textarea_set_text(_ta, b.c_str());
  }

  void send() {
    std::string raw(pixemoji_unpacked_len(lv_textarea_get_text(_ta)) + 1, '\0');
    pixemoji_unpack(lv_textarea_get_text(_ta), &raw[0], raw.size());
    const char* txt = raw.c_str();
    while (*txt == ' ') txt++;
    if (!*txt) { resendFailed(); return; }
    if ((int)strlen(txt) > m->maxTextLen(_key)) { app->toast(IC_ALERT, "Too long - shorten the message"); return; }
    if (!m->sendText(_key, txt)) {
      app->toast(IC_ERROR, _key.kind == KIND_CHANNEL ? "Couldn't send to this channel" : "This contact is no longer saved");
      return;
    }
    m->playSound(SOUND_SENT);
    lv_textarea_set_text(_ta, "");
    updateCompose();
  }

  void onMessage(const LogRecord& r, bool appended) override {
    if (r.conv() != _key || (r.flags & LOG_FLAG_SILENT)) return;
    if (appended) {
      addBubble(r, true);
      lv_obj_update_layout(_msgs);
      lv_obj_scroll_to_y(_msgs, LV_COORD_MAX, LV_ANIM_ON);
      if (r.dir == DIR_IN && app->isViewingChat(_key)) m->markRead(_key);
      return;
    }
    for (int i = 0; i < _n; i++) {
      if (_seq[i] != r.seq) continue;
      char meta[48];
      metaText(r, meta, sizeof(meta));
      ui::setText(_meta[i], meta);
      if (r.dir == DIR_OUT && r.status == ST_FAILED) lv_obj_set_style_text_color(_meta[i], T::dark() ? lv_color_hex(0x5A0A0A) : lv_color_hex(0x7A0000), 0);
      if (meta[0]) lv_obj_remove_flag(_meta[i], LV_OBJ_FLAG_HIDDEN);
      break;
    }
  }

  void onShow() override { m->markRead(_key); }

  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_CHAR: if (_sel >= 0) selectMsg(-1); typeChar(e.ch); break;
      case IN_BACKSPACE:
        if (_sel >= 0) { selectMsg(-1); return; }
        if (!*lv_textarea_get_text(_ta)) { app->pop(); return; }
        lv_textarea_delete_char(_ta);
        updateCompose();
        break;
      case IN_ENTER: case IN_SELECT:
        if (_sel >= 0) { messageMenu(_seq[_sel]); return; }
        send();
        break;
      case IN_UP:   // empty message box: walk up through the messages (newest first)
        if (_sel >= 0) { if (_sel > 0) selectMsg(_sel - 1); else lv_obj_scroll_by_bounded(_msgs, 0, 60, LV_ANIM_ON); }
        else if (!*lv_textarea_get_text(_ta) && _n > 0) selectMsg(_n - 1);
        else lv_obj_scroll_by_bounded(_msgs, 0, 60, LV_ANIM_ON);
        break;
      case IN_DOWN:
        if (_sel >= 0) { if (_sel < _n - 1) selectMsg(_sel + 1); else selectMsg(-1); }
        else lv_obj_scroll_by_bounded(_msgs, 0, -60, LV_ANIM_ON);
        break;
      case IN_LEFT:
        if (_sel >= 0) { selectMsg(-1); return; }
        if (!*lv_textarea_get_text(_ta)) { app->pop(); return; }
        lv_textarea_cursor_left(_ta);
        break;
      case IN_RIGHT: {
        const char* t = lv_textarea_get_text(_ta);
        if (!*t) { quickReply(); return; }   // empty box: ready-made messages
        int32_t chars = 0;
        for (const char* p = t; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) chars++;
        if (lv_textarea_get_cursor_pos(_ta) >= chars) { pickEmoji(); return; }   // at the end: emoji
        lv_textarea_cursor_right(_ta);
        break;
      }
      default: break;
    }
  }

private:
  ConvKey _key;
  bool _is_room = false;
  lv_obj_t* _msgs = nullptr;
  lv_obj_t* _ta = nullptr;
  lv_obj_t* _send = nullptr;
  lv_obj_t* _counter = nullptr;
  lv_obj_t* _empty = nullptr;
  uint32_t _seq[MAX_TRACK];
  lv_obj_t* _meta[MAX_TRACK];
  lv_obj_t* _bubble[MAX_TRACK];
  int _sel = -1;   // selected message (index into _seq), -1 = none
  char _ph[64] = "";
  int _n = 0;
  uint32_t _last_day = 0;
  int _last_dir = -1;
  char _last_name[LOG_NAME_MAX] = "";
};

Screen* makeChatScreen(const ConvKey& k) { return new ChatScreen(k); }

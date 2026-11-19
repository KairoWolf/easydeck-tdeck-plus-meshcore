// Chat: bubbles with day separators, delivery ticks, group sender names, compose bar.
#include "Common.h"
#include <stdio.h>
#include <string.h>

#define MAX_BUBBLES 50     // history loaded when opening a chat
#define MAX_TRACK   120    // bubbles whose status can update live

class ChatScreen : public HeaderScreen {
public:
  explicit ChatScreen(const ConvKey& k) : _key(k) {}

  bool showsChat(const ConvKey& k) override { return k == _key; }

  void build() override {
    { ContactView rc; _is_room = _key.kind == KIND_DIRECT && m->findContact(_key.id, rc) && rc.type == 3; }
    bool group = _key.kind == KIND_CHANNEL;
    char title[48] = "", sub[64] = "";
    DeviceStatus st = m->status();
    if (group) {
      ChannelView ch;
      snprintf(title, sizeof(title), "%s", m->findChannel(_key.id, ch) ? ch.name : "Group");
      snprintf(sub, sizeof(sub), "Group%s%s", *m->regionScope() ? " \xC2\xB7 " : "", m->regionScope());
    } else {
      ContactView c;
      if (m->findContact(_key.id, c)) {
        snprintf(title, sizeof(title), "%s", c.name);
        char when[24] = "";
        if (c.last_heard && st.clock_valid) ui::relTime(c.last_heard, st.now, m->tzOffset(), when, sizeof(when));
        if (c.has_location && st.gps_fix) {
          char d[32];
          ui::distanceText(st.lat, st.lon, c.lat, c.lon, d, sizeof(d));
          snprintf(sub, sizeof(sub), "%s%s%s", d, when[0] ? " \xC2\xB7 heard " : "", when);
        } else if (when[0]) snprintf(sub, sizeof(sub), "Heard %s", when);
        else snprintf(sub, sizeof(sub), "Direct message");
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
      lv_obj_t* l = ui::label(_empty, group ? "Say something to the group" : "Say hi!", T::body_semi, T::text);
      (void)l;
      lv_obj_t* s = ui::label(_empty, group ? "Everyone in this group can read it." : "Messages are end-to-end encrypted.",
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
    if (_key.kind == KIND_CHANNEL) { ChannelView ch; snprintf(ph, sizeof(ph), "Message %s", m->findChannel(_key.id, ch) ? ch.name : "group"); }
    else snprintf(ph, sizeof(ph), "Message");
    lv_textarea_set_placeholder_text(_ta, ph);
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

  static void sendCb(lv_event_t* e) { ((ChatScreen*)lv_event_get_user_data(e))->send(); }

  void updateCompose() {
    int len = (int)strlen(lv_textarea_get_text(_ta));
    int left = m->maxTextLen(_key) - len;
    lv_obj_set_style_bg_opa(_send, len ? LV_OPA_COVER : LV_OPA_40, 0);
    if (left <= 30) {
      char b[12];
      snprintf(b, sizeof(b), "%d", left);
      lv_label_set_text(_counter, b);
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

  void send() {
    const char* txt = lv_textarea_get_text(_ta);
    while (*txt == ' ') txt++;
    if (!*txt) { resendFailed(); return; }
    if (!m->sendText(_key, txt)) {
      app->toast(IC_ERROR, _key.kind == KIND_CHANNEL ? "Couldn't send to this group" : "This contact is no longer saved");
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
      lv_label_set_text(_meta[i], meta);
      if (r.dir == DIR_OUT && r.status == ST_FAILED) lv_obj_set_style_text_color(_meta[i], T::dark() ? lv_color_hex(0x5A0A0A) : lv_color_hex(0x7A0000), 0);
      if (meta[0]) lv_obj_remove_flag(_meta[i], LV_OBJ_FLAG_HIDDEN);
      break;
    }
  }

  void onShow() override { m->markRead(_key); }

  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_CHAR: lv_textarea_add_char(_ta, e.ch); updateCompose(); break;
      case IN_BACKSPACE:
        if (!*lv_textarea_get_text(_ta)) { app->pop(); return; }
        lv_textarea_delete_char(_ta);
        updateCompose();
        break;
      case IN_ENTER: case IN_SELECT: send(); break;
      case IN_UP: lv_obj_scroll_by_bounded(_msgs, 0, 60, LV_ANIM_ON); break;
      case IN_DOWN: lv_obj_scroll_by_bounded(_msgs, 0, -60, LV_ANIM_ON); break;
      case IN_LEFT:
        if (!*lv_textarea_get_text(_ta)) { app->pop(); return; }
        lv_textarea_cursor_left(_ta);
        break;
      case IN_RIGHT: lv_textarea_cursor_right(_ta); break;
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
  int _n = 0;
  uint32_t _last_day = 0;
  int _last_dir = -1;
  char _last_name[LOG_NAME_MAX] = "";
};

Screen* makeChatScreen(const ConvKey& k) { return new ChatScreen(k); }

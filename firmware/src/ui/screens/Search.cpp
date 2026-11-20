// Message search: type to filter, newest first; Enter opens the chat. Searches the recent
// window of the history (the cached records), text and sender names, any case.
#include "Common.h"
#include "../TextInput.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <ctype.h>

#define SEARCH_MAX 60

class SearchScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Search");
    _count = ui::label(_header, "", T::f12, T::muted);
    lv_obj_align(_count, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_t* c = content();
    _ta = lv_textarea_create(c);
    lv_textarea_set_one_line(_ta, true);
    lv_textarea_set_placeholder_text(_ta, "Type to search messages");
    lv_obj_set_size(_ta, SCR_W - 16, 36);
    lv_obj_set_pos(_ta, 8, 4);
    lv_obj_set_style_text_font(_ta, T::body, 0);
    lv_obj_set_style_bg_color(_ta, T::surface, 0);
    lv_obj_set_style_text_color(_ta, T::text, 0);
    lv_obj_set_style_text_color(_ta, T::faint, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(_ta, T::accent, 0);
    lv_obj_set_style_border_width(_ta, 2, 0);
    lv_obj_set_style_radius(_ta, T::style() == STYLE_TACTICAL ? 0 : 10, 0);
    lv_obj_set_style_bg_color(_ta, T::accent, LV_PART_CURSOR);
    lv_obj_add_state(_ta, LV_STATE_FOCUSED);
    _list.create(c, 0, 46, SCR_W, contentH() - 46);
    _list.animate = false;
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_hits.size()) app->openChat(_hits[i]); };
    run();
  }

  void run() {
    const char* q = lv_textarea_get_text(_ta);
    _list.clear();
    _hits.clear();
    if (!*q) {
      ui::setText(_count, "");
      _list.setEmpty(IC_SEARCH, "Search your messages", "Words or a name, e.g. \"camp\" or \"Mum\"");
      return;
    }
    uint32_t seqs[SEARCH_MAX];
    int n = m->searchMessages(q, seqs, SEARCH_MAX);
    char b[24];
    snprintf(b, sizeof(b), n >= SEARCH_MAX ? "%d+ found" : "%d found", n);
    ui::setText(_count, b);
    DeviceStatus st = m->status();
    for (int i = 0; i < n; i++) {
      LogRecord r;
      if (!m->getMessage(seqs[i], r)) continue;
      addRow(r, q, st);
      _hits.push_back(r.conv());
    }
    if (n == 0) _list.setEmpty(IC_SEARCH, "Nothing found", "Only recent messages are searched");
    else _list.select(0, false);
  }

  void addRow(const LogRecord& r, const char* q, const DeviceStatus& st) {
    lv_obj_t* row = _list.addRow(38);
    char title[64];
    if (r.kind == KIND_CHANNEL) {
      ChannelView ch;
      const char* who = r.dir == DIR_OUT ? "You" : (r.name[0] ? r.name : "?");
      if (m->findChannel(r.key, ch)) snprintf(title, sizeof(title), "%s \xC2\xB7 %s", ch.name, who);
      else snprintf(title, sizeof(title), "%s", who);
    } else snprintf(title, sizeof(title), "%s%s", r.dir == DIR_OUT ? "You \xE2\x86\x92 " : "", r.name);
    char when[24] = "";
    uint32_t ts = r.dir == DIR_OUT ? r.sender_ts : r.stored_at;
    if (st.clock_valid && ts > 1700000000UL) ui::relTime(ts, st.now, m->tzOffset(), when, sizeof(when));
    int tw = 0;
    if (when[0]) {
      lv_obj_t* tl = ui::label(row, when, T::f12, T::faint);
      lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, 0, 3);
      lv_obj_update_layout(tl);
      tw = lv_obj_get_width(tl) + 6;
    }
    lv_obj_t* t = ui::label(row, title, T::f14b, T::text);
    ui::oneLine(t, SCR_W - 2 * T::pad - 12 - tw);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 2);
    // snippet starting a little before the match so the hit is visible
    const char* txt = r.text;
    const char* hit = nullptr;
    for (const char* p = txt; *p && !hit; p++) {   // first place the query starts (any case)
      const char *a = p, *b = q;
      while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
      if (!*b) hit = p;
    }
    char snip[LOG_TEXT_MAX + 8];
    if (hit && hit - txt > 18) {
      const char* s = hit - 12;
      while (s > txt && ((unsigned char)*s & 0xC0) == 0x80) s--;   // don't start inside a UTF-8 character
      snprintf(snip, sizeof(snip), "\xE2\x80\xA6%s", s);
    } else snprintf(snip, sizeof(snip), "%s", txt);
    lv_obj_t* p = ui::label(row, snip, T::f12, T::muted);
    ui::oneLine(p, SCR_W - 2 * T::pad - 12);
    lv_obj_align(p, LV_ALIGN_BOTTOM_LEFT, 0, -3);
  }

  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_CHAR: lv_textarea_add_char(_ta, e.ch); run(); break;
      case IN_BACKSPACE:
        if (!*lv_textarea_get_text(_ta)) { app->pop(); return; }
        lv_textarea_delete_char(_ta);
        run();
        break;
      default:
        if (!_list.handleNav(e.type) && e.type == IN_LEFT) app->pop();
    }
  }

private:
  lv_obj_t *_ta = nullptr, *_count = nullptr;
  FocusList _list;
  std::vector<ConvKey> _hits;
};

Screen* makeSearchScreen() { return new SearchScreen(); }

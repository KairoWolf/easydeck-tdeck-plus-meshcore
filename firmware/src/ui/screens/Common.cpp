#include "Common.h"
#include <string.h>

// ------------------------------------------------------------ HeaderScreen

void HeaderScreen::buildHeader(const char* title, const char* subtitle, lv_obj_t** avatar_slot) {
  _header = ui::box(root);
  lv_obj_set_size(_header, SCR_W, HEADER_H);
  lv_obj_set_pos(_header, 0, STATUS_H);
  lv_obj_set_style_pad_hor(_header, 4, 0);

  bool easy = T::easy();
  lv_obj_set_style_bg_color(_header, T::surface, 0);
  lv_obj_set_style_bg_opa(_header, LV_OPA_COVER, 0);
  lv_obj_t* back = ui::box(_header);
  lv_obj_set_size(back, easy ? 34 : 24, HEADER_H);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(back, backCb, LV_EVENT_CLICKED, this);
  lv_obj_t* bl = ui::label(back, IC_LEFT, easy ? &icons_24 : &icons_16, T::accent_text);
  lv_obj_center(bl);

  int x = easy ? 38 : 26;
  if (avatar_slot) {
    int av = easy ? 30 : 20;
    *avatar_slot = ui::box(_header);
    lv_obj_set_size(*avatar_slot, av, av);
    lv_obj_align(*avatar_slot, LV_ALIGN_LEFT_MID, x, 0);
    x += av + 6;
  }
  lv_obj_t* t = ui::label(_header, title, easy ? (subtitle ? T::body_semi : T::title) : T::f14b, T::text);
  if (subtitle && easy) {
    ui::oneLine(t, SCR_W - x - 12);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, x, 2);
    lv_obj_t* s = ui::label(_header, subtitle, T::caption, T::muted);
    ui::oneLine(s, SCR_W - x - 12);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, x, -3);
  } else if (subtitle) {   // compact: subtitle to the right of the title, on one line
    lv_obj_update_layout(t);
    int tw = lv_obj_get_width(t);
    if (tw > 170) tw = 170;
    ui::oneLine(t, tw);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_t* s = ui::label(_header, subtitle, T::f12, T::muted);
    ui::oneLine(s, SCR_W - x - tw - 18);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, x + tw + 8, 1);
  } else {
    ui::oneLine(t, SCR_W - x - 12);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
  }

  // hairline under the header (amber rule in Tactical)
  lv_obj_t* line = ui::box(root);
  lv_obj_set_size(line, SCR_W, 1);
  lv_obj_set_pos(line, 0, STATUS_H + HEADER_H - 1);
  lv_obj_set_style_bg_color(line, T::style() == STYLE_TACTICAL ? T::accent : T::border, 0);
  lv_obj_set_style_bg_opa(line, T::style() == STYLE_TACTICAL ? LV_OPA_COVER : LV_OPA_60, 0);
}

lv_obj_t* HeaderScreen::content() {
  if (!_content) {
    _content = ui::box(root);
    lv_obj_set_size(_content, SCR_W, contentH());
    lv_obj_set_pos(_content, 0, STATUS_H + HEADER_H);
  }
  return _content;
}

void HeaderScreen::backCb(lv_event_t* e) {
  HeaderScreen* self = (HeaderScreen*)lv_event_get_user_data(e);
  self->app->pop();
}

// ------------------------------------------------------------ ChoiceScreen

class ChoiceScreen : public HeaderScreen {
public:
  bool lockSafe() override { return true; }   // confirm dialogs (e.g. cancel SOS, forgot PIN)
  ChoiceScreen(const char* title, const std::vector<ChoiceItem>& items, int cur, std::function<void(int)> cb, const char* q)
    : _title(title), _items(items), _cur(cur), _cb(cb), _q(q ? q : "") {}
  void build() override {
    buildHeader(_title.c_str());
    lv_obj_t* c = content();
    int y = 0;
    if (!_q.empty()) {
      lv_obj_t* l = ui::label(c, _q.c_str(), T::small, T::muted);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(l, SCR_W - 2 * T::pad - 16);
      lv_obj_set_pos(l, T::pad + 8, 6);
      lv_obj_update_layout(l);
      y = lv_obj_get_height(l) + 8;
    }
    _list.create(c, 0, y, SCR_W, contentH() - y);
    for (size_t i = 0; i < _items.size(); i++) {
      bool sub = !_items[i].subtitle.empty();
      bool easy = T::easy();
      lv_obj_t* r = _list.addRow(sub ? (easy ? T::row_h : 38) : (easy ? T::row_h - 8 : T::row_h));
      lv_obj_t* t = ui::label(r, _items[i].title.c_str(), (int)i == _cur ? T::body_semi : T::body, T::text);
      ui::oneLine(t, SCR_W - 2 * T::pad - 40);
      lv_obj_align(t, sub ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 4, sub ? (easy ? 8 : 2) : 0);
      if (sub) {
        lv_obj_t* s = ui::label(r, _items[i].subtitle.c_str(), easy ? T::small : T::f12, T::muted);
        ui::oneLine(s, SCR_W - 2 * T::pad - 40);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 4, easy ? -8 : -3);
      }
      if ((int)i == _cur) {
        lv_obj_t* ck = ui::label(r, IC_CHECK, T::icon, T::accent_text);
        lv_obj_align(ck, LV_ALIGN_RIGHT_MID, -4, 0);
      }
    }
    _list.select(_cur >= 0 ? _cur : 0, false);
    _list.onActivate = [this](int i) {
      auto cb = _cb;
      App* a = app;
      a->pop();
      if (cb) cb(i);
    };
  }
  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
private:
  std::string _title;
  std::vector<ChoiceItem> _items;
  int _cur;
  std::function<void(int)> _cb;
  std::string _q;
  FocusList _list;
};

Screen* makeChoiceScreen(const char* title, const std::vector<ChoiceItem>& items, int current,
                         std::function<void(int)> on_choose, const char* question) {
  return new ChoiceScreen(title, items, current, on_choose, question);
}

// ------------------------------------------------------------ TextEntryScreen

class TextEntryScreen : public HeaderScreen {
public:
  bool secret = false;
  TextEntryScreen(const char* title, const char* prompt, const char* initial, int max_len,
                  std::function<void(const char*)> cb, const char* ph)
    : _title(title), _prompt(prompt), _initial(initial ? initial : ""), _maxlen(max_len), _cb(cb), _ph(ph ? ph : "") {}
  void build() override {
    buildHeader(_title.c_str());
    lv_obj_t* c = content();
    lv_obj_t* l = ui::label(c, _prompt.c_str(), T::body, T::muted);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, SCR_W - 32);
    lv_obj_set_pos(l, 16, 12);

    _ta = lv_textarea_create(c);
    lv_textarea_set_one_line(_ta, true);
    lv_textarea_set_max_length(_ta, _maxlen);
    lv_textarea_set_text(_ta, _initial.c_str());
    if (!_ph.empty()) lv_textarea_set_placeholder_text(_ta, _ph.c_str());
    lv_obj_set_width(_ta, SCR_W - 32);
    lv_obj_align(_ta, LV_ALIGN_TOP_MID, 0, T::easy() ? 80 : 64);
    lv_obj_set_style_text_font(_ta, T::body, 0);
    lv_obj_set_style_text_color(_ta, T::text, 0);
    lv_obj_set_style_text_color(_ta, T::faint, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(_ta, T::surface, 0);
    lv_obj_set_style_border_color(_ta, T::accent, 0);
    lv_obj_set_style_border_width(_ta, 2, 0);
    lv_obj_set_style_radius(_ta, 12, 0);
    lv_obj_set_style_pad_hor(_ta, 12, 0);
    lv_obj_set_style_pad_ver(_ta, 10, 0);
    lv_obj_set_style_bg_color(_ta, T::accent, LV_PART_CURSOR);
    lv_obj_set_style_border_color(_ta, T::accent, LV_PART_CURSOR | LV_STATE_FOCUSED);
    if (secret) lv_textarea_set_password_mode(_ta, true);
    lv_obj_add_state(_ta, LV_STATE_FOCUSED);

    lv_obj_t* hint = ui::label(c, "Enter to save  \xC2\xB7  hold ball to cancel", T::caption, T::faint);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
  void onInput(const InputEvent& e) override {
    if (e.type == IN_CHAR) lv_textarea_add_char(_ta, e.ch);
    else if (e.type == IN_BACKSPACE) {
      if (!*lv_textarea_get_text(_ta)) app->pop();
      else lv_textarea_delete_char(_ta);
    } else if (e.type == IN_ENTER || e.type == IN_SELECT) {
      std::string t = lv_textarea_get_text(_ta);
      while (!t.empty() && t.back() == ' ') t.pop_back();
      while (!t.empty() && t.front() == ' ') t.erase(0, 1);
      if (t.empty()) return;
      auto cb = _cb;
      App* a = app;
      a->pop();
      if (cb) cb(t.c_str());
    } else if (e.type == IN_LEFT) lv_textarea_cursor_left(_ta);
    else if (e.type == IN_RIGHT) lv_textarea_cursor_right(_ta);
  }
private:
  std::string _title, _prompt, _initial;
  int _maxlen;
  std::function<void(const char*)> _cb;
  std::string _ph;
  lv_obj_t* _ta = nullptr;
};

Screen* makeTextEntryScreen(const char* title, const char* prompt, const char* initial, int max_len,
                            std::function<void(const char*)> on_done, const char* placeholder, bool secret) {
  TextEntryScreen* s = new TextEntryScreen(title, prompt, initial, max_len, on_done, placeholder);
  s->secret = secret;
  return s;
}

// ------------------------------------------------------------ InfoScreen

class InfoScreen : public HeaderScreen {
public:
  InfoScreen(const char* title, const char* text) : _title(title), _text(text) {}
  void build() override {
    buildHeader(_title.c_str());
    _scroll = content();
    lv_obj_add_flag(_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_scroll, LV_DIR_VER);
    lv_obj_set_style_pad_all(_scroll, 14, 0);
    lv_obj_t* l = ui::label(_scroll, _text.c_str(), T::body, T::text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, SCR_W - 28);
    lv_obj_set_style_text_line_space(l, 3, 0);
  }
  void onInput(const InputEvent& e) override {
    if (e.type == IN_UP) lv_obj_scroll_by_bounded(_scroll, 0, 48, LV_ANIM_ON);
    else if (e.type == IN_DOWN) lv_obj_scroll_by_bounded(_scroll, 0, -48, LV_ANIM_ON);
    else if (e.type == IN_LEFT || e.type == IN_BACKSPACE || e.type == IN_SELECT || e.type == IN_ENTER) app->pop();
  }
private:
  std::string _title, _text;
  lv_obj_t* _scroll = nullptr;
};

Screen* makeInfoScreen(const char* title, const char* text) { return new InfoScreen(title, text); }

// ------------------------------------------------------------ ConfirmScreen

Screen* makeConfirmScreen(const char* title, const char* question, const char* yes, bool danger,
                          std::function<void()> on_yes) {
  std::vector<ChoiceItem> items = { { yes, "" }, { "Cancel", "" } };
  (void)danger;
  return makeChoiceScreen(title, items, 1, [on_yes](int i) { if (i == 0 && on_yes) on_yes(); }, question);
}

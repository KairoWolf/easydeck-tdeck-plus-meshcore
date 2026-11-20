// Emoji picker: every pixel emoji (SerenityOS set, ~2000), in Unicode's groups.
//  - trackball moves in the grid; roll up past the top row to reach the group tabs, then
//    left/right changes group
//  - type letters to search by name ("fire", "flag aus", "heart"); Backspace edits the search
//  - press inserts the emoji (the packed code: a flag or family stays one character while editing)
// The grid is virtual: a fixed set of cells is re-labelled as it scrolls, so it stays fast.
#include "Common.h"
#include "../PixEmoji.h"
#include "../TextInput.h"
#include <functional>
#include <string.h>
#include <string>
#include <vector>

class EmojiScreen : public HeaderScreen {
  static const int COLS = 8, CELL = SCR_W / COLS, TABS_H = 28, MAX_ROWS = 6;
public:
  explicit EmojiScreen(std::function<void(const char*)> pick) : _pick(pick) {}

  void build() override {
    buildHeader("Emoji");
    _name = ui::label(_header, "", T::f12, T::muted);
    lv_obj_set_width(_name, 190);
    lv_label_set_long_mode(_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_name, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_name, LV_ALIGN_RIGHT_MID, -8, 0);

    // group tabs (each shows the group's first emoji)
    _ng = pixemoji_group_count();
    if (_ng > MAX_GROUPS) _ng = MAX_GROUPS;
    const int tw = SCR_W / _ng;
    for (int g = 0; g < _ng; g++) {
      lv_obj_t* t = ui::box(content());
      lv_obj_set_size(t, tw, TABS_H);
      lv_obj_set_pos(t, g * tw, 0);
      lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_color(t, T::accent, 0);
      char u[6]; pixemoji_utf8(pixemoji_at(pixemoji_group_first(g)), u);
      lv_obj_center(ui::label(t, u, &pixemoji_16, T::text));
      _tab[g] = t;
    }
    _query = ui::label(content(), "", T::f14, T::text);
    lv_obj_set_size(_query, SCR_W - 16, TABS_H - 4);
    lv_obj_set_pos(_query, 8, 5);
    lv_obj_add_flag(_query, LV_OBJ_FLAG_HIDDEN);

    _rows = (contentH() - TABS_H - 2) / CELL;
    if (_rows > MAX_ROWS) _rows = MAX_ROWS;
    const int y0 = TABS_H + 2 + (contentH() - TABS_H - 2 - _rows * CELL) / 2;
    for (int k = 0; k < _rows * COLS; k++) {
      lv_obj_t* c = ui::box(content());
      lv_obj_set_size(c, CELL - 4, CELL - 4);
      lv_obj_set_pos(c, (k % COLS) * CELL + 2, y0 + (k / COLS) * CELL + 2);
      lv_obj_set_style_radius(c, T::style() == STYLE_TACTICAL ? 0 : 8, 0);
      lv_obj_set_style_border_color(c, T::accent, LV_STATE_FOCUSED);
      lv_obj_set_style_border_width(c, 2, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_color(c, T::select_bg, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_STATE_FOCUSED);
      _glyph[k] = ui::label(c, "", &pixemoji_32, T::text);
      lv_obj_center(_glyph[k]);
      _cell[k] = c;
    }
    _none = ui::label(content(), "No emoji with that name", T::f14, T::muted);
    lv_obj_align(_none, LV_ALIGN_CENTER, 0, TABS_H / 2);
    lv_obj_add_flag(_none, LV_OBJ_FLAG_HIDDEN);
    showGroup(0);
  }

  void showGroup(int g) {
    _group = g;
    _list.clear();
    for (int i = 0; i < pixemoji_group_size(g); i++) _list.push_back((uint16_t)(pixemoji_group_first(g) + i));
    _sel = 0; _top = 0;
    refresh();
  }

  void search() {
    _list.clear();
    for (int i = 0; i < pixemoji_total() && _list.size() < 400; i++)
      if (wordsMatch(pixemoji_name(i), _q.c_str())) _list.push_back((uint16_t)i);
    _sel = 0; _top = 0; _tabs = false;
    refresh();
  }

  // every word of the search appears in the name: "flag aus" finds "flag: australia"
  static bool wordsMatch(const char* name, const char* q) {
    char w[24];
    while (*q) {
      while (*q == ' ') q++;
      int n = 0;
      while (*q && *q != ' ' && n < (int)sizeof(w) - 1) w[n++] = *q++;
      w[n] = 0;
      if (n && !textMatches(name, w)) return false;
    }
    return true;
  }

  void refresh() {
    bool searching = !_q.empty();
    for (int g = 0; g < _ng; g++) {
      bool on = !searching && g == _group;
      lv_obj_set_style_bg_color(_tab[g], _tabs && on ? T::accent : T::select_bg, 0);
      lv_obj_set_style_bg_opa(_tab[g], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(_tab[g], on ? 2 : 0, 0);
      if (searching) lv_obj_add_flag(_tab[g], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(_tab[g], LV_OBJ_FLAG_HIDDEN);
    }
    if (searching) {
      std::string q = std::string(IC_SEARCH " ") + _q + "_";
      ui::setText(_query, q.c_str());
      lv_obj_remove_flag(_query, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(_query, LV_OBJ_FLAG_HIDDEN);

    const int n = (int)_list.size();
    if (_sel >= n) _sel = n ? n - 1 : 0;
    if (_sel / COLS < _top) _top = _sel / COLS;
    if (_sel / COLS >= _top + _rows) _top = _sel / COLS - _rows + 1;
    for (int k = 0; k < _rows * COLS; k++) {
      int i = _top * COLS + k;
      if (i < n) {
        char u[6]; pixemoji_utf8(pixemoji_at(_list[i]), u);
        lv_label_set_text(_glyph[k], u);
        lv_obj_remove_flag(_cell[k], LV_OBJ_FLAG_HIDDEN);
      } else lv_obj_add_flag(_cell[k], LV_OBJ_FLAG_HIDDEN);
      if (i == _sel && !_tabs && n) lv_obj_add_state(_cell[k], LV_STATE_FOCUSED);
      else lv_obj_remove_state(_cell[k], LV_STATE_FOCUSED);
    }
    if (n == 0) lv_obj_remove_flag(_none, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_none, LV_OBJ_FLAG_HIDDEN);

    char b[64];
    if (_tabs) snprintf(b, sizeof(b), "%s", pixemoji_group_name(_group));
    else if (n) snprintf(b, sizeof(b), "%s", pixemoji_name(_list[_sel]));
    else b[0] = 0;
    lv_label_set_text(_name, b);
  }

  void onInput(const InputEvent& e) override {
    const int n = (int)_list.size();
    if (_tabs) {
      switch (e.type) {
        case IN_LEFT: if (_group > 0) showGroup(_group - 1); return;
        case IN_RIGHT: if (_group < _ng - 1) showGroup(_group + 1); return;
        case IN_DOWN: case IN_SELECT: case IN_ENTER: _tabs = false; refresh(); return;
        case IN_UP: return;
        default: break;   // typing still searches
      }
    }
    switch (e.type) {
      case IN_LEFT: if (_sel % COLS) { _sel--; refresh(); } break;
      case IN_RIGHT: if (_sel % COLS < COLS - 1 && _sel + 1 < n) { _sel++; refresh(); } break;
      case IN_UP:
        if (_sel >= COLS) _sel -= COLS;
        else if (_q.empty()) _tabs = true;
        refresh();
        break;
      case IN_DOWN:
        if (_sel + COLS < n) _sel += COLS;
        else if (_sel / COLS < (n - 1) / COLS) _sel = n - 1;
        refresh();
        break;
      case IN_SELECT: case IN_ENTER: {
        if (!n) return;
        char u[6]; pixemoji_utf8(pixemoji_at(_list[_sel]), u);
        std::string s = u;
        auto cb = _pick;
        app->pop();
        if (cb) cb(s.c_str());
        break;
      }
      case IN_CHAR:
        if ((unsigned char)e.ch >= ' ' && _q.size() < 20) {
          if (_q.empty() && e.ch == ' ') return;
          _q += (char)(e.ch >= 'A' && e.ch <= 'Z' ? e.ch + 32 : e.ch);
          search();
        }
        break;
      case IN_BACKSPACE:
        if (_q.empty()) { app->pop(); return; }
        _q.pop_back();
        if (_q.empty()) showGroup(_group); else search();
        break;
      default: break;
    }
  }

private:
  static const int MAX_GROUPS = 12;
  std::function<void(const char*)> _pick;
  lv_obj_t *_name = nullptr, *_query = nullptr, *_none = nullptr;
  lv_obj_t* _tab[MAX_GROUPS] = {};
  lv_obj_t* _cell[COLS * MAX_ROWS] = {};
  lv_obj_t* _glyph[COLS * MAX_ROWS] = {};
  std::vector<uint16_t> _list;   // picker indexes shown in the grid
  std::string _q;                // search text
  int _ng = 0, _group = 0, _sel = 0, _top = 0, _rows = 0;
  bool _tabs = false;            // focus on the group tabs
};

Screen* makeEmojiScreen(std::function<void(const char*)> pick) { return new EmojiScreen(pick); }

// Emoji picker: a scrolling grid of every emoji the device font can draw (the shortcode table).
// Trackball moves, press inserts it into the message being written.
#include "Common.h"
#include "../TextInput.h"
#include <functional>
#include <string>
#include <vector>

class EmojiScreen : public HeaderScreen {
  static const int COLS = 7;
public:
  explicit EmojiScreen(std::function<void(const char*)> pick) : _pick(pick) {}

  void build() override {
    buildHeader("Emoji");
    _name = ui::label(_header, "", T::f12, T::muted);
    lv_obj_align(_name, LV_ALIGN_RIGHT_MID, -8, 0);
    _grid = ui::box(content());
    lv_obj_set_size(_grid, SCR_W, contentH());
    lv_obj_add_flag(_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_grid, LV_SCROLLBAR_MODE_OFF);
    const int cell = (SCR_W - 8) / COLS;
    const int n = shortcodeCount();
    char e[8];
    for (int i = 0; i < n; i++) {
      lv_obj_t* c = ui::box(_grid);
      lv_obj_set_size(c, cell - 4, cell - 4);
      lv_obj_set_pos(c, 4 + (i % COLS) * cell, 4 + (i / COLS) * cell);
      lv_obj_set_style_radius(c, T::style() == STYLE_TACTICAL ? 0 : 8, 0);
      lv_obj_set_style_border_color(c, T::accent, LV_STATE_FOCUSED);
      lv_obj_set_style_border_width(c, 2, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_color(c, T::surface2, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_STATE_FOCUSED);
      lv_obj_t* l = ui::label(c, shortcodeEmoji(i, e), T::f24, T::text);
      lv_obj_center(l);
      _cells.push_back(c);
    }
    select(0);
  }

  void select(int i) {
    if (i < 0 || i >= (int)_cells.size()) return;
    lv_obj_remove_state(_cells[_sel], LV_STATE_FOCUSED);
    _sel = i;
    lv_obj_add_state(_cells[_sel], LV_STATE_FOCUSED);
    lv_obj_scroll_to_view(_cells[_sel], LV_ANIM_ON);
    char b[24];
    snprintf(b, sizeof(b), ":%s:", shortcodeName(_sel));   // teaches the shortcode too
    lv_label_set_text(_name, b);
  }

  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_LEFT: if (_sel % COLS) select(_sel - 1); break;
      case IN_RIGHT: if (_sel % COLS < COLS - 1) select(_sel + 1); break;
      case IN_UP: select(_sel - COLS); break;
      case IN_DOWN: select(_sel + COLS < (int)_cells.size() ? _sel + COLS : (int)_cells.size() - 1); break;
      case IN_SELECT: case IN_ENTER: {
        char em[8];
        shortcodeEmoji(_sel, em);
        std::string s = em;
        auto cb = _pick;
        App* a = app;
        a->pop();
        if (cb) cb(s.c_str());
        break;
      }
      case IN_BACKSPACE: app->pop(); break;
      default: break;
    }
  }

private:
  std::function<void(const char*)> _pick;
  lv_obj_t *_grid = nullptr, *_name = nullptr;
  std::vector<lv_obj_t*> _cells;
  int _sel = 0;
};

Screen* makeEmojiScreen(std::function<void(const char*)> pick) { return new EmojiScreen(pick); }

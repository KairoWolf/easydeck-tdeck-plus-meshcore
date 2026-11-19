// Easy mode: four huge tiles, and full-screen versions of the normal pages.
#include "Common.h"
#include <stdio.h>

class PageScreen : public HeaderScreen {
public:
  explicit PageScreen(int which) : _which(which) {}
  ~PageScreen() override { delete _page; }
  void build() override {
    static const char* easy_titles[] = { "Messages", "People", "Settings", "Recently heard" };
    static const char* titles[] = { "Chats", "Contacts", "Settings", "Heard" };
    buildHeader((T::easy() ? easy_titles : titles)[_which % 4]);
    _page = _which == PAGE_CHATS ? makeChatsPage() : _which == PAGE_CONTACTS ? makePeoplePage() :
            _which == PAGE_SETTINGS ? makeSettingsPage() : makeHeardPage();
    _page->app = app;
    _page->m = m;
    _page->build(content(), SCR_W, contentH());
  }
  void onInput(const InputEvent& e) override {
    if (_page->onInput(e)) return;
    if (e.type == IN_LEFT || e.type == IN_BACKSPACE) app->pop();
  }
  void onMessage(const LogRecord& r, bool appended) override { _page->onMessage(r, appended); }
  void onContacts() override { _page->onContacts(); }
  void onShow() override { _page->onShow(); }
  void tick() override { _page->tick(); }
private:
  int _which;
  Page* _page = nullptr;
};

Screen* makePageScreen(int which) { return new PageScreen(which); }
Screen* makeEasyListScreen(int which) { return new PageScreen(which); }

class EasyHome : public Screen {
public:
  void build() override {
    static const char* icons[4] = { IC_CHAT, IC_USERS, IC_SIREN, IC_SETTINGS };
    static const char* names[4] = { "Messages", "People", "SOS", "Settings" };
    const uint32_t colors[4] = { lv_color_to_u32(T::accent), 0x6E8BFF, 0xFF5A5F, 0x9AA4B0 };
    const int gap = 8, w = (SCR_W - 3 * gap) / 2, h = (SCR_H - STATUS_H - 3 * gap) / 2;
    for (int i = 0; i < 4; i++) {
      lv_obj_t* t = ui::box(root);
      lv_obj_set_size(t, w, h);
      lv_obj_set_pos(t, gap + (i % 2) * (w + gap), STATUS_H + gap + (i / 2) * (h + gap));
      lv_obj_set_style_radius(t, T::style() == STYLE_TACTICAL ? 2 : 20, 0);
      lv_obj_set_style_bg_color(t, T::surface, 0);
      lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(t, T::border, 0);
      lv_obj_set_style_border_width(t, 1, 0);
      lv_obj_set_style_border_color(t, T::accent, LV_STATE_FOCUSED);
      lv_obj_set_style_border_width(t, 4, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_color(t, T::surface2, LV_STATE_FOCUSED);
      lv_obj_set_style_transform_pivot_x(t, w / 2, 0);
      lv_obj_set_style_transform_pivot_y(t, h / 2, 0);
      lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(t, (void*)(intptr_t)i);
      lv_obj_add_event_cb(t, [](lv_event_t* e) {
        EasyHome* s = (EasyHome*)lv_event_get_user_data(e);
        int i = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
        s->select(i);
        s->open(i);
      }, LV_EVENT_CLICKED, this);
      lv_obj_t* ic = ui::label(t, icons[i], &icons_48, lv_color_hex(colors[i]));
      lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 12);
      lv_obj_t* nm = ui::label(t, names[i], T::f20b, T::text);
      lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -10);
      _tiles[i] = t;
      ui::animIn(t, i * 50);
    }
    _badge = nullptr;
    select(0);
    refreshBadge();
  }

  void refreshBadge() {
    if (_badge) { lv_obj_delete(_badge); _badge = nullptr; }
    int n = m->totalUnread();
    if (n <= 0) return;
    _badge = ui::badge(_tiles[0], n);
    lv_obj_align(_badge, LV_ALIGN_TOP_RIGHT, -8, 8);
  }

  void select(int i) {
    if (i < 0 || i > 3) return;
    lv_obj_remove_state(_tiles[_sel], LV_STATE_FOCUSED);
    lv_obj_set_style_transform_scale(_tiles[_sel], 256, 0);
    _sel = i;
    lv_obj_add_state(_tiles[_sel], LV_STATE_FOCUSED);
    lv_anim_t a;   // gentle "lift" on focus
    lv_anim_init(&a);
    lv_anim_set_var(&a, _tiles[_sel]);
    lv_anim_set_values(&a, 256, 264);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_transform_scale((lv_obj_t*)o, v, 0); });
    lv_anim_start(&a);
  }

  void open(int i) {
    switch (i) {
      case 0: app->push(makeEasyListScreen(0)); break;
      case 1: app->push(makeEasyListScreen(1)); break;
      case 2: app->push(makeSosScreen()); break;
      case 3: app->push(makeEasyListScreen(2)); break;
    }
  }

  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_UP: if (_sel >= 2) select(_sel - 2); break;
      case IN_DOWN: if (_sel < 2) select(_sel + 2); break;
      case IN_LEFT: if (_sel % 2) select(_sel - 1); break;
      case IN_RIGHT: if (!(_sel % 2)) select(_sel + 1); break;
      case IN_SELECT: case IN_ENTER: open(_sel); break;
      default: break;
    }
  }
  void onMessage(const LogRecord& r, bool appended) override { refreshBadge(); }
  void onShow() override { refreshBadge(); }

private:
  lv_obj_t* _tiles[4];
  lv_obj_t* _badge = nullptr;
  int _sel = 0;
};

Screen* makeEasyHome() { return new EasyHome(); }

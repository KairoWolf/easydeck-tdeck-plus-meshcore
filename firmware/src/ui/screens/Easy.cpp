// Easy menu: one big list (family first), and full-screen versions of the normal pages.
#include "Common.h"
#include <functional>
#include <stdio.h>
#include <vector>

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

// Easy menu: one big list, rolled up and down, pressed to open. Family first (your favourites,
// one press to their chat), then Messages, I'm OK, Map, SOS and the full menu. The same letter
// keys as the normal home screen work here too.
class EasyHome : public Screen {
public:
  void build() override {
    _list.create(root, 0, STATUS_H, SCR_W, SCR_H - STATUS_H - 20);
    _list.animate = false;
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_acts.size()) _acts[i](); };
    _hint = ui::label(root, "Roll to choose  \xC2\xB7  press to open  \xC2\xB7  hold the ball to go back", T::f12, T::muted);
    lv_obj_set_width(_hint, SCR_W);
    lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(_hint, T::bg, 0);
    lv_obj_set_style_bg_opa(_hint, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_ver(_hint, 2, 0);
    lv_obj_align(_hint, LV_ALIGN_BOTTOM_MID, 0, 0);
    fill();
  }

  lv_obj_t* bigRow(const char* icon, lv_color_t color, const char* title, const char* sub, std::function<void()> fn) {
    lv_obj_t* r = _list.addRow(50);
    lv_obj_t* ic = ui::label(r, icon, &icons_32, color);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_t* t = ui::label(r, title, T::f20b, T::text);
    ui::oneLine(t, SCR_W - 70);
    lv_obj_align(t, sub && *sub ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 52, sub && *sub ? 4 : 0);
    if (sub && *sub) {
      lv_obj_t* l = ui::label(r, sub, T::f14, T::muted);
      ui::oneLine(l, SCR_W - 70);
      lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 52, -4);
    }
    _acts.push_back(fn);
    return r;
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _acts.clear();
    // family / favourites: straight to their chat
    std::vector<ConvSummary> convs(40);
    convs.resize(m->listConversations(convs.data(), (int)convs.size()));
    auto unreadFor = [&convs](const ConvKey& k) { for (auto& c : convs) if (c.key == k) return (int)c.unread; return 0; };
    int fav = 0;
    for (int i = 0; i < m->contactCount() && fav < 4; i++) {
      ContactView c;
      if (!m->contactAt(i, c) || !c.favorite || c.type != 1) continue;
      ConvKey k = ConvKey::direct(c.key);
      int unread = unreadFor(k);
      char t[48], sub[40];
      snprintf(t, sizeof(t), "%s", c.name);
      if (unread) snprintf(sub, sizeof(sub), "%d new message%s", unread, unread == 1 ? "" : "s");
      else snprintf(sub, sizeof(sub), "Send a message");
      lv_obj_t* r = bigRow(IC_CHAT, lv_color_hex(0x3DDC84), t, sub, [this, k]() { app->openChat(k); });
      if (unread) { lv_obj_t* b = ui::badge(r, unread); lv_obj_align(b, LV_ALIGN_RIGHT_MID, -8, 0); }
      fav++;
    }
    {
      int n = m->totalUnread();
      char sub[40];
      if (n) snprintf(sub, sizeof(sub), "%d new", n); else snprintf(sub, sizeof(sub), "All your chats");
      lv_obj_t* r = bigRow(IC_CHATS, T::accent, "Messages", sub, [this]() { app->push(makeEasyListScreen(0)); });
      if (n) { lv_obj_t* b = ui::badge(r, n); lv_obj_align(b, LV_ALIGN_RIGHT_MID, -8, 0); }
    }
    bigRow(IC_OK_CIRCLE, lv_color_hex(0x3DDC84), "I'm OK", fav ? "Tell your family you're OK" : "Star family members first", [this]() { imOk(); });
    bigRow(IC_MAP, lv_color_hex(0x26C6DA), "Map", "Where everyone is", [this]() { app->push(makeMapScreen()); });
    bigRow(IC_SIREN, lv_color_hex(0xFF5A5F), "SOS", "Ask for help", [this]() { app->push(makeSosScreen()); });
    bigRow(IC_SETTINGS, lv_color_hex(0x9AA4B0), "Full menu", "Everything else, and settings", [this]() { app->push(makeMainScreen(0)); });
    if (!fav) {   // first time: show how to put family on top
      bigRow(IC_STAR, T::warning, "Add family here", "Messages > a person > Favourite", [this]() { app->push(makeEasyListScreen(1)); });
    }
    _list.select(sel >= 0 && sel < _list.count() ? sel : 0, false);
  }

  void imOk() {
    app->push(makeConfirmScreen("I'm OK", "Tell your family you're OK? Your location is included.", "Send I'm OK", false,
      [this]() { app->sos().checkIn(); app->toast(IC_OK_CIRCLE, "Sent - your family knows you're OK", 3000); }));
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type != IN_CHAR) return;
    switch (e.ch) {   // same letters as the normal home screen
      case 'c': case 'C': app->push(makeEasyListScreen(0)); break;
      case 'o': case 'O': app->push(makeEasyListScreen(1)); break;
      case 'm': case 'M': app->push(makeMapScreen()); break;
      case 'x': case 'X': app->push(makeSosScreen()); break;
      case 'k': case 'K': imOk(); break;
      case 's': case 'S': app->push(makeEasyListScreen(2)); break;
      default: break;
    }
  }
  void onMessage(const LogRecord& r, bool appended) override { if (appended) fill(); }
  void onContacts() override { fill(); }
  void onShow() override { fill(); }

private:
  FocusList _list;
  std::vector<std::function<void()>> _acts;
  lv_obj_t* _hint = nullptr;
};

Screen* makeEasyHome() { return new EasyHome(); }

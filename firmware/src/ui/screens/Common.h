#pragma once
#include <functional>
#include <string>
#include <vector>
#include "ui/App.h"
#include "ui/Widgets.h"
#include "core/Version.h"

inline int headerH() { return T::easy() ? 40 : 26; }
#define HEADER_H headerH()

// A pushed screen with a header bar: [<]  Title            [action]
class HeaderScreen : public Screen {
public:
  void buildHeader(const char* title, const char* subtitle = nullptr, lv_obj_t** avatar_slot = nullptr);
  lv_obj_t* content();                      // area below the header
  int contentH() const { return SCR_H - STATUS_H - HEADER_H; }
  void setSubtitle(const char* s) { if (_sub) lv_label_set_text(_sub, s); }
protected:
  lv_obj_t* _header = nullptr;
  lv_obj_t* _sub = nullptr;   // subtitle label (if any)
  lv_obj_t* _content = nullptr;
  static void backCb(lv_event_t* e);
};

struct ChoiceItem {
  std::string title;
  std::string subtitle;
};

Screen* makeChoiceScreen(const char* title, const std::vector<ChoiceItem>& items, int current,
                         std::function<void(int)> on_choose, const char* question = nullptr);
Screen* makeTextEntryScreen(const char* title, const char* prompt, const char* initial, int max_len,
                            std::function<void(const char*)> on_done, const char* placeholder = nullptr,
                            bool secret = false);   // secret: show dots (PINs, passwords)
Screen* makeInfoScreen(const char* title, const char* text);
Screen* makeEmojiScreen(std::function<void(const char*)> pick);   // emoji grid; pick() gets the UTF-8 emoji
Screen* makeConfirmScreen(const char* title, const char* question, const char* yes, bool danger,
                          std::function<void()> on_yes);

// Pages live inside the tabbed main screen (or full-screen in easy mode)
class Page {
public:
  virtual ~Page() {}
  virtual void build(lv_obj_t* parent, int w, int h) = 0;
  virtual bool onInput(const InputEvent& e) { return false; }   // true = consumed
  virtual void onMessage(const LogRecord& r, bool appended) {}
  virtual void onContacts() {}
  virtual void onShow() {}
  virtual void tick() {}
  App* app = nullptr;
  Model* m = nullptr;
};

Page* makeChatsPage();
Page* makePeoplePage();
Page* makeSettingsPage();
Page* makeHeardPage();

enum PageKind { PAGE_CHATS = 0, PAGE_CONTACTS = 1, PAGE_SETTINGS = 2, PAGE_HEARD = 3 };
Screen* makePageScreen(int kind);
Screen* makeSignalScreen();
Screen* makeGpsScreen();

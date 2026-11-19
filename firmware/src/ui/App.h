#pragma once
// App — navigation (screen stack with animated transitions), chrome (status bar,
// toasts, incoming-message banner), input routing, and screen power.
// Platform-neutral: needs only LVGL + Model.
#include <lvgl.h>
#include <vector>
#include "core/Model.h"
#include "Theme.h"
#include "Sos.h"

class App;

class Screen {
public:
  virtual ~Screen() {}
  virtual void build() = 0;                       // populate root (content starts at y = STATUS_H)
  virtual void onInput(const InputEvent& e) {}
  virtual void onMessage(const LogRecord& r, bool appended) {}
  virtual void onContacts() {}
  virtual void onShow() {}                        // became the top screen (first time or resumed)
  virtual void onHide() {}                        // removed from the stack: root is about to be deleted,
                                                  // stop timers now (the object itself is freed later)
  virtual void tick() {}                          // ~2 Hz while on top
  virtual bool fullscreen() { return false; }     // hide the status bar
  virtual bool showsChat(const ConvKey& k) { return false; }
  lv_obj_t* root = nullptr;
  App* app = nullptr;
  Model* m = nullptr;
};

class App {
public:
  void begin(Model* model);
  void input(const InputEvent& e);
  void touched();                         // touch activity (wakes screen)
  void loop();                            // call every main-loop pass

  // events from the device
  void onMessage(const LogRecord& r, bool appended);
  void onContactsChanged();
  bool isViewingChat(const ConvKey& k);

  // navigation
  void push(Screen* s, bool anim = true);
  void pop(bool anim = true);
  void replaceAll(Screen* s);             // new root (after setup / mode change)
  void rebuildAll() { _rebuild = true; }  // theme / text size changed (applied on next loop)
  int  depth() const { return (int)_stack.size(); }
  void openChat(const ConvKey& k);
  void openHome();                        // main tabs or easy home, per settings

  // feedback
  void toast(const char* icon, const char* text, uint32_t ms = 2200);
  void refreshStatus();

  Model* model() { return _m; }
  Sos& sos() { return _sos; }
  bool screenOn() const { return _power != PWR_OFF; }

private:
  void buildChrome();
  void showBanner(const LogRecord& r);
  void setPower(int p);
  void doRebuild();
  bool _rebuild = false;
  void reap();
  static void bannerClickCb(lv_event_t* e);

  Model* _m = nullptr;
  Sos _sos;
  lv_obj_t* _st_sos = nullptr;
  std::vector<Screen*> _stack;
  struct Dead { Screen* s; uint32_t at; };
  std::vector<Dead> _graveyard;           // screens whose LVGL objects are still animating out

  lv_obj_t* _status = nullptr;
  lv_obj_t* _st_time = nullptr;
  lv_obj_t* _st_icons = nullptr;
  lv_obj_t* _toast = nullptr;
  lv_obj_t* _toast_lbl = nullptr;
  uint32_t _toast_until = 0;
  lv_obj_t* _banner = nullptr;
  uint32_t _banner_until = 0;
  ConvKey _banner_key;

  enum { PWR_ON = 0, PWR_DIM = 1, PWR_OFF = 2 };
  int _power = PWR_ON;
  uint32_t _last_activity = 0;
  uint32_t _next_tick = 0, _next_status = 0;
  uint32_t _next_advert_ms = 0;
  bool _adverted_this_boot = false;
  void autoAdvert();
  void recordTrack();
  float _trk_lat = 0, _trk_lon = 0;
  uint32_t _trk_ms = 0;
};

// screen factories (ui/screens/*.cpp)
Screen* makeMainScreen(int tab = 0);
Screen* makeEasyHome();
Screen* makeSetupScreen();
Screen* makeChatScreen(const ConvKey& k);
Screen* makeContactScreen(const uint8_t* key);
Screen* makeGroupsScreen();
Screen* makeDeviceCheckScreen();
Screen* makeEasyListScreen(int which);   // 0 chats, 1 people (easy mode, full screen)
Screen* makeSosScreen();
Screen* makeMapScreen();
Screen* makeAdminScreen(const uint8_t* key);
Screen* makeSosAlarmScreen(const LogRecord& r);

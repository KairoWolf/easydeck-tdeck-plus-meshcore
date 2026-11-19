// Settings packs: ready-made settings for a region (radio preset + channels with their region
// scopes + local tips). Some are built in; more come from packs/ in the EasyDeck GitHub repo.
#include "Common.h"
#include "../SetupFile.h"
#include <functional>
#include <stdio.h>
#include <string>
#include <vector>

#define PACKS_URL "https://raw.githubusercontent.com/KairoWolf/easydeck-tdeck-plus-meshcore/main/packs/"

Screen* makeWifiScreen(std::function<void()> done, int target);

// "#test|au-nsw-tro" -> "#test  (au-nsw-tro)"
static std::string channelLine(const std::string& g) {
  size_t bar = g.find('|');
  std::string name = g.substr(0, bar), scope = bar == std::string::npos ? "" : g.substr(bar + 1);
  if (scope.empty()) return name;
  return name + "  \xC2\xB7  " + (scope == "none" ? std::string("no scope") : scope);
}

// What a pack changes, then Apply.
class PackDetailScreen : public HeaderScreen {
public:
  PackDetailScreen(const std::string& text, std::function<void()> applied) : _text(text), _applied(applied) {}
  void build() override {
    SetupFile::PackInfo p;
    SetupFile::describe(_text.c_str(), p);
    buildHeader(p.name.c_str());
    int h = contentH();
    _scroll = ui::box(content());
    lv_obj_set_size(_scroll, SCR_W, h - 44);
    lv_obj_add_flag(_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_scroll, LV_SCROLLBAR_MODE_OFF);
    std::string t;
    if (!p.about.empty()) t += p.about + "\n\n";
    if (!p.preset.empty()) t += "Radio preset: " + p.preset + "\n\n";
    if (!p.groups.empty()) {
      t += "Channels (joined, with their region scope):\n";
      for (auto& g : p.groups) t += "  " + channelLine(g) + "\n";
      t += "\n";
    }
    for (auto& n : p.notes) t += n + "\n";
    lv_obj_t* l = ui::label(_scroll, t.c_str(), T::small, T::text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, SCR_W - 24);
    lv_obj_set_pos(l, 12, 6);
    // Apply button
    lv_obj_t* b = ui::box(content());
    lv_obj_set_size(b, SCR_W - 24, 34);
    lv_obj_set_pos(b, 12, h - 40);
    lv_obj_set_style_radius(b, T::style() == STYLE_TACTICAL ? 0 : 17, 0);
    lv_obj_set_style_bg_color(b, T::accent, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { ((PackDetailScreen*)lv_event_get_user_data(e))->apply(); }, LV_EVENT_CLICKED, this);
    lv_obj_t* bl = ui::label(b, "Apply this pack", T::f16b, T::on_accent);
    lv_obj_center(bl);
  }
  void apply() {
    SetupFile::PackInfo p;
    SetupFile::describe(_text.c_str(), p);
    int n = SetupFile::applyText(m, _text.c_str());
    char b[64];
    snprintf(b, sizeof(b), n ? "%s applied" : "Nothing applied", p.name.c_str());
    auto cb = _applied;
    App* a = app;
    a->pop();
    a->toast(n ? IC_CHECK : IC_ERROR, b);
    if (n && cb) cb();
  }
  void onInput(const InputEvent& e) override {
    switch (e.type) {
      case IN_UP: lv_obj_scroll_by_bounded(_scroll, 0, 48, LV_ANIM_ON); break;
      case IN_DOWN: lv_obj_scroll_by_bounded(_scroll, 0, -48, LV_ANIM_ON); break;
      case IN_SELECT: case IN_ENTER: apply(); break;
      case IN_LEFT: case IN_BACKSPACE: app->pop(); break;
      default: break;
    }
  }
private:
  std::string _text;
  std::function<void()> _applied;
  lv_obj_t* _scroll = nullptr;
};

class PacksScreen : public HeaderScreen {
public:
  explicit PacksScreen(std::function<void()> applied) : _applied(applied) {}
  void build() override {
    buildHeader("Settings packs");
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_acts.size() && _acts[i]) _acts[i](); };
    fill();
  }

  void add(const char* ic, lv_color_t c, const char* t, const char* v, std::function<void()> fn) {
    _list.rowSetting(ic, c, t, v, fn != nullptr);
    _acts.push_back(fn);
  }
  void header(const char* t) { _list.addHeader(t); }   // not a row: no action slot

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _acts.clear();
    header("BUILT IN");
    for (int i = 0; i < SetupFile::builtinPackCount(); i++) {
      SetupFile::PackInfo p;
      if (!SetupFile::describe(SetupFile::builtinPack(i), p)) continue;
      add(IC_MAP, lv_color_hex(0x66BB6A), p.name.c_str(), p.about.c_str(), [this, i]() { open(SetupFile::builtinPack(i)); });
    }
    header("FROM GITHUB");
    for (size_t i = 0; i < _online.size(); i++) {
      add(IC_DOWNLOAD, lv_color_hex(0x42A5F5), _online[i].name.c_str(), _online[i].about.c_str(), [this, i]() {
        if (!wifiReady(app, m, Model::WIFI_FOR_PACKS)) return;
        _want = _online[i].id;
        std::string url = std::string(PACKS_URL) + _want + ".txt";
        if (m->netFetch(url.c_str())) { _busy = 2; fill(); } else app->toast(IC_ERROR, m->netFetchText());
      });
    }
    const char* ssid = m->wifiSsid();
    if (_busy) add(IC_REFRESH, T::muted, _busy == 1 ? "Getting the list..." : "Downloading pack...", ssid, nullptr);
    else add(IC_REFRESH, T::accent, _online.empty() ? "Get packs from GitHub" : "Refresh the list", *ssid ? ssid : "needs Wi-Fi", [this]() {
      if (!*m->wifiSsid()) { app->push(makeWifiScreen([this]() { getList(); }, Model::WIFI_FOR_PACKS)); return; }
      getList();
    });
    add(IC_WIFI, lv_color_hex(0x9AA4B0), "Wi-Fi", *ssid ? ssid : "Not set", [this]() { app->push(makeWifiScreen([this]() { fill(); }, Model::WIFI_FOR_PACKS)); });
    if (m->wifiMode()) add(IC_REFRESH, lv_color_hex(0x9AA4B0), "Done - restart with Bluetooth", "Wi-Fi mode is on", [this]() { m->restartNormal(); });
    if (sel >= 0 && sel < _list.count()) _list.select(sel, false); else _list.select(0, false);
  }

  void getList() {
    if (!wifiReady(app, m, Model::WIFI_FOR_PACKS)) return;
    std::string url = std::string(PACKS_URL) + "index.txt";
    if (m->netFetch(url.c_str())) _busy = 1; else app->toast(IC_ERROR, m->netFetchText());
    fill();
  }

  void open(const char* text) {
    auto cb = _applied;
    App* a = app;
    app->push(new PackDetailScreen(text, [a, cb]() { a->pop(); if (cb) cb(); }));   // also close this list
  }

  void tick() override {
    if (!_busy) return;
    int st = m->netFetchState();
    if (st == 1) return;
    int what = _busy;
    _busy = 0;
    if (st == 3) { app->toast(IC_ERROR, m->netFetchText()); fill(); return; }
    if (st != 2) { fill(); return; }
    std::string text = m->netFetchText();
    if (what == 1) {   // index: id|name|about
      _online.clear();
      size_t p = 0;
      while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t a = line.find('|'), b = a == std::string::npos ? a : line.find('|', a + 1);
        if (a == std::string::npos) continue;
        Entry en;
        en.id = line.substr(0, a);
        en.name = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        en.about = b == std::string::npos ? "" : line.substr(b + 1);
        bool ok = !en.id.empty() && en.id.find('/') == std::string::npos && en.id.find('.') == std::string::npos;
        if (ok && _online.size() < 30) _online.push_back(en);
      }
      if (_online.empty()) app->toast(IC_INFO, "No packs on GitHub yet");
      fill();
    } else {
      SetupFile::PackInfo pi;
      if (!SetupFile::describe(text.c_str(), pi)) { app->toast(IC_ERROR, "That pack couldn't be read"); fill(); return; }
      fill();
      open(text.c_str());
    }
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_BACKSPACE || e.type == IN_LEFT) app->pop();
  }

private:
  struct Entry { std::string id, name, about; };
  std::function<void()> _applied;
  FocusList _list;
  std::vector<std::function<void()>> _acts;
  std::vector<Entry> _online;
  std::string _want;
  int _busy = 0;   // 1 fetching the index, 2 fetching a pack
};

Screen* makePacksScreen(std::function<void()> applied) { return new PacksScreen(applied); }

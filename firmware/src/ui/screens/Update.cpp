// Software update: check GitHub for a newer EasyDeck over Wi-Fi, download it to the SD card
// (checksum verified), then hand over to the SD Launcher to install it.
#include "Common.h"
#include "core/Version.h"
#include <functional>
#include <stdio.h>
#include <vector>

Screen* makeWifiScreen(std::function<void()> done, int target);

class UpdateScreen : public HeaderScreen {
public:
  void build() override {
    buildHeader("Software update");
    int h = contentH();
    _list.create(content(), 0, 0, SCR_W, h - 40);
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_acts.size() && _acts[i]) _acts[i](); };
    _bar = lv_bar_create(content());
    lv_obj_set_size(_bar, SCR_W - 16, 6);
    lv_obj_set_pos(_bar, 8, h - 34);
    lv_obj_set_style_bg_color(_bar, T::surface2, 0);
    lv_obj_set_style_bg_color(_bar, T::accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_bar, T::style() == STYLE_TACTICAL ? 0 : 3, 0);
    lv_obj_set_style_radius(_bar, T::style() == STYLE_TACTICAL ? 0 : 3, LV_PART_INDICATOR);
    lv_bar_set_range(_bar, 0, 1000);
    _msg = ui::label(content(), "", T::f12, T::muted);
    ui::oneLine(_msg, SCR_W - 16);
    lv_obj_set_pos(_msg, 8, h - 24);
    fill();
  }

  void add(const char* ic, lv_color_t c, const char* t, const char* v, std::function<void()> fn, bool chevron = true) {
    _list.rowSetting(ic, c, t, v, chevron && fn != nullptr);
    _acts.push_back(fn);
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _acts.clear();
    UpdateStatus u = m->updateStatus();
    _state = u.state;
    char v[64];

    add(IC_INFO, lv_color_hex(0x9AA4B0), "Installed", "EasyDeck " EASYDECK_VERSION_STR, nullptr, false);
    add(IC_GLOBE, lv_color_hex(0x26C6DA), "Download via", m->useMirror() ? "mesh.nah.rocks" : "GitHub", [this]() {
      app->push(makeChoiceScreen("Download via", { { "mesh.nah.rocks (Sydney)", "fast in Australia; GitHub if it's down" }, { "GitHub", "" } },
        m->useMirror() ? 0 : 1, [this](int k) { m->setUseMirror(k == 0); fill(); },
        "Updates are always checked against the checksum published on GitHub."));
    });
    const char* ssid = m->wifiSsid();
    add(IC_WIFI, lv_color_hex(0x42A5F5), "Wi-Fi", *ssid ? ssid : "Not set", [this]() {
      app->push(makeWifiScreen([this]() { fill(); }, Model::WIFI_FOR_UPDATE));
    });

    switch (u.state) {
      case UPD_CHECKING:
      case UPD_DOWNLOADING:
        add(IC_X, T::danger, u.state == UPD_CHECKING ? "Cancel check" : "Cancel download", "", [this]() { m->updateCancel(); fill(); }, false);
        break;
      case UPD_AVAILABLE:
        snprintf(v, sizeof(v), "%.1f MB", m->updateStatus().size / 1048576.0);
        { char t[40]; snprintf(t, sizeof(t), "Download %s", u.version);
          add(IC_DOWNLOAD, T::accent, t, v, [this]() {
            if (!wifiReady(app, m, Model::WIFI_FOR_UPDATE)) return;
            if (!m->updateDownload()) app->toast(IC_ERROR, m->updateStatus().msg);
            fill();
          }, false); }
        if (u.launcher) add(IC_INFO, lv_color_hex(0x9AA4B0), "Installed via the Launcher", "", nullptr, false);
        if (u.notes[0]) {
          char t[40]; snprintf(t, sizeof(t), "What's new in %s", u.version);
          add(IC_INFO, lv_color_hex(0xBA68C8), t, "", [this]() {
            UpdateStatus s = m->updateStatus();
            char title[32]; snprintf(title, sizeof(title), "EasyDeck %s", s.version);
            app->push(makeInfoScreen(title, s.notes));
          });
        }
        break;
      case UPD_READY:
        if (u.launcher) {
          add(IC_CHECK, T::success, "Restart to install", "in the Launcher", [this]() {
            UpdateStatus s = m->updateStatus();
            char q[320];
            snprintf(q, sizeof(q), "It isn't installed yet. EasyDeck restarts into the Launcher; there:\n"
                     "1. open the SD card\n2. choose %s\n3. press Install\n"
                     "Contacts and settings carry over.", s.file + 1);
            app->push(makeConfirmScreen("Install update", q, "Restart now", false, [this]() { m->updateInstall(); }));
          }, false);
        } else {
          add(IC_CHECK, T::success, "Saved to the SD card", u.file + 1, [this]() {
            UpdateStatus s = m->updateStatus();
            char t[200];
            snprintf(t, sizeof(t), "%s is on the SD card. Install it with the SD Launcher, or flash it over USB at 0x0. "
                     "Your contacts and settings carry over.", s.file + 1);
            app->push(makeInfoScreen("Install update", t));
          });
        }
        break;
      default:   // idle / latest / failed
        add(IC_REFRESH, T::accent, "Check for update", "", [this]() {
          if (!wifiReady(app, m, Model::WIFI_FOR_UPDATE)) return;
          if (!*m->wifiSsid()) { app->push(makeWifiScreen([this]() { m->updateCheck(); fill(); }, Model::WIFI_FOR_UPDATE)); return; }
          if (!m->updateCheck()) app->toast(IC_ERROR, m->updateStatus().msg);
          fill();
        }, false);
        break;
    }
    if (m->wifiMode())
      add(IC_REFRESH, lv_color_hex(0x9AA4B0), "Done - restart with Bluetooth", "Wi-Fi mode is on", [this]() { m->restartNormal(); }, false);
    if (sel >= 0 && sel < _list.count()) _list.select(sel, false);
    else _list.select(u.state == UPD_IDLE ? 2 : _list.count() - 1, false);
    tick();
  }

  void tick() override {
    UpdateStatus u = m->updateStatus();
    if (u.state != _state) { fill(); return; }
    int per = u.size ? (int)((uint64_t)u.done * 1000 / u.size) : 0;
    lv_bar_set_value(_bar, u.state == UPD_DOWNLOADING || u.state == UPD_READY ? (u.state == UPD_READY ? 1000 : per) : 0, LV_ANIM_OFF);
    char b[96];
    if (u.state == UPD_DOWNLOADING && u.done)
      snprintf(b, sizeof(b), "%d%%  \xC2\xB7  %.1f of %.1f MB", per / 10, u.done / 1048576.0, u.size / 1048576.0);
    else if (u.state == UPD_IDLE) snprintf(b, sizeof(b), "New versions from GitHub, via mesh.nah.rocks");
    else snprintf(b, sizeof(b), "%s", u.msg);
    lv_label_set_text(_msg, b);
    lv_obj_set_style_text_color(_msg, u.state == UPD_FAILED ? T::danger :
                                (u.state == UPD_READY || u.state == UPD_LATEST || u.state == UPD_AVAILABLE) ? T::success : T::muted, 0);
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_BACKSPACE || e.type == IN_LEFT) app->pop();
  }

private:
  FocusList _list;
  std::vector<std::function<void()>> _acts;
  lv_obj_t *_bar = nullptr, *_msg = nullptr;
  uint8_t _state = 0xFF;
};

Screen* makeUpdateScreen() { return new UpdateScreen(); }

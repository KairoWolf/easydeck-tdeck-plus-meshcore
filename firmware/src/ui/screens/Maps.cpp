// Offline maps: pick a Wi-Fi network and a tile source, then download the map for an area
// around you (or around the spot you were looking at on the map) straight to the SD card.
#include "Common.h"
#include "../MapView.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static const int kRadiusKm[] = { 5, 10, 25, 50, 100 };
static const struct { int zmax; const char* name; } kDetail[] = {
  { 11, "Region" }, { 13, "Towns" }, { 14, "Suburbs" }, { 15, "Streets" }, { 16, "Detailed streets" },
};
#define MAP_ZMIN 6
#define BYTES_PER_TILE 15000UL   // typical raster tile
#define MAX_TILES 40000UL        // ~600 MB: beyond this, ask for a smaller area

// remembered for the session
static int s_radius = 1;   // 10 km
static int s_detail = 3;   // streets

static const char* sourceName(const char* url, char* buf, int n) {
  if (!url || !*url) return "Not set";
  if (strstr(url, "thunderforest.com/outdoors")) return "Thunderforest Outdoors";
  if (strstr(url, "thunderforest.com/landscape")) return "Thunderforest Landscape";
  if (strstr(url, "thunderforest.com/atlas")) return "Thunderforest Atlas";
  const char* h = strstr(url, "://");
  h = h ? h + 3 : url;
  int i = 0;
  while (h[i] && h[i] != '/' && i < n - 1) { buf[i] = h[i]; i++; }
  buf[i] = 0;
  return buf;
}

// ---------------------------------------------------------------- Wi-Fi picker

class WifiScreen : public HeaderScreen {
public:
  explicit WifiScreen(std::function<void()> done) : _done(done) {}
  void build() override {
    buildHeader("Wi-Fi");
    _list.create(content(), 0, 0, SCR_W, contentH());
    _list.onActivate = [this](int i) { pick(i); };
    scan();
  }
  void scan() {
    _nets.clear();
    _list.clear();
    if (m->wifiScanStart()) { _scanning = true; _list.setEmpty(IC_WIFI, "Looking for Wi-Fi...", "This takes a few seconds"); }
    else { _scanning = false; _list.setEmpty(IC_ERROR, "Wi-Fi isn't available right now", "Try again after the download, or turn Bluetooth off"); }
  }
  void tick() override {
    if (!_scanning) return;
    WifiNet buf[16];
    int n = m->wifiScanResults(buf, 16);
    if (n < 0) return;
    _scanning = false;
    _nets.assign(buf, buf + n);
    _list.clear();
    if (n == 0) _list.addHeader("NO NETWORKS FOUND");
    for (auto& w : _nets) {
      char sub[32];
      int bars = w.rssi > -55 ? 4 : w.rssi > -67 ? 3 : w.rssi > -78 ? 2 : 1;
      snprintf(sub, sizeof(sub), "%s \xC2\xB7 signal %d/4", w.secure ? "password" : "open", bars);
      _list.rowSetting(IC_WIFI, T::accent, w.ssid, sub, false);
    }
    _list.rowSetting(IC_REFRESH, lv_color_hex(0x9AA4B0), "Search again", "", false);
    _list.select(0, false);
  }
  void pick(int i) {
    if (_scanning) return;
    if (i == (int)_nets.size()) { scan(); return; }
    if (i < 0 || i >= (int)_nets.size()) return;
    WifiNet w = _nets[i];
    std::function<void()> done = _done;
    Model* mm = m;
    App* ap = app;
    if (!w.secure) { mm->setWifi(w.ssid, ""); ap->pop(); if (done) done(); return; }
    std::string ssid = w.ssid;
    app->push(makeTextEntryScreen("Wi-Fi password", ssid.c_str(), "", 64, [mm, ap, ssid, done](const char* pw) {
      mm->setWifi(ssid.c_str(), pw);
      ap->pop();   // the Wi-Fi list (the text entry pops itself)
      ap->toast(IC_WIFI, "Wi-Fi saved");
      if (done) done();
    }, "password"));
  }
  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_BACKSPACE || e.type == IN_LEFT) app->pop();
  }
  void onHide() override { if (_scanning) { m->wifiScanStop(); _scanning = false; } }   // don't leave Wi-Fi on
private:
  std::function<void()> _done;
  FocusList _list;
  std::vector<WifiNet> _nets;
  bool _scanning = false;
};

// ---------------------------------------------------------------- offline maps

class OfflineMapsScreen : public HeaderScreen {
public:
  OfflineMapsScreen() {}
  OfflineMapsScreen(double lat, double lon) : _have_center(true), _clat(lat), _clon(lon), _from_map(true) {}

  void build() override {
    buildHeader("Offline maps");
    int h = contentH();
    _list.create(content(), 0, 0, SCR_W, h - 40);
    _list.onActivate = [this](int i) { if (i >= 0 && i < (int)_acts.size() && _acts[i]) _acts[i](); };
    // progress strip
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

  bool center(double& lat, double& lon) {
    if (_have_center) { lat = _clat; lon = _clon; return true; }
    DeviceStatus st = m->status();
    if (st.gps_fix) { lat = st.lat; lon = st.lon; return true; }
    return false;
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _acts.clear();
    TileDlStatus d = m->tileDownloadStatus();
    bool busy = d.state == DL_CONNECTING || d.state == DL_RUNNING;
    char v[64], b[48];

    const char* ssid = m->wifiSsid();
    add(IC_WIFI, lv_color_hex(0x42A5F5), "Wi-Fi", *ssid ? ssid : "Not set", [this]() {
      app->push(new WifiScreen([this]() { fill(); }));
    });
    add(IC_MAP, lv_color_hex(0x66BB6A), "Map source", sourceName(m->tileUrl(), b, sizeof(b)), [this]() { chooseSource(); });

    double lat, lon;
    bool have = center(lat, lon);
    snprintf(v, sizeof(v), "%d km \xC2\xB7 %s", kRadiusKm[s_radius], _from_map ? "map spot" : (have ? "around you" : "no GPS fix"));
    add(IC_CROSSHAIR, lv_color_hex(0xFF8A65), "Area", v, [this]() {
      std::vector<ChoiceItem> it;
      for (int r : kRadiusKm) { char t[24]; snprintf(t, sizeof(t), "%d km", r); it.push_back({ t, "" }); }
      app->push(makeChoiceScreen("Area", it, s_radius, [this](int i) { s_radius = i; fill(); },
                                 "How far around the centre should the map reach?"));
    });
    add(IC_ZOOM_IN, lv_color_hex(0xBA68C8), "Detail", kDetail[s_detail].name, [this]() {
      std::vector<ChoiceItem> it;
      for (auto& k : kDetail) { char t[24]; snprintf(t, sizeof(t), "up to zoom %d", k.zmax); it.push_back({ k.name, t }); }
      app->push(makeChoiceScreen("Detail", it, s_detail, [this](int i) { s_detail = i; fill(); },
                                 "More detail means many more tiles and a longer download."));
    });

    uint32_t tiles = have ? mapv::tilesInArea(lat, lon, kRadiusKm[s_radius], MAP_ZMIN, kDetail[s_detail].zmax) : 0;
    _tiles = tiles;
    if (have) {
      unsigned long mb = (unsigned long)((tiles * BYTES_PER_TILE + 999999UL) / 1000000UL);
      snprintf(v, sizeof(v), "%lu tiles \xC2\xB7 about %lu MB", (unsigned long)tiles, mb);
    } else snprintf(v, sizeof(v), "needs a GPS fix (or open from the map)");
    add(IC_INFO, lv_color_hex(0x9AA4B0), "Size", v, nullptr, false);

    if (busy) add(IC_X, T::danger, "Cancel download", "", [this]() { m->tileDownloadCancel(); fill(); }, false);
    else add(IC_DOWNLOAD, T::accent, "Download now", "", [this]() { startDownload(); }, false);
    if (sel >= 0) _list.select(sel < _list.count() ? sel : _list.count() - 1, false);
    else _list.select(0, false);
    _busy = busy;
    tick();
  }

  void add(const char* ic, lv_color_t c, const char* t, const char* v, std::function<void()> fn, bool chevron = true) {
    _list.rowSetting(ic, c, t, v, chevron && fn != nullptr);
    _acts.push_back(fn);
  }

  void chooseSource() {
    std::vector<ChoiceItem> it = {
      { "Thunderforest Outdoors", "trails & contours \xC2\xB7 free API key" },
      { "Thunderforest Landscape", "terrain \xC2\xB7 free API key" },
      { "Custom URL", "any {z}/{x}/{y}.png tile server" },
      { "None", "use tiles you copy to the SD card" },
    };
    app->push(makeChoiceScreen("Map source", it, -1, [this](int i) {
      static const char* styles[] = { "outdoors", "landscape" };
      if (i <= 1) {
        std::string style = styles[i];
        app->push(makeTextEntryScreen("API key", "Free key from thunderforest.com (Hobby plan). Check the provider's terms for offline use.",
                                      "", 40, [this, style](const char* key) {
          char url[192];
          snprintf(url, sizeof(url), "https://tile.thunderforest.com/%s/{z}/{x}/{y}.png?apikey=%s", style.c_str(), key);
          m->setTileUrl(url);
          fill();
        }, "api key"));
      } else if (i == 2) {
        app->push(makeTextEntryScreen("Tile URL", "Use {z} {x} {y}. PNG tiles only. Or set map_url= in easydeck-setup.txt.",
                                      m->tileUrl(), 190, [this](const char* url) { m->setTileUrl(url); fill(); },
                                      "https://host/{z}/{x}/{y}.png"));
      } else { m->setTileUrl(""); fill(); }
    }, "Where map tiles are downloaded from. OpenStreetMap's own servers don't allow downloading areas for offline use."));
  }

  void startDownload() {
    double lat, lon;
    if (!center(lat, lon)) { app->toast(IC_SATELLITE, "No GPS fix: open the map, move to your area, press D"); return; }
    if (_tiles > MAX_TILES) { app->toast(IC_ALERT, "Too big: choose a smaller area or less detail"); return; }
    if (!m->tileDownloadStart(lat, lon, kRadiusKm[s_radius], MAP_ZMIN, kDetail[s_detail].zmax)) {
      app->toast(IC_ERROR, m->tileDownloadStatus().msg);
    }
    fill();
  }

  void tick() override {
    TileDlStatus d = m->tileDownloadStatus();
    bool busy = d.state == DL_CONNECTING || d.state == DL_RUNNING;
    if (busy != _busy) { fill(); return; }   // started/finished: swap the Download/Cancel row
    int per = d.total ? (int)((uint64_t)d.done * 1000 / d.total) : 0;
    lv_bar_set_value(_bar, d.state == DL_IDLE ? 0 : per, LV_ANIM_OFF);
    char b[96];
    if (d.state == DL_RUNNING)
      snprintf(b, sizeof(b), "%lu / %lu tiles \xC2\xB7 %lu MB", (unsigned long)d.done, (unsigned long)d.total,
               (unsigned long)(d.bytes / 1000000UL));
    else if (d.state == DL_IDLE) snprintf(b, sizeof(b), "%s", m->hasSd() ? "Tiles are saved to the SD card (/tiles)" : "Insert an SD card to store maps");
    else snprintf(b, sizeof(b), "%s", d.msg);
    lv_label_set_text(_msg, b);
    lv_obj_set_style_text_color(_msg, d.state == DL_FAILED ? T::danger : (d.state == DL_DONE ? T::success : T::muted), 0);
  }

  void onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return;
    if (e.type == IN_BACKSPACE || e.type == IN_LEFT) app->pop();
  }

private:
  bool _have_center = false;
  double _clat = 0, _clon = 0;
  bool _from_map = false;
  bool _busy = false;
  uint32_t _tiles = 0;
  FocusList _list;
  std::vector<std::function<void()>> _acts;
  lv_obj_t* _bar = nullptr;
  lv_obj_t* _msg = nullptr;
};

Screen* makeOfflineMapsScreen() { return new OfflineMapsScreen(); }
Screen* makeOfflineMapsScreenAt(double lat, double lon) { return new OfflineMapsScreen(lat, lon); }

#include "TileDownloader.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "store/Storage.h"
#include "core/Version.h"
#include "ui/MapView.h"

TileDownloader tile_downloader;

#define NET_FILE   "/easydeck_net"
#define NET_MAGIC  0x4E455431UL   // "NET1"
#define MAX_TILE_BYTES (256 * 1024)
#define IN_FLIGHT  2
#define MAX_FAIL_STREAK 12
#define MIN_INTERNAL_FREE 60000   // Wi-Fi + TLS buffers come from internal RAM

void TileDownloader::begin() {
  Cfg c;
  if (safeLoad(DataFS, NET_FILE, NET_MAGIC, &c, sizeof(c))) {
    c.ssid[sizeof(c.ssid) - 1] = 0; c.pass[sizeof(c.pass) - 1] = 0; c.url[sizeof(c.url) - 1] = 0;
    _cfg = c;
  }
  _reqq = xQueueCreate(4, sizeof(Req));
  _resq = xQueueCreate(4, sizeof(Res));
}

void TileDownloader::save() { safeSave(DataFS, NET_FILE, NET_MAGIC, &_cfg, sizeof(_cfg)); }

void TileDownloader::setWifi(const char* ssid, const char* pass) {
  snprintf(_cfg.ssid, sizeof(_cfg.ssid), "%s", ssid ? ssid : "");
  if (pass) snprintf(_cfg.pass, sizeof(_cfg.pass), "%s", pass);
  save();
}

void TileDownloader::setUrl(const char* url) {
  snprintf(_cfg.url, sizeof(_cfg.url), "%s", url ? url : "");
  save();
}

// ---------------------------------------------------------------- Wi-Fi scan

bool TileDownloader::scanStart() {
  if (_running) return false;   // busy downloading
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE / 2) return false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  _scanning = WiFi.scanNetworks(true /*async*/, false) == WIFI_SCAN_RUNNING;
  if (!_scanning) stopWifi();
  return _scanning;
}

int TileDownloader::scanResults(WifiNet* out, int max) {
  if (!_scanning) return 0;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < k && !dup; j++) dup = strcmp(out[j].ssid, s.c_str()) == 0;
    if (dup) continue;
    snprintf(out[k].ssid, sizeof(out[k].ssid), "%s", s.c_str());
    out[k].rssi = WiFi.RSSI(i);
    out[k].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    k++;
  }
  for (int a = 0; a < k; a++)   // strongest first
    for (int b = a + 1; b < k; b++)
      if (out[b].rssi > out[a].rssi) { WifiNet t = out[a]; out[a] = out[b]; out[b] = t; }
  WiFi.scanDelete();
  _scanning = false;
  if (!_running) stopWifi();
  return k;
}

void TileDownloader::scanStop() {
  if (!_scanning) return;
  WiFi.scanDelete();
  _scanning = false;
  if (!_running) stopWifi();
}

void TileDownloader::stopWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ---------------------------------------------------------------- download control

bool TileDownloader::start(double lat, double lon, double radius_km, int zmin, int zmax) {
  if (_running) return false;
  _st = TileDlStatus();
  if (!_cfg.ssid[0]) { finish(DL_FAILED, "Choose a Wi-Fi network first"); return false; }
  if (!strstr(_cfg.url, "{z}") || !strstr(_cfg.url, "{x}") || !strstr(_cfg.url, "{y}")) {
    finish(DL_FAILED, "Set a map source first"); return false;
  }
  if (!sdPresent()) { finish(DL_FAILED, "Insert an SD card for the map"); return false; }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE) {
    finish(DL_FAILED, "Not enough memory: turn Bluetooth off and retry"); return false;
  }
  _lat = lat; _lon = lon; _r = radius_km; _zmin = zmin; _zmax = zmax;
  _st.total = mapv::tilesInArea(lat, lon, radius_km, zmin, zmax);
  _z = zmin;
  setRange();
  _cursor_done = false;
  _in_flight = 0;
  _fail_streak = 0;
  _cancel = false; _connected = false; _net_failed = false; _net_msg[0] = 0;
  _last_dir[0] = 0;
  xQueueReset((QueueHandle_t)_reqq);
  _st.state = DL_CONNECTING;
  snprintf(_st.msg, sizeof(_st.msg), "Connecting to %s...", _cfg.ssid);
  _running = true;   // set before the task exists: it clears this itself when it ends
  if (xTaskCreatePinnedToCore(taskMain, "tiles", 10240, this, 1, nullptr, 0) != pdPASS) {
    _running = false;
    finish(DL_FAILED, "Couldn't start the download");
    return false;
  }
  _wifi_ms = millis();
  return true;
}

void TileDownloader::cancel() {
  if (_running) finish(DL_CANCELLED, "Download cancelled");
}

void TileDownloader::finish(uint8_t state, const char* msg) {
  _st.state = state;
  snprintf(_st.msg, sizeof(_st.msg), "%s", msg);
  _cancel = true;
  if (_reqq) { Req stop = { 255, 0, 0 }; xQueueSend((QueueHandle_t)_reqq, &stop, 0); }
}

// ---------------------------------------------------------------- loop task side

bool TileDownloader::tileOnSd(int z, uint32_t x, uint32_t y) {
  char p[48];
  snprintf(p, sizeof(p), "/tiles/%d/%lu/%lu.png", z, (unsigned long)x, (unsigned long)y);
  return SD.exists(p);
}

void TileDownloader::setRange() {
  mapv::TileRange tr = mapv::tileRange(_lat, _lon, _r, _z);
  _x0 = tr.x0; _x1 = tr.x1; _y0 = tr.y0; _y1 = tr.y1;
  _x = _x0; _y = _y0;
}

// Walks zoom by zoom, column by column, row by row over the area.
bool TileDownloader::nextTile(Req& r) {
  int budget = 12;   // SD existence checks per call (keeps each loop pass short)
  while (budget-- > 0) {
    if (_y > _y1) { _y = _y0; _x++; }
    if (_x > _x1) {
      if (++_z > _zmax) { _cursor_done = true; return false; }
      setRange();
    }
    uint32_t x = _x, y = _y++;
    if (tileOnSd(_z, x, y)) { _st.skipped++; _st.done++; continue; }
    r.z = (uint8_t)_z; r.x = x; r.y = y;
    return true;
  }
  return false;   // out of budget this pass, carry on next time
}

static bool pngOk(const uint8_t* d, uint32_t n) {
  return n > 26 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G' && d[24] <= 8;   // LVGL: <= 8 bit/ch
}

bool TileDownloader::writeTile(const Res& r) {
  char dir[32], p[48];
  snprintf(dir, sizeof(dir), "/tiles/%u/%lu", r.z, (unsigned long)r.x);
  if (strcmp(dir, _last_dir) != 0) {
    char zdir[16];
    snprintf(zdir, sizeof(zdir), "/tiles/%u", r.z);
    SD.mkdir("/tiles");
    SD.mkdir(zdir);
    SD.mkdir(dir);
    snprintf(_last_dir, sizeof(_last_dir), "%s", dir);
  }
  snprintf(p, sizeof(p), "%s/%lu.png", dir, (unsigned long)r.y);
  File f = SD.open(p, FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(r.data, r.len) == r.len;
  f.close();
  if (!ok) SD.remove(p);
  return ok;
}

void TileDownloader::loop() {
  // results that arrive after a cancel just get freed
  if (_resq && _st.state != DL_RUNNING) {
    Res r;
    while (xQueueReceive((QueueHandle_t)_resq, &r, 0) == pdTRUE) free(r.data);
  }
  if (!_running) {   // the network task ended on its own (wrong password, network not found...)
    if (_st.state == DL_CONNECTING || _st.state == DL_RUNNING)
      finish(DL_FAILED, _net_failed ? _net_msg : "Download stopped");
    return;
  }

  if (_st.state == DL_CONNECTING) {
    if (_net_failed) { finish(DL_FAILED, _net_msg); return; }
    if (_connected) { _st.state = DL_RUNNING; snprintf(_st.msg, sizeof(_st.msg), "Downloading..."); }
    return;
  }
  if (_st.state != DL_RUNNING) return;
  if (_net_failed) { finish(DL_FAILED, _net_msg); return; }

  Res r;
  if (xQueueReceive((QueueHandle_t)_resq, &r, 0) == pdTRUE) {   // at most one SD write per pass
    if (_in_flight) _in_flight--;
    _st.done++;
    bool png = r.data && pngOk(r.data, r.len);
    if (png && writeTile(r)) { _st.fetched++; _st.bytes += r.len; _fail_streak = 0; }
    else if (png) {   // the tile was fine: the SD card refused it
      _st.failed++;
      free(r.data);
      finish(DL_FAILED, "Couldn't write to the SD card (full or read-only?)");
      return;
    }
    else {
      _st.failed++;
      if (++_fail_streak >= MAX_FAIL_STREAK) {
        free(r.data);
        char m[64];
        if (r.http == 401 || r.http == 403) snprintf(m, sizeof(m), "Map server refused (%d): check the API key", r.http);
        else if (r.http > 0 && r.http != 200) snprintf(m, sizeof(m), "Map server error %d: check the URL", r.http);
        else if (r.data) snprintf(m, sizeof(m), "Tiles aren't 8-bit PNG: choose another source");
        else snprintf(m, sizeof(m), "Downloads keep failing: check Wi-Fi");
        _fail_streak = 0;
        finish(DL_FAILED, m);
        return;
      }
    }
    free(r.data);
  }
  while (_in_flight < IN_FLIGHT && !_cursor_done) {
    Req q;
    if (!nextTile(q)) break;
    if (xQueueSend((QueueHandle_t)_reqq, &q, 0) != pdTRUE) {   // shouldn't happen (queue 4 > 2 in flight)
      _st.failed++; _st.done++;
      break;
    }
    _in_flight++;
  }
  if (_cursor_done && _in_flight == 0) {
    char m[64];
    snprintf(m, sizeof(m), "Done: %lu new, %lu already there%s", (unsigned long)_st.fetched, (unsigned long)_st.skipped,
             _st.failed ? ", some failed" : "");
    finish(DL_DONE, m);
  }
}

// ---------------------------------------------------------------- network task

static void buildUrl(const char* tpl, int z, uint32_t x, uint32_t y, char* out, size_t n) {
  size_t o = 0;
  for (const char* p = tpl; *p && o + 12 < n; ) {
    if (p[0] == '{' && p[2] == '}' && (p[1] == 'z' || p[1] == 'x' || p[1] == 'y')) {
      unsigned long v = p[1] == 'z' ? (unsigned long)z : p[1] == 'x' ? (unsigned long)x : (unsigned long)y;
      o += snprintf(out + o, n - o, "%lu", v);
      p += 3;
    } else out[o++] = *p++;
  }
  out[o] = 0;
}

void TileDownloader::taskMain(void* arg) {
  TileDownloader* self = (TileDownloader*)arg;
  WiFi.mode(WIFI_STA);
  WiFi.begin(self->_cfg.ssid, self->_cfg.pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && !self->_cancel) {
    if (millis() - t0 > 20000) {
      wl_status_t s = WiFi.status();
      snprintf(self->_net_msg, sizeof(self->_net_msg), s == WL_NO_SSID_AVAIL ? "Wi-Fi network not found" :
               s == WL_CONNECT_FAILED ? "Wi-Fi password rejected" : "Couldn't connect to Wi-Fi");
      self->_net_failed = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (!self->_net_failed && !self->_cancel) {
    self->_connected = true;
    bool https = strncmp(self->_cfg.url, "https", 5) == 0;
    WiFiClientSecure* sec = https ? new WiFiClientSecure() : nullptr;
    WiFiClient* plain = https ? nullptr : new WiFiClient();
    if (sec) sec->setInsecure();   // public map tiles; PNG content is validated before it's stored
    {   // HTTPClient's destructor calls _client->stop(): it must go before the clients are deleted
    HTTPClient http;
    http.setReuse(true);           // keep-alive: one TLS handshake for many tiles
    http.setTimeout(15000);
    http.setUserAgent("EasyDeck/" EASYDECK_VERSION_STR " (LilyGO T-Deck offline maps)");
    char url[224];
    while (!self->_cancel) {
      Req q;
      if (xQueueReceive((QueueHandle_t)self->_reqq, &q, pdMS_TO_TICKS(500)) != pdTRUE) continue;
      if (q.z == 255 || self->_cancel) break;
      if (WiFi.status() != WL_CONNECTED) {
        snprintf(self->_net_msg, sizeof(self->_net_msg), "Wi-Fi connection lost");
        self->_net_failed = true;
        break;
      }
      buildUrl(self->_cfg.url, q.z, q.x, q.y, url, sizeof(url));
      Res r = { q.z, q.x, q.y, nullptr, 0, 0 };
      bool ok = https ? http.begin(*sec, url) : http.begin(*plain, url);
      if (ok) {
        r.http = http.GET();
        if (r.http == 200) {
          int len = http.getSize();
          if (len > 0 && len <= MAX_TILE_BYTES) {
            r.data = (uint8_t*)ps_malloc(len);
            WiFiClient* s = http.getStreamPtr();
            uint32_t got = 0, ts = millis();
            while (r.data && got < (uint32_t)len && millis() - ts < 15000 && http.connected()) {
              int a = s->available();
              if (a > 0) { got += s->readBytes(r.data + got, min((uint32_t)a, (uint32_t)len - got)); ts = millis(); }
              else vTaskDelay(pdMS_TO_TICKS(5));
            }
            if (got == (uint32_t)len) r.len = got; else { free(r.data); r.data = nullptr; }
          } else if (len < 0) {   // chunked: let HTTPClient collect it (large Strings land in PSRAM)
            String body = http.getString();
            if (body.length() > 0 && body.length() <= MAX_TILE_BYTES) {
              r.data = (uint8_t*)ps_malloc(body.length());
              if (r.data) { memcpy(r.data, body.c_str(), body.length()); r.len = body.length(); }
            }
          }
        }
        http.end();
      }
      if (xQueueSend((QueueHandle_t)self->_resq, &r, pdMS_TO_TICKS(2000)) != pdTRUE) free(r.data);
      vTaskDelay(pdMS_TO_TICKS(40));   // be gentle with the tile server
    }
    }   // http destroyed here
    delete sec;
    delete plain;
  }
  self->stopWifi();
  self->_running = false;
  vTaskDelete(NULL);
}

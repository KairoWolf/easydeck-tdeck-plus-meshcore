#include "Updater.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "TileDownloader.h"
#include "GithubCerts.h"
#include "store/Storage.h"
#include "core/Version.h"
#include "../TDeckPlusBoard.h"

extern TDeckPlusBoard board;
Updater updater;

#define RELEASES_URL "https://api.github.com/repos/KairoWolf/easydeck-tdeck-plus-meshcore/releases?per_page=1"
#define PART_FILE    "/EasyDeck-update.part"
#define CHUNK        8192
#define MAX_FW       (6 * 1024 * 1024)   // our app slot
#define MIN_INTERNAL_FREE 60000          // Wi-Fi + TLS buffers come from internal RAM
#define USER_AGENT   "EasyDeck/" EASYDECK_VERSION_STR " (LilyGO T-Deck update check)"

void Updater::begin() {
  _q = xQueueCreate(4, sizeof(Chunk));
  _st.launcher = board.isRunningUnderLauncher();
}

// ---------------------------------------------------------------- helpers

static int verCmp(const char* a, const char* b) {   // "0.48.0" vs "0.47.0"
  int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
  if (*a == 'v') a++;
  if (*b == 'v') b++;
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

// First "### New in ..." section of the release notes as plain text lines.
static void notesFrom(const char* body, char* out, size_t n) {
  out[0] = 0;
  const char* s = strstr(body, "### New in");
  if (!s) return;
  s = strchr(s, '\n');
  if (!s) return;
  size_t o = 0;
  while (*s && o + 2 < n) {
    while (*s == '\n' || *s == '\r') s++;
    if (!*s || (s[0] == '#' && s[1] == '#')) break;   // next section
    if (s[0] == '-' && s[1] == ' ') s += 2;
    const char* e = strchr(s, '\n');
    if (!e) e = s + strlen(s);
    for (const char* c = s; c < e && o + 2 < n; c++) {
      if (*c == '*' || *c == '\r') continue;           // markdown bold
      out[o++] = *c;
    }
    out[o++] = '\n';
    s = e;
  }
  if (o) o--;   // drop the last newline
  out[o] = 0;
}

bool Updater::connectWifi() {
  const char* ssid = tile_downloader.ssid();
  if (!ssid[0]) { snprintf(_net_msg, sizeof(_net_msg), "Choose a Wi-Fi network first"); return false; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, tile_downloader.pass());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && !_cancel) {
    if (millis() - t0 > 20000) {
      wl_status_t s = WiFi.status();
      snprintf(_net_msg, sizeof(_net_msg), s == WL_NO_SSID_AVAIL ? "Wi-Fi network not found" :
               s == WL_CONNECT_FAILED ? "Wi-Fi password rejected" : "Couldn't connect to Wi-Fi");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  return !_cancel;
}

// ---------------------------------------------------------------- control (loop task)

static bool startTask(Updater* u, TaskFunction_t fn) {
  return xTaskCreatePinnedToCore(fn, "update", 12288, u, 1, nullptr, 0) == pdPASS;
}

bool Updater::check() {
  if (_running) return false;
  if (tile_downloader.busy()) { fail("Wi-Fi is busy with a map download"); return false; }
  if (!tile_downloader.ssid()[0]) { fail("Choose a Wi-Fi network first"); return false; }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE) { fail("Not enough memory: turn Bluetooth off and retry"); return false; }
  bool launcher = _st.launcher;
  _st = UpdateStatus();
  _st.launcher = launcher;
  _st.state = UPD_CHECKING;
  snprintf(_st.msg, sizeof(_st.msg), "Connecting to %s...", tile_downloader.ssid());
  _mode = 1; _cancel = false; _net_done = false; _net_ok = false; _net_msg[0] = 0;
  _running = true;
  if (!startTask(this, taskMain)) { _running = false; fail("Couldn't start the update check"); return false; }
  return true;
}

bool Updater::download() {
  if (_running || _st.state != UPD_AVAILABLE || !_url[0]) return false;
  if (tile_downloader.busy()) { fail("Wi-Fi is busy with a map download"); return false; }
  if (!sdPresent()) { fail("Insert the SD card first"); return false; }
  uint64_t free_b = SD.totalBytes() - SD.usedBytes();
  if (free_b < (uint64_t)_st.size + 1024 * 1024) { fail("Not enough space on the SD card"); return false; }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE) { fail("Not enough memory: turn Bluetooth off and retry"); return false; }
  SD.remove(PART_FILE);
  File* f = new File(SD.open(PART_FILE, FILE_WRITE));
  if (!*f) { delete f; fail("Couldn't write to the SD card"); return false; }
  _file = f;
  _written = 0;
  xQueueReset((QueueHandle_t)_q);
  _st.state = UPD_DOWNLOADING;
  _st.done = 0;
  snprintf(_st.msg, sizeof(_st.msg), "Connecting to %s...", tile_downloader.ssid());
  _mode = 2; _cancel = false; _net_done = false; _net_ok = false; _net_msg[0] = 0;
  _running = true;
  if (!startTask(this, taskMain)) { _running = false; fail("Couldn't start the download"); return false; }
  return true;
}

bool Updater::fetch(const char* url) {
  if (_running) return false;
  if (tile_downloader.busy()) { _fstate = 3; snprintf(_fmsg, sizeof(_fmsg), "Wi-Fi is busy with a map download"); return false; }
  if (!tile_downloader.ssid()[0]) { _fstate = 3; snprintf(_fmsg, sizeof(_fmsg), "Choose a Wi-Fi network first"); return false; }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE) { _fstate = 3; snprintf(_fmsg, sizeof(_fmsg), "Not enough memory: turn Bluetooth off and retry"); return false; }
  snprintf(_furl, sizeof(_furl), "%s", url);
  free(_ftext); _ftext = nullptr;
  _fstate = 1; _fmsg[0] = 0;
  _mode = 3; _cancel = false; _net_done = false; _net_ok = false; _net_msg[0] = 0;
  _running = true;
  if (!startTask(this, taskMain)) { _running = false; _fstate = 3; snprintf(_fmsg, sizeof(_fmsg), "Couldn't start"); return false; }
  return true;
}

void Updater::cancel() {
  if (_running) _cancel = true;
  if (_st.state == UPD_CHECKING || _st.state == UPD_DOWNLOADING) fail("Cancelled");
}

void Updater::fail(const char* msg) {
  _st.state = UPD_FAILED;
  snprintf(_st.msg, sizeof(_st.msg), "%s", msg);
  _cancel = true;
  if (_file) { File* f = (File*)_file; f->close(); delete f; _file = nullptr; SD.remove(PART_FILE); }
}

void Updater::loop() {
  // write downloaded chunks (at most two per pass: keeps the loop responsive)
  Chunk c;
  for (int k = 0; k < 2 && _q && xQueueReceive((QueueHandle_t)_q, &c, 0) == pdTRUE; k++) {
    if (_file && _st.state == UPD_DOWNLOADING) {
      File* f = (File*)_file;
      if (f->write(c.data, c.len) != c.len) { free(c.data); fail("Couldn't write to the SD card (full?)"); continue; }
      _written += c.len;
      _st.done = _written;
    }
    free(c.data);
  }
  if (_running || !_net_done) return;
  _net_done = false;
  if (_mode == 3) {
    if (_net_ok) _fstate = 2;
    else { snprintf(_fmsg, sizeof(_fmsg), "%s", _net_msg[0] ? _net_msg : "Couldn't download"); _fstate = 3; }
    return;
  }
  if (_mode == 1) {
    if (_st.state != UPD_CHECKING) return;   // cancelled meanwhile
    if (!_net_ok) { fail(_net_msg[0] ? _net_msg : "Update check failed"); return; }
    // _st.version / notes / size were filled by the task
    if (verCmp(_st.version, EASYDECK_VERSION_STR) > 0) {
      _st.state = UPD_AVAILABLE;
      snprintf(_st.msg, sizeof(_st.msg), "Version %s is available", _st.version);
    } else {
      _st.state = UPD_LATEST;
      snprintf(_st.msg, sizeof(_st.msg), "You have the latest version");
    }
  } else if (_mode == 2) {
    if (_st.state != UPD_DOWNLOADING) return;
    if (uxQueueMessagesWaiting((QueueHandle_t)_q)) { _net_done = true; return; }   // drain first
    if (!_net_ok) { fail(_net_msg[0] ? _net_msg : "Download failed"); return; }
    finishDownload();
  }
}

void Updater::finishDownload() {
  File* f = (File*)_file;
  f->close(); delete f; _file = nullptr;
  if (_written != _st.size) { SD.remove(PART_FILE); fail("Download incomplete - try again"); return; }
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", _got_sha[i]);
  if (_sha[0] && strcmp(hex, _sha) != 0) { SD.remove(PART_FILE); fail("Download damaged (checksum) - try again"); return; }
  // remove older EasyDeck firmware files in the card root, then give the new one its real name
  File root = SD.open("/");
  if (root) {
    String old[8];
    int n = 0;
    for (File e = root.openNextFile(); e && n < 8; e = root.openNextFile()) {
      String nm = e.name();
      if (nm.startsWith("/")) nm = nm.substring(1);
      if (!e.isDirectory() && nm.startsWith("EasyDeck-") && nm.endsWith("-full.bin")) old[n++] = "/" + nm;
      e.close();
    }
    root.close();
    for (int i = 0; i < n; i++) SD.remove(old[i]);
  }
  snprintf(_st.file, sizeof(_st.file), "/EasyDeck-%s-full.bin", _st.version);
  SD.remove(_st.file);
  if (!SD.rename(PART_FILE, _st.file)) { fail("Couldn't rename the download"); return; }
  _st.state = UPD_READY;
  snprintf(_st.msg, sizeof(_st.msg), "Downloaded and checked");
}

// ---------------------------------------------------------------- network task (core 0)

void Updater::taskMain(void* arg) {
  Updater* self = (Updater*)arg;
  if (self->connectWifi()) {
    if (self->_mode == 1) self->netCheck(); else if (self->_mode == 2) self->netDownload(); else self->netFetch();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  self->_net_done = true;
  self->_running = false;
  vTaskDelete(NULL);
}

void Updater::netCheck() {
  snprintf(_st.msg, sizeof(_st.msg), "Asking GitHub...");
  WiFiClientSecure* sec = new WiFiClientSecure();
  sec->setCACert(kGithubRootCAs);
  String body;
  int code = 0;
  {
    HTTPClient http;
    http.setTimeout(15000);
    http.setUserAgent(USER_AGENT);
    if (http.begin(*sec, RELEASES_URL)) {
      http.addHeader("Accept", "application/vnd.github+json");
      code = http.GET();
      if (code == 200) body = http.getString();
      http.end();
    }
  }
  delete sec;
  if (code != 200) {
    snprintf(_net_msg, sizeof(_net_msg), code == 403 ? "GitHub is busy (rate limit) - try later" :
             code < 0 ? "Couldn't reach GitHub (secure connection failed)" : "GitHub error %d", code);
    return;
  }
  JsonDocument filter;
  filter[0]["tag_name"] = true;
  filter[0]["body"] = true;
  filter[0]["assets"][0]["name"] = true;
  filter[0]["assets"][0]["size"] = true;
  filter[0]["assets"][0]["digest"] = true;
  filter[0]["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) || !doc[0]["tag_name"].is<const char*>()) {
    snprintf(_net_msg, sizeof(_net_msg), "Unexpected answer from GitHub");
    return;
  }
  JsonObject rel = doc[0];
  const char* tag = rel["tag_name"];
  snprintf(_st.version, sizeof(_st.version), "%s", tag[0] == 'v' ? tag + 1 : tag);
  notesFrom(rel["body"] | "", _st.notes, sizeof(_st.notes));
  _url[0] = 0; _sha[0] = 0; _st.size = 0;
  char want[48];
  snprintf(want, sizeof(want), "EasyDeck-%s-full.bin", _st.version);
  for (JsonObject a : rel["assets"].as<JsonArray>()) {
    if (strcmp(a["name"] | "", want) != 0) continue;
    snprintf(_url, sizeof(_url), "%s", a["browser_download_url"] | "");
    _st.size = a["size"] | 0;
    const char* d = a["digest"] | "";
    if (strncmp(d, "sha256:", 7) == 0 && strlen(d + 7) == 64) snprintf(_sha, sizeof(_sha), "%s", d + 7);
  }
  bool newer = verCmp(_st.version, EASYDECK_VERSION_STR) > 0;
  if (newer && (!_url[0] || _st.size == 0 || _st.size > MAX_FW)) { snprintf(_net_msg, sizeof(_net_msg), "Release %s has no T-Deck file", _st.version); return; }
  if (newer && !_sha[0]) { snprintf(_net_msg, sizeof(_net_msg), "Release %s has no checksum", _st.version); return; }
  _net_ok = true;
}

void Updater::netFetch() {
  WiFiClientSecure* sec = new WiFiClientSecure();
  sec->setCACert(kGithubRootCAs);
  int code = 0;
  {
    HTTPClient http;
    http.setTimeout(15000);
    http.setUserAgent(USER_AGENT);
    if (http.begin(*sec, _furl)) {
      code = http.GET();
      if (code == 200) {
        String body = http.getString();
        if (body.length() > 0 && body.length() <= 16384) {
          char* t = (char*)ps_malloc(body.length() + 1);
          if (t) { memcpy(t, body.c_str(), body.length() + 1); _ftext = t; _net_ok = true; }
        } else snprintf(_net_msg, sizeof(_net_msg), "File too large");
      }
      http.end();
    }
  }
  delete sec;
  if (!_net_ok && !_net_msg[0])
    snprintf(_net_msg, sizeof(_net_msg), code == 404 ? "Not found on GitHub" : code < 0 ? "Couldn't reach GitHub" : "GitHub error %d", code);
}

void Updater::netDownload() {
  snprintf(_st.msg, sizeof(_st.msg), "Downloading %s...", _st.version);
  WiFiClientSecure* sec = new WiFiClientSecure();
  sec->setCACert(kGithubRootCAs);
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  uint32_t got = 0;
  {
    HTTPClient http;
    http.setTimeout(20000);
    http.setUserAgent(USER_AGENT);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // github.com -> release-assets host
    int code = -1;
    if (http.begin(*sec, _url)) code = http.GET();
    if (code == 200) {
      int len = http.getSize();
      if (len > 0 && (uint32_t)len != _st.size) snprintf(_net_msg, sizeof(_net_msg), "Size mismatch - try again");
      else {
        WiFiClient* s = http.getStreamPtr();
        uint32_t ts = millis();
        while (got < _st.size && !_cancel && millis() - ts < 20000) {
          uint8_t* buf = (uint8_t*)ps_malloc(CHUNK);
          if (!buf) { snprintf(_net_msg, sizeof(_net_msg), "Out of memory"); break; }
          uint32_t want = min((uint32_t)CHUNK, _st.size - got), n = 0;
          while (n < want && !_cancel && millis() - ts < 20000) {
            int a = s->available();
            if (a > 0) { n += s->readBytes(buf + n, min((uint32_t)a, want - n)); ts = millis(); }
            else if (!http.connected()) break;
            else vTaskDelay(pdMS_TO_TICKS(5));
          }
          if (n == 0) { free(buf); break; }
          mbedtls_sha256_update(&sha, buf, n);
          Chunk c = { buf, n };
          if (xQueueSend((QueueHandle_t)_q, &c, pdMS_TO_TICKS(10000)) != pdTRUE) { free(buf); snprintf(_net_msg, sizeof(_net_msg), "SD card too slow"); break; }
          got += n;
          vTaskDelay(1);   // let the idle task run (task watchdog)
        }
        if (got != _st.size && !_net_msg[0]) snprintf(_net_msg, sizeof(_net_msg), "Connection lost - try again");
      }
    } else {
      snprintf(_net_msg, sizeof(_net_msg), code < 0 ? "Couldn't reach GitHub (secure connection failed)" : "Download error %d", code);
    }
    http.end();
  }
  delete sec;
  mbedtls_sha256_finish(&sha, _got_sha);
  mbedtls_sha256_free(&sha);
  _net_ok = got == _st.size && !_cancel && !_net_msg[0];
}

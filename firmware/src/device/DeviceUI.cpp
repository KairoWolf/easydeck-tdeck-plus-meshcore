#include "DeviceUI.h"
#include "TileDownloader.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "EasyMesh.h"
#include "DeviceModel.h"
#include "hal/Display.h"
#include "hal/Input.h"
#include "hal/Sound.h"
#include "ui/App.h"
#include "ui/Sos.h"
#include "ui/Widgets.h"
#include <CayenneLPP.h>

#define DRAW_BUF_LINES 30

static App app;

// ------------------------------------------------------------ LVGL drivers

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((uint16_t*)px, w * h, true);   // LVGL RGB565 little-endian -> panel order
  tft.endWrite();                                // releases the shared SPI bus for radio/SD
  lv_display_flush_ready(disp);
}

static void touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  TouchPoint p = input.touch();
  data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = p.x;
  data->point.y = p.y;
  if (p.pressed) app.touched();
}

static uint32_t tick_cb() { return millis(); }

// ------------------------------------------------------------ setup

void DeviceUI::beginDisplay() {
  tft.start();
  tft.fillScreen(0x0841);                      // near-black, matches the dark theme
  tft.setBacklight(10);
  tft.fillCircle(160, 96, 40, 0x2E94);         // mint logo disc
  tft.setTextColor(0xF7BE);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.drawString("EasyDeck", 160, 166);
}

void DeviceUI::showFatal(const char* msg) {
  tft.setBacklight(12);
  tft.fillScreen(0x0841);
  tft.setTextColor(0xF7BE);
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setCursor(12, 16);
  tft.println(msg);
}

void DeviceUI::begin() {
  input.begin();
  sound.begin();

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t* disp = lv_display_create(SCR_W, SCR_H);
  size_t bytes = SCR_W * DRAW_BUF_LINES * 2;
  void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b1 || !b2) {   // fall back to one PSRAM buffer
    if (b1) heap_caps_free(b1);
    if (b2) heap_caps_free(b2);
    b1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    b2 = nullptr;
  }
  lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  if (input.hasTouch()) {
    lv_indev_t* t = lv_indev_create();
    lv_indev_set_type(t, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(t, touch_cb);
  }

  _log.setObserver(1, this);
  app.begin(&device_model);
  _started = true;
}

// ------------------------------------------------------------ mesh events -> log

void DeviceUI::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (txt_type == 1 /*CLI reply from a repeater we administer*/) { device_model.onCliReply(from.id.pub_key, text); return; }
  if (txt_type != 0 /*plain*/ && txt_type != 2 /*signed (room server)*/) return;
  LogRecord r;
  memset(&r, 0, sizeof(r));
  r.kind = KIND_DIRECT;
  r.dir = DIR_IN;
  r.origin = ORIGIN_RADIO;
  r.sender_ts = sender_timestamp;
  r.snr4 = pkt ? (int8_t)(pkt->getSNR() * 4) : 0;
  r.path_len = (pkt && pkt->isRouteFlood()) ? pkt->path_len : 0xFF;
  memcpy(r.key, from.id.pub_key, LOG_KEY_LEN);
  strncpy(r.name, from.name, LOG_NAME_MAX - 1);
  if (txt_type == 2 && easy_mesh.currentAuthor()) {   // room post: show who wrote it
    const uint8_t* a = easy_mesh.currentAuthor();
    ContactInfo* author = easy_mesh.lookupContactByPubKey(a, 4);
    if (author) strncpy(r.name, author->name, LOG_NAME_MAX - 1);
    else if (memcmp(a, easy_mesh.self_id.pub_key, 4) == 0) strncpy(r.name, easy_mesh.getNodeName(), LOG_NAME_MAX - 1);
    else snprintf(r.name, LOG_NAME_MAX, "%02X%02X%02X%02X", a[0], a[1], a[2], a[3]);
  }
  strncpy(r.text, text, LOG_TEXT_MAX);
  if (_started && app.isViewingChat(r.conv())) r.flags |= LOG_FLAG_READ;
  if (Sos::isSosText(text)) r.flags |= LOG_FLAG_SOS;
  double la, lo;
  if ((Sos::isLocText(text) || Sos::isSosText(text) || strncmp(text, "OK check-in", 11) == 0) && Sos::parseLocation(text, la, lo)) {
    device_model.noteLocation(from.id.pub_key, la, lo);
    if (_started) app.onContactsChanged();
  }
  if (Sos::isLocText(text)) r.flags |= LOG_FLAG_SILENT | LOG_FLAG_READ;   // location beacon: map only
  _log.append(r);
}

void DeviceUI::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel, const char* text) {
  LogRecord r;
  memset(&r, 0, sizeof(r));
  r.kind = KIND_CHANNEL;
  r.dir = DIR_IN;
  r.origin = ORIGIN_RADIO;
  r.sender_ts = rtc_clock.getCurrentTime();
  r.snr4 = pkt ? (int8_t)(pkt->getSNR() * 4) : 0;
  r.path_len = (pkt && pkt->isRouteFlood()) ? pkt->path_len : 0xFF;
  memcpy(r.key, channel.channel.secret, LOG_KEY_LEN);
  r.channel_idx = (uint8_t)easy_mesh.findChannelIdx(channel.channel);
  const char* sep = strstr(text, ": ");   // "Sender Name: message"
  if (sep && sep - text < LOG_NAME_MAX) {
    memcpy(r.name, text, sep - text);
    r.name[sep - text] = 0;
    strncpy(r.text, sep + 2, LOG_TEXT_MAX);
  } else {
    strncpy(r.text, text, LOG_TEXT_MAX);
  }
  if (_started && app.isViewingChat(r.conv())) r.flags |= LOG_FLAG_READ;
  if (Sos::isSosText(r.text)) r.flags |= LOG_FLAG_SOS;
  _log.append(r);
}

void DeviceUI::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (!_started) return;
  if (is_new && contact.type == ADV_TYPE_CHAT) {
    char b[64];
    snprintf(b, sizeof(b), "%s joined your contacts", contact.name);
    app.toast(IC_USER_PLUS, b, 3000);
  }
  app.onContactsChanged();
}

// Telemetry replies to "Ask for their location": find the GPS field in the CayenneLPP payload.
struct LppProbe : public CayenneLPP {
  LppProbe() : CayenneLPP(1) {}
  using CayenneLPP::getTypeSize;
  using CayenneLPP::isType;
};

bool DeviceUI::onUnhandledResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len) {
  if (device_model.onResponse(from.id.pub_key, tag, data, len)) return true;   // login / status
  LppProbe probe;
  for (int i = 0; i + 2 <= len; ) {
    uint8_t type = data[i + 1];
    if (!probe.isType(type)) return false;
    uint8_t sz = probe.getTypeSize(type);
    if (i + 2 + sz > len) return false;
    if (type == LPP_GPS && sz >= 9) {
      const uint8_t* g = &data[i + 2];
      int32_t lat = ((int32_t)g[0] << 16) | (g[1] << 8) | g[2];
      int32_t lon = ((int32_t)g[3] << 16) | (g[4] << 8) | g[5];
      if (lat & 0x800000) lat |= 0xFF000000;   // sign-extend 24-bit
      if (lon & 0x800000) lon |= 0xFF000000;
      double la = lat / 10000.0, lo = lon / 10000.0;
      device_model.noteLocation(from.id.pub_key, la, lo);
      if (_started) {
        DeviceStatus st = device_model.status();
        char d[32] = "", b[80];
        if (st.gps_fix) ui::distanceText(st.lat, st.lon, la, lo, d, sizeof(d));
        else snprintf(d, sizeof(d), "%.4f, %.4f", la, lo);
        snprintf(b, sizeof(b), "%s: %s", from.name, d);
        if (!app.locked()) app.toast(IC_PIN, b, 4000);   // no names/places on the lock screen
        app.onContactsChanged();
      }
      return true;
    }
    i += 2 + sz;
  }
  return false;
}

void DeviceUI::onLogAppended(const LogRecord& r) { if (_started) app.onMessage(r, true); }
extern MessageLog message_log;
void deviceUiContactsChanged() { app.onContactsChanged(); }
void DeviceUI::onLogUpdated(const LogRecord& r) { if (_started && !message_log.inBulk()) app.onMessage(r, false); }

// ------------------------------------------------------------ loop

void DeviceUI::loop() {
  if (!_started) return;
  input.poll();
  InputEvent ev;
  while (input.next(ev)) app.input(ev);
  sound.loop();
  device_model.loopFlash();
  device_model.loopRooms();
  tile_downloader.loop();
  app.loop();
  if (app.screenOn()) lv_timer_handler();
  else if (input.touch().pressed) app.touched();   // LVGL's touch callback doesn't run while the screen is off
}

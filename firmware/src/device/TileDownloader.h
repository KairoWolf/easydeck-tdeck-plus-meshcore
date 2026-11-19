#pragma once
// Offline map download over Wi-Fi.
//
// Threading: the SD card shares one SPI bus with the display and the LoRa radio, all driven from
// the Arduino loop task. So the loop task owns every SD access (existence checks, writes) and a
// separate FreeRTOS task only does the network part (Wi-Fi + HTTP), handing tiles over through
// two small queues. Wi-Fi is switched on only while scanning or downloading.
#include <Arduino.h>
#include "core/Model.h"

class TileDownloader {
public:
  void begin();
  void loop();   // loop task: feeds requests, writes finished tiles to SD

  // settings (persisted)
  void setWifi(const char* ssid, const char* pass);
  const char* ssid() const { return _cfg.ssid; }
  const char* pass() const { return _cfg.pass; }
  void setUrl(const char* url);
  const char* url() const { return _cfg.url; }

  bool scanStart();
  int  scanResults(WifiNet* out, int max);   // -1 while scanning
  void scanStop();                            // leaving the Wi-Fi list: radio off again

  bool start(double lat, double lon, double radius_km, int zmin, int zmax);
  void cancel();
  TileDlStatus status() const { return _st; }
  bool busy() const { return _running || _scanning; }   // Wi-Fi in use

private:
  struct Cfg { char ssid[33]; char pass[65]; char url[192]; };
  struct Req { uint8_t z; uint32_t x, y; };
  struct Res { uint8_t z; uint32_t x, y; uint8_t* data; uint32_t len; int http; };

  void save();
  bool nextTile(Req& r);    // advance the cursor over the area, skipping tiles already on SD
  bool tileOnSd(int z, uint32_t x, uint32_t y);
  bool writeTile(const Res& r);
  void finish(uint8_t state, const char* msg);
  void stopWifi();
  static void taskMain(void* arg);     // first worker: connects Wi-Fi, then fetches
  static void taskWorker(void* arg);   // second worker: waits for Wi-Fi, then fetches
  void fetchLoop();
  void workerDone();
  volatile int _workers = 0;

  Cfg _cfg = {};
  TileDlStatus _st;
  // area cursor
  double _lat = 0, _lon = 0, _r = 0;
  int _zmin = 0, _zmax = 0, _z = 0;
  uint32_t _x = 0, _y = 0, _x0 = 0, _x1 = 0, _y0 = 0, _y1 = 0;
  bool _cursor_done = true;
  uint32_t _in_flight = 0;

  void* _reqq = nullptr;   // QueueHandle_t
  void* _resq = nullptr;
  volatile bool _running = false;   // network task alive
  uint8_t _fail_streak = 0;
  void setRange();
  volatile bool _cancel = false;
  volatile bool _connected = false;
  volatile bool _net_failed = false;
  char _net_msg[64] = "";
  bool _scanning = false;
  uint32_t _wifi_ms = 0;
  char _last_dir[32] = "";
};

extern TileDownloader tile_downloader;

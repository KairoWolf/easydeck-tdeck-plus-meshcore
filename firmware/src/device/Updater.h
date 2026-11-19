#pragma once
// Software update over Wi-Fi from the project's GitHub releases.
//
//  1. Check: GET api.github.com/repos/<repo>/releases?per_page=1 (newest, betas included),
//     compare the tag with our version, find the EasyDeck-<ver>-full.bin asset and its SHA-256.
//  2. Download: stream the asset (HTTPS, certificates checked against GithubCerts.h) to
//     SD:/EasyDeck-update.part, hash it on the way, rename to /EasyDeck-<ver>-full.bin when the
//     hash matches. Older EasyDeck-*-full.bin files in the card root are removed.
//  3. Install: the SD Launcher installs it (restart into the Launcher). There is only one app
//     slot (Launcher-friendly layout), so EasyDeck never rewrites its own flash.
//
// Threading as in TileDownloader: a FreeRTOS task on core 0 does Wi-Fi/HTTP only; the loop task
// does every SD access (the card shares the SPI bus with the display and the radio).
#include <Arduino.h>
#include "core/Model.h"

class Updater {
public:
  void begin();
  void loop();                 // loop task: writes downloaded chunks, finishes up
  bool check();
  bool download();
  void cancel();
  bool busy() const { return _running; }
  UpdateStatus status() const { return _st; }

private:
  struct Chunk { uint8_t* data; uint32_t len; };
  static void taskMain(void* arg);
  void netCheck();
  void netDownload();
  bool connectWifi();
  void fail(const char* msg);
  void finishDownload();

  UpdateStatus _st;
  volatile bool _running = false, _cancel = false;
  volatile bool _net_done = false, _net_ok = false;
  uint8_t _mode = 0;           // 1 check, 2 download
  char _url[200] = "";
  char _sha[65] = "";          // expected, lower-case hex
  uint8_t _got_sha[32];
  char _net_msg[64] = "";
  void* _q = nullptr;          // QueueHandle_t of Chunk
  void* _file = nullptr;       // File* (loop task only)
  uint32_t _written = 0;
};

extern Updater updater;

#include "Backup.h"
#include "Storage.h"
#include <SD.h>
#include <rom/crc.h>

#define BK_DIR       "/easydeck/backup"
#define BK_STEP_MS   2000UL      // one file per step
#define BK_PASS_MS   300000UL    // a full pass every 5 minutes
#define BK_MAX_FILE  (256 * 1024)

// Everything that makes this T-Deck "yours". safeSave() records have a second copy at "<path>~".
// Not included: the breadcrumb track (large, not a setting), advert blobs in /bl (re-learned from
// the next advert), the message history (already on the SD card when one is present).
static const char* const kFiles[] = {
  "/identity/_main.id",                                           // MeshCore identity (keys)
  "/prefs.json", "/new_prefs", "/contacts3", "/channels2",        // MeshCore prefs / contacts / groups
  "/easydeck_ui2", "/easydeck_ui2~",                              // EasyDeck settings
  "/easydeck_wp", "/easydeck_wp~", "/easydeck_mute", "/easydeck_mute~",
  "/easydeck_notes", "/easydeck_notes~", "/easydeck_owner", "/easydeck_owner~",
  "/easydeck_net", "/easydeck_net~", "/easydeck_chscope.bin", "/easydeck_rooms.bin",
};
static const int kN = sizeof(kFiles) / sizeof(kFiles[0]);

static uint32_t _crc[kN];        // CRC of what is on the card (0 = unknown)
static bool _known[kN];
static int _next = 0;
static uint32_t _step_ms = 0, _pass_start = 0, _last_pass = 0;
static bool _in_pass = false;

// "/identity/_main.id" -> "/easydeck/backup/identity__main.id" (flat folder on FAT)
static String sdName(const char* p) {
  String s = BK_DIR "/";
  for (const char* c = p + 1; *c; c++) s += (*c == '/') ? '_' : *c;
  return s;
}

static bool fileCrc(fs::FS& fs, const char* path, uint32_t& crc, size_t& size) {
  File f = fs.open(path, "r");
  if (!f || f.isDirectory()) return false;
  uint8_t buf[512];
  crc = 0; size = 0;
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) { crc = crc32_le(crc, buf, n); size += n; if (size > BK_MAX_FILE) break; }
  f.close();
  return size <= BK_MAX_FILE;
}

static bool copyFile(fs::FS& from, const char* src, fs::FS& to, const String& dst) {
  File in = from.open(src, "r");
  if (!in) return false;
  String tmp = dst + ".tmp";
  File out = to.open(tmp, "w");
  if (!out) { in.close(); return false; }
  uint8_t buf[512];
  bool ok = true;
  int n;
  size_t total = 0;
  while ((n = in.read(buf, sizeof(buf))) > 0) {
    if (out.write(buf, n) != (size_t)n) { ok = false; break; }
    total += n;
    if (total > BK_MAX_FILE) { ok = false; break; }
  }
  in.close();
  out.close();
  if (!ok) { to.remove(tmp); return false; }
  to.remove(dst);
  return to.rename(tmp, dst);
}

bool backupActive() { return storageMode() == STORAGE_FLASH && sdPresent(); }

int backupRestoreIfFresh() {
  if (!backupActive()) return 0;
  if (DataFS.exists(kFiles[0])) return 0;                 // already has an identity: not fresh
  if (!SD.exists(sdName(kFiles[0]))) return 0;            // no backup with an identity
  int n = 0;
  for (int i = 0; i < kN; i++) {
    String s = sdName(kFiles[i]);
    if (!SD.exists(s)) continue;
    // DataFS is SPIFFS (flat names); copy directly, no tmp/rename needed on a fresh partition
    File in = SD.open(s, "r");
    if (!in) continue;
    File out = DataFS.open(kFiles[i], "w");
    if (!out) { in.close(); continue; }
    uint8_t buf[512];
    int r;
    while ((r = in.read(buf, sizeof(buf))) > 0) out.write(buf, r);
    in.close();
    out.close();
    n++;
  }
  Serial.printf("EasyDeck: restored %d files from SD backup\n", n);
  return n;
}

void backupLoop(bool enabled) {
  if (!enabled || !backupActive()) return;
  uint32_t now = millis();
  if (!_in_pass) {
    if (_last_pass && now - _pass_start < BK_PASS_MS) return;
    if (!_last_pass && now < 60000UL) return;              // let boot settle first
    if (!SD.exists(BK_DIR)) { SD.mkdir("/easydeck"); SD.mkdir(BK_DIR); }
    _in_pass = true; _next = 0; _pass_start = now; _step_ms = 0;
  }
  if (_step_ms && now - _step_ms < BK_STEP_MS) return;
  _step_ms = now ? now : 1;

  int i = _next++;
  const char* p = kFiles[i];
  uint32_t crc; size_t size;
  if (fileCrc(DataFS, p, crc, size)) {
    String dst = sdName(p);
    if (!_known[i]) {                                      // first look: what's on the card?
      uint32_t c2; size_t s2;
      if (fileCrc(SD, dst.c_str(), c2, s2)) { _crc[i] = c2; _known[i] = true; }
    }
    if (!_known[i] || _crc[i] != crc) {
      if (copyFile(DataFS, p, SD, dst)) { _crc[i] = crc; _known[i] = true; }
    }
  }
  if (_next >= kN) { _in_pass = false; _last_pass = millis(); if (!_last_pass) _last_pass = 1; }
}

uint32_t backupLastMs() { return _last_pass; }

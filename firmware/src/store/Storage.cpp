#include "Storage.h"
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <vfs_api.h>
#include <esp_partition.h>
#include "TDeckPlusBoard.h"

#define EASYDECK_FS_LABEL  "edfs"
#define SD_DATA_DIR        "/easydeck/meshcore"
#define SD_FREQ_HZ         20000000

static std::shared_ptr<VFSImpl> data_impl = std::make_shared<VFSImpl>();
fs::FS DataFS(data_impl);

static StorageMode _mode = STORAGE_NONE;
static bool _sd_ok = false;

bool sdBegin() {
  if (_sd_ok) return true;
  // format_if_empty = false: never touch a card we can't read
  _sd_ok = SD.begin(PIN_SD_CS, SPI, SD_FREQ_HZ, "/sd", 5, false) && SD.cardType() != CARD_NONE;
  return _sd_ok;
}

bool sdPresent() { return _sd_ok; }

StorageMode storageMode() { return _mode; }

StorageMode storageBegin() {
  // 1. our own labelled flash partition
  if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, EASYDECK_FS_LABEL)) {
    if (SPIFFS.begin(true, "/spiffs", 10, EASYDECK_FS_LABEL)) {
      data_impl->mountpoint("/spiffs");
      _mode = STORAGE_FLASH;
      return _mode;
    }
  }
  // 2. folder on the SD card
  if (sdBegin()) {
    if (!SD.exists("/easydeck")) SD.mkdir("/easydeck");
    if (!SD.exists(SD_DATA_DIR)) SD.mkdir(SD_DATA_DIR);
    if (SD.exists(SD_DATA_DIR)) {
      data_impl->mountpoint("/sd" SD_DATA_DIR);
      _mode = STORAGE_SD;
      return _mode;
    }
  }
  _mode = STORAGE_NONE;
  return _mode;
}

String storageDiagnostics() {
  String s = "SD card: ";
  s += _sd_ok ? "yes" : "not found";
  s += "\nData partitions:\n";
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
  int n = 0;
  while (it && n < 8) {
    const esp_partition_t* p = esp_partition_get(it);
    char line[64];
    snprintf(line, sizeof(line), " %s sub=%02x %luK\n", p->label, p->subtype, (unsigned long)(p->size / 1024));
    s += line;
    n++;
    it = esp_partition_next(it);
  }
  if (it) esp_partition_iterator_release(it);
  return s;
}

static void removeTree(fs::FS& fs, const String& path) {
  File dir = fs.open(path);
  if (!dir) return;
  if (!dir.isDirectory()) { dir.close(); fs.remove(path); return; }
  File f;
  while ((f = dir.openNextFile())) {
    String child = String(f.path());
    bool is_dir = f.isDirectory();
    f.close();
    if (is_dir) { removeTree(fs, child); fs.rmdir(child); }
    else fs.remove(child);
  }
  dir.close();
}

bool storageFactoryReset() {
  bool ok = true;
  if (_mode == STORAGE_FLASH) ok = SPIFFS.format();   // SPIFFS was begun with our own label
  if (_sd_ok) {
    // only EasyDeck's folder
    if (SD.exists(SD_DATA_DIR)) removeTree(SD, SD_DATA_DIR);
    SD.remove("/easydeck/messages.log");
    SD.remove("/easydeck/messages.meta");
  }
  return ok;
}

// ---------------------------------------------------------------- crash-safe records
struct __attribute__((packed)) SafeHdr { uint32_t magic, gen, len, sum; };
static uint32_t fnv1a(const uint8_t* p, size_t n, uint32_t h = 2166136261UL) {
  while (n--) { h ^= *p++; h *= 16777619UL; }
  return h;
}

static bool readCopy(fs::FS& fs, const String& p, uint32_t magic, void* data, size_t len, uint32_t& gen) {
  File f = fs.open(p, "r");
  if (!f) return false;
  SafeHdr h;
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == magic && h.len == len &&
            f.read((uint8_t*)data, len) == (int)len && fnv1a((const uint8_t*)data, len, h.gen) == h.sum;
  f.close();
  if (ok) gen = h.gen;
  return ok;
}


bool safeLoad(fs::FS& fs, const char* path, uint32_t magic, void* data, size_t len) {
  uint8_t* tmp = (uint8_t*)malloc(len);
  if (!tmp) return false;
  uint32_t g1 = 0, g2 = 0;
  bool a = readCopy(fs, String(path), magic, data, len, g1);
  bool b = readCopy(fs, String(path) + "~", magic, tmp, len, g2);
  if (b && (!a || g2 > g1)) memcpy(data, tmp, len);
  free(tmp);
  if (a || b) return true;
  // legacy format: [magic][data]
  File f = fs.open(path, "r");
  if (!f) return false;
  uint32_t m = 0;
  bool ok = f.size() == 4 + len &&   // exactly [magic][data]: never misread a damaged new-format file
            f.read((uint8_t*)&m, 4) == 4 && m == magic && f.read((uint8_t*)data, len) == (int)len;
  f.close();
  return ok;
}

bool safeSave(fs::FS& fs, const char* path, uint32_t magic, const void* data, size_t len) {
  // write over the OLDER (or damaged) copy, with a newer generation
  uint8_t* tmp = (uint8_t*)malloc(len);
  if (!tmp) return false;
  uint32_t g1 = 0, g2 = 0;
  bool a = readCopy(fs, String(path), magic, tmp, len, g1);
  bool b = readCopy(fs, String(path) + "~", magic, tmp, len, g2);
  free(tmp);
  uint32_t gen = (a && g1 > g2 ? g1 : g2) + 1;
  String target = (!a) ? String(path) : (!b) ? String(path) + "~" : (g1 <= g2 ? String(path) : String(path) + "~");
  SafeHdr h = { magic, gen, (uint32_t)len, fnv1a((const uint8_t*)data, len, gen) };
  File f = fs.open(target, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h) && f.write((const uint8_t*)data, len) == len;
  f.close();
  return ok;
}

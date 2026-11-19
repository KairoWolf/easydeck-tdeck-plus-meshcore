#include "Crash.h"
#include <esp_core_dump.h>
#include <SD.h>
#include "store/Storage.h"
#include "core/Version.h"

static String s_summary;

void crashCaptureAtBoot() {
  s_summary = "";
  if (esp_core_dump_image_check() != ESP_OK) return;   // no (valid) dump
  esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)calloc(1, sizeof(esp_core_dump_summary_t));
  if (!s) return;
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    char line[160];
    snprintf(line, sizeof(line), "task %s, cause %lu, PC 0x%08lx, addr 0x%08lx", s->exc_task,
             (unsigned long)s->ex_info.exc_cause, (unsigned long)s->exc_pc, (unsigned long)s->ex_info.exc_vaddr);
    s_summary = line;
    String full = String("EasyDeck " EASYDECK_VERSION_STR " crash: ") + line + "\nbacktrace:";
    for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
      snprintf(line, sizeof(line), " 0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
      full += line;
    }
    if (s->exc_bt_info.corrupted) full += " (corrupted)";
    full += "\nelf sha256: ";
    full += (const char*)s->app_elf_sha256;
    full += "\n\n";
    Serial.print(full);
    File f;
    if (sdPresent()) { SD.mkdir("/easydeck"); f = SD.open("/easydeck/crash.txt", FILE_APPEND); }
    else f = DataFS.open("/easydeck_crash.txt", FILE_APPEND);
    if (f) { f.print(full); f.close(); }
  }
  free(s);
  esp_core_dump_image_erase();
}

const char* crashSummary() { return s_summary.c_str(); }

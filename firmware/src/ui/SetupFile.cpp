#include "SetupFile.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <string>

namespace SetupFile {

static void trim(char* s) {
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\t')) *--e = 0;
  char* b = s;
  while (*b == ' ' || *b == '\t') b++;
  if (b != s) memmove(s, b, strlen(b) + 1);
}

static bool yes(const char* v) { return !strcasecmp(v, "1") || !strcasecmp(v, "yes") || !strcasecmp(v, "on") || !strcasecmp(v, "true"); }

int importIfPresent(Model* m) {
  const int cap = 4096;
  char* buf = (char*)malloc(cap);   // temporary: only needed once at boot
  if (!buf) return 0;
  if (!m->readTextFile(SETUP_IMPORT_PATH, buf, cap)) { free(buf); return 0; }
  int applied = 0;
  UiSettings& u = m->ui();
  char* save = nullptr;
  for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
    trim(line);
    if (!*line || *line == '#') continue;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char* k = line;
    char* v = eq + 1;
    trim(k); trim(v);
    if (!strcasecmp(k, "name") && *v) { m->setNodeName(v); applied++; }
    else if (!strcasecmp(k, "preset")) {
      for (int i = 0; i < kNumRadioPresets; i++) if (!strcasecmp(v, kRadioPresets[i].name)) { m->setRadio(kRadioPresets[i]); applied++; }
    }
    else if (!strcasecmp(k, "scope")) { m->setRegionScope(!strcasecmp(v, "off") ? "" : v); applied++; }
    else if (!strcasecmp(k, "easy_mode")) { u.easy_mode = yes(v); applied++; }
    else if (!strcasecmp(k, "style")) {
      for (int i = 0; i < NUM_THEMES; i++) if (!strcasecmp(v, kThemeNamesPlain[i])) { u.theme = i; applied++; }
    }
    else if (!strcasecmp(k, "text_size")) { u.text_size = !strcasecmp(v, "large"); applied++; }
    else if (!strcasecmp(k, "timezone")) { m->setTzOffset(atoi(v)); applied++; }
    else if (!strcasecmp(k, "gps")) { m->setGps(yes(v)); applied++; }
    else if (!strcasecmp(k, "advert_location")) { m->setAdvertLocation(yes(v)); applied++; }
    else if (!strcasecmp(k, "auto_advert_hours")) {
      int h = atoi(v);
      for (int i = 0; i < NUM_AUTO_ADVERT; i++) if (kAutoAdvertHours[i] == h) u.auto_advert = i;
      applied++;
    }
    else if (!strcasecmp(k, "share_location_min")) {
      int mi = atoi(v);
      for (int i = 0; i < NUM_SHARE; i++) if (kShareMinutes[i] == mi) u.share_loc = i;
      applied++;
    }
    else if (!strcasecmp(k, "sos_emergency_channel")) { u.sos_post_emergency = yes(v); applied++; }
    else if (!strcasecmp(k, "sound")) { u.sound = yes(v); applied++; }
    else if (!strcasecmp(k, "group")) { if (m->joinChannel(v)) applied++; }
    else if (!strcasecmp(k, "private_group")) {   // Name:hexkey
      char* c = strrchr(v, ':');
      if (c) { *c = 0; if (m->joinPrivateChannel(v, c + 1)) applied++; }
    }
  }
  free(buf);
  if (applied) {
    u.setup_done = 1;
    m->saveUi();
    m->renameFile(SETUP_IMPORT_PATH, SETUP_DONE_PATH);
  }
  return applied;
}

bool exportTo(Model* m, const char* path) {
  std::string t;
  char b[160];
  UiSettings& u = m->ui();
  t += "# EasyDeck setup file. Rename to easydeck-setup.txt and put it on another device's SD card.\n";
  t += "# WARNING: contains private group keys - only share with people you trust.\n";
  snprintf(b, sizeof(b), "name=%s\n", m->nodeName()); t += b;
  int pi = findPreset(m->radio());
  if (pi >= 0) { snprintf(b, sizeof(b), "preset=%s\n", kRadioPresets[pi].name); t += b; }
  snprintf(b, sizeof(b), "scope=%s\n", *m->regionScope() ? m->regionScope() : "off"); t += b;
  snprintf(b, sizeof(b), "style=%s\neasy_mode=%d\ntext_size=%s\n", kThemeNamesPlain[u.theme % NUM_THEMES], u.easy_mode, u.text_size ? "large" : "normal"); t += b;
  snprintf(b, sizeof(b), "timezone=%d\ngps=%d\nadvert_location=%d\n", m->tzOffset(), m->status().gps_enabled, m->advertLocation()); t += b;
  snprintf(b, sizeof(b), "auto_advert_hours=%d\nshare_location_min=%d\nsos_emergency_channel=%d\nsound=%d\n",
           kAutoAdvertHours[u.auto_advert % NUM_AUTO_ADVERT], kShareMinutes[u.share_loc % NUM_SHARE], u.sos_post_emergency, u.sound); t += b;
  for (int i = 0; i < m->channelCount(); i++) {
    ChannelView ch;
    if (!m->channelAt(i, ch) || !strcasecmp(ch.name, "Public")) continue;
    if (ch.name[0] == '#') { snprintf(b, sizeof(b), "group=%s\n", ch.name); t += b; }
    else {
      char hex[33];
      if (m->channelSecretHex(ch.key, hex)) { snprintf(b, sizeof(b), "private_group=%s:%s\n", ch.name, hex); t += b; }
    }
  }
  return m->writeTextFile(path, t.c_str());
}

}  // namespace SetupFile

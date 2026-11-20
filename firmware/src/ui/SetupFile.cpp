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

#include "BuiltinPacks.inc"
int builtinPackCount() { return (int)(sizeof(kBuiltinPacks) / sizeof(kBuiltinPacks[0])); }
const char* builtinPack(int i) { return i >= 0 && i < builtinPackCount() ? kBuiltinPacks[i] : ""; }

// group=<name>[|<scope>]: join a hashtag group (Public is only looked up) and set its scope
static bool applyGroup(Model* m, char* v) {
  char* bar = strchr(v, '|');
  char* scope = nullptr;
  if (bar) { *bar = 0; scope = bar + 1; trim(scope); }
  trim(v);
  if (!*v) return false;
  if (v[0] == '#' && !m->joinChannel(v)) return false;
  if (!scope || !*scope) return true;
  for (int i = 0; i < m->channelCount(); i++) {
    ChannelView ch;
    if (!m->channelAt(i, ch) || strcasecmp(ch.name, v) != 0) continue;
    if (!strcasecmp(scope, "none")) m->setChannelScope(ch.key, 1, "");
    else if (!strcasecmp(scope, "default")) m->setChannelScope(ch.key, 0, "");
    else m->setChannelScope(ch.key, 2, scope);
    return true;
  }
  return false;
}

// Applies key=value lines (modifies buf). Wi-Fi details are returned, not applied.
static int applyLines(Model* m, char* buf, std::string& wifi_ssid, std::string& wifi_pass) {
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
      for (int i = 0; i < NUM_THEMES; i++) if (!strcasecmp(v, kThemeNamesPlain[i])) { u.theme = themeMake(i, themeAccent(u.theme)); applied++; }
    }
    else if (!strcasecmp(k, "text_size")) { u.text_size = !strcasecmp(v, "large"); applied++; }
    else if (!strcasecmp(k, "timezone")) { m->setTzOffset(!strcasecmp(v, "au-east") ? TZ_AU_EAST : atoi(v)); applied++; }
    else if (!strcasecmp(k, "path_hash_bytes")) { int b = atoi(v); if (b >= 1 && b <= 3) { m->setPathHashBytes(b); applied++; } }
    else if (!strcasecmp(k, "radio")) {   // freq,bw,sf,cr like MeshCore's "set radio"
      RadioSettings r = m->radio(); float f, bw; int sf, cr;
      if (sscanf(v, "%f , %f , %d , %d", &f, &bw, &sf, &cr) == 4) { r.freq = f; r.bw = bw; r.sf = sf; r.cr = cr; m->setRadioCustom(r); applied++; }
    }
    else if (!strcasecmp(k, "tx_power")) { RadioSettings r = m->radio(); r.tx_power = atoi(v); m->setRadioCustom(r); applied++; }
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
    else if (!strcasecmp(k, "group") || !strcasecmp(k, "channel")) { if (applyGroup(m, v)) applied++; }
    else if (!strcasecmp(k, "wifi_ssid")) { wifi_ssid = v; }
    else if (!strcasecmp(k, "wifi_pass")) { wifi_pass = v; }
    else if (!strcasecmp(k, "map_url")) {   // {z}/{x}/{y} tile URL for Offline maps
      if (!*v || (strstr(v, "{z}") && strstr(v, "{x}") && strstr(v, "{y}"))) { m->setTileUrl(v); applied++; }
    }
    else if (!strcasecmp(k, "private_group") || !strcasecmp(k, "private_channel")) {   // Name:hexkey
      char* c = strrchr(v, ':');
      if (c) { *c = 0; if (m->joinPrivateChannel(v, c + 1)) applied++; }
    }
  }
  return applied;
}

int applyText(Model* m, const char* text) {
  size_t n = strlen(text);
  char* buf = (char*)malloc(n + 1);
  if (!buf) return 0;
  memcpy(buf, text, n + 1);
  std::string ws, wp;
  m->setRegionScope("");                       // packs set scopes per channel
  int applied = applyLines(m, buf, ws, wp);   // packs never carry Wi-Fi details
  free(buf);
  m->saveUi();
  return applied;
}

bool describe(const char* text, PackInfo& out) {
  out = PackInfo();
  const char* p = text;
  while (*p) {
    const char* e = strchr(p, '\n');
    if (!e) e = p + strlen(p);
    std::string line(p, e - p);
    p = *e ? e + 1 : e;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "pack_name") out.name = v;
    else if (k == "pack_about") out.about = v;
    else if (k == "preset") out.preset = v;
    else if (k == "scope") out.scope = v;
    else if (k == "path_hash_bytes") out.hash_bytes = v;
    else if (k == "timezone") out.timezone = v;
    else if (k == "group" || k == "channel") out.groups.push_back(v);   // "name|scope"
    else if (k == "note") out.notes.push_back(v);
  }
  return !out.name.empty();
}

int importIfPresent(Model* m) {
  const int cap = 4096;
  char* buf = (char*)malloc(cap);   // temporary: only needed once at boot
  if (!buf) return 0;
  if (!m->readTextFile(SETUP_IMPORT_PATH, buf, cap)) { free(buf); return 0; }
  UiSettings& u = m->ui();
  std::string wifi_ssid, wifi_pass;
  int applied = applyLines(m, buf, wifi_ssid, wifi_pass);
  free(buf);
  if (!wifi_ssid.empty()) { m->setWifi(wifi_ssid.c_str(), wifi_pass.c_str()); applied++; }
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
  t += "# WARNING: contains private channel keys - only share with people you trust.\n";
  snprintf(b, sizeof(b), "name=%s\n", m->nodeName()); t += b;
  int pi = findPreset(m->radio());
  if (pi >= 0) { snprintf(b, sizeof(b), "preset=%s\n", kRadioPresets[pi].name); t += b; }
  snprintf(b, sizeof(b), "style=%s\neasy_mode=%d\ntext_size=%s\n", kThemeNamesPlain[themeStyle(u.theme)], u.easy_mode, u.text_size ? "large" : "normal"); t += b;
  if (m->tzSetting() == TZ_AU_EAST) t += "timezone=au-east\n"; else { snprintf(b, sizeof(b), "timezone=%d\n", m->tzSetting()); t += b; }
  snprintf(b, sizeof(b), "gps=%d\nadvert_location=%d\npath_hash_bytes=%d\n", m->status().gps_enabled, m->advertLocation(), m->pathHashBytes()); t += b;
  snprintf(b, sizeof(b), "auto_advert_hours=%d\nshare_location_min=%d\nsos_emergency_channel=%d\nsound=%d\n",
           kAutoAdvertHours[u.auto_advert % NUM_AUTO_ADVERT], kShareMinutes[u.share_loc % NUM_SHARE], u.sos_post_emergency, u.sound); t += b;
  if (*m->tileUrl()) { t += "# map tile source (may contain your API key)\nmap_url="; t += m->tileUrl(); t += "\n"; }
  t += "# wifi_ssid=  wifi_pass=   (Wi-Fi for Offline maps; not exported)\n";
  for (int i = 0; i < m->channelCount(); i++) {
    ChannelView ch;
    if (!m->channelAt(i, ch)) continue;
    char sc[32], suffix[40] = "";
    int sm = m->channelScope(ch.key, sc);
    if (sm == 2) snprintf(suffix, sizeof(suffix), "|%s", sc); else if (sm == 1) snprintf(suffix, sizeof(suffix), "|none");
    if (!strcasecmp(ch.name, "Public")) { if (suffix[0]) { snprintf(b, sizeof(b), "channel=Public%s\n", suffix); t += b; } continue; }
    if (ch.name[0] == '#') { snprintf(b, sizeof(b), "channel=%s%s\n", ch.name, suffix); t += b; }
    else {
      char hex[33];
      if (m->channelSecretHex(ch.key, hex)) { snprintf(b, sizeof(b), "private_channel=%s:%s\n", ch.name, hex); t += b; }
    }
  }
  return m->writeTextFile(path, t.c_str());
}

}  // namespace SetupFile

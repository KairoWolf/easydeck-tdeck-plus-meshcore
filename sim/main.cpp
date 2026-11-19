#include <strings.h>
// EasyDeck UI simulator: runs the real UI against SimModel and writes PNG screenshots.
// Usage: easydeck_sim <scenario.txt> <out_dir>
// Scenario commands (one per line, # comments):
//   setup 0|1        start with onboarding (1) or finished setup (0, default)
//   easy 0|1  theme 0|1  large 0|1
//   start            build the UI (after the settings above)
//   wait <ms>        run the UI for this long
//   key up|down|left|right|select|back|enter|bksp
//   type <text>      type characters
//   incoming <contact#> <text>
//   deliver          mark the last sent message delivered
//   shot <name>      save <out_dir>/<name>.png (2x scaled)
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <fstream>
#include <sstream>
#include "SimModel.h"
#include "ui/App.h"

static uint32_t g_ms = 0;
static uint16_t g_fb[SCR_W * SCR_H];

static uint32_t tick_cb() { return g_ms; }

static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  uint16_t* src = (uint16_t*)px;
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) g_fb[y * SCR_W + x] = *src++;
  lv_display_flush_ready(d);
}

static App app;
// --- test helpers: find the focused list row and read its labels -----------------------
static bool labelTreeContains(lv_obj_t* o, const char* needle) {
  if (lv_obj_check_type(o, &lv_label_class)) {
    const char* t = lv_label_get_text(o);
    if (t && strcasestr(t, needle)) return true;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
    if (labelTreeContains(lv_obj_get_child(o, i), needle)) return true;
  return false;
}
static lv_obj_t* findFocusedRow(lv_obj_t* o) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t* c = lv_obj_get_child(o, i);
    if (lv_obj_has_state(c, LV_STATE_FOCUSED) && !lv_obj_check_type(c, &lv_textarea_class) && lv_obj_get_child_count(c) > 0) return c;
    lv_obj_t* r = findFocusedRow(c);
    if (r) return r;
  }
  return nullptr;
}

static SimModel model;

static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 5) {
    g_ms += 5;
    app.loop();
    lv_timer_handler();
  }
}

static void shot(const std::string& dir, const std::string& name) {
  run(10);
  std::string ppm = dir + "/" + name + ".ppm", png = dir + "/" + name + ".png";
  FILE* f = fopen(ppm.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    uint16_t c = g_fb[i];
    uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63), (uint8_t)((c & 0x1F) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  std::string cmd = "magick " + ppm + " -filter point -resize 200% " + png + " && rm " + ppm;
  if (system(cmd.c_str()) != 0) fprintf(stderr, "convert failed\n");
  printf("shot %s\n", png.c_str());
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s scenario.txt out_dir\n", argv[0]); return 1; }
  std::string out = argv[2];
  system(("mkdir -p " + out).c_str());

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t* disp = lv_display_create(SCR_W, SCR_H);
  static uint8_t buf[SCR_W * 40 * 2];
  lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  model.ui().setup_done = 1;
  model.onAppend = [](const LogRecord& r) { app.onMessage(r, true); };
  model.onUpdate = [](const LogRecord& r) { app.onMessage(r, false); };

  std::ifstream in(argv[1]);
  std::string line;
  bool started = false;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    std::string rest;
    std::getline(ss, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    if (cmd == "setup") model.ui().setup_done = atoi(rest.c_str()) ? 0 : 1;
    else if (cmd == "easy") model.ui().easy_mode = atoi(rest.c_str());
    else if (cmd == "theme") model.ui().theme = atoi(rest.c_str());
    else if (cmd == "large") model.ui().text_size = atoi(rest.c_str());
    else if (cmd == "start") { app.begin(&model); started = true; run(400); }
    else if (!started) { fprintf(stderr, "'start' first\n"); return 1; }
    else if (cmd == "wait") run(atoi(rest.c_str()));
    else if (cmd == "shot") shot(out, rest);
    else if (cmd == "deliver") { model.deliverPending(); run(50); }
    else if (cmd == "incoming") {
      std::istringstream rs(rest);
      int c; rs >> c; std::string t; std::getline(rs, t);
      if (!t.empty() && t[0] == ' ') t.erase(0, 1);
      model.incoming(c, t.c_str());
      run(50);
    } else if (cmd == "downto") {   // downto <text>: press down until the focused row shows <text>
      bool found = false;
      for (int i = 0; i < 80 && !found; i++) {
        lv_obj_t* row = findFocusedRow(lv_screen_active());
        if (row && labelTreeContains(row, rest.c_str())) { found = true; break; }
        app.input({ IN_DOWN, 0 }); run(30);
      }
      if (!found) fprintf(stderr, "SCENARIO ERROR: downto '%s' not found\n", rest.c_str());
    } else if (cmd == "locreply") { // locreply <contact index> <lat> <lon>: their position arrives
      std::istringstream rs(rest);
      int c; double la, lo; rs >> c >> la >> lo;
      ContactView v;
      if (model.contactAt(c, v)) { model.noteLocation(v.key, la, lo); app.onContactsChanged(); }
      run(50);
    } else if (cmd == "screenoff") { // screen times out
      app.setPower(2 /* PWR_OFF */); run(100);
    } else if (cmd == "pin") {      // pin <code>: set the screen-lock PIN directly
      app.setPin(rest.c_str()); run(50);
    } else if (cmd == "lock") {     // lock now (as if the screen turned off)
      app.lockNow(); run(300);
    } else if (cmd == "owner") {    // owner <name>|<ice>|<medical>
      OwnerInfo o = {};
      std::string a = rest, parts[3];
      for (int k = 0; k < 3; k++) { size_t b = a.find('|'); parts[k] = a.substr(0, b); a = b == std::string::npos ? "" : a.substr(b + 1); }
      snprintf(o.owner, sizeof(o.owner), "%s", parts[0].c_str());
      snprintf(o.ice, sizeof(o.ice), "%s", parts[1].c_str());
      snprintf(o.medical, sizeof(o.medical), "%s", parts[2].c_str());
      model.setOwnerInfo(o); run(50);
    } else if (cmd == "chanin") {   // chanin <channel index> <sender>|<text>
      std::istringstream rs(rest);
      int c; rs >> c; std::string t; std::getline(rs, t);
      if (!t.empty() && t[0] == ' ') t.erase(0, 1);
      size_t bar = t.find('|');
      std::string who = bar == std::string::npos ? "Someone" : t.substr(0, bar);
      std::string msg = bar == std::string::npos ? t : t.substr(bar + 1);
      model.incomingChannel(c, who.c_str(), msg.c_str());
      run(50);
    } else if (cmd == "type") {
      for (char ch : rest) { app.input({ IN_CHAR, ch }); run(20); }
    } else if (cmd == "key") {
      InputType t = rest == "up" ? IN_UP : rest == "down" ? IN_DOWN : rest == "left" ? IN_LEFT : rest == "right" ? IN_RIGHT :
                    rest == "select" ? IN_SELECT : rest == "back" ? IN_BACK : rest == "enter" ? IN_ENTER : rest == "bksp" ? IN_BACKSPACE : rest == "sos" ? IN_SOS_HOLD : rest == "screenshot" ? IN_SCREENSHOT : IN_NONE;
      app.input({ t, 0 });
      run(60);
    } else fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  }
  return 0;
}

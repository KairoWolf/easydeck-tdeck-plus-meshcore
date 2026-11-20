#include "Theme.h"
#include <string.h>
#include <stdlib.h>

namespace T {
  lv_color_t bg, surface, surface2, border, text, muted, faint;
  lv_color_t accent, accent_text, on_accent, danger, warning, success;
  lv_color_t bubble_mine, bubble_mine_text, bubble_theirs, bubble_theirs_text;
  lv_color_t select_bg, select_border;
  const lv_font_t *f12, *f14, *f14b, *f16, *f16b, *f20, *f20b, *f24, *f24b, *f32b;
  const lv_font_t *caption, *small, *body, *body_semi, *title, *hero, *icon, *icon_big;
  int row_h, avatar, radius, pad;
  bool shadows, round_avatars;
  static UiStyle _style = STYLE_TACTICAL;
  static bool _dark = true, _easy = false;

  UiStyle style() { return _style; }
  bool dark() { return _dark; }
  bool easy() { return _easy; }

  static lv_color_t h(uint32_t v) { return lv_color_hex(v); }

  // luminance 0..255 of an 0xRRGGBB colour
  static int lum(uint32_t c) { return (int)(((c >> 16) & 0xFF) * 299 + ((c >> 8) & 0xFF) * 587 + (c & 0xFF) * 114) / 1000; }
  static uint32_t mix(uint32_t a, uint32_t b, int pct_b) {   // a blended towards b
    uint32_t r = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
      int x = (a >> sh) & 0xFF, y = (b >> sh) & 0xFF;
      r |= (uint32_t)((x * (100 - pct_b) + y * pct_b) / 100) << sh;
    }
    return r;
  }
  static uint32_t _bg_rgb = 0;

  // One accent colour drives the highlights, your message bubbles and the selection.
  static void setAccent(uint32_t rgb) {
    accent = h(rgb);
    accent_text = _dark ? h(rgb) : h(mix(rgb, 0x000000, 25));       // readable on light backgrounds
    on_accent = lum(rgb) > 150 ? h(0x000000) : h(0xFFFFFF);
    bubble_mine = h(rgb); bubble_mine_text = on_accent;
    select_bg = h(mix(_bg_rgb, rgb, _dark ? 18 : 22));
    select_border = _style == STYLE_TACTICAL ? h(rgb) : select_bg;
  }

  void applyCustom(const char* src) {
    const char* p = src;
    while (p && *p) {
      const char* e = strchr(p, '\n');
      size_t n = e ? (size_t)(e - p) : strlen(p);
      char line[48];
      if (n < sizeof(line)) {
        memcpy(line, p, n); line[n] = 0;
        char* eq = strchr(line, '=');
        char* hash = eq ? strchr(eq, '#') : nullptr;
        if (eq && hash && line[0] != '#') {
          *eq = 0;
          char* k = line; while (*k == ' ') k++;
          char* ke = eq; while (ke > k && ke[-1] == ' ') *--ke = 0;
          char* end = nullptr;
          unsigned long v = strtoul(hash + 1, &end, 16);
          if (end - (hash + 1) == 6) {
            lv_color_t c = h(v);
            if (!strcmp(k, "bg")) { bg = c; _bg_rgb = v; }
            else if (!strcmp(k, "surface")) surface = c;
            else if (!strcmp(k, "surface2")) surface2 = c;
            else if (!strcmp(k, "border")) border = c;
            else if (!strcmp(k, "text")) { text = c; bubble_theirs_text = c; }
            else if (!strcmp(k, "muted")) muted = c;
            else if (!strcmp(k, "faint")) faint = c;
            else if (!strcmp(k, "accent")) setAccent(v);
            else if (!strcmp(k, "bubble_mine")) { bubble_mine = c; bubble_mine_text = lum(v) > 150 ? h(0x000000) : h(0xFFFFFF); }
            else if (!strcmp(k, "bubble_theirs")) bubble_theirs = c;
            else if (!strcmp(k, "danger")) danger = c;
          }
        }
      }
      p = e ? e + 1 : nullptr;
    }
  }

  void apply(const UiSettings& s) {
    uint8_t th = themeStyle(s.theme);
    _style = th == 0 ? STYLE_TACTICAL : STYLE_MODERN;
    _dark = th != 2;
    _easy = s.easy_mode != 0;
    danger = h(0xFF453A);
    warning = h(0xFFB000);
    success = h(0x30D158);

    if (_style == STYLE_TACTICAL) {
      bg = h(0x000000); _bg_rgb = 0x000000; surface = h(0x0E0E0E); surface2 = h(0x1A1A1A); border = h(0x3A3A3A);
      text = h(0xF2F2F2); muted = h(0x9C9C9C); faint = h(0x5E5E5E);
      accent = h(0xFFB000); accent_text = h(0xFFB000); on_accent = h(0x000000);
      bubble_mine = h(0xFFB000); bubble_mine_text = h(0x000000);
      bubble_theirs = h(0x1A1A1A); bubble_theirs_text = h(0xF2F2F2);
      select_bg = h(0x241A00); select_border = h(0xFFB000);
      f12 = &tac_f12; f14 = &tac_f14; f14b = &tac_f14b; f16 = &tac_f16; f16b = &tac_f16b;
      f20 = &tac_f20; f20b = &tac_f20b; f24 = &tac_f24; f24b = &tac_f24b; f32b = &tac_f32b;
      shadows = false; round_avatars = false;
    } else if (_dark) {
      bg = h(0x0B1220); _bg_rgb = 0x0B1220; surface = h(0x131C2E); surface2 = h(0x1C2840); border = h(0x2A3A57);
      text = h(0xF1F5F9); muted = h(0x94A3B8); faint = h(0x56657E);
      accent = h(0x3B82F6); accent_text = h(0x60A5FA); on_accent = h(0xFFFFFF);
      bubble_mine = h(0x2563EB); bubble_mine_text = h(0xFFFFFF);
      bubble_theirs = h(0x1C2840); bubble_theirs_text = h(0xF1F5F9);
      select_bg = h(0x1E3A66); select_border = h(0x1E3A66);
      f12 = &mod_f12; f14 = &mod_f14; f14b = &mod_f14b; f16 = &mod_f16; f16b = &mod_f16b;
      f20 = &mod_f20; f20b = &mod_f20b; f24 = &mod_f24; f24b = &mod_f24b; f32b = &mod_f32b;
      shadows = true; round_avatars = true;
    } else {
      bg = h(0xEEF2F7); _bg_rgb = 0xEEF2F7; surface = h(0xFFFFFF); surface2 = h(0xDDE5F0); border = h(0xC5D0DF);
      text = h(0x0F172A); muted = h(0x475569); faint = h(0x94A3B8);
      accent = h(0x2563EB); accent_text = h(0x1D4ED8); on_accent = h(0xFFFFFF);
      bubble_mine = h(0x2563EB); bubble_mine_text = h(0xFFFFFF);
      bubble_theirs = h(0xFFFFFF); bubble_theirs_text = h(0x0F172A);
      select_bg = h(0xCFE0FB); select_border = h(0xCFE0FB);
      f12 = &mod_f12; f14 = &mod_f14; f14b = &mod_f14b; f16 = &mod_f16; f16b = &mod_f16b;
      f20 = &mod_f20; f20b = &mod_f20b; f24 = &mod_f24; f24b = &mod_f24b; f32b = &mod_f32b;
      shadows = true; round_avatars = true;
    }
    bool tac = _style == STYLE_TACTICAL;
    uint8_t ac = themeAccent(s.theme);
    if (ac > 0 && ac < NUM_ACCENTS) setAccent(kAccentRgb[ac]);

    if (_easy) {
      caption = f16b; small = f20; body = f24; body_semi = f24b; title = f24b; hero = f32b;
      icon = &icons_32; icon_big = &icons_48;
      row_h = 72; avatar = 48; radius = tac ? 2 : 16; pad = 10;
    } else if (s.text_size == 1) {
      caption = f12; small = f14; body = f16; body_semi = f16b; title = f20b; hero = f32b;
      icon = &icons_20; icon_big = &icons_32;
      row_h = 36; avatar = 26; radius = tac ? 0 : 8; pad = 4;
    } else {
      caption = f12; small = f12; body = f14; body_semi = f14b; title = f16b; hero = f32b;
      icon = &icons_16; icon_big = &icons_32;
      row_h = 30; avatar = 22; radius = tac ? 0 : 8; pad = 4;
    }
  }

  lv_color_t avatarColor(const uint8_t* key) {
    static const uint32_t pal[] = {
      0x3B82F6, 0xF97316, 0x06B6D4, 0xA855F7, 0xEAB308, 0x22C55E, 0xEC4899, 0x6366F1, 0x14B8A6, 0xEF4444
    };
    uint32_t hh = 2166136261u;
    for (int i = 0; i < LOG_KEY_LEN; i++) hh = (hh ^ key[i]) * 16777619u;
    return lv_color_hex(pal[hh % (sizeof(pal) / sizeof(pal[0]))]);
  }
}

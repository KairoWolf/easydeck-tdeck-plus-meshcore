#pragma once
// EasyDeck design system. Two visual styles share one set of tokens:
//   Tactical (default): black + amber, Barlow Semi Condensed, square edges, outlines, no glow
//   Modern (MeshOS-like): navy + blue, Ubuntu, rounded cards, colourful filled icons
#include <lvgl.h>
#include "fonts/fonts.h"
#include "fonts/Icons.h"
#include "core/Model.h"

#define SCR_W 320
#define SCR_H 240
#define STATUS_H 20
#define TABBAR_H 0
#define INFOBAR_H 18

enum UiStyle : uint8_t { STYLE_TACTICAL = 0, STYLE_MODERN = 1 };
// UiSettings.theme: low 4 bits = style (0 Tactical, 1 Modern dark, 2 Modern light),
// high 4 bits = accent colour (0 = the style's own, 1..NUM_ACCENTS-1 presets, ACCENT_CUSTOM = theme file)
#define NUM_THEMES 3
static const char* const kThemeNames[NUM_THEMES] = { "Tactical", "Modern", "Modern light" };
#define NUM_ACCENTS 9
#define ACCENT_CUSTOM 15
static const char* const kAccentNames[NUM_ACCENTS] = { "Style colour", "Amber", "Green", "Cyan", "Blue", "Purple", "Pink", "Red", "White" };
static const uint32_t kAccentRgb[NUM_ACCENTS] = { 0, 0xFFB000, 0x30D158, 0x22D3EE, 0x3B82F6, 0xA855F7, 0xEC4899, 0xFF453A, 0xE5E7EB };
inline uint8_t themeStyle(uint8_t t) { return (t & 0x0F) < NUM_THEMES ? (t & 0x0F) : 0; }
inline uint8_t themeAccent(uint8_t t) { return t >> 4; }
inline uint8_t themeMake(uint8_t style, uint8_t accent) { return (uint8_t)((accent << 4) | (style & 0x0F)); }
#define THEME_FILE "/easydeck/theme.txt"   // custom colours (SD card), see T::applyCustom

namespace T {
  // custom colours: lines like "accent=#FF8800" (keys: bg surface surface2 border text muted faint
  // accent bubble_mine bubble_theirs danger); unknown keys and bad values are ignored
  void applyCustom(const char* text);
  // ---- colours ----
  extern lv_color_t bg, surface, surface2, border, text, muted, faint;
  extern lv_color_t accent, accent_text, on_accent, danger, warning, success;
  extern lv_color_t bubble_mine, bubble_mine_text, bubble_theirs, bubble_theirs_text;
  extern lv_color_t select_bg, select_border;

  // ---- fonts by size (current family) ----
  extern const lv_font_t *f12, *f14, *f14b, *f16, *f16b, *f20, *f20b, *f24, *f24b, *f32b;
  // ---- fonts by role ----
  extern const lv_font_t* caption;
  extern const lv_font_t* small;
  extern const lv_font_t* body;
  extern const lv_font_t* body_semi;
  extern const lv_font_t* title;
  extern const lv_font_t* hero;
  extern const lv_font_t* icon;       // icons sized for list rows
  extern const lv_font_t* icon_big;

  // ---- metrics & shape ----
  extern int row_h, avatar, radius, pad;
  extern bool shadows;      // soft shadows/glows (Modern only)
  extern bool round_avatars;

  constexpr uint32_t ANIM_FAST = 140;
  constexpr uint32_t ANIM_SCREEN = 200;

  void apply(const UiSettings& s);
  UiStyle style();
  bool dark();
  bool easy();
  lv_color_t avatarColor(const uint8_t* key);
}

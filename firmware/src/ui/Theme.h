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
// UiSettings.theme: 0 Tactical, 1 Modern dark, 2 Modern light
#define NUM_THEMES 3
static const char* const kThemeNames[NUM_THEMES] = { "Tactical", "Modern", "Modern light" };

namespace T {
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

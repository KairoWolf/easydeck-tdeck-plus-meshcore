#include "MapView.h"
#include "Theme.h"
#include "Widgets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace mapv {

double lon2x(double lon, int z) { return (lon + 180.0) / 360.0 * (double)(1 << z) * TILE; }
double lat2y(double lat, int z) {
  double r = lat * M_PI / 180.0;
  return (1.0 - log(tan(r) + 1.0 / cos(r)) / M_PI) / 2.0 * (double)(1 << z) * TILE;
}
double x2lon(double x, int z) { return x / ((double)(1 << z) * TILE) * 360.0 - 180.0; }
double y2lat(double y, int z) {
  double n = M_PI - 2.0 * M_PI * y / ((double)(1 << z) * TILE);
  return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

TileRange tileRange(double lat, double lon, double radius_km, int z) {
  double dlat = radius_km / 111.32;
  double c = cos(lat * M_PI / 180.0);
  double dlon = radius_km / (111.32 * (c < 0.05 ? 0.05 : c));
  double la0 = fmax(-85.0, lat - dlat), la1 = fmin(85.0, lat + dlat);
  double lo0 = fmax(-180.0, lon - dlon), lo1 = fmin(179.9999, lon + dlon);
  int n = 1 << z;
  TileRange r;
  r.x0 = (int)floor(lon2x(lo0, z) / TILE); r.x1 = (int)floor(lon2x(lo1, z) / TILE);
  r.y0 = (int)floor(lat2y(la1, z) / TILE); r.y1 = (int)floor(lat2y(la0, z) / TILE);   // north = smaller y
  if (r.x0 < 0) r.x0 = 0; if (r.y0 < 0) r.y0 = 0;
  if (r.x1 > n - 1) r.x1 = n - 1; if (r.y1 > n - 1) r.y1 = n - 1;
  return r;
}

uint32_t tilesInArea(double lat, double lon, double radius_km, int zmin, int zmax) {
  uint32_t t = 0;
  for (int z = zmin; z <= zmax; z++) {
    TileRange r = tileRange(lat, lon, radius_km, z);
    t += (uint32_t)(r.x1 - r.x0 + 1) * (uint32_t)(r.y1 - r.y0 + 1);
  }
  return t;
}

bool tileOk(Model* m, int z, int x, int y) {
  char p[128];
  snprintf(p, sizeof(p), "%s/%d/%d/%d.png", m->tileRoot(), z, x, y);
  lv_fs_file_t f;
  if (lv_fs_open(&f, p, LV_FS_MODE_RD) != LV_FS_RES_OK) return false;
  // Only accept PNGs with <= 8 bits per channel: LVGL 9.2's lodepng path overflows on
  // 16-bit images. (Ripple/OSM tiles are 8-bit palette or RGB(A).)
  uint8_t hdr[26];
  uint32_t rn = 0;
  bool ok = lv_fs_read(&f, hdr, sizeof(hdr), &rn) == LV_FS_RES_OK && rn == sizeof(hdr) &&
            hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G' && hdr[24] <= 8;
  lv_fs_close(&f);
  return ok;
}

bool anyTileNear(Model* m, double lat, double lon, int z) {
  int n = 1 << z;
  int tx = (int)floor(lon2x(lon, z) / TILE), ty = (int)floor(lat2y(lat, z) / TILE);
  if (ty < 0 || ty >= n) return false;
  return tileOk(m, z, ((tx % n) + n) % n, ty);
}

int drawTiles(lv_obj_t* view, Model* m, double lat, double lon, int z, int w, int h) {
  double ox = lon2x(lon, z) - w / 2.0, oy = lat2y(lat, z) - h / 2.0;
  int tx0 = (int)floor(ox / TILE), ty0 = (int)floor(oy / TILE);
  int tx1 = (int)floor((ox + w - 1) / TILE), ty1 = (int)floor((oy + h - 1) / TILE);
  int n = 1 << z, have = 0;
  for (int ty = ty0; ty <= ty1; ty++) {
    for (int tx = tx0; tx <= tx1; tx++) {
      if (ty < 0 || ty >= n) continue;
      int wx = ((tx % n) + n) % n;
      if (!tileOk(m, z, wx, ty)) continue;
      char p[128];
      snprintf(p, sizeof(p), "%s/%d/%d/%d.png", m->tileRoot(), z, wx, ty);
      lv_obj_t* img = lv_image_create(view);
      lv_image_set_src(img, p);
      lv_obj_set_pos(img, (int)(tx * TILE - ox), (int)(ty * TILE - oy));
      lv_obj_set_size(img, TILE, TILE);
      have++;
    }
  }
  return have;
}

void project(double lat, double lon, double clat, double clon, int z, int w, int h, int& px, int& py) {
  px = (int)lround(lon2x(lon, z) - lon2x(clon, z) + w / 2.0);
  py = (int)lround(lat2y(lat, z) - lat2y(clat, z) + h / 2.0);
}

static void ring_anim(void* o, int32_t v) {
  lv_obj_t* r = (lv_obj_t*)o;
  int cx = (int)(intptr_t)lv_obj_get_user_data(r);
  int px = cx >> 16, py = (int16_t)(cx & 0xFFFF);
  int s = 10 + v / 4;   // 10 .. 35 px
  lv_obj_set_size(r, s, s);
  lv_obj_set_pos(r, px - s / 2, py - s / 2);
  lv_obj_set_style_border_opa(r, (lv_opa_t)(255 - v * 255 / 100), 0);
}

static void pin(lv_obj_t* view, int px, int py, const Pin& p) {
  if (p.pulse) {   // expanding, fading ring
    lv_obj_t* r = ui::box(view);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, 2, 0);
    lv_obj_set_style_border_color(r, p.color, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_user_data(r, (void*)(intptr_t)((px << 16) | (py & 0xFFFF)));
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, r);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 1400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, ring_anim);
    lv_anim_start(&a);
  }
  lv_obj_t* d = ui::box(view);
  int s = p.pulse ? 12 : 10;
  lv_obj_set_size(d, s, s);
  lv_obj_set_pos(d, px - s / 2, py - s / 2);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, p.color, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(d, 2, 0);
  lv_obj_set_style_border_color(d, lv_color_white(), 0);
  if (p.label && *p.label) {
    lv_obj_t* l = ui::label(view, p.label, T::f12, lv_color_white());
    lv_obj_set_style_bg_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(l, 3, 0);
    lv_obj_set_pos(l, px + 8, py - 8);
  }
}

// metres per pixel at this latitude / zoom (Web Mercator)
static double mpp(double lat, int z) { return 156543.03392 * cos(lat * M_PI / 180.0) / (double)(1 << z); }

lv_obj_t* preview(lv_obj_t* parent, Model* m, int x, int y, int w, int h, const Pin* pins, int n, int focus) {
  lv_obj_t* v = ui::box(parent);
  lv_obj_set_pos(v, x, y);
  lv_obj_set_size(v, w, h);
  lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(v, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(v, lv_color_hex(0x1C2026), 0);
  lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(v, 1, 0);
  lv_obj_set_style_border_color(v, lv_color_hex(0x000000), 0);
  lv_obj_set_style_radius(v, T::style() == STYLE_TACTICAL ? 0 : 8, 0);
  lv_obj_set_style_clip_corner(v, true, 0);
  if (n <= 0) return v;
  if (focus < 0 || focus >= n) focus = 0;
  // centre on the middle of all pins (one pin: on it), so two far-apart pins get the most detail
  double la0 = pins[0].lat, la1 = la0, lo0 = pins[0].lon, lo1 = lo0;
  for (int i = 1; i < n; i++) {
    la0 = fmin(la0, pins[i].lat); la1 = fmax(la1, pins[i].lat);
    lo0 = fmin(lo0, pins[i].lon); lo1 = fmax(lo1, pins[i].lon);
  }
  double clat = n > 1 ? (la0 + la1) / 2 : pins[focus].lat, clon = n > 1 ? (lo0 + lo1) / 2 : pins[focus].lon;

  // highest zoom (<= 16) that keeps every pin inside the view with a margin
  int z = 16;
  for (; z > 3; z--) {
    bool fits = true;
    for (int i = 0; i < n && fits; i++) {
      int px, py;
      project(pins[i].lat, pins[i].lon, clat, clon, z, w, h, px, py);
      fits = px >= 14 && px <= w - 14 && py >= 14 && py <= h - 14;
    }
    if (fits) break;
  }
  // fall back to a zoom we actually have tiles for (tile packs often stop at z12-14)
  int tz = -1;
  for (int k = z; k >= 3 && k >= z - 8; k--) if (anyTileNear(m, clat, clon, k)) { tz = k; break; }

  if (tz >= 0) {
    z = tz;
    drawTiles(v, m, clat, clon, z, w, h);
  } else {   // no tiles: a plain grid still shows direction and distance
    for (int gx = w / 4; gx < w; gx += w / 4) {
      lv_obj_t* l = ui::box(v);
      lv_obj_set_size(l, 1, h); lv_obj_set_pos(l, gx, 0);
      lv_obj_set_style_bg_color(l, lv_color_hex(0x323A44), 0); lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
    for (int gy = h / 4; gy < h; gy += h / 4) {
      lv_obj_t* l = ui::box(v);
      lv_obj_set_size(l, w, 1); lv_obj_set_pos(l, 0, gy);
      lv_obj_set_style_bg_color(l, lv_color_hex(0x323A44), 0); lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
    lv_obj_t* nt = ui::label(v, "no map tiles", T::f12, lv_color_hex(0x8A96A3));
    lv_obj_align(nt, LV_ALIGN_TOP_LEFT, 4, 2);
  }

  for (int i = 0; i < n; i++) {
    int px, py;
    project(pins[i].lat, pins[i].lon, clat, clon, z, w, h, px, py);
    if (px < -20 || px > w + 20 || py < -20 || py > h + 20) continue;
    pin(v, px, py, pins[i]);
  }

  // scale bar (bottom-left): a round distance close to w/4 pixels
  double m_per_px = mpp(clat, z);
  double target = m_per_px * (w / 4.0);
  static const double steps[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000 };
  double d = steps[0];
  for (double s : steps) if (s <= target) d = s;
  int bar = (int)(d / m_per_px);
  lv_obj_t* sb = ui::box(v);
  lv_obj_set_size(sb, bar, 3);
  lv_obj_align(sb, LV_ALIGN_BOTTOM_LEFT, 6, -5);
  lv_obj_set_style_bg_color(sb, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sb, 1, 0);
  lv_obj_set_style_border_color(sb, lv_color_black(), 0);
  char t[16];
  if (d >= 1000) snprintf(t, sizeof(t), "%g km", d / 1000.0); else snprintf(t, sizeof(t), "%g m", d);
  lv_obj_t* sl = ui::label(v, t, T::f12, lv_color_white());
  lv_obj_set_style_bg_color(sl, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(sl, LV_OPA_60, 0);
  lv_obj_set_style_pad_hor(sl, 2, 0);
  lv_obj_align(sl, LV_ALIGN_BOTTOM_LEFT, 4, -10);
  return v;
}

}  // namespace mapv

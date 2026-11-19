#pragma once
// Shared slippy-map helpers: tile maths, tile drawing and a small "map preview" widget
// (used by the SOS screens). Tiles: <tileRoot>/z/x/y.png, 256 px, Web Mercator.
#include <lvgl.h>
#include "core/Model.h"

namespace mapv {

constexpr int TILE = 256;

double lon2x(double lon, int z);
double lat2y(double lat, int z);
double x2lon(double x, int z);
double y2lat(double y, int z);

// True if the tile exists and is a PNG LVGL can decode (<= 8 bits per channel).
bool tileOk(Model* m, int z, int x, int y);
bool anyTileNear(Model* m, double lat, double lon, int z);

// Draw the tiles covering a w x h view centred on (lat, lon) at zoom z into `view`.
// Returns the number of tiles drawn.
int drawTiles(lv_obj_t* view, Model* m, double lat, double lon, int z, int w, int h);

// Where (lat, lon) lands in that view, in pixels.
void project(double lat, double lon, double clat, double clon, int z, int w, int h, int& px, int& py);

// Tiles covering a square of +-radius_km around (lat, lon) at zoom z (inclusive ranges).
struct TileRange { int x0, x1, y0, y1; };
TileRange tileRange(double lat, double lon, double radius_km, int z);
uint32_t tilesInArea(double lat, double lon, double radius_km, int zmin, int zmax);

struct Pin {
  double lat, lon;
  lv_color_t color;
  bool pulse;          // animated ring (SOS)
  const char* label;   // optional
};

// A self-contained map preview: picks a zoom that fits all pins, falls back to lower zooms
// when there are no tiles, and to a plain grid with a scale when there are none at all.
// `focus` is the pin the map centres on.
lv_obj_t* preview(lv_obj_t* parent, Model* m, int x, int y, int w, int h,
                  const Pin* pins, int n_pins, int focus = 0);

}  // namespace mapv

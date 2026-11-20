// Pixel-art emoji runtime: the pixemoji_<size> fonts (LVGL image glyphs), on-demand 2x/3x
// scaling into PSRAM, and packing of multi-codepoint emoji into private-use codes.
#include "PixEmoji.h"
#include "lvgl.h"
#include <string.h>

extern const int pix_n, pix_nseq, pix_ngroups, pix_norder;
extern const uint32_t pix_keys[], pix_offs[], pix_seq[], pix_unpack[], pix_order[], pix_name_off[];
extern const uint8_t pix_w[], pix_blob[];
extern const uint16_t pix_unpack_off[], pix_group_first[], pix_group_count[];
extern const char* const pix_group_names[];
extern const char pix_names[];

#define PIX_H 10
#define CACHE_CAP (1536u * 1024u)   // scaled copies kept in PSRAM; past this, 1x is used

static int find(uint32_t cp) {
  int lo = 0, hi = pix_n - 1;
  while (lo <= hi) { int m = (lo + hi) / 2; if (pix_keys[m] == cp) return m; if (pix_keys[m] < cp) lo = m + 1; else hi = m - 1; }
  return -1;
}
int pixemoji_has(uint32_t cp) { return find(cp) >= 0; }

// ---- images ----
static lv_image_dsc_t** s_cache[4];
static size_t s_bytes;

static const lv_image_dsc_t* image(int i, int sc) {
  if (sc < 1 || sc > 3) sc = 1;
  if (!s_cache[sc]) {
    s_cache[sc] = (lv_image_dsc_t**)lv_malloc(sizeof(lv_image_dsc_t*) * pix_n);
    if (!s_cache[sc]) return NULL;
    memset(s_cache[sc], 0, sizeof(lv_image_dsc_t*) * pix_n);
  }
  if (s_cache[sc][i]) return s_cache[sc][i];
  int w = pix_w[i], W = w * sc, H = PIX_H * sc;
  const uint8_t* src = pix_blob + pix_offs[i];
  size_t need = sizeof(lv_image_dsc_t) + (sc > 1 ? (size_t)W * H * 3 : 0);
  if (sc > 1 && s_bytes + need > CACHE_CAP) return image(i, 1);
  lv_image_dsc_t* d = (lv_image_dsc_t*)lv_malloc(need);
  if (!d) return sc > 1 ? image(i, 1) : NULL;
  memset(d, 0, sizeof(*d));
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB565A8;
  d->header.w = W; d->header.h = H; d->header.stride = W * 2;
  d->data_size = (uint32_t)W * H * 3;
  if (sc == 1) d->data = src;
  else {   // nearest neighbour: pixels stay crisp
    uint8_t* px = (uint8_t*)(d + 1);
    uint8_t* a = px + (size_t)W * H * 2;
    const uint8_t* sa = src + (size_t)w * PIX_H * 2;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        int s = (y / sc) * w + x / sc, o = y * W + x;
        px[o * 2] = src[s * 2]; px[o * 2 + 1] = src[s * 2 + 1];
        a[o] = sa[s];
      }
    d->data = px;
  }
  s_bytes += need;
  s_cache[sc][i] = d;
  return d;
}

// ---- fonts ----
typedef struct { int scale; } pix_dsc_t;

static int invisible(uint32_t c) {   // joiners, selectors, skin tones and tags left over after packing
  return c == 0xFE0F || c == 0xFE0E || c == 0x200D || c == 0x20E3 || (c >= 0x1F3FB && c <= 0x1F3FF) ||
         (c >= 0xE0020 && c <= 0xE007F);
}

static bool pix_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* d, uint32_t letter, uint32_t next) {
  (void)next;
  const pix_dsc_t* p = (const pix_dsc_t*)font->dsc;
  if (invisible(letter)) {
    d->adv_w = 0; d->box_w = 0; d->box_h = 0; d->ofs_x = 0; d->ofs_y = 0;
    d->format = LV_FONT_GLYPH_FORMAT_NONE; d->is_placeholder = 0; d->gid.src = NULL;
    return true;
  }
  int i = find(letter);
  if (i < 0) return false;   // not in the pixel set: the fallback (Noto Emoji) draws it
  const lv_image_dsc_t* img = image(i, p->scale);
  if (!img) return false;
  int sc = img->header.h / PIX_H;
  d->is_placeholder = 0;
  d->adv_w = img->header.w + sc;   // one pixel of space after it
  d->box_w = img->header.w;
  d->box_h = img->header.h;
  d->ofs_x = 0;
  d->ofs_y = -sc;                  // sit slightly below the baseline, like text
  d->format = LV_FONT_GLYPH_FORMAT_IMAGE;
  d->gid.src = img;
  return true;
}

static const void* pix_glyph_bitmap(lv_font_glyph_dsc_t* d, lv_draw_buf_t* b) { (void)b; return d->gid.src; }

#define PIXFONT(size, sc) \
  extern const lv_font_t emoji_##size; \
  static const pix_dsc_t pd_##size = { sc }; \
  const lv_font_t pixemoji_##size = { .get_glyph_dsc = pix_glyph_dsc, .get_glyph_bitmap = pix_glyph_bitmap, \
    .line_height = PIX_H * sc, .base_line = 0, .subpx = LV_FONT_SUBPX_NONE, .dsc = &pd_##size, .fallback = &emoji_##size };
PIXFONT(12, 1) PIXFONT(14, 1) PIXFONT(16, 2) PIXFONT(20, 2) PIXFONT(24, 2) PIXFONT(32, 3) PIXFONT(48, 3)

// ---- UTF-8 ----
static uint32_t dec(const char* s, int* len) {
  const unsigned char* u = (const unsigned char*)s;
  if (u[0] < 0x80) { *len = 1; return u[0]; }
  if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) { *len = 2; return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); }
  if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
    *len = 3; return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
  }
  if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
    *len = 4; return ((uint32_t)(u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
  }
  *len = 1; return u[0];   // stray byte: pass it through
}

int pixemoji_utf8(uint32_t c, char* b) {
  if (c < 0x80) { b[0] = (char)c; b[1] = 0; return 1; }
  if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3F)); b[2] = 0; return 2; }
  if (c < 0x10000) { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); b[3] = 0; return 3; }
  b[0] = (char)(0xF0 | (c >> 18)); b[1] = (char)(0x80 | ((c >> 12) & 0x3F)); b[2] = (char)(0x80 | ((c >> 6) & 0x3F));
  b[3] = (char)(0x80 | (c & 0x3F)); b[4] = 0; return 4;
}

// ---- packing ----
static const int* seq_starts(void) {   // offset of each variable-length record in pix_seq
  static int* starts = NULL;
  if (!starts) {
    starts = (int*)lv_malloc(sizeof(int) * (pix_nseq + 1));
    if (!starts) return NULL;
    int o = 0;
    for (int i = 0; i < pix_nseq; i++) { starts[i] = o; o += (int)pix_seq[o] + 2; }
    starts[pix_nseq] = o;
  }
  return starts;
}

static int seq_first(uint32_t c) {   // first record starting with c (records sorted by first cp), or -1
  const int* st = seq_starts();
  if (!st) return -1;
  int lo = 0, hi = pix_nseq - 1, r = -1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (pix_seq[st[m] + 1] >= c) { r = m; hi = m - 1; } else lo = m + 1;
  }
  return r >= 0 && pix_seq[st[r] + 1] == c ? r : -1;
}

int pixemoji_needs_pack(const char* s) {
  for (const unsigned char* u = (const unsigned char*)s; *u; u++)
    if (*u >= 0xE2) return 1;   // 3/4-byte sequences: anything that can start or join an emoji
  return 0;
}

size_t pixemoji_pack(const char* in, char* out, size_t outsz) {
  size_t o = 0;
  if (!outsz) return 0;
  const char* p = in;
  while (*p) {
    int l;
    uint32_t c = dec(p, &l);
    int rec = c >= 0x23 ? seq_first(c) : -1;
    int used = 0; uint32_t key = 0;
    if (rec >= 0) {
      // decode up to 10 code points ahead (skipping FE0F) to compare against the records
      uint32_t cp[10]; int end[10]; int n = 0; const char* q = p;
      while (*q && n < 10) {
        int ql; uint32_t x = dec(q, &ql); q += ql;
        if (x == 0xFE0F || x == 0xFE0E) { if (n) end[n - 1] = (int)(q - p); continue; }
        cp[n] = x; end[n] = (int)(q - p); n++;
      }
      const int* st = seq_starts();
      for (int r = rec; r < pix_nseq && pix_seq[st[r] + 1] == c; r++) {   // longest first
        const uint32_t* R = pix_seq + st[r];
        int len = (int)R[0];
        if (len > n) continue;
        int ok = 1;
        for (int k = 1; k < len && ok; k++) ok = R[1 + k] == cp[k];
        if (ok) { key = R[1 + len]; used = end[len - 1]; break; }
      }
    }
    char b[5]; int bl;
    if (key) { bl = pixemoji_utf8(key, b); p += used; }
    else { memcpy(b, p, l); bl = l; p += l; }
    if (o + bl >= outsz) break;
    memcpy(out + o, b, bl); o += bl;
  }
  out[o] = 0;
  return o;
}

static int unpack_one(uint32_t c, char* b, size_t room) {   // bytes written, or -1 if not a sequence code
  if (c < PIX_PUA || c >= PIX_PUA + (uint32_t)pix_nseq) return -1;
  const uint32_t* u = pix_unpack + pix_unpack_off[c - PIX_PUA];
  int n = (int)u[0], w = 0;
  for (int k = 1; k <= n; k++) {
    char t[5]; int tl = pixemoji_utf8(u[k], t);
    if ((size_t)(w + tl) < room) memcpy(b + w, t, tl);
    w += tl;
  }
  return w;
}

size_t pixemoji_unpack(const char* in, char* out, size_t outsz) {
  size_t o = 0;
  if (!outsz) return 0;
  for (const char* p = in; *p;) {
    int l; uint32_t c = dec(p, &l);
    char b[48]; int bl = unpack_one(c, b, sizeof(b));
    if (bl < 0) { memcpy(b, p, l); bl = l; }
    if (o + bl >= outsz) break;
    memcpy(out + o, b, bl); o += bl; p += l;
  }
  out[o] = 0;
  return o;
}

size_t pixemoji_unpacked_len(const char* s) {
  size_t n = 0;
  for (const char* p = s; *p;) {
    int l; uint32_t c = dec(p, &l);
    char b[48]; int bl = unpack_one(c, b, sizeof(b));
    n += bl < 0 ? (size_t)l : (size_t)bl; p += l;
  }
  return n;
}

// ---- picker ----
int pixemoji_group_count(void) { return pix_ngroups; }
const char* pixemoji_group_name(int g) { return g >= 0 && g < pix_ngroups ? pix_group_names[g] : ""; }
int pixemoji_group_first(int g) { return g >= 0 && g < pix_ngroups ? pix_group_first[g] : 0; }
int pixemoji_group_size(int g) { return g >= 0 && g < pix_ngroups ? pix_group_count[g] : 0; }
int pixemoji_total(void) { return pix_norder; }
uint32_t pixemoji_at(int i) { return i >= 0 && i < pix_norder ? pix_order[i] : 0; }
const char* pixemoji_name(int i) { return i >= 0 && i < pix_norder ? pix_names + pix_name_off[i] : ""; }

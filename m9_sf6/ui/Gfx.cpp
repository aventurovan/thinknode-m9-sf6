#include "Gfx.h"
#include <string.h>
#include "EmojiData.h"

void Gfx::clear(uint16_t c) { for (int i = 0; i < GFX_W * GFX_H; i++) _fb[i] = c; }

void Gfx::fillRect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > GFX_W) w = GFX_W - x;
  if (y + h > GFX_H) h = GFX_H - y;
  for (int j = 0; j < h; j++) {
    uint16_t* row = _fb + (y + j) * GFX_W + x;
    for (int i = 0; i < w; i++) row[i] = c;
  }
}

void Gfx::rect(int x, int y, int w, int h, uint16_t c) {
  fillRect(x, y, w, 1, c); fillRect(x, y + h - 1, w, 1, c);
  fillRect(x, y, 1, h, c); fillRect(x + w - 1, y, 1, h, c);
}

// ile pikseli ścinamy w wierszu i od rogu o promieniu r (i = 0 to skrajny wiersz)
static int cornerCut(int r, int i) {
  int dy = r - i, d = 0;
  while ((r - d) * (r - d) + dy * dy > r * r + r / 2) d++;
  return d;
}

void Gfx::fillRound(int x, int y, int w, int h, int r, uint16_t c) {
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  for (int j = 0; j < h; j++) {
    int cut = 0;
    if (j < r) cut = cornerCut(r, j); else if (j >= h - r) cut = cornerCut(r, h - 1 - j);
    fillRect(x + cut, y + j, w - 2 * cut, 1, c);
  }
}

void Gfx::image(int x, int y, int w, int h, const uint16_t* px) {
  for (int j = 0; j < h; j++) {
    int py = y + j;
    if (py < 0 || py >= GFX_H) continue;
    for (int i = 0; i < w; i++) { int qx = x + i; if (qx >= 0 && qx < GFX_W) _fb[py * GFX_W + qx] = px[j * w + i]; }
  }
}

void Gfx::dim() {
  for (int i = 0; i < GFX_W * GFX_H; i++) {
    uint16_t p = _fb[i];
    _fb[i] = (uint16_t)(((p >> 2) & 0x39E7));        // każdy kanał do 1/4
  }
}

void Gfx::swapBytes() {
  for (int i = 0; i < GFX_W * GFX_H; i++) { uint16_t p = _fb[i]; _fb[i] = (uint16_t)((p >> 8) | (p << 8)); }
}

uint32_t Gfx::nextCp(const char*& s, const char* end) {
  if (s >= end) return 0;
  uint8_t c = (uint8_t)*s++;
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  if (extra == 0) return 0xFFFD;
  while (extra-- > 0) {
    if (s >= end || ((uint8_t)*s & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | ((uint8_t)*s++ & 0x3F);
  }
  return cp;
}

const GfxGlyph* Gfx::glyph(const GfxFont& f, uint32_t cp) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t g = f.glyphs[mid].cp;
    if (g == cp) return &f.glyphs[mid];
    if (g < cp) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

void Gfx::drawGlyph(int x, int y, const GfxFont& f, const GfxGlyph* g, uint16_t c) {
  int r = (c >> 11) & 31, gg = (c >> 5) & 63, b = c & 31;
  const uint8_t* bits = f.bits + g->off;
  int idx = 0;
  for (int j = 0; j < g->h; j++) {
    int py = y + g->yo + j;
    for (int i = 0; i < g->w; i++, idx++) {
      uint8_t a = (idx & 1) ? (bits[idx >> 1] & 15) : (bits[idx >> 1] >> 4);
      if (a == 0) continue;
      int px = x + g->xo + i;
      if (px < 0 || py < 0 || px >= GFX_W || py >= GFX_H) continue;
      uint16_t* p = &_fb[py * GFX_W + px];
      if (a == 15) { *p = c; continue; }
      int br = (*p >> 11) & 31, bg = (*p >> 5) & 63, bb = *p & 31;
      br += (r - br) * a / 15; bg += (gg - bg) * a / 15; bb += (b - bb) * a / 15;
      *p = (uint16_t)((br << 11) | (bg << 5) | bb);
    }
  }
}

int Gfx::emojiFind(uint32_t key) {
  int lo = 0, hi = EMOJI_N - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t k = EMOJI_CP[mid];
    if (k == key) return mid;
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

// Obrazek ma 16x16; w małym tekście (size 12) jest pomniejszany: każdy piksel to średnia z 4 próbek źródła.
void Gfx::emoji(int x, int y, int idx, int size) {
  if (idx < 0 || idx >= EMOJI_N) return;
  const uint16_t* rgb = EMOJI16_RGB + idx * 256;
  const uint8_t* al = EMOJI16_A + idx * 128;
  if (size != 16) size = 12;
  for (int j = 0; j < size; j++) {
    int py = y + j;
    if (py < 0 || py >= GFX_H) continue;
    for (int i = 0; i < size; i++) {
      int px = x + i;
      if (px < 0 || px >= GFX_W) continue;
      int a, r, g, b;
      if (size == 16) {
        int k = j * 16 + i;
        a = (k & 1) ? (al[k >> 1] & 15) : (al[k >> 1] >> 4);
        if (a == 0) continue;
        uint16_t c = rgb[k];
        r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31;
      } else {
        int sa = 0, sr = 0, sg = 0, sb = 0;
        for (int q = 0; q < 4; q++) {                  // próbki w ćwiartkach piksela docelowego (skala 4:3)
          int sx = (i * 8 + (q & 1) * 4 + 2) / 6, sy = (j * 8 + (q >> 1) * 4 + 2) / 6;
          if (sx > 15) sx = 15;
          if (sy > 15) sy = 15;
          int k = sy * 16 + sx;
          int aa = (k & 1) ? (al[k >> 1] & 15) : (al[k >> 1] >> 4);
          uint16_t c = rgb[k];
          sa += aa; sr += ((c >> 11) & 31) * aa; sg += ((c >> 5) & 63) * aa; sb += (c & 31) * aa;
        }
        if (sa == 0) continue;
        r = sr / sa; g = sg / sa; b = sb / sa; a = (sa + 2) / 4;
        if (a == 0) continue;
      }
      uint16_t* p = &_fb[py * GFX_W + px];
      if (a >= 15) { *p = (uint16_t)((r << 11) | (g << 5) | b); continue; }
      int br = (*p >> 11) & 31, bg = (*p >> 5) & 63, bb = *p & 31;
      br += (r - br) * a / 15; bg += (g - bg) * a / 15; bb += (b - bb) * a / 15;
      *p = (uint16_t)((br << 11) | (bg << 5) | bb);
    }
  }
}

static bool emojiRange(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FBFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0x2300 && cp <= 0x23FF);
}

int Gfx::item(const char*& s, const char* end, const GfxFont& f, const GfxGlyph*& g, int& em) {
  uint32_t cp = nextCp(s, end);
  g = cp < 0x10000 ? glyph(f, cp) : NULL;
  em = -1;
  if (g) return g->adv;
  if (cp < 0x2000) return 0;
  // dodatki do emotek (wariant, łącznik, odcień skóry, ramka klawisza, znaczniki): nic nie rysują
  if (cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F)) return 0;
  int w = emojiSize(f) + 1;
  if (cp >= 0x1F1E6 && cp <= 0x1F1FF) {                // flaga = dwie litery regionu
    const char* q = s;
    uint32_t c2 = nextCp(q, end);
    em = -2;
    if (c2 >= 0x1F1E6 && c2 <= 0x1F1FF) {
      s = q;
      int i = emojiFind(0x80000000u | (('A' + (cp - 0x1F1E6)) << 8) | ('A' + (c2 - 0x1F1E6)));
      if (i >= 0) em = i;
    }
    return w;
  }
  int i = emojiFind(cp);
  if (i >= 0) { em = i; return w; }
  if (emojiRange(cp)) { em = -2; return w; }
  return 0;
}

void Gfx::drawItem(int x, int y, const GfxFont& f, const GfxGlyph* g, int em, uint16_t c) {
  if (g) { if (g->w) drawGlyph(x, y, f, g, c); return; }
  int sz = emojiSize(f), top = y + (f.line_h - sz) / 2;
  if (em >= 0) emoji(x, top, em, sz);
  else if (em == -2) rect(x + 2, top + 2, sz - 4, sz - 4, c);      // emotka spoza zestawu
}

int Gfx::textWidth(const char* s, const GfxFont& f, int nbytes) {
  const char* end = s + (nbytes < 0 ? strlen(s) : (size_t)nbytes);
  int w = 0, em;
  const GfxGlyph* g;
  while (s < end) w += item(s, end, f, g, em);
  return w;
}

int Gfx::textN(int x, int y, const char* s, int nbytes, const GfxFont& f, uint16_t c) {
  const char* end = s + nbytes;
  int x0 = x, em;
  const GfxGlyph* g;
  while (s < end) {
    int a = item(s, end, f, g, em);
    if (a) drawItem(x, y, f, g, em, c);
    x += a;
  }
  return x - x0;
}

int Gfx::text(int x, int y, const char* s, const GfxFont& f, uint16_t c, int maxW) {
  int len = (int)strlen(s);
  if (maxW <= 0 || textWidth(s, f) <= maxW) return textN(x, y, s, len, f, c);
  // obcinanie z wielokropkiem
  const GfxGlyph* dots = glyph(f, 0x2026);
  int dw = dots ? dots->adv : 0;
  const char* p = s; const char* end = s + len;
  int w = 0, fit = 0;
  while (p < end) {
    const char* q = p;
    const GfxGlyph* g; int em;
    int a = item(q, end, f, g, em);
    if (w + a + dw > maxW) break;
    w += a; p = q; fit = (int)(p - s);
  }
  while (fit > 0 && s[fit - 1] == ' ') fit--;
  int drawn = textN(x, y, s, fit, f, c);
  if (dots) { drawGlyph(x + drawn, y, f, dots, c); drawn += dw; }
  return drawn;
}

int Gfx::textRight(int xr, int y, const char* s, const GfxFont& f, uint16_t c) {
  int w = textWidth(s, f);
  text(xr - w, y, s, f, c);
  return w;
}

int Gfx::textCenter(int xc, int y, const char* s, const GfxFont& f, uint16_t c) {
  int w = textWidth(s, f);
  text(xc - w / 2, y, s, f, c);
  return w;
}

int Gfx::wrap(const char* s, const GfxFont& f, int maxW, GfxLine* lines, int maxLines) {
  int n = 0;
  const char* end = s + strlen(s);
  const char* p = s;
  while (p < end && n < maxLines) {
    while (p < end && *p == ' ') p++;
    if (p >= end) break;
    const char* line = p; const char* q = p; const char* lastSpace = NULL;
    int w = 0;
    while (q < end) {
      if (*q == '\n') break;
      const char* r = q;
      const GfxGlyph* g; int em;
      int a = item(r, end, f, g, em);
      if (w + a > maxW && q > line) break;
      if (*q == ' ') lastSpace = q;
      w += a; q = r;
    }
    const char* stop = q;
    if (q < end && *q != '\n' && lastSpace && lastSpace > line) stop = lastSpace;   // łamanie na spacji
    lines[n].start = (uint16_t)(line - s);
    lines[n].len = (uint16_t)(stop - line);
    n++;
    p = stop;
    if (p < end && *p == '\n') p++;
  }
  return n;
}

void Gfx::icon(int x, int y, const char* const* rows, uint16_t c) {
  for (int j = 0; j < 12; j++) {
    const char* r = rows[j];
    for (int i = 0; i < 12 && r[i]; i++) if (r[i] == '#') fillRect(x + i, y + j, 1, 1, c);
  }
}

#include "Gfx.h"
#include <string.h>

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

int Gfx::textWidth(const char* s, const GfxFont& f, int nbytes) {
  const char* end = s + (nbytes < 0 ? strlen(s) : (size_t)nbytes);
  int w = 0;
  while (s < end) {
    const GfxGlyph* g = glyph(f, nextCp(s, end));
    if (g) w += g->adv;
  }
  return w;
}

int Gfx::textN(int x, int y, const char* s, int nbytes, const GfxFont& f, uint16_t c) {
  const char* end = s + nbytes;
  int x0 = x;
  while (s < end) {
    const GfxGlyph* g = glyph(f, nextCp(s, end));
    if (!g) continue;
    if (g->w) drawGlyph(x, y, f, g, c);
    x += g->adv;
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
    const GfxGlyph* g = glyph(f, nextCp(q, end));
    int a = g ? g->adv : 0;
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
      uint32_t cp = nextCp(r, end);
      const GfxGlyph* g = glyph(f, cp);
      int a = g ? g->adv : 0;
      if (w + a > maxW && q > line) break;
      if (cp == ' ') lastSpace = q;
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

#include "McmMap.h"
#include <math.h>
#include <string.h>

static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool mcmParseHeader(const uint8_t* h, McmHeader* out) {
  if (memcmp(h, "MCM1", 4) != 0) return false;
  out->zoom = h[4];
  out->x0 = rd32(h + 8);
  out->y0 = rd32(h + 12);
  out->nx = rd32(h + 16);
  out->ny = rd32(h + 20);
  return out->zoom <= 20 && out->nx > 0 && out->ny > 0;
}

int64_t mcmIndexPos(const McmHeader* h, uint32_t x, uint32_t y) {
  if (x < h->x0 || y < h->y0 || x >= h->x0 + h->nx || y >= h->y0 + h->ny) return -1;
  // indeks kolumnami: najpierw wszystkie wiersze kolumny x0, potem x0+1 ...
  return (int64_t)MCM_HEADER_SIZE + ((int64_t)(x - h->x0) * h->ny + (y - h->y0)) * 8;
}

void mcmParseIndexEntry(const uint8_t* e, uint64_t* offset, uint32_t* len) {
  *offset = (uint64_t)rd32(e) | ((uint64_t)e[4] << 32);
  *len = (uint32_t)e[5] | ((uint32_t)e[6] << 8) | ((uint32_t)e[7] << 16);
}

bool mcmDecodeTile(const uint8_t* d, uint32_t len, uint16_t* fb, int fbW, int fbH, int dx, int dy, bool swap) {
  if (len < 1) return false;
  int nc = d[0];
  if (nc < 1 || nc > 16 || len < (uint32_t)(1 + nc * 2)) return false;

  uint16_t pal[16];
  for (int i = 0; i < nc; i++) {
    uint16_t v = (uint16_t)(d[1 + i * 2] | (d[2 + i * 2] << 8));
    pal[i] = swap ? (uint16_t)((v >> 8) | (v << 8)) : v;
  }

  const uint32_t total = MCM_TILE_PX * MCM_TILE_PX;
  uint32_t p = 1 + nc * 2;
  uint32_t px = 0;
  while (px < total && p < len) {
    uint8_t b = d[p++];
    uint8_t c = b >> 4, n = b & 15;
    uint32_t run;
    if (n < 14) {
      run = n + 1;
    } else if (n == 14) {
      if (p >= len) return false;
      run = 15 + d[p++];
    } else {
      if (p + 1 >= len) return false;
      run = 271 + (uint32_t)(d[p] | (d[p + 1] << 8));
      p += 2;
    }
    if (c >= nc || px + run > total) return false;

    uint16_t col = pal[c];
    while (run > 0) {            // seria może przechodzić przez koniec wiersza
      int tx = px & 255, ty = px >> 8;
      uint32_t seg = 256 - tx;
      if (seg > run) seg = run;
      int sy = dy + ty;
      if (sy >= 0 && sy < fbH) {
        int a = dx + tx, e = a + (int)seg;
        if (a < 0) a = 0;
        if (e > fbW) e = fbW;
        uint16_t* row = fb + (uint32_t)sy * fbW;
        for (int x = a; x < e; x++) row[x] = col;
      }
      px += seg;
      run -= seg;
    }
  }
  return px == total;
}

void mcmLatLonToPixel(double lat, double lon, int zoom, double* px, double* py) {
  const double PI_ = 3.14159265358979323846;
  double n = (double)(1UL << zoom) * MCM_TILE_PX;
  if (lat > 85.0511) lat = 85.0511;
  if (lat < -85.0511) lat = -85.0511;
  double r = lat * PI_ / 180.0;
  *px = (lon + 180.0) / 360.0 * n;
  *py = (1.0 - log(tan(r) + 1.0 / cos(r)) / PI_) / 2.0 * n;
}

void mcmPixelToLatLon(double px, double py, int zoom, double* lat, double* lon) {
  const double PI_ = 3.14159265358979323846;
  double n = (double)(1UL << zoom) * MCM_TILE_PX;
  *lon = px / n * 360.0 - 180.0;
  *lat = atan(sinh(PI_ * (1.0 - 2.0 * py / n))) * 180.0 / PI_;
}

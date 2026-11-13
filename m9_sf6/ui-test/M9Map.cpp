#include "M9Map.h"
#include "M9Lcd.h"
#include <SPI.h>
#include <SD.h>
#include <errno.h>
#include <stdarg.h>

#define M9MAP_SD_CS 48

#define COL_BG      M9LCD_RGB(16, 23, 31)
#define COL_GRID    M9LCD_RGB(40, 52, 68)
#define COL_ME      M9LCD_RGB(255, 176, 32)
#define COL_WHITE   M9LCD_RGB(242, 245, 247)
#define COL_BLACK   M9LCD_RGB(0, 0, 0)

bool M9Map::begin() {
  if (_fb == NULL) _fb = (uint16_t*)ps_malloc(M9MAP_W * M9MAP_H * 2);
  if (_fb == NULL) _fb = (uint16_t*)malloc(M9MAP_W * M9MAP_H * 2);
  if (_tile == NULL) _tile = (uint8_t*)ps_malloc(MCM_MAX_TILE_LEN);
  if (_tile == NULL) _tile = (uint8_t*)malloc(MCM_MAX_TILE_LEN);
  return _fb != NULL && _tile != NULL;
}

bool M9Map::mount() {
  SD.end();
  _sd_ok = SD.begin(M9MAP_SD_CS, SPI, 40000000);
  if (!_sd_ok) { SD.end(); _sd_ok = SD.begin(M9MAP_SD_CS, SPI, 20000000); }
  if (_sd_ok && SD.cardType() == CARD_NONE) { SD.end(); _sd_ok = false; }
  _sd_mb = _sd_ok ? (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL)) : 0;
  return _sd_ok;
}

int M9Map::scan() { return _sd_ok ? scan(SD) : 0; }

int M9Map::scan(fs::FS& fs) {
  _fs = &fs;
  _num = 0;
  if (_file) _file.close();
  _open_idx = -1;

  File dir = fs.open(M9MAP_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  while (_num < M9MAP_MAX_PACKS) {
    File f = dir.openNextFile();
    if (!f) break;
    const char* name = f.name();
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    size_t n = strlen(name);
    if (!f.isDirectory() && n > 4 && strcasecmp(name + n - 4, ".mcm") == 0 && n < 30) {
      uint8_t hb[MCM_HEADER_SIZE];
      Pack& p = _packs[_num];
      if (f.read(hb, sizeof(hb)) == sizeof(hb) && mcmParseHeader(hb, &p.h)) {
        snprintf(p.path, sizeof(p.path), "%s/%s", M9MAP_DIR, name);
        _num++;
      }
    }
    f.close();
  }
  dir.close();
  return _num;
}

int M9Map::minZoom() const {
  int z = 99;
  for (int i = 0; i < _num; i++) if (_packs[i].h.zoom < z) z = _packs[i].h.zoom;
  return _num ? z : 6;
}

int M9Map::maxZoom() const {
  int z = 0;
  for (int i = 0; i < _num; i++) if (_packs[i].h.zoom > z) z = _packs[i].h.zoom;
  return _num ? z : 15;
}

bool M9Map::hasZoom(int z) const {
  for (int i = 0; i < _num; i++) if (_packs[i].h.zoom == z) return true;
  return false;
}

bool M9Map::loadTile(int zoom, uint32_t x, uint32_t y, uint32_t* len) {
  for (int i = 0; i < _num; i++) {
    if (_packs[i].h.zoom != zoom) continue;
    int64_t ip = mcmIndexPos(&_packs[i].h, x, y);
    if (ip < 0) continue;

    if (_open_idx != i) {              // trzymamy otwarty ostatnio używany plik
      if (_file) _file.close();
      _file = _fs->open(_packs[i].path);
      _open_idx = _file ? i : -1;
      if (!_file) return false;
    }
    uint8_t e[8];
    if (!_file.seek((uint32_t)ip) || _file.read(e, 8) != 8) return false;
    uint64_t off; uint32_t n;
    mcmParseIndexEntry(e, &off, &n);
    if (n == 0 || n > MCM_MAX_TILE_LEN || off > 0xFFFFFFFFULL) return false;
    if (!_file.seek((uint32_t)off) || _file.read(_tile, n) != (size_t)n) return false;
    *len = n;
    return true;
  }
  return false;
}

void M9Map::fillRect(int x, int y, int w, int h, uint16_t col) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > M9MAP_W) w = M9MAP_W - x;
  if (y + h > M9MAP_H) h = M9MAP_H - y;
  for (int j = 0; j < h; j++) {
    uint16_t* row = _fb + (y + j) * M9MAP_W + x;
    for (int i = 0; i < w; i++) row[i] = col;
  }
}

void M9Map::fillCircle(int cx, int cy, int r, uint16_t col) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, col);
  }
}

void M9Map::clear() {
  if (_fb) fillRect(0, 0, M9MAP_W, M9MAP_H, COL_BLACK);
}

int M9Map::render(double lat, double lon, int zoom) {
  if (_fb == NULL) return 0;
  double cx, cy;
  mcmLatLonToPixel(lat, lon, zoom, &cx, &cy);
  long left = (long)floor(cx) - M9MAP_W / 2;
  long top = (long)floor(cy) - M9MAP_H / 2;
  long maxTile = (1L << zoom) - 1;

  int drawn = 0;
  for (long ty = top >> 8; ty <= (top + M9MAP_H - 1) >> 8; ty++) {
    for (long tx = left >> 8; tx <= (left + M9MAP_W - 1) >> 8; tx++) {
      int dx = (int)(tx * 256 - left), dy = (int)(ty * 256 - top);
      uint32_t len = 0;
      bool ok = tx >= 0 && ty >= 0 && tx <= maxTile && ty <= maxTile && _num > 0 && loadTile(zoom, (uint32_t)tx, (uint32_t)ty, &len)
                && mcmDecodeTile(_tile, len, _fb, M9MAP_W, M9MAP_H, dx, dy, true);    // true: bufor trzymamy w kolejności bajtów panelu
      if (ok) {
        drawn++;
      } else {                        // brak kafelka: tło i ramka kafelka (widok „bez podkładu”)
        fillRect(dx, dy, 256, 256, COL_BG);
        fillRect(dx, dy, 256, 1, COL_GRID);
        fillRect(dx, dy, 1, 256, COL_GRID);
      }
    }
  }
  return drawn;
}

void M9Map::drawMarker(double lat, double lon, double centerLat, double centerLon, int zoom) {
  double cx, cy, px, py;
  mcmLatLonToPixel(centerLat, centerLon, zoom, &cx, &cy);
  mcmLatLonToPixel(lat, lon, zoom, &px, &py);
  int x = (int)(px - floor(cx)) + M9MAP_W / 2, y = (int)(py - floor(cy)) + M9MAP_H / 2;
  if (x < -10 || y < -10 || x > M9MAP_W + 10 || y > M9MAP_H + 10) return;
  fillCircle(x, y, 8, COL_WHITE);
  fillCircle(x, y, 6, COL_ME);
}

void M9Map::drawCross() {
  int x = M9MAP_W / 2, y = M9MAP_H / 2;
  fillRect(x - 7, y, 15, 1, COL_WHITE);
  fillRect(x, y - 7, 1, 15, COL_WHITE);
}


// Test karty SD: czy da się na niej pisać, co na niej jest i ile jest wolnego miejsca.
// Liczenie wolnego miejsca czyta całą tablicę plików karty — na tej karcie trwa to kilkadziesiąt sekund.
void M9Map::diag(char* o, size_t n) {
  o[0] = 0;
  auto add = [&](const char* fmt, ...) {
    size_t k = strlen(o);
    if (k + 2 >= n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(o + k, n - k, fmt, ap); va_end(ap);
  };
  bool ok = mount();
  add("Karta: %s, %lu MB\n", ok ? "odpowiada" : "NIE odpowiada", (unsigned long)_sd_mb);
  if (!ok) return;

  // zapis próbny: 4 kB do nowego pliku, odczyt i porównanie
  const char* p = "/m9test.bin";
  uint8_t* b = (uint8_t*)ps_malloc(8192);
  if (b) {
    for (int i = 0; i < 4096; i++) b[i] = (uint8_t)(i * 7 + 3);
    errno = 0;
    File f = SD.open(p, FILE_WRITE);
    if (!f) add("Zapis próbny: nie mogę utworzyć pliku (błąd %d: %s)\n", errno, strerror(errno));
    else {
      errno = 0;
      size_t w = f.write(b, 4096);
      int e = errno;
      f.close();
      if (w != 4096) add("Zapis próbny: zapisano %u z 4096 B (błąd %d: %s)\n", (unsigned)w, e, strerror(e));
      else {
        File r = SD.open(p);
        size_t g = r ? r.read(b + 4096, 4096) : 0;
        if (r) r.close();
        add("Zapis próbny: działa, odczyt %s\n", (g == 4096 && memcmp(b, b + 4096, 4096) == 0) ? "zgodny" : "NIEZGODNY");
      }
      SD.remove(p);
    }
    free(b);
  }

  // co jest na karcie
  File d = SD.open("/");
  int cnt = 0;
  if (d) {
    for (;;) {
      File e = d.openNextFile();
      if (!e) break;
      if (cnt < 8) { if (e.isDirectory()) add("[folder] %s\n", e.name()); else add("%s %lu kB\n", e.name(), (unsigned long)(e.size() / 1024)); }
      e.close();
      if (++cnt >= 300) break;
    }
    d.close();
  }
  add("Katalog główny: pozycji %d%s\n", cnt, cnt >= 300 ? " lub więcej" : "");
  d = SD.open("/mapa");
  if (d) {
    int files = 0; uint64_t bytes = 0;
    for (;;) { File e = d.openNextFile(); if (!e) break; files++; bytes += e.size(); e.close(); if (files >= 300) break; }
    d.close();
    add("Folder /mapa: plików %d, %lu MB\n", files, (unsigned long)(bytes >> 20));
  } else add("Folder /mapa: nie ma\n");

  uint64_t tot = SD.totalBytes(), used = SD.usedBytes();
  add("Zajęte: %lu z %lu MB, wolne %lu MB\n", (unsigned long)(used >> 20), (unsigned long)(tot >> 20), (unsigned long)((tot - used) >> 20));
}

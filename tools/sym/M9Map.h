#pragma once
// Atrapa mapy dla symulatora: rysuje umowny podkład (siatka i „drogi”) zamiast kafelków z karty.
#include <stdint.h>
#include <string.h>
#include "McmMap.h"
#define M9MAP_W 320
#define M9MAP_H 216
class M9Map {
  uint16_t fb_[M9MAP_W * M9MAP_H];
  static uint16_t sw(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
public:
  bool begin() { return true; }
  bool mount() { return true; }
  int scan() { return 12; }
  void diag(char* out, size_t n) { snprintf(out, n, "Karta: odpowiada, 14910 MB\nZapis próbny: działa, odczyt zgodny\nKatalog główny: 3 pozycje\n[folder] mapa\n[folder] System Volume Information\nnotatka.txt 1 kB\nFolder /mapa: 12 plików, 329 MB\nZajęte: 340 z 14910 MB"); }
  bool sdOk() const { return true; }
  uint32_t sdSizeMB() const { return 14910; }
  int numPacks() const { return 12; }
  int minZoom() const { return 6; }
  int maxZoom() const { return 15; }
  bool hasZoom(int z) const { return z >= 6 && z <= 15; }
  uint16_t* fb() { return fb_; }
  int render(double, double, int) {
    for (int y = 0; y < M9MAP_H; y++) for (int x = 0; x < M9MAP_W; x++) {
      uint16_t c = 0x10A3;                                                  // tło
      if ((x * 3 + y * 2) % 400 < 90 && y > 60) c = 0x1124;                 // „las”
      if (((x + y / 3) % 160) < 2 || ((y + x / 4) % 110) < 2) c = 0x39E9;   // drogi
      if ((y + x / 2) % 190 > 186) c = 0x8BA6;                              // droga główna
      if ((x - y / 5 + 400) % 260 < 3) c = 0x1A4D;                          // rzeka
      fb_[y * M9MAP_W + x] = sw(c);
    }
    return 4;
  }
};

#pragma once
// Mapa z karty SD (pliki /mapa/*.mcm) — rysowanie widoku do bufora ekranu. Wersja testowa.

#include <Arduino.h>
#include <FS.h>
#include "McmMap.h"

#define M9MAP_MAX_PACKS  24
#define M9MAP_W          320
#define M9MAP_H          216      // pod paskiem tekstu (24 px)
#define M9MAP_DIR        "/mapa"

class M9Map {
  struct Pack { char path[40]; McmHeader h; };
  Pack _packs[M9MAP_MAX_PACKS];
  int _num = 0;
  fs::FS* _fs = NULL;
  File _file;
  int _open_idx = -1;
  uint8_t* _tile = NULL;
  uint16_t* _fb = NULL;

  bool loadTile(int zoom, uint32_t x, uint32_t y, uint32_t* len);
  void fillRect(int x, int y, int w, int h, uint16_t col);
  void fillCircle(int cx, int cy, int r, uint16_t col);

public:
  bool begin();                       // przydział pamięci (PSRAM); false = brak pamięci
  int  scan(fs::FS& fs);              // szuka plików mapy; zwraca liczbę plików
  int  numPacks() const { return _num; }
  int  minZoom() const;
  int  maxZoom() const;
  bool hasZoom(int z) const;
  uint16_t* fb() { return _fb; }

  // rysuje widok wyśrodkowany na (lat, lon); zwraca liczbę narysowanych kafelków z karty
  int render(double lat, double lon, int zoom);
  // znacznik pozycji (jeśli mieści się w widoku)
  void drawMarker(double lat, double lon, double centerLat, double centerLon, int zoom);
  void drawCross();
  void clear();
};

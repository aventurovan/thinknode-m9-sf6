#pragma once
// Format mapy „MCM” (MC Mapa) — część przenośna: dekoder kafelka i przeliczenia współrzędnych.
// Bez zależności od Arduino, żeby ten sam kod dało się sprawdzić na komputerze (mapa/test).
// Opis formatu: mapa/FORMAT.md

#include <stdint.h>

#define MCM_HEADER_SIZE   32
#define MCM_TILE_PX       256
#define MCM_MAX_TILE_LEN  (1 + 16 * 2 + 65536)   // najgorszy przypadek: każdy piksel osobnym bajtem

struct McmHeader {
  uint8_t  zoom;
  uint32_t x0, y0;     // pierwszy kafelek (kolumna, wiersz)
  uint32_t nx, ny;     // liczba kolumn i wierszy
};

// nagłówek pliku (32 bajty) -> struktura; false, jeśli to nie jest plik MCM1
bool mcmParseHeader(const uint8_t* h, McmHeader* out);

// pozycja wpisu indeksu dla kafelka (x, y); -1, jeśli kafelek jest poza plikiem
int64_t mcmIndexPos(const McmHeader* h, uint32_t x, uint32_t y);

// wpis indeksu (8 bajtów): położenie i długość danych kafelka (długość 0 = brak kafelka)
void mcmParseIndexEntry(const uint8_t* e, uint64_t* offset, uint32_t* len);

// Rysuje kafelek 256x256 do bufora ekranu (fbW x fbH pikseli RGB565), lewy górny róg kafelka w (dx, dy);
// część poza buforem jest obcinana. swap = zamiana bajtów koloru (panel ST7789 chce starszy bajt pierwszy).
bool mcmDecodeTile(const uint8_t* data, uint32_t len, uint16_t* fb, int fbW, int fbH, int dx, int dy, bool swap);

// Web Mercator: współrzędne -> piksel globalny przy danym zoomie (i odwrotnie)
void mcmLatLonToPixel(double lat, double lon, int zoom, double* px, double* py);
void mcmPixelToLatLon(double px, double py, int zoom, double* lat, double* lon);

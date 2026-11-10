// Test dekodera kafelków na komputerze: ten sam kod co w firmware (m9_sf6/ui-test/McmMap.cpp).
// Użycie: test_dekoder plik.mcm x y wynik.raw   (wynik: 256x256 pikseli RGB565, młodszy bajt pierwszy)
#include "../../m9_sf6/ui-test/McmMap.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 5) return 2;
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 3;
  uint8_t hb[MCM_HEADER_SIZE];
  McmHeader h;
  if (fread(hb, 1, sizeof(hb), f) != sizeof(hb) || !mcmParseHeader(hb, &h)) return 4;
  int64_t ip = mcmIndexPos(&h, (uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]));
  if (ip < 0) return 5;
  uint8_t e[8];
  fseek(f, (long)ip, SEEK_SET);
  if (fread(e, 1, 8, f) != 8) return 6;
  uint64_t off; uint32_t len;
  mcmParseIndexEntry(e, &off, &len);
  if (len == 0 || len > MCM_MAX_TILE_LEN) return 7;
  std::vector<uint8_t> d(len);
  fseek(f, (long)off, SEEK_SET);
  if (fread(d.data(), 1, len, f) != len) return 8;
  // bufor większy niż kafelek i przesunięcie ujemne/dodatnie: sprawdza też obcinanie
  std::vector<uint16_t> fb(256 * 256, 0);
  if (!mcmDecodeTile(d.data(), len, fb.data(), 256, 256, 0, 0, false)) return 9;
  std::vector<uint16_t> clip(100 * 80, 0xFFFF);
  if (!mcmDecodeTile(d.data(), len, clip.data(), 100, 80, -200, -30, false)) return 10;
  for (int y = 0; y < 80; y++) for (int x = 0; x < 100; x++) {
    int tx = x + 200, ty = y + 30;
    uint16_t want = (tx < 256 && ty < 256) ? fb[ty * 256 + tx] : 0xFFFF;
    if (clip[y * 100 + x] != want) return 11;
  }
  FILE* o = fopen(argv[4], "wb");
  fwrite(fb.data(), 2, fb.size(), o);
  fclose(o); fclose(f);
  double px, py, lat, lon;
  mcmLatLonToPixel(49.8225, 19.0469, 13, &px, &py);
  mcmPixelToLatLon(px, py, 13, &lat, &lon);
  printf("OK len=%u mercator z13: kafelek %d/%d, odwrotnie %.4f %.4f\n", len, (int)(px / 256), (int)(py / 256), lat, lon);
  return 0;
}

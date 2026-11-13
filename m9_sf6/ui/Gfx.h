#pragma once
// Rysowanie na buforze ekranu 320x240 (RGB565) — część przenośna, bez zależności od Arduino,
// żeby ekrany dało się wyrenderować i obejrzeć na komputerze (tools/podglad).

#include <stdint.h>
#include <stddef.h>

#ifndef PROGMEM
  #define PROGMEM
#endif

#define GFX_W 320
#define GFX_H 240
#define GFX_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

struct GfxGlyph { uint16_t cp; uint16_t off; uint8_t w, h; int8_t xo, yo; uint8_t adv; };
struct GfxFont { const uint8_t* bits; const GfxGlyph* glyphs; uint16_t n; uint8_t line_h; uint8_t ascent; };
struct GfxLine { uint16_t start, len; };

class Gfx {
  uint16_t* _fb;
public:
  explicit Gfx(uint16_t* fb) : _fb(fb) { }
  uint16_t* fb() { return _fb; }

  void clear(uint16_t c);
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void rect(int x, int y, int w, int h, uint16_t c);
  void fillRound(int x, int y, int w, int h, int r, uint16_t c);
  void dim();                                   // przyciemnia cały ekran (tło okienka)

  // tekst UTF-8; y = górna krawędź wiersza. Znaki spoza czcionki (np. emotki) są pomijane.
  // maxW > 0: tekst jest obcinany z wielokropkiem. Zwraca szerokość narysowanego tekstu.
  int text(int x, int y, const char* s, const GfxFont& f, uint16_t c, int maxW = 0);
  int textN(int x, int y, const char* s, int nbytes, const GfxFont& f, uint16_t c);
  int textRight(int xr, int y, const char* s, const GfxFont& f, uint16_t c);
  int textCenter(int xc, int y, const char* s, const GfxFont& f, uint16_t c);
  static int textWidth(const char* s, const GfxFont& f, int nbytes = -1);
  // dzieli tekst na wiersze o szerokości do maxW (po spacjach, długie słowa łamie); zwraca liczbę wierszy
  static int wrap(const char* s, const GfxFont& f, int maxW, GfxLine* lines, int maxLines);

  // ikona 12x12 z wzoru znakowego ('#' = piksel)
  void icon(int x, int y, const char* const* rows, uint16_t c);

  // zamiana bajtów całego bufora (panel ST7789 chce starszy bajt pierwszy) — wołać przed i po wysłaniu
  void swapBytes();

private:
  static const GfxGlyph* glyph(const GfxFont& f, uint32_t cp);
  static uint32_t nextCp(const char*& s, const char* end);
  void drawGlyph(int x, int y, const GfxFont& f, const GfxGlyph* g, uint16_t c);
};

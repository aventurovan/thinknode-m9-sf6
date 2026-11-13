#include "UiView.h"
#include "FontData.h"
#include <string.h>
#include <stdio.h>

#define C_BG    GFX_RGB(0x0B, 0x0F, 0x14)
#define C_BAR   GFX_RGB(0x13, 0x1A, 0x22)
#define C_LINE  GFX_RGB(0x24, 0x30, 0x40)
#define C_TXT   GFX_RGB(0xF2, 0xF5, 0xF7)
#define C_DIM   GFX_RGB(0x9A, 0xA7, 0xB4)
#define C_ACC   GFX_RGB(0xFF, 0xB0, 0x20)
#define C_INK   GFX_RGB(0x0B, 0x0F, 0x14)
#define C_SELD  GFX_RGB(0x2A, 0x22, 0x10)
#define C_GRN   GFX_RGB(0x3D, 0xDC, 0x84)
#define C_RED   GFX_RGB(0xFF, 0x4D, 0x5E)
#define C_PINK  GFX_RGB(0xFF, 0x8A, 0x95)
#define C_BLUE  GFX_RGB(0x7C, 0xC4, 0xFF)
#define C_TILE  GFX_RGB(0x15, 0x1C, 0x24)
#define C_BUB   GFX_RGB(0x1A, 0x23, 0x2E)

#define STATUS_H 20
#define BOTTOM_H 24
#define HEADER_H 24
#define ROW_H    21

// ---------- ikony 12x12 ----------
static const char* const IC_MAIL[12] = { "", "", "############", "##........##", "#.#......#.#", "#..#....#..#", "#...#..#...#", "#....##....#", "#..........#", "############", "", "" };
static const char* const IC_TOWER[12] = { ".....##.....", ".#...##...#.", "#..#.##.#..#", "#..#.##.#..#", ".#...##...#.", ".....##.....", "....####....", "....#..#....", "...##..##...", "...#....#...", "..########..", "" };
static const char* const IC_USER[12] = { "", "....####....", "...######...", "...######...", "....####....", "............", "..########..", ".##########.", ".##########.", ".##########.", "", "" };
static const char* const IC_HASH[12] = { "", "...#...#....", "...#...#....", ".#########..", "...#...#....", "..#...#.....", "..#...#.....", "#########...", "..#...#.....", "..#...#.....", "", "" };
static const char* const IC_STAR[12] = { ".....##.....", ".....##.....", "....####....", "############", ".##########.", "..########..", "...######...", "..########..", "..###..###..", ".##......##.", "", "" };
static const char* const IC_GPS[12] = { ".....##.....", ".....##.....", "...######...", "..##....##..", "..#..##..#..", "###.####.###", "###.####.###", "..#..##..#..", "..##....##..", "...######...", ".....##.....", ".....##....." };
static const char* const IC_BT[12] = { ".....#......", ".....##.....", ".#...#.#....", "..#..#..#...", "...#.#.#....", "....###.....", "....###.....", "...#.#.#....", "..#..#..#...", ".#...#.#....", ".....##.....", ".....#......" };
static const char* const IC_LOCK[12] = { "....####....", "...#....#...", "...#....#...", "...#....#...", "..########..", "..########..", "..###..###..", "..###..###..", "..########..", "..########..", "", "" };
static const char* const IC_BOLT[12] = { "......##....", ".....##.....", "....##......", "...##.......", "..#######...", ".....##.....", "....##......", "...##.......", "..##........", ".##.........", "", "" };
static const char* const IC_SD[12] = { "..#######...", "..#.#.#.##..", "..#.#.#..##.", "..#.......#.", "..#.......#.", "..#.......#.", "..#.......#.", "..#.......#.", "..#.......#.", "..#########.", "", "" };
static const char* const IC_ROOM[12] = { "", ".##########.", ".#........#.", ".#.######.#.", ".#........#.", ".#.####...#.", ".#........#.", ".####.#####.", "....##......", "....#.......", "", "" };

static const char* const IC_HOME[12] = { ".....##.....", "....####....", "...######...", "..########..", ".##########.", "############", "..########..", "..###..###..", "..###..###..", "..###..###..", "..###..###..", "" };
static const char* const IC_GEAR[12] = { ".....##.....", "..#.####.#..", ".##########.", "..###..###..", ".###....###.", "###......###", "###......###", ".###....###.", "..###..###..", ".##########.", "..#.####.#..", ".....##....." };
static const char* const IC_SCREEN[12] = { "", "############", "#..........#", "#..........#", "#..........#", "#..........#", "#..........#", "############", ".....##.....", "...######...", "", "" };
static const char* const IC_SHIELD[12] = { ".....##.....", "..########..", ".##########.", ".##########.", ".##########.", ".##########.", "..########..", "..########..", "...######...", "....####....", ".....##.....", "" };

static const char* const IC_CHECK[12] = { "", "", "..........##", ".........##.", "........##..", "##.....##...", ".##...##....", "..##.##.....", "...###......", "....#.......", "", "" };
static const char* const IC_MUTE[12] = { "#.....#.....", ".#...##.....", "..#.###.....", "#####.#.....", "####.#......", "####..#.....", "#####..#....", "...###..#...", "....##...#..", ".....#....#.", "...........#", "" };

static const char* const* iconRows(uint8_t i) {
  switch (i) {
    case UII_MAIL: return IC_MAIL; case UII_TOWER: return IC_TOWER; case UII_USER: return IC_USER; case UII_HASH: return IC_HASH;
    case UII_STAR: return IC_STAR; case UII_GPS: return IC_GPS; case UII_BT: return IC_BT; case UII_LOCK: return IC_LOCK;
    case UII_BOLT: return IC_BOLT; case UII_SD: return IC_SD; case UII_ROOM: return IC_ROOM;
    case UII_CHECK: return IC_CHECK; case UII_MUTE: return IC_MUTE;
    case UII_HOME: return IC_HOME; case UII_GEAR: return IC_GEAR; case UII_SCREEN: return IC_SCREEN; case UII_SHIELD: return IC_SHIELD;
  }
  return NULL;
}

static const char* const TABS[5] = { "Start", "Wiadomości", "Kontakty", "Repeatery", "Ustawienia" };

// ---------- elementy wspólne ----------
static void drawStatus(Gfx& g, const UiView& v) {
  g.fillRect(0, 0, GFX_W, STATUS_H, C_BAR);
  int x = 6;
  x += g.text(x, 3, v.time, F_BOLD, C_TXT) + 8;
  if (v.unread > 0) {
    char t[12]; snprintf(t, sizeof(t), "%d", v.unread);
    g.icon(x, 4, IC_MAIL, C_ACC);
    g.text(x + 15, 3, t, F_BOLD, C_ACC);
  }
  // prawa strona: bateria, Bluetooth, GPS
  char t[12];
  int xr = GFX_W - 6;
  snprintf(t, sizeof(t), "%d%%", v.batt);
  xr -= g.textRight(xr, 4, t, F_SMALL, C_TXT) + 3;
  if (v.charging) { xr -= 9; g.icon(xr - 1, 4, IC_BOLT, C_ACC); xr -= 2; }
  xr -= 22;
  g.fillRect(xr, 5, 20, 10, C_TXT); g.fillRect(xr + 1, 6, 18, 8, C_BAR); g.fillRect(xr + 20, 8, 2, 4, C_TXT);
  int fill = v.batt * 16 / 100; if (fill < 1) fill = 1; if (fill > 16) fill = 16;
  g.fillRect(xr + 2, 7, fill, 6, v.batt <= 15 ? C_RED : C_GRN);
  xr -= 8;
  if (v.bt) { xr -= 12; g.icon(xr, 4, IC_BT, v.bt == 2 ? C_BLUE : C_DIM); xr -= 6; }
  if (v.gps_on) {
    snprintf(t, sizeof(t), "%d", v.sats);
    xr -= g.textRight(xr, 4, t, F_SMALL, C_TXT) + 2;
    xr -= 12; g.icon(xr, 4, IC_GPS, v.sats > 0 ? C_TXT : C_DIM);
  }
}

// dolny pasek: ikony Start, Wiadomości, Kontakty, Repeatery, Ustawienia; aktywna na bursztynowym tle
static void drawTabs(Gfx& g, int active, int unread, bool focus) {
  static const char* const* const IC[5] = { IC_HOME, IC_MAIL, IC_USER, IC_TOWER, IC_GEAR };
  int y = GFX_H - BOTTOM_H;
  g.fillRect(0, y, GFX_W, BOTTOM_H, C_BAR);
  for (int i = 0; i < 5; i++) {
    int cx = i * 64 + 32;
    if (i == active && focus) { g.fillRound(cx - 25, y + 1, 50, 22, 11, C_TXT); g.fillRound(cx - 22, y + 3, 44, 18, 9, C_ACC); g.icon(cx - 6, y + 6, IC[i], C_INK); }   // chodzimy po pasku: biała ramka
    else if (i == active) { g.fillRound(cx - 22, y + 3, 44, 18, 9, C_ACC); g.icon(cx - 6, y + 6, IC[i], C_INK); }
    else g.icon(cx - 6, y + 6, IC[i], C_DIM);
    if (i == 1 && unread > 0) {                       // licznik nieprzeczytanych przy kopercie
      char t[8]; snprintf(t, sizeof(t), "%d", unread > 99 ? 99 : unread);
      int bw = Gfx::textWidth(t, F_SMALL) + 8;
      g.fillRound(cx + 8, y + 2, bw, 13, 6, C_RED);
      g.text(cx + 12, y + 2, t, F_SMALL, C_TXT);
    }
  }
}

static void drawHints(Gfx& g, const UiView& v) {
  int y = GFX_H - BOTTOM_H;
  g.fillRect(0, y, GFX_W, BOTTOM_H, C_BAR);
  int x = 6;
  for (int i = 0; i < v.nhints; i++) {
    int kw = Gfx::textWidth(v.hint[i].key, F_SMALL) + 8;
    g.fillRound(x, y + 4, kw, 15, 3, C_DIM); g.fillRound(x + 1, y + 5, kw - 2, 13, 2, C_BAR);
    g.text(x + 4, y + 5, v.hint[i].key, F_SMALL, C_TXT);
    x += kw + 4;
    x += g.text(x, y + 5, v.hint[i].txt, F_SMALL, C_DIM) + 10;
  }
}

static void drawBottom(Gfx& g, const UiView& v) { if (v.tab >= 0) drawTabs(g, v.tab, v.unread, v.bar_focus); else drawHints(g, v); }

static void drawHeader(Gfx& g, const UiView& v, int y) {
  int rw = v.title_right[0] ? Gfx::textWidth(v.title_right, F_SMALL) : 0;
  g.text(8, y + 4, v.title, F_BOLD, C_TXT, GFX_W - 24 - rw);
  if (rw) g.textRight(GFX_W - 8, y + 6, v.title_right, F_SMALL, C_DIM);
  g.fillRect(0, y + HEADER_H - 1, GFX_W, 1, C_LINE);
}

static void drawPopup(Gfx& g, const UiView& v) {
  g.dim();
  int w = 250, h = v.popup2[0] ? 62 : 42, x = (GFX_W - w) / 2, y = (GFX_H - h) / 2;
  g.fillRound(x, y, w, h, 6, v.popup_danger ? C_RED : C_ACC);
  g.fillRound(x + 2, y + 2, w - 4, h - 4, 5, C_TILE);
  g.textCenter(GFX_W / 2, y + 12, v.popup1, F_BOLD, C_TXT);
  if (v.popup2[0]) g.textCenter(GFX_W / 2, y + 34, v.popup2, F_SMALL, C_DIM);
}

// ---------- ekrany ----------
static void drawList(Gfx& g, const UiView& v) {
  drawHeader(g, v, STATUS_H);
  int y = STATUS_H + HEADER_H;
  if (v.nchips > 0) {                                 // wiersz filtrów: aktywny wypełniony, zaznaczony (gdy chodzimy po wierszu) w ramce
    int x = 3;
    for (int i = 0; i < v.nchips; i++) {
      int w = Gfx::textWidth(v.chips[i], F_SMALL) + 7;
      bool act = i == v.chip_active;
      if (v.chip_focus && act && !v.bar_focus) { g.fillRound(x - 1, y + 1, w + 2, 19, 9, C_TXT); g.fillRound(x + 1, y + 3, w - 2, 15, 7, C_ACC); }
      else if (act) g.fillRound(x, y + 2, w, 17, 8, v.chip_focus ? C_ACC : GFX_RGB(0x6B, 0x4A, 0x10));
      else { g.fillRound(x, y + 2, w, 17, 8, C_LINE); g.fillRound(x + 1, y + 3, w - 2, 15, 7, C_BG); }
      g.text(x + 4, y + 4, v.chips[i], F_SMALL, act ? (v.chip_focus ? C_INK : C_TXT) : C_DIM);
      x += w + 2;
    }
    y += 22;
  }
  if (v.nrows == 0) {
    GfxLine ln[5];
    int n = Gfx::wrap(v.empty, F_NORMAL, GFX_W - 32, ln, 5);
    for (int i = 0; i < n; i++) g.textN(16, y + 14 + i * 17, v.empty + ln[i].start, ln[i].len, F_NORMAL, C_DIM);
    return;
  }
  for (int i = 0; i < v.nrows; i++, y += ROW_H) {
    const UiRow& r = v.rows[i];
    bool sel = i == v.sel && !v.chip_focus && !v.bar_focus;
    uint16_t fg = sel ? C_INK : C_TXT, dim = sel ? GFX_RGB(0x2B, 0x24, 0x12) : C_DIM;
    if (sel) g.fillRect(0, y, GFX_W, ROW_H, C_ACC);
    int x = 8;
    const char* const* ic = iconRows(r.icon);
    if (ic) { g.icon(x, y + 4, ic, sel ? C_INK : (r.icon_accent ? C_ACC : C_DIM)); x += 17; }
    int right = GFX_W - 8;
    if (r.badge) {
      char t[6]; snprintf(t, sizeof(t), "%d", r.badge);
      int bw = Gfx::textWidth(t, F_SMALL) + 10;
      g.fillRound(right - bw, y + 3, bw, 15, 7, sel ? C_INK : C_ACC);
      g.text(right - bw + 5, y + 4, t, F_SMALL, sel ? C_ACC : C_INK);
      right -= bw + 6;
    }
    if (r.right[0]) right -= g.textRight(right, y + 4, r.right, F_SMALL, dim) + 6;
    int nameMax = right - x;
    if (r.sub[0] && nameMax > 150) nameMax = 150;
    int nw = g.text(x, y + 3, r.text, sel ? F_BOLD : F_NORMAL, fg, nameMax);
    if (r.sub[0] && right - (x + nw + 8) > 30) g.text(x + nw + 8, y + 4, r.sub, F_SMALL, dim, right - (x + nw + 8));
  }
  // znaczniki przewijania
  if (v.more_up) g.fillRect(GFX_W - 3, STATUS_H + HEADER_H + 2, 2, 8, C_ACC);
  if (v.more_dn) g.fillRect(GFX_W - 3, GFX_H - BOTTOM_H - 10, 2, 8, C_ACC);
}

// Siatka kafelków, po której chodzi się strzałkami. Start: duży zegar i 3 kolumny; Ustawienia: nagłówek i 2 kolumny.
static void drawGrid(Gfx& g, const UiView& v) {
  int top;
  bool start = v.big[0] != 0;
  if (start) {
    int cw = g.text(8, STATUS_H + 1, v.big, F_HUGE, C_TXT);
    g.text(8 + cw + 10, STATUS_H + 8, v.name, F_BOLD, C_TXT, GFX_W - cw - 26);
    g.text(8 + cw + 10, STATUS_H + 25, v.line1, F_SMALL, C_DIM, GFX_W - cw - 26);
    top = STATUS_H + 46;
  } else {
    drawHeader(g, v, STATUS_H);
    top = STATUS_H + HEADER_H + 4;
  }
  int cols = v.grid_cols > 0 ? v.grid_cols : 2;
  int rows = (v.ntiles + cols - 1) / cols;
  if (rows < 1) return;
  const int gap = 4;
  int tw = (GFX_W - 12 - (cols - 1) * gap) / cols;
  int th = (GFX_H - BOTTOM_H - top - 4 - (rows - 1) * gap) / rows;
  for (int i = 0; i < v.ntiles; i++) {
    const UiTile& t = v.tile[i];
    int x = 6 + (i % cols) * (tw + gap), y = top + (i / cols) * (th + gap);
    bool sel = i == v.sel && !v.bar_focus;
    if (sel) { g.fillRound(x - 1, y - 1, tw + 2, th + 2, 7, C_ACC); g.fillRound(x + 1, y + 1, tw - 2, th - 2, 5, GFX_RGB(0x22, 0x1C, 0x10)); }
    else g.fillRound(x, y, tw, th, 6, C_TILE);
    const char* const* ic = iconRows(t.icon);
    if (start) {                                      // ikona + nazwa, duża wartość, drobny opis
      if (ic) g.icon(x + 7, y + 6, ic, sel ? C_ACC : C_DIM);
      g.text(x + 23, y + 6, t.label, F_SMALL, sel ? C_ACC : C_DIM, tw - 28);
      bool fits = Gfx::textWidth(t.value, F_BIG) <= tw - 14;
      g.text(x + 7, y + (fits ? 23 : 26), t.value, fits ? F_BIG : F_BOLD, t.accent ? C_ACC : C_TXT, tw - 14);
      g.text(x + 7, y + 46, t.sub, F_SMALL, C_DIM, tw - 14);
    } else {                                          // ikona w kółku, nazwa i jedna linia podsumowania
      g.fillRound(x + 6, y + (th - 22) / 2, 22, 22, 6, sel ? C_ACC : C_LINE);
      if (ic) g.icon(x + 11, y + (th - 22) / 2 + 5, ic, sel ? C_INK : C_TXT);
      g.text(x + 34, y + th / 2 - 15, t.label, F_BOLD, C_TXT, tw - 40);
      g.text(x + 34, y + th / 2 + 2, t.sub, F_SMALL, C_DIM, tw - 40);
    }
  }
}

static void drawThread(Gfx& g, const UiView& v) {
  drawHeader(g, v, STATUS_H);
  const int top = STATUS_H + HEADER_H + 2, inputH = 26;
  const int bottom = GFX_H - BOTTOM_H - inputH - 4;
  const int maxW = 232, lh = 15;

  // dymki od najnowszego w górę, aż skończy się miejsce
  int y = bottom;
  for (int i = v.nbub - 1; i >= 0 && y > top; i--) {
    const UiBubble& b = v.bub[i];
    GfxLine ln[8];
    int n = Gfx::wrap(b.text, F_NORMAL, maxW, ln, 8);
    if (n == 0) n = 1, ln[0].start = 0, ln[0].len = 0;
    int tw = 0;
    for (int k = 0; k < n; k++) { int w = Gfx::textWidth(b.text + ln[k].start, F_NORMAL, ln[k].len); if (w > tw) tw = w; }
    char head[52]; head[0] = 0;
    if (!b.out && b.who[0]) snprintf(head, sizeof(head), "%s", b.who);
    int hw = head[0] ? Gfx::textWidth(head, F_SMALL) : 0, mw = Gfx::textWidth(b.meta, F_SMALL);
    int headLine = (head[0] || !b.out) ? 13 : 0, metaLine = b.out ? 12 : 0, pathLine = b.path[0] ? 13 : 0;
    if (pathLine) { int pw = Gfx::textWidth(b.path, F_SMALL); if (pw > tw) tw = pw; }
    if (!b.out && hw + mw + 8 > tw) tw = hw + mw + 8;
    if (b.out && mw > tw) tw = mw;
    if (tw > maxW) tw = maxW;
    int bw = tw + 14, bh = headLine + n * lh + metaLine + pathLine + 6;
    int by = y - bh, bx = b.out ? GFX_W - 8 - bw : 8;
    if (by < top && i != v.nbub - 1) break;                  // dymek, który się nie mieści w całości, pomijamy
    if (b.sel) { g.fillRound(bx - 2, by - 2, bw + 4, bh + 4, 7, C_BLUE); g.fillRound(bx, by, bw, bh, 6, b.out ? C_TILE : C_BUB); }   // wybrana wiadomość
    else if (b.out) { g.fillRound(bx, by, bw, bh, 6, C_ACC); g.fillRound(bx + 1, by + 1, bw - 2, bh - 2, 5, C_TILE); }
    else g.fillRound(bx, by, bw, bh, 6, C_BUB);
    int ty = by + 3;
    if (headLine) {
      int hx = bx + 7;
      if (head[0]) hx += g.text(hx, ty, head, F_SMALL, C_BLUE, maxW - mw - 8) + 6;
      g.text(hx, ty, b.meta, F_SMALL, C_DIM);
      ty += headLine;
    }
    for (int k = 0; k < n; k++, ty += lh) g.textN(bx + 7, ty, b.text + ln[k].start, ln[k].len, F_NORMAL, C_TXT);
    if (metaLine) g.textRight(bx + bw - 7, ty, b.meta, F_SMALL, C_DIM);
    if (pathLine) g.text(bx + 7, ty, b.path, F_SMALL, C_ACC, maxW);
    y = by - 4;
  }
  g.fillRect(0, STATUS_H + HEADER_H, GFX_W, 2, C_BG);      // dymek nie wchodzi pod nagłówek
  g.fillRect(0, STATUS_H, GFX_W, HEADER_H, C_BG);
  drawHeader(g, v, STATUS_H);
  if (v.line1[0] || v.line2[0]) {                    // pole do wpisania czegoś innego niż wiadomość: objaśnienie
    GfxLine ln[4];
    int n = Gfx::wrap(v.line1, F_NORMAL, GFX_W - 32, ln, 4);
    for (int i = 0; i < n; i++) g.textN(16, 70 + i * 17, v.line1 + ln[i].start, ln[i].len, F_NORMAL, C_DIM);
  } else if (v.nbub == 0) g.textCenter(GFX_W / 2, 100, "Brak wiadomości — napisz pierwszą", F_SMALL, C_DIM);

  // pole pisania
  int iy = GFX_H - BOTTOM_H - inputH - 2;
  g.fillRect(0, iy - 2, GFX_W, inputH + 4, C_BG);
  g.fillRound(6, iy, GFX_W - 12, inputH, 5, v.polish_next ? C_BLUE : C_ACC);
  g.fillRound(7, iy + 1, GFX_W - 14, inputH - 2, 4, C_BG);
  char cnt[12]; snprintf(cnt, sizeof(cnt), "%d/%d", v.input_chars, UI_INPUT_MAX);
  int cw = g.textRight(GFX_W - 12, iy + 7, cnt, F_SMALL, C_DIM);
  int avail = GFX_W - 12 - 14 - cw - 12;
  // pokazujemy koniec tekstu, jeśli się nie mieści
  const char* s = v.input;
  while (*s && Gfx::textWidth(s, F_NORMAL) > avail) { s++; while ((*s & 0xC0) == 0x80) s++; }
  int w = v.input[0] ? g.text(13, iy + 5, s, F_NORMAL, C_TXT) : g.text(13, iy + 5, v.polish_next ? "ctrl: teraz litera (ą ć ę ł ń ó ś ź ż)" : (v.line1[0] ? "" : "Napisz wiadomość…"), F_NORMAL, C_DIM);
  if (v.input[0] || !v.polish_next) g.fillRect(13 + (v.input[0] ? w : 0) + 1, iy + 5, 2, 15, v.polish_next ? C_BLUE : C_ACC);
}

static void drawInfo(Gfx& g, const UiView& v) {
  drawHeader(g, v, STATUS_H);
  if (v.more_up) g.fillRect(GFX_W - 3, STATUS_H + HEADER_H + 2, 2, 8, C_ACC);
  if (v.more_dn) g.fillRect(GFX_W - 3, GFX_H - BOTTOM_H - 10, 2, 8, C_ACC);
  int y = STATUS_H + HEADER_H + 4;
  for (int i = 0; i < v.nkv; i++, y += 20) {
    if (v.kv[i].v[0] == 0) { g.text(8, y + 2, v.kv[i].k, F_NORMAL, C_TXT, GFX_W - 16); continue; }     // sam tekst na całą szerokość
    int vw = g.textRight(GFX_W - 8, y + 2, v.kv[i].v, F_BOLD, C_TXT);
    g.text(8, y + 2, v.kv[i].k, F_NORMAL, C_DIM, GFX_W - 24 - vw);
  }
}

static void drawBoot(Gfx& g, const UiView& v) {
  // fale radiowe: trzy łuki z prostokątów
  for (int k = 0; k < 3; k++) {
    int r = 16 + k * 14;
    uint16_t c = k == 0 ? C_ACC : (k == 1 ? GFX_RGB(0xA8, 0x74, 0x18) : GFX_RGB(0x5C, 0x40, 0x10));
    for (int a = -r; a <= r; a++) {
      int hh = r * r - a * a, yy = 0;
      while ((yy + 1) * (yy + 1) <= hh) yy++;
      if (yy * 10 >= r * 5) g.fillRect(GFX_W / 2 + a - 1, 78 - yy - 1, 3, 3, c);
    }
  }
  g.textCenter(GFX_W / 2, 84, "MC SF6", F_HUGE, C_TXT);
  g.textCenter(GFX_W / 2, 134, "MeshCore Południe", F_BOLD, C_ACC);
  g.textCenter(GFX_W / 2, 162, v.line1, F_SMALL, C_DIM);
  g.textCenter(GFX_W / 2, 178, v.line2, F_SMALL, C_DIM);
}

static void drawLock(Gfx& g, const UiView& v) {
  g.textCenter(GFX_W / 2, 62, v.big, F_HUGE, C_TXT);
  g.textCenter(GFX_W / 2, 118, v.line1, F_NORMAL, C_DIM);
  if (v.unread > 0) g.textCenter(GFX_W / 2, 146, v.line2, F_BOLD, C_ACC);
  g.icon(GFX_W / 2 - 6, 176, IC_LOCK, C_DIM);
  g.textCenter(GFX_W / 2, 196, v.footer, F_SMALL, C_DIM);
  if (v.batt > 0 && v.popup1[0]) g.textCenter(GFX_W / 2, 214, v.popup1, F_BOLD, C_ACC);     // postęp przytrzymania
}

void uiRender(Gfx& g, const UiView& v) {
  g.clear(C_BG);
  switch (v.screen) {
    case UIS_BOOT: drawBoot(g, v); return;
    case UIS_LOCK: drawStatus(g, v); drawLock(g, v); return;
    case UIS_START: case UIS_GRID: drawGrid(g, v); break;
    case UIS_LIST: drawList(g, v); break;
    case UIS_THREAD: drawThread(g, v); break;
    case UIS_INFO: drawInfo(g, v); break;
  }
  drawStatus(g, v);
  drawBottom(g, v);
  if (v.popup) drawPopup(g, v);
}

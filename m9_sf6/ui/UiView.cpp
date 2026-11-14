#include "UiView.h"
#include <stdlib.h>
#include "FontData.h"
#include "LogoData.h"
#include <string.h>
#include <stdio.h>

#define C_BG    GFX_RGB(0x00, 0x00, 0x00)
#define C_BAR   GFX_RGB(0x0C, 0x10, 0x15)
#define C_LINE  GFX_RGB(0x24, 0x30, 0x40)
#define C_TXT   GFX_RGB(0xF2, 0xF5, 0xF7)
#define C_DIM   GFX_RGB(0x9A, 0xA7, 0xB4)
#define C_ACC   GFX_RGB(0xFF, 0xB0, 0x20)
#define C_INK   GFX_RGB(0x0B, 0x0F, 0x14)
#define C_GRN   GFX_RGB(0x3D, 0xDC, 0x84)
#define C_RED   GFX_RGB(0xFF, 0x4D, 0x5E)
#define C_PINK  GFX_RGB(0xFF, 0x8A, 0x95)
#define C_BLUE  GFX_RGB(0x7C, 0xC4, 0xFF)
#define C_TILE  GFX_RGB(0x11, 0x16, 0x1D)
#define C_BUB   GFX_RGB(0x16, 0x1D, 0x26)
#define C_SELT  GFX_RGB(0x22, 0x1C, 0x10)

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

static const char* const IC_SIGNAL[12] = { "", "..........##", "..........##", ".......##.##", ".......##.##", "....##.##.##", "....##.##.##", ".##.##.##.##", ".##.##.##.##", ".##.##.##.##", "", "" };

static const char* const* iconRows(uint8_t i) {
  switch (i) {
    case UII_MAIL: return IC_MAIL; case UII_TOWER: return IC_TOWER; case UII_USER: return IC_USER; case UII_HASH: return IC_HASH;
    case UII_STAR: return IC_STAR; case UII_GPS: return IC_GPS; case UII_BT: return IC_BT; case UII_LOCK: return IC_LOCK;
    case UII_BOLT: return IC_BOLT; case UII_SD: return IC_SD; case UII_ROOM: return IC_ROOM;
    case UII_CHECK: return IC_CHECK; case UII_MUTE: return IC_MUTE; case UII_SIGNAL: return IC_SIGNAL;
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
  if (v.wifi) {                                   // WiFi: 1 = łączy (szare), 2..5 = 1..4 kreski, 6 = własna sieć do wgrywania
    static const char* const W3[12] = { "...######...", ".##......##.", "#..........#", "", "", "", "", "", "", "", "", "" };
    static const char* const W2[12] = { "", "", "....####....", "..##....##..", "", "", "", "", "", "", "", "" };
    static const char* const W1[12] = { "", "", "", "", "....####....", "...#....#...", "", "", "", "", "", "" };
    static const char* const W0[12] = { "", "", "", "", "", "", "", ".....##.....", ".....##.....", "", "", "" };
    int lvl = v.wifi == 6 ? 4 : v.wifi - 1;
    uint16_t on = v.wifi == 6 ? C_ACC : C_TXT;
    xr -= 13;
    g.icon(xr, 5, W0, lvl >= 1 ? on : C_DIM);
    g.icon(xr, 5, W1, lvl >= 2 ? on : C_LINE);
    g.icon(xr, 5, W2, lvl >= 3 ? on : C_LINE);
    g.icon(xr, 5, W3, lvl >= 4 ? on : C_LINE);
    xr -= 6;
  }
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
    if (r.right[0]) {                                // prawy opis nigdy nie wchodzi na nazwę: najwyżej 190 px, z wielokropkiem
      int rw = Gfx::textWidth(r.right, F_SMALL);
      if (rw > 190) rw = 190;
      g.text(right - rw, y + 4, r.right, F_SMALL, dim, rw);
      right -= rw + 6;
    }
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
    if (sel) { g.fillRound(x - 1, y - 1, tw + 2, th + 2, 7, C_ACC); g.fillRound(x + 1, y + 1, tw - 2, th - 2, 5, C_SELT); }
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

static const int TH_TOP = STATUS_H + HEADER_H + 2, TH_INPUT_H = 26, TH_BOTTOM = GFX_H - BOTTOM_H - TH_INPUT_H - 4;
static const int TH_MAXW = 232, TH_LH = 15;

int uiThreadViewH() { return TH_BOTTOM - TH_TOP; }

// układ dymka: wiersze tekstu, szerokość i wysokość — ten sam dla rysowania i dla przewijania
struct BubLayout { GfxLine ln[8]; int n, bw, bh, headLine, metaLine, pathLine, mw; char head[52]; };
static void bubLayout(const UiBubble& b, BubLayout& L) {
  L.n = Gfx::wrap(b.text, F_NORMAL, TH_MAXW, L.ln, 8);
  if (L.n == 0) L.n = 1, L.ln[0].start = 0, L.ln[0].len = 0;
  int tw = 0;
  for (int k = 0; k < L.n; k++) { int w = Gfx::textWidth(b.text + L.ln[k].start, F_NORMAL, L.ln[k].len); if (w > tw) tw = w; }
  L.head[0] = 0;
  if (!b.out && b.who[0]) snprintf(L.head, sizeof(L.head), "%s", b.who);
  int hw = L.head[0] ? Gfx::textWidth(L.head, F_SMALL) : 0;
  L.mw = Gfx::textWidth(b.meta, F_SMALL);
  L.headLine = (L.head[0] || !b.out) ? 13 : 0; L.metaLine = b.out ? 12 : 0; L.pathLine = b.path[0] ? 13 : 0;
  if (L.pathLine) { int pw = Gfx::textWidth(b.path, F_SMALL); if (pw > tw) tw = pw; }
  if (!b.out && hw + L.mw + 8 > tw) tw = hw + L.mw + 8;
  if (b.out && L.mw > tw) tw = L.mw;
  if (tw > TH_MAXW) tw = TH_MAXW;
  L.bw = tw + 14; L.bh = L.headLine + L.n * TH_LH + L.metaLine + L.pathLine + 6;
}

int uiBubbleHeight(const UiBubble& b) { BubLayout L; bubLayout(b, L); return L.bh; }

static void drawThread(Gfx& g, const UiView& v) {
  drawHeader(g, v, STATUS_H);
  const int top = TH_TOP, inputH = TH_INPUT_H, bottom = TH_BOTTOM, maxW = TH_MAXW, lh = TH_LH;

  // dymki od najnowszego w górę; dolny może być przesunięty w dół (przewijanie co piksel),
  // to, co wystaje poza obszar rozmowy, zasłaniają nagłówek i pole pisania
  int y = bottom + v.bub_off;
  for (int i = v.nbub - 1; i >= 0 && y > top; i--) {
    const UiBubble& b = v.bub[i];
    BubLayout L; bubLayout(b, L);
    const int bw = L.bw, bh = L.bh, n = L.n, mw = L.mw;
    int by = y - bh, bx = b.out ? GFX_W - 8 - bw : 8;
    if (by < bottom) {
      if (b.sel) g.fillRect(bx - 6, by + 1, 3, bh - 2, C_ACC);       // wybrana wiadomość (do opcji): pasek z lewej strony
      if (b.out) { g.fillRound(bx, by, bw, bh, 6, C_ACC); g.fillRound(bx + 1, by + 1, bw - 2, bh - 2, 5, C_TILE); }
      else g.fillRound(bx, by, bw, bh, 6, C_BUB);
      int ty = by + 3;
      if (L.headLine) {
        int hx = bx + 7;
        if (L.head[0]) hx += g.text(hx, ty, L.head, F_SMALL, C_BLUE, maxW - mw - 8) + 6;
        g.text(hx, ty, b.meta, F_SMALL, C_DIM);
        ty += L.headLine;
      }
      for (int k = 0; k < n; k++, ty += lh) g.textN(bx + 7, ty, b.text + L.ln[k].start, L.ln[k].len, F_NORMAL, C_TXT);
      if (L.metaLine) { g.textRight(bx + bw - 7, ty, b.meta, F_SMALL, C_DIM); ty += L.metaLine; }
      if (L.pathLine) { if (b.out) g.textRight(bx + bw - 7, ty, b.path, F_SMALL, C_ACC); else g.text(bx + 7, ty, b.path, F_SMALL, C_ACC, maxW); }
    }
    y = by - 4;
  }
  g.fillRect(0, 0, GFX_W, STATUS_H, C_BG);
  // suwak z prawej: ile jest rozmowy i gdzie jesteśmy (tylko gdy rozmowa nie mieści się na ekranie)
  if (v.sb_total > v.sb_view && v.sb_view > 0) {
    int H = bottom - top - 4;
    int th = H * v.sb_view / v.sb_total; if (th < 10) th = 10;
    int range = v.sb_total - v.sb_view;
    int ty = top + 2 + (H - th) - (int)((long)(H - th) * v.sb_pos / range);
    g.fillRect(GFX_W - 3, top + 2, 2, H, C_LINE);
    g.fillRect(GFX_W - 4, ty, 3, th, C_ACC);
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
  g.image((GFX_W - LOGO_W) / 2, 14, LOGO_W, LOGO_H, LOGO_PX);
  g.textCenter(GFX_W / 2, 104, "MeshSpeed", F_HUGE, C_TXT);
  g.textCenter(GFX_W / 2, 152, "MeshCore Południe", F_BOLD, C_ACC);
  g.textCenter(GFX_W / 2, 182, v.line1, F_SMALL, C_DIM);
  g.textCenter(GFX_W / 2, 198, v.line2, F_SMALL, C_DIM);
}

static void drawLock(Gfx& g, const UiView& v) {
  g.textCenter(GFX_W / 2, 62, v.big, F_HUGE, C_TXT);
  g.textCenter(GFX_W / 2, 118, v.line1, F_NORMAL, C_DIM);
  if (v.line2[0]) g.textCenter(GFX_W / 2, 146, v.line2, F_BOLD, C_ACC);
  g.icon(GFX_W / 2 - 6, 176, IC_LOCK, C_DIM);
  g.textCenter(GFX_W / 2, 196, v.footer, F_SMALL, C_DIM);
  if (v.batt > 0 && v.popup1[0]) g.textCenter(GFX_W / 2, 214, v.popup1, F_BOLD, C_ACC);     // postęp przytrzymania
}

// ustawianie daty i godziny: pięć pól, ◄ ► wybiera pole, ▲ ▼ zmienia wartość
static void drawTime(Gfx& g, const UiView& v) {
  drawHeader(g, v, STATUS_H);
  static const int W[5] = { 76, 48, 48, 48, 48 };
  int x = 12, y = 78;
  for (int i = 0; i < 5; i++) {
    bool sel = i == v.sel;
    if (i == 3) x += 12;                              // odstęp między datą a godziną
    if (sel) { g.fillRound(x - 2, y - 2, W[i] + 4, 58, 8, C_ACC); g.fillRound(x, y, W[i], 54, 6, C_SELT); }
    else g.fillRound(x, y, W[i], 54, 6, C_TILE);
    g.textCenter(x + W[i] / 2, y + 6, v.tile[i].label, F_SMALL, sel ? C_ACC : C_DIM);
    g.textCenter(x + W[i] / 2, y + 24, v.tile[i].value, F_BIG, C_TXT);
    if (sel) { g.textCenter(x + W[i] / 2, y - 18, "▲", F_SMALL, C_ACC); g.textCenter(x + W[i] / 2, y + 60, "▼", F_SMALL, C_ACC); }
    x += W[i] + 6;
  }
  g.textCenter(GFX_W / 2, 168, v.line1, F_SMALL, C_DIM);
}

void uiRender(Gfx& g, const UiView& v) {
  g.clear(C_BG);
  switch (v.screen) {
    case UIS_BOOT: drawBoot(g, v); return;
    case UIS_LOCK: drawStatus(g, v); drawLock(g, v); return;
    case UIS_START: case UIS_GRID: drawGrid(g, v); break;
    case UIS_TIME: drawTime(g, v); break;
    case UIS_LIST: drawList(g, v); break;
    case UIS_THREAD: drawThread(g, v); break;
    case UIS_INFO: drawInfo(g, v); break;
  }
  drawStatus(g, v);
  drawBottom(g, v);
  if (v.popup) drawPopup(g, v);
}

// ---------- mapa ----------

uint16_t uiColorBg() { return C_BG; }

static void dot(Gfx& g, int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    g.fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
  }
}

void uiMapLine(Gfx& g, int x0, int y0, int x1, int y1) {
  int dx = x1 - x0, dy = y1 - y0;
  int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  if (n == 0) return;
  for (int pass = 0; pass < 2; pass++) {              // najpierw czarna obwódka (5 px), na niej biała linia (3 px)
    int r = pass == 0 ? 2 : 1;
    uint16_t col = pass == 0 ? C_INK : C_TXT;
    for (int i = 0; i <= n; i++) {
      int x = x0 + dx * i / n, y = y0 + dy * i / n;
      if (y - r < UI_MAP_TOP || y + r >= UI_MAP_TOP + UI_MAP_H) continue;
      g.fillRect(x - r, y - r, 2 * r + 1, 2 * r + 1, col);
    }
  }
}

void uiMapMarker(Gfx& g, int x, int y, uint8_t kind, const char* label) {
  if (kind == UIM_CROSS) { g.fillRect(x - 7, y, 15, 1, C_TXT); g.fillRect(x, y - 7, 1, 15, C_TXT); return; }
  if (kind == UIM_ME) { dot(g, x, y, 8, C_TXT); dot(g, x, y, 6, C_BLUE); dot(g, x, y, 2, C_INK); }      // ja: niebieski, żeby nie mylić z repeaterem
  else if (kind == UIM_PERSON) { dot(g, x, y, 5, C_INK); dot(g, x, y, 4, C_GRN); }
  else { dot(g, x, y, 6, C_INK); dot(g, x, y, 4, C_ACC); if (kind == UIM_RPT_FAV) dot(g, x, y, 1, C_INK); }   // repeater: pomarańczowy punkt z czarną ramką
  if (label && label[0]) {
    int w = Gfx::textWidth(label, F_SMALL);
    int lx = x + 10;
    if (lx + w + 6 > GFX_W) lx = x - 12 - w;                // przy prawej krawędzi podpis idzie na lewo
    g.fillRound(lx - 2, y - 7, w + 6, 15, 3, C_BAR);
    g.text(lx + 1, y - 7, label, F_SMALL, C_TXT);
  }
}

void uiMapLabels(Gfx& g, const char* top_right, const char* bottom_left, const char* note) {
  if (top_right && top_right[0]) {
    int w = Gfx::textWidth(top_right, F_SMALL);
    g.fillRound(GFX_W - w - 14, UI_MAP_TOP + 4, w + 10, 16, 4, C_BAR);
    g.text(GFX_W - w - 9, UI_MAP_TOP + 5, top_right, F_SMALL, C_TXT);
  }
  if (bottom_left && bottom_left[0]) {
    int w = Gfx::textWidth(bottom_left, F_SMALL);
    g.fillRound(4, UI_MAP_TOP + UI_MAP_H - 20, w + 10, 16, 4, C_BAR);
    g.text(9, UI_MAP_TOP + UI_MAP_H - 19, bottom_left, F_SMALL, C_DIM);
  }
  if (note && note[0]) {
    GfxLine ln[3];
    int n = Gfx::wrap(note, F_SMALL, GFX_W - 60, ln, 3);
    g.fillRound(20, UI_MAP_TOP + 8, GFX_W - 40, n * 14 + 8, 5, C_BAR);
    for (int i = 0; i < n; i++) g.textN(28, UI_MAP_TOP + 12 + i * 14, note + ln[i].start, ln[i].len, F_SMALL, C_TXT);
  }
}

void uiRenderOverlay(Gfx& g, const UiView& v) {
  drawStatus(g, v);
  drawBottom(g, v);
  if (v.popup) drawPopup(g, v);
}

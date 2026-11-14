// Makieta nowych ekranów: Widmo, Log pakietów, Info o kanale.
// Rysuje tym samym kodem co urządzenie (Gfx, czcionki, kolory, pasek stanu i podpowiedzi z UiView.cpp),
// z przykładowymi danymi. Budowanie i uruchomienie: zob. README.md w tym katalogu.
#include "../../m9_sf6/ui/UiView.cpp"
#include "qr_demo.h"
#include <math.h>
#include <initializer_list>
#include <utility>

static uint16_t fb[GFX_W * GFX_H];
static Gfx g(fb);

static void savePPM(const char* name) {
  FILE* f = fopen(name, "wb"); fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
  for (int i = 0; i < GFX_W * GFX_H; i++) { uint16_t p = fb[i]; uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31) }; fwrite(rgb, 1, 3, f); }
  fclose(f);
}

static UiView v;
static void frame(const char* title, const char* right, std::initializer_list<std::pair<const char*, const char*>> hints) {
  memset(&v, 0, sizeof(v));
  strcpy(v.time, "12:14"); v.batt = 78; v.bt = 2; v.gps_on = true; v.sats = 7; v.unread = 2; v.tab = -1;
  snprintf(v.title, sizeof(v.title), "%s", title); snprintf(v.title_right, sizeof(v.title_right), "%s", right);
  for (auto& h : hints) { snprintf(v.hint[v.nhints].key, sizeof(v.hint[0].key), "%s", h.first); snprintf(v.hint[v.nhints].txt, sizeof(v.hint[0].txt), "%s", h.second); v.nhints++; }
  g.clear(C_BG);
  drawStatus(g, v);
  drawHeader(g, v, STATUS_H);
  drawHints(g, v);
}

// wiersz przełączników pod nagłówkiem (jak filtry w Kontaktach)
static void chips(int y, std::initializer_list<const char*> names, int active) {
  int x = 8, i = 0;
  for (const char* n : names) {
    int w = Gfx::textWidth(n, F_SMALL) + 14;
    g.fillRound(x, y, w, 17, 8, i == active ? C_ACC : C_TILE);
    g.text(x + 7, y + 2, n, F_SMALL, i == active ? C_INK : C_DIM);
    x += w + 6; i++;
  }
}

static uint16_t heat(float dbm) {              // kolor wodospadu: czarny → niebieski → bursztynowy → biały
  struct { float d; int r, gg, b; } k[] = { { -125, 4, 6, 12 }, { -112, 16, 40, 90 }, { -98, 124, 196, 255 }, { -84, 255, 176, 32 }, { -68, 255, 255, 255 } };
  if (dbm <= k[0].d) return GFX_RGB(k[0].r, k[0].gg, k[0].b);
  for (int i = 1; i < 5; i++) if (dbm <= k[i].d) {
    float t = (dbm - k[i - 1].d) / (k[i].d - k[i - 1].d);
    return GFX_RGB((int)(k[i - 1].r + t * (k[i].r - k[i - 1].r)), (int)(k[i - 1].gg + t * (k[i].gg - k[i - 1].gg)), (int)(k[i - 1].b + t * (k[i].b - k[i - 1].b)));
  }
  return GFX_RGB(255, 255, 255);
}

static float rnd() { static uint32_t s = 12345; s = s * 1103515245 + 12345; return ((s >> 16) & 0x7FFF) / 32767.0f; }

// przykładowe pasmo 863–870 MHz: szum ok. -117 dBm i kilka nadajników
static float bandAt(float mhz, int t) {
  float d = -117 + rnd() * 5;
  struct { float f, p, w; int every; } s[] = { { 868.1f, -86, 0.07f, 3 }, { 868.3f, -97, 0.07f, 4 }, { 868.5f, -101, 0.07f, 5 }, { 869.525f, -79, 0.09f, 2 }, { 869.618f, -72, 0.04f, 3 }, { 865.7f, -104, 0.1f, 7 } };
  for (auto& x : s) if (t % x.every == 0) { float e = (mhz - x.f) / x.w; d = fmaxf(d, x.p - e * e * 9 + rnd() * 3); }
  return d;
}

static void screenSpectrumBand() {
  frame("Widmo", "863–870 MHz · co 125 kHz", { { "◄►", "zakres" }, { "Enter", "pauza" }, { "Menu", "opcje" } });
  chips(STATUS_H + HEADER_H + 3, { "Pasmo", "Nasz kanał" }, 0);
  const int x0 = 32, bw = 5, nb = 56, yt = 70, yb = 148;
  const float dTop = -60, dBot = -130;
  auto ydb = [&](float d) { int y = yb - (int)((d - dBot) * (yb - yt) / (dTop - dBot)); return y < yt ? yt : y > yb ? yb : y; };
  // podpasmo P (869,4–869,65 MHz, 500 mW) w tle
  int px0 = x0 + (int)((869.4f - 863.0f) / 0.125f) * bw, px1 = x0 + (int)((869.65f - 863.0f) / 0.125f) * bw;
  g.fillRect(px0, yt, px1 - px0, yb - yt, C_TILE);
  for (int d = -60; d >= -120; d -= 20) {
    char t[8]; snprintf(t, sizeof(t), "%d", d);
    g.textRight(x0 - 4, ydb(d) - 7, t, F_SMALL, C_DIM);
    for (int x = x0; x < x0 + nb * bw; x += 4) g.fillRect(x, ydb(d), 2, 1, C_LINE);
  }
  float peak[56];
  for (int i = 0; i < nb; i++) peak[i] = -130;
  for (int t = 0; t < 12; t++) for (int i = 0; i < nb; i++) peak[i] = fmaxf(peak[i], bandAt(863.0f + i * 0.125f + 0.0625f, t));
  for (int i = 0; i < nb; i++) {
    float d = bandAt(863.0f + i * 0.125f + 0.0625f, 0);
    int y = ydb(d);
    g.fillRect(x0 + i * bw, y, bw - 1, yb - y, d > -100 ? C_BLUE : GFX_RGB(0x2A, 0x50, 0x80));
    g.fillRect(x0 + i * bw, ydb(peak[i]), bw - 1, 2, C_ACC);              // szczyt z ostatnich przebiegów
  }
  g.fillRect(x0, yb, nb * bw, 1, C_LINE);
  // nasza częstotliwość
  int xm = x0 + (int)((869.618f - 863.0f) / 0.125f * bw);
  for (int y = yt - 2; y < yb; y += 4) g.fillRect(xm, y, 1, 2, C_ACC);
  g.textRight(xm - 3, yt - 2, "SF6", F_SMALL, C_ACC);
  const char* xl[] = { "863", "865", "867", "869", "870" }; float xf[] = { 863, 865, 867, 869, 870 };
  for (int i = 0; i < 5; i++) { int x = x0 + (int)((xf[i] - 863) / 0.125f * bw); g.fillRect(x, yb, 1, 3, C_DIM); if (i == 4) g.textRight(x + 1, yb + 3, xl[i], F_SMALL, C_DIM); else g.textCenter(x, yb + 3, xl[i], F_SMALL, C_DIM); }
  // wodospad: ostatnie przebiegi, najnowszy u góry
  const int wy = 168, rows = 16;
  for (int r = 0; r < rows; r++) for (int i = 0; i < nb; i++) g.fillRect(x0 + i * bw, wy + r * 2, bw, 2, heat(bandAt(863.0f + i * 0.125f + 0.0625f, r)));
  g.textRight(x0 - 4, wy - 2, "teraz", F_SMALL, C_DIM);
  g.textRight(x0 - 4, wy + rows * 2 - 13, "-30 s", F_SMALL, C_DIM);
  g.text(8, 201, "szum -116 dBm · najsilniej 869,525 MHz  -78 dBm", F_SMALL, C_DIM);
  savePPM("1-widmo-pasmo.ppm");
}

static void tileStat(int x, int y, int w, const char* label, const char* val, const char* sub, uint16_t c) {
  g.fillRound(x, y, w, 46, 5, C_TILE);
  g.text(x + 7, y + 3, label, F_SMALL, C_DIM);
  int vw = g.text(x + 7, y + 17, val, F_BOLD, c);
  g.text(x + 9 + vw, y + 20, sub, F_SMALL, C_DIM);
}

static void screenSpectrumChannel() {
  frame("Widmo", "869,618 MHz · 62,5 kHz", { { "◄►", "okres" }, { "Enter", "pauza" }, { "Menu", "opcje" } });
  chips(STATUS_H + HEADER_H + 3, { "Pasmo", "Nasz kanał" }, 1);
  int y = 68;
  tileStat(8, y, 98, "Szum", "-114", "dBm", C_TXT);
  tileStat(111, y, 98, "Ostatni pakiet", "-71", "SNR 9,5", C_ACC);
  tileStat(214, y, 98, "Zajętość", "7 %", "1 min", C_TXT);
  // wykres 10 minut: szum (linia) i siła odebranych pakietów (kropki)
  const int x0 = 32, x1 = 312, yt = 124, yb = 196;
  const float dTop = -60, dBot = -125;
  auto ydb = [&](float d) { int yy = yb - (int)((d - dBot) * (yb - yt) / (dTop - dBot)); return yy < yt ? yt : yy > yb ? yb : yy; };
  for (int d = -60; d >= -120; d -= 20) {
    char t[8]; snprintf(t, sizeof(t), "%d", d);
    g.textRight(x0 - 4, ydb(d) - 7, t, F_SMALL, C_DIM);
    for (int x = x0; x < x1; x += 4) g.fillRect(x, ydb(d), 2, 1, C_LINE);
  }
  int prev = -1;
  for (int x = x0; x < x1; x++) {
    float n = -115 + 2.5f * sinf(x * 0.05f) + rnd() * 2 + (x > 220 && x < 240 ? 6 : 0);
    int yy = ydb(n);
    if (prev >= 0) { int a = prev < yy ? prev : yy, b = prev < yy ? yy : prev; g.fillRect(x, a, 1, b - a + 1, C_BLUE); }
    prev = yy;
  }
  for (int i = 0; i < 26; i++) {
    int x = x0 + 4 + (int)(rnd() * (x1 - x0 - 8));
    float d = -105 + rnd() * 38;
    dot(g, x, ydb(d), 2, d > -80 ? C_ACC : GFX_RGB(0xC0, 0x84, 0x18));
  }
  g.fillRect(x0, yb, x1 - x0, 1, C_LINE);
  g.text(x0, yb + 3, "-10 min", F_SMALL, C_DIM);
  g.textRight(x1, yb + 3, "teraz", F_SMALL, C_DIM);
  { int lx = (x0 + x1) / 2 - 50; g.fillRect(lx, yb + 10, 12, 2, C_BLUE); lx += 16; lx += g.text(lx, yb + 3, "szum", F_SMALL, C_DIM) + 12; dot(g, lx + 3, yb + 10, 2, C_ACC); g.text(lx + 10, yb + 3, "pakiety", F_SMALL, C_DIM); }
  savePPM("2-widmo-nasz-kanal.ppm");
}

struct Pkt { const char* t; const char* type; uint16_t c; const char* route; const char* who; const char* snr; const char* rssi; };
static const Pkt PK[] = {
  { "12:14:03", "KANAŁ", C_BLUE, "3h", "#bot · BBot · 1829→7915→1515", "9,5", "-71" },
  { "12:13:58", "ADV", C_GRN, "2h", "RPT Skrzyczne 1829 · 7915", "6,0", "-88" },
  { "12:13:41", "ACK", C_DIM, "D", "do: Marek SP9", "11,2", "-64" },
  { "12:13:40", "PRYW", C_PINK, "D", "Marek SP9", "11,0", "-65" },
  { "12:13:12", "TRASA", C_ACC, "4h", "Ania → ja · 1829→7915→1111→D900", "3,5", "-97" },
  { "12:12:50", "ADV", C_GRN, "1h", "Ustroń RPT 77C1", "8,2", "-79" },
  { "12:12:31", "KANAŁ", C_BLUE, "5h", "Public · Set · A3F2→…→D900", "-2,0", "-109" },
  { "12:12:02", "ŚLEDŹ", C_DIM, "3h", "trace 1829→7915→1515", "4,0", "-93" },
  { "12:11:47", "?", C_LINE, "2h", "nieznany typ 0x0E · 41 B", "1,5", "-101" },
};

static void screenLog() {
  frame("Log pakietów", "234 · 12/min", { { "▲▼", "wybierz" }, { "Enter", "szczegóły" }, { "Menu", "pauza, filtr" } });
  chips(STATUS_H + HEADER_H + 3, { "Wszystkie", "Adverty", "Wiadomości", "Inne" }, 0);
  int y = 68;
  g.text(8, y, "czas", F_SMALL, C_DIM); g.text(62, y, "typ", F_SMALL, C_DIM); g.text(104, y, "hop", F_SMALL, C_DIM);
  g.text(130, y, "od / ścieżka", F_SMALL, C_DIM); g.textRight(276, y, "SNR", F_SMALL, C_DIM); g.textRight(312, y, "dBm", F_SMALL, C_DIM);
  y += 15;
  for (int i = 0; i < 8; i++, y += 16) {
    const Pkt& p = PK[i];
    bool sel = i == 1;
    if (sel) g.fillRect(0, y, GFX_W, 16, C_ACC);
    uint16_t fg = sel ? C_INK : C_TXT, dim = sel ? C_INK : C_DIM;
    g.text(8, y + 1, p.t, F_SMALL, dim);
    int bw = 40;
    g.fillRound(60, y + 1, bw, 14, 3, sel ? C_INK : p.c);
    g.textCenter(60 + bw / 2, y + 1, p.type, F_SMALL, sel ? C_ACC : C_INK);
    g.text(106, y + 1, p.route, F_SMALL, fg);
    g.text(130, y + 1, p.who, F_SMALL, fg, 118);
    g.textRight(276, y + 1, p.snr, F_SMALL, fg);
    g.textRight(312, y + 1, p.rssi, F_SMALL, fg);
  }
  g.fillRect(GFX_W - 3, 84, 2, 128, C_LINE); g.fillRect(GFX_W - 4, 84, 3, 22, C_ACC);
  savePPM("3-log-lista.ppm");
}

static void kv(int& y, const char* k, const char* val, uint16_t c = C_TXT) {
  int vw = g.textRight(GFX_W - 8, y + 2, val, F_BOLD, c);
  g.text(8, y + 2, k, F_NORMAL, C_DIM, GFX_W - 24 - vw);
  y += 19;
}

static void screenLogDetail() {
  frame("Pakiet 12:13:58", "2 z 234", { { "◄►", "poprzedni / następny" }, { "Menu", "pokaż na mapie" } });
  int y = STATUS_H + HEADER_H + 4;
  kv(y, "Typ", "Advert repeatera");
  kv(y, "Nadawca", "RPT Skrzyczne");
  kv(y, "Klucz", "1829 a4f0 …");
  kv(y, "Trasa", "flood · 2 hopy");
  kv(y, "Ścieżka", "1829 → 7915", C_ACC);
  kv(y, "Sygnał", "SNR 6,0 dB · -88 dBm");
  kv(y, "Długość", "112 B · w eterze 98 ms");
  kv(y, "Pozycja", "49,685 N 19,031 E · 14 km");
  savePPM("4-log-szczegoly.ppm");
}

static void screenChannelInfo() {
  frame("# #bot", "Info o kanale", { { "Enter", "kod QR" }, { "Menu", "opcje" } });
  int y = STATUS_H + HEADER_H + 4;
  kv(y, "Rodzaj", "hasztag · klucz z nazwy");
  kv(y, "Hash kanału", "5A · miejsce 3 z 40");
  kv(y, "Wiadomości", "19 · 6 nowych");
  kv(y, "Ostatnia", "9:11 · BBot");
  kv(y, "Średnio do nas", "2,4 hopa · SNR 5,1");
  y += 3;
  g.fillRect(8, y, GFX_W - 16, 1, C_LINE);
  g.text(8, y + 3, "Najaktywniejsi (24 h)", F_SMALL, C_DIM);
  y += 19;
  struct { const char* n; int c; } top[] = { { "BBot", 9 }, { "Set", 6 }, { "Ania", 3 } };
  for (auto& t : top) {
    g.text(8, y, t.n, F_SMALL, C_BLUE);
    g.fillRound(60, y + 3, t.c * 24, 9, 3, C_ACC);
    char c[6]; snprintf(c, sizeof(c), "%d", t.c); g.text(66 + t.c * 24, y, c, F_SMALL, C_DIM);
    y += 14;
  }
  savePPM("5-kanal-info.ppm");
}

static void screenChannelQR() {
  frame("# #bot", "Udostępnij kanał", { { "Wstecz", "" } });
  const int m = 4, n = QR_N, q = 2;
  int box = (n + 2 * q) * m, bx = 10, by = STATUS_H + HEADER_H + 8;
  g.fillRect(bx, by, box, box, GFX_RGB(255, 255, 255));
  for (int r = 0; r < n; r++) for (int c = 0; c < n; c++) if (QR_ROWS[r][c] == '#') g.fillRect(bx + (q + c) * m, by + (q + r) * m, m, m, C_BG);
  int x = bx + box + 12, y = by + 2;
  g.text(x, y, "Zeskanuj telefonem", F_BOLD, C_TXT); y += 19;
  g.text(x, y, "w aplikacji MeshCore", F_SMALL, C_DIM); y += 14;
  g.text(x, y, "albo MeshSpeed —", F_SMALL, C_DIM); y += 14;
  g.text(x, y, "kanał doda się sam.", F_SMALL, C_DIM); y += 24;
  g.text(x, y, "Klucz", F_SMALL, C_DIM); y += 14;
  g.text(x, y, "8b33 87e9 c5cd ea6a", F_SMALL, C_TXT); y += 14;
  g.text(x, y, "c9e5 edba a115 cd72", F_SMALL, C_TXT);
  savePPM("6-kanal-qr.ppm");
}


// ---------- Start (Home): 9 kafelków 3×3, zegar w jednym wierszu ----------
static const char* const IC_WIFI[12] = { "", "...######...", ".##......##.", "#..........#", "....####....", "..##....##..", "............", "....####....", "...#....#...", "............", ".....##.....", ".....##....." };
static const char* const IC_SPEC[12] = { "", "", "..#.........", "..#.....#...", "..#.....#...", "..#..#..#...", "#.#..#..#.#.", "#.#..#..#.#.", "#.#.##.##.##", "#.#.##.##.##", "############", "" };
static const char* const IC_BATT[12] = { "", "", "", "##########..", "#........#..", "#.######.###", "#.######.###", "#........#..", "##########..", "", "", "" };

struct HomeTile { const char* const* ic; const char* label; const char* val; const char* sub; uint16_t vc; int spark; int bar; };

static void sparkline(int x, int y, int w, int h) {   // mały wykres szumu z ostatnich minut
  int prev = -1;
  for (int i = 0; i < w; i++) {
    float n = 0.5f + 0.25f * sinf(i * 0.35f) + rnd() * 0.25f + (i > w - 8 ? 0.3f : 0);
    int yy = y + h - 1 - (int)(n * (h - 1)); if (yy < y) yy = y;
    if (prev >= 0) { int a = prev < yy ? prev : yy, b = prev < yy ? yy : prev; g.fillRect(x + i, a, 1, b - a + 1, C_BLUE); }
    prev = yy;
  }
}

static void screenHome(const char* file, const HomeTile* T, int selIdx, const UiView& st) {
  memset(&v, 0, sizeof(v));
  v = st;
  g.clear(C_BG);
  drawStatus(g, v);
  // zegar, nazwa i data w jednym pasku (mniejszy zegar, żeby zmieściły się 3 rzędy kafelków)
  int y0 = STATUS_H + 2;
  int cw = g.text(8, y0, v.time, F_BIG, C_TXT);
  g.text(8 + cw + 10, y0 + 1, "SP9 Paweł M9", F_BOLD, C_TXT, 150);
  g.textRight(GFX_W - 8, y0 + 4, "sobota, 14.11.2026", F_SMALL, C_DIM);
  const int top = STATUS_H + 28, cols = 3, gap = 4;
  const int tw = (GFX_W - 12 - (cols - 1) * gap) / cols;
  const int th = (GFX_H - BOTTOM_H - top - 4 - 2 * gap) / 3;
  for (int i = 0; i < 9; i++) {
    const HomeTile& t = T[i];
    int x = 6 + (i % cols) * (tw + gap), y = top + (i / cols) * (th + gap);
    bool sel = i == selIdx;
    if (sel) { g.fillRound(x - 1, y - 1, tw + 2, th + 2, 7, C_ACC); g.fillRound(x + 1, y + 1, tw - 2, th - 2, 5, C_SELT); }
    else g.fillRound(x, y, tw, th, 6, C_TILE);
    g.icon(x + 6, y + 4, t.ic, sel ? C_ACC : C_DIM);
    g.text(x + 21, y + 3, t.label, F_SMALL, sel ? C_ACC : C_DIM, tw - 26);
    int vw = g.text(x + 6, y + 17, t.val, F_BOLD, t.vc, tw - 12);
    if (t.spark) sparkline(x + vw + 12, y + 20, tw - vw - 18, 13);
    if (t.bar >= 0) {                               // poziom baterii jako pasek obok napięcia
      int bx = x + vw + 12, bw = tw - vw - 20;
      g.fillRound(bx, y + 22, bw, 9, 3, C_LINE);
      g.fillRound(bx, y + 22, bw * t.bar / 100, 9, 3, t.bar <= 15 ? C_RED : C_GRN);
    }
    g.text(x + 6, y + th - 16, t.sub, F_SMALL, C_DIM, tw - 12);
  }
  drawTabs(g, 0, v.unread, false);
  savePPM(file);
}

static void screensHome() {
  UiView st; memset(&st, 0, sizeof(st));
  strcpy(st.time, "12:14"); st.batt = 78; st.bt = 2; st.gps_on = true; st.sats = 7; st.unread = 2; st.tab = 0;
  const HomeTile A[9] = {
    { IC_MAIL, "Wiadomości", "2 nowe", "#bot · BBot", C_ACC, 0, -1 },
    { IC_USER, "Kontakty", "38", "usłyszane: 12", C_TXT, 0, -1 },
    { IC_TOWER, "Repeatery", "21", "Skrzyczne, 2 min", C_TXT, 0, -1 },
    { IC_SPEC, "Widmo", "-114", "zajęte 7 %", C_TXT, 1, -1 },
    { IC_SIGNAL, "Radio", "SF6", "869,618 · 62,5", C_TXT, 0, -1 },
    { IC_GPS, "Pozycja", "49,8214", "19,0461 · sat. 7", C_TXT, 0, -1 },
    { IC_BT, "Bluetooth", "połączony", "PIN 482913", C_BLUE, 0, -1 },
    { IC_WIFI, "WiFi", "wyłączone", "Enter: włącz", C_DIM, 0, -1 },
    { IC_BATT, "Bateria", "3,92 V", "zostało ok. 14 h", C_TXT, 0, 78 },
  };
  screenHome("7-home.ppm", A, 0, st);
  st.bt = 0; st.wifi = 4; st.charging = true; st.batt = 64; st.unread = 0;
  const HomeTile B[9] = {
    { IC_MAIL, "Wiadomości", "brak nowych", "rozmów: 6", C_TXT, 0, -1 },
    { IC_USER, "Kontakty", "38", "usłyszane: 12", C_TXT, 0, -1 },
    { IC_TOWER, "Repeatery", "21", "Ustroń, 5 min", C_TXT, 0, -1 },
    { IC_SPEC, "Widmo", "-109", "zajęte 12 %", C_TXT, 1, -1 },
    { IC_SIGNAL, "Radio", "SF8", "EU/UK Narrow", C_TXT, 0, -1 },
    { IC_GPS, "Pozycja", "GPS wył.", "Enter: włącz", C_DIM, 0, -1 },
    { IC_BT, "Bluetooth", "wyłączony", "Enter: włącz", C_DIM, 0, -1 },
    { IC_WIFI, "WiFi", "-62 dBm", "Dom_5G", C_TXT, 0, -1 },
    { IC_BATT, "Bateria", "4,05 V", "pełna za 1 h", C_ACC, 0, 64 },
  };
  screenHome("8-home-inne-stany.ppm", B, 8, st);
}

// ---------- Start (Home) w wersji z 6 kafelkami 3×2 i dużym zegarem ----------
enum { K_PLAIN, K_SPARK, K_BATT, K_LINK };
struct Tile6 { const char* const* ic; const char* label; const char* val; const char* sub; uint16_t vc; int kind; int pct; };

static void home6(const char* file, const Tile6* T, int selIdx, const UiView& st, bool btOn, bool btConn, int wifiDbm, const char* wifiName) {
  v = st;
  g.clear(C_BG);
  drawStatus(g, v);
  int cw = g.text(8, STATUS_H + 1, v.time, F_HUGE, C_TXT);
  g.text(8 + cw + 10, STATUS_H + 8, "SP9 Paweł M9", F_BOLD, C_TXT, GFX_W - cw - 26);
  g.text(8 + cw + 10, STATUS_H + 25, "sobota, 14.11.2026", F_SMALL, C_DIM, GFX_W - cw - 26);
  const int top = STATUS_H + 46, cols = 3, gap = 4;
  const int tw = (GFX_W - 12 - (cols - 1) * gap) / cols, th = (GFX_H - BOTTOM_H - top - 4 - gap) / 2;
  for (int i = 0; i < 6; i++) {
    const Tile6& t = T[i];
    int x = 6 + (i % cols) * (tw + gap), y = top + (i / cols) * (th + gap);
    bool sel = i == selIdx;
    if (sel) { g.fillRound(x - 1, y - 1, tw + 2, th + 2, 7, C_ACC); g.fillRound(x + 1, y + 1, tw - 2, th - 2, 5, C_SELT); }
    else g.fillRound(x, y, tw, th, 6, C_TILE);
    g.icon(x + 7, y + 6, t.ic, sel ? C_ACC : C_DIM);
    g.text(x + 23, y + 6, t.label, F_SMALL, sel ? C_ACC : C_DIM, tw - 28);
    if (t.kind == K_LINK) {                          // dwa wiersze: Bluetooth i WiFi
      g.icon(x + 7, y + 26, IC_BT, btOn ? (btConn ? C_BLUE : C_TXT) : C_LINE);
      g.text(x + 23, y + 24, btOn ? (btConn ? "połączony" : "czeka") : "wyłączony", F_BOLD, btOn ? (btConn ? C_BLUE : C_TXT) : C_DIM, tw - 28);
      g.icon(x + 7, y + 47, IC_WIFI, wifiDbm ? C_TXT : C_LINE);
      char w[24]; if (wifiDbm) snprintf(w, sizeof(w), "%d dBm", wifiDbm); else strcpy(w, "wyłączone");
      g.text(x + 23, y + 45, w, F_BOLD, wifiDbm ? C_TXT : C_DIM, tw - 28);
      continue;
    }
    int vw = g.text(x + 7, y + 23, t.val, F_BIG, t.vc, tw - 14);
    if (t.kind == K_SPARK) sparkline(x + vw + 13, y + 26, tw - vw - 20, 15);
    if (t.kind == K_BATT) {
      int bx = x + vw + 13, bw = tw - vw - 21;
      g.fillRound(bx, y + 29, bw, 10, 3, C_LINE);
      g.fillRound(bx, y + 29, bw * t.pct / 100, 10, 3, t.pct <= 15 ? C_RED : C_GRN);
    }
    g.text(x + 7, y + 47, t.sub, F_SMALL, C_DIM, tw - 14);
  }
  drawTabs(g, 0, v.unread, false);
  savePPM(file);
}

static void screensHome6() {
  UiView st; memset(&st, 0, sizeof(st));
  strcpy(st.time, "12:14"); st.batt = 78; st.bt = 2; st.gps_on = true; st.sats = 7; st.unread = 2; st.tab = 0;
  // wariant A: Kontakty i Repeatery zostają; Radio łączy się z Widmem, Bateria z Bluetooth i WiFi (Zasilanie)
  const Tile6 A[6] = {
    { IC_MAIL, "Wiadomości", "2 nowe", "#bot · BBot", C_ACC, K_PLAIN, 0 },
    { IC_USER, "Kontakty", "38", "usłyszane: 12", C_TXT, K_PLAIN, 0 },
    { IC_TOWER, "Repeatery", "21", "Skrzyczne, 2 min", C_TXT, K_PLAIN, 0 },
    { IC_SPEC, "Radio", "SF6", "szum -114 · 7 %", C_TXT, K_SPARK, 0 },
    { IC_GPS, "Pozycja", "49,8214", "19,0461 · sat. 7", C_TXT, K_PLAIN, 0 },
    { IC_BATT, "Bateria", "78 %", "zostało ok. 14 h", C_TXT, K_BATT, 78 },
  };
  home6("9-home6-A.ppm", A, 3, st, true, true, 0, "");
  // wariant B: Kontakty i Repeatery tylko na dolnym pasku (i pod klawiszem pinezki); osobno Widmo, Radio, Łączność, Bateria
  const Tile6 B[6] = {
    { IC_MAIL, "Wiadomości", "2 nowe", "#bot · BBot", C_ACC, K_PLAIN, 0 },
    { IC_SPEC, "Widmo", "-114", "zajęte 7 %", C_TXT, K_SPARK, 0 },
    { IC_SIGNAL, "Radio", "SF6", "869,618 · 62,5", C_TXT, K_PLAIN, 0 },
    { IC_GPS, "Pozycja", "49,8214", "19,0461 · sat. 7", C_TXT, K_PLAIN, 0 },
    { IC_BT, "Łączność", "", "", C_TXT, K_LINK, 0 },
    { IC_BATT, "Bateria", "78 %", "zostało ok. 14 h", C_TXT, K_BATT, 78 },
  };
  st.wifi = 4;
  home6("10-home6-B.ppm", B, 4, st, true, true, -62, "Dom_5G");
}

int main() {
  screensHome6();
  screensHome();
  screenSpectrumBand();
  screenSpectrumChannel();
  screenLog();
  screenLogDetail();
  screenChannelInfo();
  screenChannelQR();
  return 0;
}

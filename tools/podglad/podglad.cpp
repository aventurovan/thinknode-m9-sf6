// Podgląd ekranów na komputerze: rysuje przykładowe ekrany tym samym kodem co firmware i zapisuje pliki .ppm.
// Budowanie: g++ -O1 -I ../../m9_sf6/ui podglad.cpp ../../m9_sf6/ui/Gfx.cpp ../../m9_sf6/ui/UiView.cpp -o podglad
#include "UiView.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static std::vector<uint16_t> fb(GFX_W * GFX_H);
static void save(const char* name) {
  FILE* f = fopen(name, "wb");
  fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
  for (uint16_t p : fb) { uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31) }; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
static void base(UiView& v) {
  memset(&v, 0, sizeof(v));
  strcpy(v.time, "14:32"); v.unread = 3; v.sats = 9; v.gps_on = true; v.bt = 2; v.batt = 82; v.status = true;
}
static void row(UiView& v, const char* t, const char* sub, const char* right, uint8_t icon, uint8_t badge = 0, bool acc = false) {
  UiRow& r = v.rows[v.nrows++]; strcpy(r.text, t); strcpy(r.sub, sub); strcpy(r.right, right); r.icon = icon; r.badge = badge; r.icon_accent = acc;
}
static void hint(UiView& v, const char* k, const char* t) { strcpy(v.hint[v.nhints].key, k); strcpy(v.hint[v.nhints++].txt, t); }

int main() {
  Gfx g(fb.data());
  UiView v;

  base(v); v.screen = UIS_BOOT; strcpy(v.line1, "M9-Paweł · ThinkNode M9 · wersja 0.9.0"); strcpy(v.line2, "869,618 MHz · BW 62,5 · SF6 · CR 4/5");
  uiRender(g, v); save("0-start.ppm");

  base(v); v.screen = UIS_START; v.tab = 0; strcpy(v.name, "M9-Paweł"); strcpy(v.preset, "869,618 · BW 62,5 · SF6 · CR 5");
  const char* T[4][3] = { {"Wiadomości", "3 nowe", "Public 2 · Marek SP9 1"}, {"Sieć", "RPT Skrzyczne", "2 min temu · SNR 9,5 dB"},
                          {"Pozycja", "49.6845, 19.0312", "9 satelitów · 512 m n.p.m."}, {"Urządzenie", "82% · 3,98 V", "Bluetooth połączony"} };
  uint8_t IC[4] = { UII_MAIL, UII_TOWER, UII_GPS, UII_BOLT };
  for (int i = 0; i < 4; i++) { strcpy(v.tile[i].label, T[i][0]); strcpy(v.tile[i].value, T[i][1]); strcpy(v.tile[i].sub, T[i][2]); v.tile[i].icon = IC[i]; }
  v.tile[0].accent = true;
  uiRender(g, v); save("1-start.ppm");

  base(v); v.screen = UIS_LIST; v.tab = 1; strcpy(v.title, "Wiadomości"); strcpy(v.title_right, "3 nieprzeczytane");
  row(v, "Public", "Marek SP9: ktoś słyszy Klimczok?", "14:30", UII_HASH, 2);
  row(v, "Marek SP9", "Jadę na Równicę, będę o 16", "14:21", UII_USER, 1);
  row(v, "południe", "Ania: nowy repeater w Ustroniu działa", "13:58", UII_HASH);
  row(v, "Ania", "Ty: ok, dzięki", "12:40", UII_USER);
  row(v, "Tomek kamper", "Pozycja: 49.7801, 18.9422", "wczoraj", UII_USER);
  row(v, "test", "Ty: test 3", "wczoraj", UII_HASH);
  v.sel = 0; v.more_dn = true;
  uiRender(g, v); save("2-wiadomosci.ppm");

  base(v); v.unread = 1; v.screen = UIS_THREAD; v.tab = -1; strcpy(v.title, "# Public"); strcpy(v.title_right, "kanał");
  const char* B[3][3] = { {"Ania", "13:58", "Nowy repeater w Ustroniu działa, sprawdźcie zasięg na Czantorii i Równicy"}, {"", "14:05 · wysłano", "U mnie słychać, SNR 7"},
                          {"Marek SP9", "14:30", "Ktoś słyszy Klimczok? U mnie cisza od rana. Żółć gęślą jaźń."} };
  for (int i = 0; i < 3; i++) { strcpy(v.bub[i].who, B[i][0]); strcpy(v.bub[i].meta, B[i][1]); strcpy(v.bub[i].text, B[i][2]); v.bub[i].out = i == 1; }
  v.nbub = 3; strcpy(v.input, "Słyszę go ze Skrzycznego, SNR 9"); v.input_chars = 31;
  hint(v, "Enter", "wyślij"); hint(v, "ctrl", "ąęł"); hint(v, "Wstecz", "lista");
  uiRender(g, v); save("3-rozmowa.ppm");

  base(v); v.screen = UIS_LIST; v.tab = 2; strcpy(v.title, "Kontakty"); strcpy(v.title_right, "37 z 350");
  row(v, "Ania", "", "5 min", UII_STAR, 0, true); row(v, "Marek SP9", "", "12 min", UII_STAR, 0, true);
  row(v, "Tomek kamper", "", "1 godz.", UII_USER); row(v, "Kasia", "", "3 godz.", UII_USER); row(v, "Pokój Beskidy", "", "wczoraj", UII_ROOM);
  v.sel = 1;
  uiRender(g, v); save("4-kontakty.ppm");

  base(v); v.screen = UIS_INFO; v.tab = -1; strcpy(v.title, "Informacje"); strcpy(v.title_right, "MC SF6 · ThinkNode M9");
  const char* K[6][2] = { {"Nazwa", "M9-Paweł"}, {"Wersja", "0.9.0"}, {"Radio", "869,618 · 62,5 · SF6 · CR 5"}, {"Moc", "22 dBm"}, {"Bateria", "3,98 V · 82%"}, {"Kontakty", "37 z 350"} };
  for (int i = 0; i < 6; i++) { strcpy(v.kv[i].k, K[i][0]); strcpy(v.kv[i].v, K[i][1]); }
  v.nkv = 6; hint(v, "Wstecz", "ustawienia");
  v.popup = true; strcpy(v.popup1, "Radio: preset SF6"); strcpy(v.popup2, "869,618 MHz · BW 62,5 · SF6 · CR 4/5");
  uiRender(g, v); save("5-informacje.ppm");

  base(v); v.screen = UIS_LOCK; strcpy(v.big, "14:32"); strcpy(v.line1, "M9-Paweł"); strcpy(v.line2, "3 nowe wiadomości");
  uiRender(g, v); save("6-blokada.ppm");
  return 0;
}

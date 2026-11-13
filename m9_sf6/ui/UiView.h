#pragma once
// Interfejs „MC SF6” dla ThinkNode M9 — opis tego, co ma być na ekranie (UiView), i jego rysowanie.
// Część przenośna: nie zna sieci ani sprzętu. UITask wypełnia UiView danymi, uiRender rysuje.

#include "Gfx.h"

#define UI_ROWS_MAX   8
#define UI_BUB_MAX    8
#define UI_KV_MAX     9
#define UI_INPUT_MAX  140

enum UiScreen { UIS_START, UIS_LIST, UIS_THREAD, UIS_INFO, UIS_BOOT, UIS_LOCK, UIS_GRID };
enum UiIcon { UII_NONE, UII_MAIL, UII_TOWER, UII_USER, UII_HASH, UII_STAR, UII_GPS, UII_BT, UII_LOCK, UII_BOLT, UII_SD, UII_ROOM, UII_HOME, UII_GEAR, UII_SCREEN, UII_SHIELD, UII_CHECK, UII_MUTE };

struct UiRow { char text[44]; char sub[80]; char right[64]; uint8_t icon; uint8_t badge; bool icon_accent; };
struct UiBubble { char who[24]; char meta[32]; char text[168]; char path[72]; bool out; bool sel; };
struct UiKV { char k[64]; char v[52]; };
struct UiHint { char key[14]; char txt[32]; };
struct UiTile { char label[24]; char value[36]; char sub[72]; uint8_t icon; bool accent; };

struct UiView {
  // pasek stanu
  char time[6]; int unread; int sats; bool gps_on; uint8_t bt; int batt; bool charging; bool status;
  // ekran
  uint8_t screen;
  int8_t tab;                       // 0..4 = zakładka na dolnym pasku, -1 = podpowiedzi klawiszy
  bool bar_focus;                   // chodzimy po dolnym pasku (ikona w ramce)
  char title[48]; char title_right[80];
  // lista
  UiRow rows[UI_ROWS_MAX]; int nrows; int sel; bool more_up, more_dn;
  char empty[240];                   // tekst, gdy lista jest pusta
  // Start
  char name[36]; char preset[56]; UiTile tile[8]; char footer[56];
  int ntiles; int grid_cols;        // siatka kafelków (Start: 3 kolumny z zegarem, Ustawienia: 2 kolumny); zaznaczony = sel
  // wiersz filtrów nad listą (Kontakty)
  char chips[8][20]; int nchips; int chip_active; bool chip_focus;
  // rozmowa
  UiBubble bub[UI_BUB_MAX]; int nbub; char input[UI_INPUT_MAX * 2 + 4]; int input_chars; bool polish_next;
  // informacje (pary nazwa–wartość)
  UiKV kv[UI_KV_MAX]; int nkv;
  // podpowiedzi klawiszy
  UiHint hint[4]; int nhints;
  // okienko
  bool popup; bool popup_danger; char popup1[56]; char popup2[80];
  // start/blokada
  char big[12]; char line1[280]; char line2[80];
};

void uiRender(Gfx& g, const UiView& v);

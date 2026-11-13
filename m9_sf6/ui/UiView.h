#pragma once
// Interfejs „MC SF6” dla ThinkNode M9 — opis tego, co ma być na ekranie (UiView), i jego rysowanie.
// Część przenośna: nie zna sieci ani sprzętu. UITask wypełnia UiView danymi, uiRender rysuje.

#include "Gfx.h"

#define UI_ROWS_MAX   8
#define UI_BUB_MAX    8
#define UI_KV_MAX     9
#define UI_INPUT_MAX  140

enum UiScreen { UIS_START, UIS_LIST, UIS_THREAD, UIS_INFO, UIS_BOOT, UIS_LOCK };
enum UiIcon { UII_NONE, UII_MAIL, UII_TOWER, UII_USER, UII_HASH, UII_STAR, UII_GPS, UII_BT, UII_LOCK, UII_BOLT, UII_SD, UII_ROOM };

struct UiRow { char text[32]; char sub[72]; char right[28]; uint8_t icon; uint8_t badge; bool icon_accent; };
struct UiBubble { char who[24]; char meta[24]; char text[168]; bool out; };
struct UiKV { char k[24]; char v[44]; };
struct UiHint { char key[10]; char txt[20]; };
struct UiTile { char label[16]; char value[28]; char sub[48]; uint8_t icon; bool accent; };

struct UiView {
  // pasek stanu
  char time[6]; int unread; int sats; bool gps_on; uint8_t bt; int batt; bool charging; bool status;
  // ekran
  uint8_t screen;
  int8_t tab;                       // 0..4 = zakładka na dolnym pasku, -1 = podpowiedzi klawiszy
  char title[32]; char title_right[40];
  // lista
  UiRow rows[UI_ROWS_MAX]; int nrows; int sel; bool more_up, more_dn;
  char empty[64];                   // tekst, gdy lista jest pusta
  // Start
  char name[24]; char preset[44]; UiTile tile[4]; char footer[56];
  // rozmowa
  UiBubble bub[UI_BUB_MAX]; int nbub; char input[UI_INPUT_MAX * 2 + 4]; int input_chars; bool polish_next;
  // informacje (pary nazwa–wartość)
  UiKV kv[UI_KV_MAX]; int nkv;
  // podpowiedzi klawiszy
  UiHint hint[4]; int nhints;
  // okienko
  bool popup; bool popup_danger; char popup1[40]; char popup2[64];
  // start/blokada
  char big[12]; char line1[48]; char line2[48];
};

void uiRender(Gfx& g, const UiView& v);

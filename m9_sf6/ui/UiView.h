#pragma once
// Interfejs „MC SF6” dla ThinkNode M9 — opis tego, co ma być na ekranie (UiView), i jego rysowanie.
// Część przenośna: nie zna sieci ani sprzętu. UITask wypełnia UiView danymi, uiRender rysuje.

#include "Gfx.h"

#define UI_ROWS_MAX   8
#define UI_BUB_MAX    8
#define UI_KV_MAX     9
#define UI_INPUT_MAX  140

enum UiScreen { UIS_START, UIS_LIST, UIS_THREAD, UIS_INFO, UIS_BOOT, UIS_LOCK, UIS_GRID, UIS_TIME, UIS_SPEC, UIS_LOG, UIS_CHINFO, UIS_QR, UIS_EMOJI };
enum UiIcon { UII_NONE, UII_MAIL, UII_TOWER, UII_USER, UII_HASH, UII_STAR, UII_GPS, UII_BT, UII_LOCK, UII_BOLT, UII_SD, UII_ROOM, UII_HOME, UII_GEAR, UII_SCREEN, UII_SHIELD, UII_CHECK, UII_MUTE, UII_SIGNAL, UII_NET, UII_SPEC, UII_BATT, UII_WIFI, UII_PING };
// kolor ikony kafelka (0 = domyślny szary)
enum UiIconColor { UIC_DEF, UIC_AMBER, UIC_BLUE, UIC_GREEN, UIC_PINK, UIC_RED, UIC_VIOLET, UIC_TEAL };

struct UiRow { char text[44]; char sub[80]; char right[64]; uint8_t icon; uint8_t badge; bool icon_accent; };
struct UiBubble { char who[40]; char meta[32]; char text[168]; char path[72]; bool out; bool sel; bool sep; };   // sep: nad dymkiem kreska „nowe wiadomości”
struct UiKV { char k[64]; char v[52]; };
struct UiHint { char key[14]; char txt[32]; };
struct UiTile { char label[24]; char value[36]; char sub[72]; uint8_t icon; bool accent; uint8_t kind; char side[16]; char sub2[48]; uint8_t icol; };   // icol: UIC_*   // side: drobny dopisek z prawej strony wartości   // kind: UIT_*
enum UiTileKind { UIT_PLAIN, UIT_NOISE, UIT_BATT, UIT_NET };

#define UI_SPARK_N    48        // punkty małych wykresów na kafelkach
#define UI_SPEC_BINS  56        // słupki widma
#define UI_SPEC_WF    16        // wiersze wodospadu
#define UI_NOISE_N    280       // szum z 10 minut (co ok. 2 s)
#define UI_PKDOT_MAX  64        // kropki pakietów na wykresie kanału
#define UI_LOG_ROWS   8
#define UI_QR_BYTES   410       // bufor kodu QR do wersji 10
enum UiLogColor { ULC_BLUE, ULC_GREEN, ULC_DIM, ULC_PINK, ULC_ACC, ULC_LINE };
struct UiLogRow { char t[10]; char type[8]; uint8_t col; char hop[6]; char who[64]; char snr[8]; char rssi[8]; };

struct UiView {
  // pasek stanu
  char time[6]; int unread; int sats; bool gps_on; uint8_t bt; uint8_t wifi; int batt; bool charging; bool status;
  char radio[6]; bool radio_ok; bool tx_now, rx_now;      // pasek: preset radia (np. SF6), czy radio słucha, mignięcie przy nadaniu / odbiorze
  // ekran
  uint8_t screen;
  int8_t tab;                       // 0..5 = zakładka na dolnym pasku, -1 = podpowiedzi klawiszy
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
  UiBubble bub[UI_BUB_MAX]; int nbub;
  int bub_off;                      // o ile pikseli dolny dymek jest przesunięty w dół (płynne przewijanie)
  int sb_total, sb_view, sb_pos;    // suwak: wysokość całej rozmowy, widocznej części i przewinięcie od dołu (px)
  char input[UI_INPUT_MAX * 2 + 4]; int input_chars; bool polish_next;
  // informacje (pary nazwa–wartość)
  UiKV kv[UI_KV_MAX]; int nkv;
  // podpowiedzi klawiszy
  UiHint hint[4]; int nhints;
  // okienko
  bool popup; bool popup_danger; char popup1[56]; char popup2[80];
  // start/blokada
  char big[12]; char line1[280]; char line2[80];
  // Start: wykres szumu, napięcie baterii, Bluetooth i WiFi
  int8_t spark[UI_SPARK_N]; int nspark;
  uint16_t bhist[UI_SPARK_N]; int nbhist; bool bchg;
  uint8_t bt_state; char bt_txt[24]; int wifi_dbm;          // bt_state: 0 wył., 1 czeka, 2 połączony; wifi_dbm 0 = wył.
  // widmo
  uint8_t spec_tab; bool spec_pause; char spec_range[40];
  float spec_f0, spec_step; int8_t spec_cur[UI_SPEC_BINS], spec_peak[UI_SPEC_BINS]; bool spec_have;
  int8_t spec_wf[UI_SPEC_WF][UI_SPEC_BINS]; int spec_wf_n; float spec_mark; char spec_info[80];
  int noise_dbm; int last_rssi; char last_snr[16]; int busy_pct; bool have_last;
  int8_t nhist[UI_NOISE_N]; int nnhist;                      // szum od najstarszego (co ok. 2 s)
  int16_t pk_x[UI_PKDOT_MAX]; int8_t pk_dbm[UI_PKDOT_MAX]; int npk;   // pakiety: pozycja 0..UI_NOISE_N-1 i siła
  // log pakietów
  UiLogRow lrow[UI_LOG_ROWS]; int nlrow;
  // info o kanale: najaktywniejsi
  char top_name[3][24]; int top_cnt[3]; int ntop;
  // kod QR
  int16_t emo[32]; uint8_t emo_n; int16_t emo_total, emo_first;   // emo_total > 0: przeglądanie całego zestawu (suwak)                  // wybór emotki: numery w zestawie (Gfx::emojiFind)
  uint8_t qr[UI_QR_BYTES]; bool qr_ok; char qr_line[4][40];
};

void uiRender(Gfx& g, const UiView& v);
// rozmowa: wysokość dymka w pikselach (bez odstępu) i wysokość obszaru z dymkami
int uiBubbleHeight(const UiBubble& b);
int uiThreadViewH();

// Mapa: UITask sam składa podkład w buforze, a te funkcje dorysowują resztę.
#define UI_MAP_TOP 20          // mapa zajmuje wiersze 20..215 (196 px)
#define UI_MAP_H   196
enum UiMapMark { UIM_ME, UIM_RPT, UIM_RPT_FAV, UIM_PERSON, UIM_CROSS };
void uiMapMarker(Gfx& g, int x, int y, uint8_t kind, const char* label);
void uiMapLine(Gfx& g, int x0, int y0, int x1, int y1);   // odcinek trasy: biały z czarną obwódką
void uiMapLabels(Gfx& g, const char* top_right, const char* bottom_left, const char* note);
void uiRenderOverlay(Gfx& g, const UiView& v);     // pasek stanu, dolny pasek i okienko na wierzchu mapy
uint16_t uiColorBg();

#include "UITask.h"
#include "M9Lcd.h"
#include "M9Ble.h"
#include "M9Download.h"
#include "M9Radio.h"
#include "M9PktLog.h"
#include "qrcodegen.h"
#include "../Version.h"
#include <MyMesh.h>
#include <helpers/sensors/LocationProvider.h>
#include <SPIFFS.h>
#include <math.h>

#define M9_PIN_EXT_PWR   1          // LOW = podłączone zasilanie zewnętrzne
#define ROWS_VISIBLE     8
#define KV_VISIBLE       8
#define CFG_MAGIC        0x4D395531UL   // „M9U1”
#define FILE_CFG         "/m9ui.cfg"
#define FILE_PW          "/m9ui.pw"
#define FILE_MSGS        "/m9ui.msg"
#define FILE_MUTED       "/m9ui.mut"
#define FILE_WIFI        "/m9ui.wifi"
#define FILE_LOCK        "/m9ui.lck"
#define MAP_URL_DEFAULT  "https://github.com/aventurovan/thinknode-m9-sf6/releases/download/mapa/"
#define OTA_URL          "https://github.com/aventurovan/thinknode-m9-sf6/releases/download/latest-build/"
#define MAP_URL_OLD      "https://github.com/aventurovan/m9-mapa/releases/download/mapa/"
#define DEF_LAT          49.8225         // środek mapy, gdy nie ma pozycji: Bielsko-Biała
#define DEF_LON          19.0469
#define FILE_MSGS_TMP    "/m9ui.tmp"
#define FILE_MSGS_BAK    "/m9ui.bak"
#define SAVE_DELAY_MS    45000UL        // wiadomości zapisujemy 45 s po zmianie (albo po wygaszeniu ekranu)
#define SAVE_CHUNK       6              // ile wiadomości na jedno przejście pętli

static const int8_t TX_STEPS[] = { 10, 14, 17, 20, 22 };
static const float BW_STEPS[] = { 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };
static const uint32_t SLEEP_STEPS[] = { 30000UL, 60000UL, 300000UL, 0 };            // 0 = nigdy
static const char* const SLEEP_NAMES[] = { "30 s", "1 min", "5 min", "nigdy" };
static const uint8_t ADV_HOURS[] = { 0, 1, 3, 6, 12, 24 };
// oszczędzanie energii
static const uint32_t DIM_STEPS[] = { 10000UL, 20000UL, 30000UL, 0 };
static const char* const DIM_NAMES[] = { "po 10 s", "po 20 s", "po 30 s", "wyłączone" };
static const uint16_t GPS_MIN[] = { 0, 5, 15, 30, 60 };
static const char* const GPS_NAMES[] = { "włączony stale", "co 5 min", "co 15 min", "co 30 min", "co 60 min" };
static const uint16_t BTA_MIN[] = { 0, 5, 15, 30, 60 };
static const char* const BTA_NAMES[] = { "nigdy", "po 5 min", "po 15 min", "po 30 min", "po 60 min" };
// zaszyfrowane hasła w plikach: 2 bajty kontroli + dane wyrównane do 16 bajtów
#define WIFI_SEALED      (2 + 64)                               // hasło WiFi (64 B)
#define PW_PLAIN         ((UI_PW_MAX * 22 + 15) / 16 * 16)      // hasła repeaterów: 12 × (klucz 6 B + hasło 16 B)
#define PW_SEALED        (2 + PW_PLAIN)
#define NOISE_SLOT_MS    2143UL         // 10 minut szumu w 280 punktach
#define BATT_SAMPLE_MS   450000UL       // napięcie baterii co 7,5 min
static const char* const FILTER_NAMES[] = { "wszystkie", "osoby", "repeatery", "pokoje", "czujniki", "ulubione" };
static const char* const QUICK[] = { "Test", "OK", "Tak", "Nie", "Zaraz będę", "Oddzwonię później", "Moja pozycja", "Moja pozycja + link do mapy" };
#define N_QUICK_TEXT 6      // pierwsze pozycje to gotowy tekst, dwie ostatnie to pozycja
static const uint8_t BRIGHT_STEPS[] = { 10, 25, 50, 75, 100 };
static const char* const CLI_CMDS[] = { "ver", "clock", "neighbors", "get radio", "get tx", "get name", "get repeat" };

// ---------------- pomocnicze ----------------

#ifdef ESP32
  static uint32_t esp_random_m9() { return esp_random(); }
#else
  static uint32_t esp_random_m9() { return (uint32_t)rand() * 2654435761u; }
#endif
static void scopy(char* dest, size_t n, const char* src) { strncpy(dest, src, n - 1); dest[n - 1] = 0; }

// czas lokalny dla Polski z czasu UTC (czas letni: ostatnia niedziela marca – ostatnia niedziela października)
static void localTime(uint32_t utc, struct tm* out) {
  time_t t = (time_t)utc;
  struct tm g;
  gmtime_r(&t, &g);
  bool dst = false;
  if (g.tm_mon > 2 && g.tm_mon < 9) dst = true;
  else if (g.tm_mon == 2 || g.tm_mon == 9) {
    int lastSun = 31 - ((g.tm_wday - g.tm_mday + 31 + 700) % 7);      // dzień miesiąca ostatniej niedzieli
    if (g.tm_mon == 2) dst = g.tm_mday > lastSun || (g.tm_mday == lastSun && g.tm_hour >= 1);
    else dst = g.tm_mday < lastSun || (g.tm_mday == lastSun && g.tm_hour < 1);
  }
  t += dst ? 7200 : 3600;
  gmtime_r(&t, out);
}

static int battPercent(uint16_t mv) {
  int p = ((int)mv - 3300) * 100 / (4150 - 3300);
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

static double distKm(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371.0, D = 3.14159265358979 / 180.0;
  double a = sin((lat2 - lat1) * D / 2), b = sin((lon2 - lon1) * D / 2);
  double h = a * a + cos(lat1 * D) * cos(lat2 * D) * b * b;
  return 2 * R * asin(sqrt(h));
}

// przecinek dziesiętny: kropka tylko między cyframi („15.4 km” tak, „godz.” nie)
static void comma(char* s) { for (char* p = s; *p; p++) if (*p == '.' && p > s && p[-1] >= '0' && p[-1] <= '9' && p[1] >= '0' && p[1] <= '9') *p = ','; }

static void fmtSnr(char* dest, size_t n, int snr_x4) {
  int a = snr_x4 < 0 ? -snr_x4 : snr_x4;
  snprintf(dest, n, "%s%d,%d dB", snr_x4 < 0 ? "-" : "", a / 4, (a % 4) * 25 / 10);
}

// liczba hopów słownie, jak w aplikacji MeshSpeed
static void hopsTxt(char* dest, size_t n, int hops, bool direct) {
  if (direct) scopy(dest, n, "trasa stała");
  else if (hops <= 0) scopy(dest, n, "bezpośrednio");
  else if (hops == 1) scopy(dest, n, "1 hop");
  else snprintf(dest, n, hops < 5 ? "%d hopy" : "%d hopów", hops);
}

static const char* typeName(uint8_t t) {
  switch (t) { case ADV_TYPE_CHAT: return "osoba"; case ADV_TYPE_REPEATER: return "repeater"; case ADV_TYPE_ROOM: return "pokój"; case ADV_TYPE_SENSOR: return "czujnik"; }
  return "inne";
}

static uint8_t typeIcon(uint8_t t, bool fav) {
  if (fav) return UII_STAR;
  switch (t) { case ADV_TYPE_REPEATER: return UII_TOWER; case ADV_TYPE_ROOM: return UII_ROOM; case ADV_TYPE_SENSOR: return UII_BOLT; }
  return UII_USER;
}

// polska litera dla klawisza naciśniętego po ctrl (UTF-8); NULL = brak
static const char* polishFor(uint8_t k) {
  switch (k) {
    case 'a': return "ą"; case 'c': return "ć"; case 'e': return "ę"; case 'l': return "ł"; case 'n': return "ń";
    case 'o': return "ó"; case 's': return "ś"; case 'z': return "ż"; case 'x': return "ź";
    case 'A': return "Ą"; case 'C': return "Ć"; case 'E': return "Ę"; case 'L': return "Ł"; case 'N': return "Ń";
    case 'O': return "Ó"; case 'S': return "Ś"; case 'Z': return "Ż"; case 'X': return "Ź";
  }
  return NULL;
}

static void setRow(UiRow& r, const char* text, const char* right = "", uint8_t icon = UII_NONE, bool accent = false) {
  scopy(r.text, sizeof(r.text), text);
  scopy(r.right, sizeof(r.right), right);
  r.icon = icon; r.icon_accent = accent;
}

static const char* onOff(bool on) { return on ? "włączone" : "wyłączone"; }
static const char* yesNo(bool y) { return y ? "tak" : "nie"; }

UITask::UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
  _prefs = NULL; _fb = NULL; _v = NULL; _msgs = NULL; _list = NULL; _kv = NULL;
  _msg_n = _thread_n = _list_n = _kv_n = 0;
  _kv_title[0] = _kv_right[0] = 0;
  _depth = 1; _nav[0].scr = S_BOOT; _nav[0].sel = _nav[0].top = 0;
  memset(_tabsel, 0, sizeof(_tabsel));
  memset(&_cfg, 0, sizeof(_cfg));
  _cfg.magic = CFG_MAGIC; _cfg.show_path = 1; _cfg.sleep_idx = 1; _cfg.kb_light = 1;
  _pw_n = 0;
  _cur[0] = 0; _cur_chan = false; memset(_cur_key, 0, sizeof(_cur_key)); _msgs_old_format = false; _scroll_unread = -1; _scroll_px = _scroll_tgt = 0; _scroll_next = 0; _th_total = _th_n = 0; _th_sel = -1; _th_new = -1; _th_idx = NULL; _th_h = NULL; _bt_boot_done = false; _wifi_bars = -1;
  _nh_n = 0; _nh_next = _nh_slot_end = 0; _nh_min = 1; _bh_n = _bh_seg = 0; _bh_next = 30000; _bh_chg = false;
  _sp_tab = _sp_range = 0; _sp_pause = _sp_have = false; _sp_next = 0; _sp_ms = 0; _sp_wf_n = 0;
  memset(_sp_cur, -128, sizeof(_sp_cur)); memset(_sp_peak, -128, sizeof(_sp_peak)); memset(_sp_wf, -128, sizeof(_sp_wf));
  _lg_filter = 0; _lg_n = 0; _kv_pkt = 0;
  _dimmed = false; _gps_off_phase = false; _gps_next = _gps_on_at = _gps_fix_at = 0; _gps_cache = false; _gps_lat = _gps_lon = 0;
  _bt_idle_since = 0; _bt_auto_off = false;
  _sel_mode = false; _selk = NULL; _sel_n = 0;
  _input[0] = 0; _input_chars = 0; _polish = false; _input_mode = IN_MSG;
  memset(_ck, 0, sizeof(_ck)); _cname[0] = 0; _ctype = 0; _cfav = false;
  _msg_idx = -1; _menu_n = 0; _menu_title[0] = 0; _menu_msg = false; _search[0] = 0;
  _rf = 869.618f; _rbw = 62.5f; _rsf = 6; _rcr = 8;
  _wait_kind = 0; _wait_until = 0; _cli_cmd[0] = 0; _pw_try[0] = 0; _pw_new = false; _disc_until = 0;
  _dirty = true; _awake = true; _msgs_dirty = false;
  _next_tick = _popup_until = _sleep_at = _boot_until = _save_at = _next_advert = _last_key = 0;
  _pop1[0] = _pop2[0] = 0; _pop_danger = false;
  _confirm = C_NONE; _confirm_name[0] = 0;
  _msgcount = 0;
  _menu_thread = false; _tname[0] = 0; _tchan = false;
  _pick_id = 0; _pick_n = 0; _pick_cur = 0; _pick_title[0] = 0;
  _muted_n = 0;
  _bar = false; _bar_sel = 0;
  _locked = false; _ul_first = _ul_last = 0; _ul_back = false;
  _pin_in[0] = 0; _pin_have = false; _pin_fails = 0; _pin_wait = 0; memset(_pin_salt, 0, sizeof(_pin_salt)); memset(_pin_hash, 0, sizeof(_pin_hash));
  _nb_total = _nb_got = 0; _kv_more = false;
  _ble_conn_seen = _ble_auth_seen = 0; _ble_pin_at = 0; _pin_popup = false;
  _chas_pos = false; _clat = _clon = 0;
  _map_ready = false; _sd_pending = true;
  _map_lat = DEF_LAT; _map_lon = DEF_LON; _map_zoom = 12; _map_follow = true;
  memset(&_wifi, 0, sizeof(_wifi)); strcpy(_wifi.url, MAP_URL_DEFAULT);
  memset(_tf, 0, sizeof(_tf)); _tf_sel = 0; _snap = NULL;
  _bt_killed = false; _adv_seen = 0; _sd_test_at = 0;
  _msg_loaded = 0; _msg_src = 0; _msg_saved = 0; _msg_saved_at = 0;
  _route_n = 0; _route_star = false; _route_unknown = 0; _route_title[0] = 0; _ota_reboot_at = 0;
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _prefs = node_prefs;

  pinMode(M9_PIN_EXT_PWR, INPUT);
  _fb = (uint16_t*)ps_malloc(GFX_W * GFX_H * 2);
  _v = (UiView*)ps_calloc(1, sizeof(UiView));
  _msgs = (UiMsg*)ps_calloc(UI_MSG_MAX, sizeof(UiMsg));
  _list = (uint16_t*)ps_calloc(UI_LIST_MAX, sizeof(uint16_t));
  _kv = (UiKV*)ps_calloc(UI_KVS_MAX, sizeof(UiKV));
  _snap = (UiMsg*)ps_calloc(UI_MSG_MAX, sizeof(UiMsg));
  _selk = (uint8_t (*)[6])ps_calloc(UI_LIST_MAX, 6);
  _th_idx = (int16_t*)ps_calloc(UI_MSG_MAX, sizeof(int16_t));
  _th_h = (uint16_t*)ps_calloc(UI_MSG_MAX, sizeof(uint16_t));

  loadAll();
  _kb.begin();
  _buzzer.begin();
  _buzzer.quiet(_prefs->buzzer_quiet);
  if (_display) _display->turnOn();
  m9lcdBacklight(brightPercent());
  _boot_until = millis() + 2500;
  _sleep_at = millis() + 60000UL;
  if (ADV_HOURS[_cfg.adv_idx]) _next_advert = millis() + 120000UL;
  draw();
}

// ---------------- zapis w pamięci urządzenia ----------------
// Małe pliki w systemie plików MeshCore. Wiadomości zapisujemy rzadko (po chwili bez klawiszy), żeby nie zużywać pamięci.

void UITask::loadAll() {
  File f = SPIFFS.open(FILE_CFG, "r");
  if (f) {
    Cfg c = _cfg;                                    // starszy (krótszy) plik: nowe pola zostają domyślne
    int n = f.read((uint8_t*)&c, sizeof(c));
    if (n >= 8 && c.magic == CFG_MAGIC) _cfg = c;
    f.close();
  }
  if (_cfg.sleep_idx > 3) _cfg.sleep_idx = 1;
  if (_cfg.adv_idx >= sizeof(ADV_HOURS)) _cfg.adv_idx = 0;
  if (_cfg.ct_filter > 5) _cfg.ct_filter = 0;
  if (_cfg.map_range > 2) _cfg.map_range = 0;
  if (_cfg.gps_mode > 4) _cfg.gps_mode = 0;
  if (_cfg.dim_idx > 3) _cfg.dim_idx = 0;
  if (_cfg.bt_auto > 4) _cfg.bt_auto = 0;

  f = SPIFFS.open(FILE_PW, "r");
  if (f) {
    uint8_t n = 0;
    // nowy format „P2”: liczba haseł i zaszyfrowany blok; stary (jawny) wczytujemy raz i od razu zapisujemy zaszyfrowany
    uint8_t h[3]; bool old = false;
    uint8_t sealed[PW_SEALED];
    if (f.size() == 3 + PW_SEALED && f.read(h, 3) == 3 && h[0] == 'P' && h[1] == '2' && h[2] <= UI_PW_MAX && f.read(sealed, PW_SEALED) == PW_SEALED) {
      uint8_t plain[PW_PLAIN];
      if (the_mesh.uiUnseal(plain, sealed, PW_SEALED) >= (int)sizeof(_pw)) { memcpy(_pw, plain, sizeof(_pw)); _pw_n = h[2]; }
      memset(plain, 0, sizeof(plain));
    } else {
      f.seek(0);
      uint8_t n = 0;
      if (f.read(&n, 1) == 1 && n <= UI_PW_MAX && f.read((uint8_t*)_pw, sizeof(Pw) * n) == sizeof(Pw) * n) { _pw_n = n; old = true; }
    }
    for (int i = 0; i < _pw_n; i++) _pw[i].pw[sizeof(_pw[0].pw) - 1] = 0;
    f.close();
    if (old) savePw();
  }

  // wiadomości: główny plik, a gdy go nie ma albo jest niepełny — kopia sprzed podmiany albo plik tymczasowy
  static const char* const MSG_FILES[3] = { FILE_MSGS, FILE_MSGS_BAK, FILE_MSGS_TMP };
  for (int k = 0; k < 3 && _msgs && _msg_n == 0; k++) {
    int n = readMsgFile(MSG_FILES[k]);
    if (n >= 0) { _msg_n = n; _msg_src = k; break; }
  }
  for (int i = 0; i < _msg_n; i++) {                           // plik mógł zostać uszkodzony: pola w bezpiecznych granicach
    UiMsg& m = _msgs[i];
    m.sent_id = 0;                                              // numery wysyłek nie przeżywają restartu
    m.thread[sizeof(m.thread) - 1] = 0; m.who[sizeof(m.who) - 1] = 0; m.text[sizeof(m.text) - 1] = 0;
    if (m.hash_size > 3) m.hash_size = 0;
    if (m.path_bytes > sizeof(m.path)) m.path_bytes = 0;
    if (m.rep_n > 6) m.rep_n = 0;
    if (m.rep_hs > 3) m.rep_hs = 0;
    m.chan = m.chan ? 1 : 0; m.out = m.out ? 1 : 0;
  }
  if (_msgs_old_format) {                                      // starszy plik: rozmowom prywatnym dopisz klucz kontaktu o tej nazwie
    ContactInfo c;
    int total = the_mesh.getTotalContactSlots();
    for (int i = 0; i < _msg_n; i++) {
      UiMsg& m = _msgs[i];
      if (m.chan) continue;
      if (i > 0 && !_msgs[i - 1].chan && strcmp(_msgs[i - 1].thread, m.thread) == 0) { memcpy(m.key, _msgs[i - 1].key, 6); continue; }
      for (int k = MAX_ANON_CONTACTS; k < total; k++) if (the_mesh.getContactByIdx(k, c) && c.type != ADV_TYPE_NONE && strcmp(c.name, m.thread) == 0) { memcpy(m.key, c.id.pub_key, 6); break; }
    }
    _msgs_old_format = false;
    if (_msg_n > 0) { _msgs_dirty = true; _save_at = millis() + 10000; }
  }
  _msg_loaded = _msg_n;

  f = SPIFFS.open(FILE_WIFI, "r");
  if (f) {
    // nowy format „W2”: nazwa sieci i adres jawnie, hasło zaszyfrowane; stary (jawny) wczytujemy raz i zapisujemy od nowa
    WifiCfg w; memset(&w, 0, sizeof(w));
    uint8_t h[2], sealed[WIFI_SEALED]; bool old = false;
    if (f.size() == 2 + sizeof(w.ssid) + sizeof(w.url) + WIFI_SEALED && f.read(h, 2) == 2 && h[0] == 'W' && h[1] == '2'
        && f.read((uint8_t*)w.ssid, sizeof(w.ssid)) == sizeof(w.ssid) && f.read((uint8_t*)w.url, sizeof(w.url)) == sizeof(w.url) && f.read(sealed, WIFI_SEALED) == WIFI_SEALED) {
      uint8_t plain[sizeof(w.pass)];
      if (the_mesh.uiUnseal(plain, sealed, WIFI_SEALED) >= (int)sizeof(w.pass)) memcpy(w.pass, plain, sizeof(w.pass));
      memset(plain, 0, sizeof(plain));
      w.ssid[32] = 0; w.pass[63] = 0; w.url[127] = 0; _wifi = w;
    } else if (f.size() == sizeof(w)) {
      f.seek(0);
      if (f.read((uint8_t*)&w, sizeof(w)) == sizeof(w)) { w.ssid[32] = 0; w.pass[63] = 0; w.url[127] = 0; _wifi = w; old = true; }
    }
    f.close();
    if (old) saveWifi();
  }
  if (!_wifi.url[0] || strcmp(_wifi.url, MAP_URL_OLD) == 0) strcpy(_wifi.url, MAP_URL_DEFAULT);

  f = SPIFFS.open(FILE_LOCK, "r");
  if (f) {
    if (f.read(_pin_salt, 8) == 8 && f.read(_pin_hash, 32) == 32 && f.read(&_pin_fails, 1) == 1) _pin_have = true;
    f.close();
  }
  if (_cfg.lock > 2) _cfg.lock = 0;
  if (_cfg.lock == 2 && !_pin_have) _cfg.lock = 1;             // brak pliku z PIN-em: zostaje zwykła blokada klawiszy
  if (_cfg.lock == 2) { _locked = true; pinPenalty(millis()); }   // z PIN-em urządzenie startuje zablokowane

  f = SPIFFS.open(FILE_MUTED, "r");
  if (f) {
    uint8_t n = 0;
    if (f.read(&n, 1) == 1 && n <= UI_MUTED_MAX && f.read((uint8_t*)_muted, 32 * n) == (size_t)(32 * n)) _muted_n = n;
    for (int i = 0; i < _muted_n; i++) _muted[i][31] = 0;
    f.close();
  }
}

// PIN blokady ekranu: sól, skrót PIN-u (nie sam PIN) i licznik złych prób
void UITask::savePin() {
  File f = SPIFFS.open(FILE_LOCK, "w");
  if (!f) return;
  f.write(_pin_salt, 8); f.write(_pin_hash, 32); f.write(&_pin_fails, 1);
  f.close();
}

// po 5 złych próbach przerwa: 30 s, 1 min, 2 min… najwyżej 15 minut
void UITask::pinPenalty(unsigned long now) {
  if (_pin_fails < 5) { _pin_wait = 0; return; }
  uint32_t s = 30; for (int i = 5; i < _pin_fails && s < 900; i++) s *= 2;
  if (s > 900) s = 900;
  _pin_wait = now + s * 1000UL; if (!_pin_wait) _pin_wait = 1;
}

void UITask::saveWifi() {
  uint8_t plain[sizeof(_wifi.pass)], sealed[WIFI_SEALED + 16];
  memset(plain, 0, sizeof(plain)); memcpy(plain, _wifi.pass, strnlen(_wifi.pass, sizeof(plain) - 1));
  int n = the_mesh.uiSeal(sealed, plain, sizeof(plain));
  memset(plain, 0, sizeof(plain));
  if (n != WIFI_SEALED) return;
  File f = SPIFFS.open(FILE_WIFI, "w");
  if (!f) return;
  f.write((const uint8_t*)"W2", 2);
  f.write((const uint8_t*)_wifi.ssid, sizeof(_wifi.ssid));
  f.write((const uint8_t*)_wifi.url, sizeof(_wifi.url));
  f.write(sealed, WIFI_SEALED);
  f.close();
}

void UITask::saveMuted() {
  File f = SPIFFS.open(FILE_MUTED, "w");
  if (!f) return;
  uint8_t n = (uint8_t)_muted_n;
  f.write(&n, 1);
  f.write((const uint8_t*)_muted, 32 * _muted_n);
  f.close();
}

bool UITask::isMuted(const char* name) {
  for (int i = 0; i < _muted_n; i++) if (strcmp(_muted[i], name) == 0) return true;
  return false;
}

void UITask::toggleMute(const char* name) {
  for (int i = 0; i < _muted_n; i++) if (strcmp(_muted[i], name) == 0) {
    memmove(_muted[i], _muted[i + 1], 32 * (_muted_n - i - 1)); _muted_n--;
    saveMuted();
    return;
  }
  if (_muted_n == UI_MUTED_MAX) { memmove(_muted[0], _muted[1], 32 * (UI_MUTED_MAX - 1)); _muted_n--; }
  scopy(_muted[_muted_n++], 32, name);
  saveMuted();
}

// Wiadomość w formacie sprzed wersji 0.20 (bez klucza kontaktu) — tylko do jednorazowego przeniesienia starego pliku.
struct UiMsgV1 {
  char thread[32]; char who[24]; char text[164];
  uint32_t ts;
  uint8_t chan, out, unread, hops;
  uint16_t sent_id;
  uint8_t repeats, delivered;
  uint8_t direct, hash_size, path_bytes;
  uint8_t path[24];
  int8_t snr_x4; int16_t rssi;
  uint8_t rep_n, rep_hs; uint8_t rep_id[6][3]; int8_t rep_snr[6];
};

// czyta plik wiadomości; -1 = brak albo uszkodzony (zła długość)
// Formaty: „MB” — bieżący (z kluczem kontaktu); „MA” i „M9” — starsze, przenoszone przy wczytaniu.
int UITask::readMsgFile(const char* path) {
  File f = SPIFFS.open(path, "r");
  if (!f) return -1;
  uint8_t h[6];
  int n = -1;
  bool old = false;
  if (f.read(h, 4) == 4) {
    if (h[0] == 'M' && h[1] == 'B' && h[2] == (uint8_t)(sizeof(UiMsg) & 0xFF) && f.read(h + 4, 2) == 2) n = h[4] | (h[5] << 8);
    else if (h[2] == (uint8_t)(sizeof(UiMsgV1) & 0xFF)) {
      old = true;
      if (h[0] == 'M' && h[1] == '9') n = h[3];
      else if (h[0] == 'M' && h[1] == 'A' && f.read(h + 4, 2) == 2) n = h[4] | (h[5] << 8);
    }
  }
  if (n > UI_MSG_MAX) n = -1;
  if (n > 0 && !old && f.read((uint8_t*)_msgs, sizeof(UiMsg) * n) != sizeof(UiMsg) * n) n = -1;
  if (n > 0 && old) {
    UiMsgV1 o;
    size_t keep = offsetof(UiMsg, key) < sizeof(o) ? offsetof(UiMsg, key) : sizeof(o);
    for (int i = 0; i < n; i++) {
      if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) { n = -1; break; }
      memset(&_msgs[i], 0, sizeof(UiMsg));
      memcpy(&_msgs[i], &o, keep);
    }
    if (n > 0) _msgs_old_format = true;                // po wczytaniu uzupełnimy klucze kontaktów i zapiszemy w nowym formacie
  }
  f.close();
  return n;
}

// podmiana pliku wiadomości tak, żeby przerwa w dowolnym momencie zostawiła któryś pełny plik
static void swapMsgFiles() {
  SPIFFS.remove(FILE_MSGS_BAK);
  if (SPIFFS.exists(FILE_MSGS)) SPIFFS.rename(FILE_MSGS, FILE_MSGS_BAK);
  if (SPIFFS.rename(FILE_MSGS_TMP, FILE_MSGS)) SPIFFS.remove(FILE_MSGS_BAK);
}

void UITask::saveCfg() {
  File f = SPIFFS.open(FILE_CFG, "w");
  if (!f) return;
  f.write((const uint8_t*)&_cfg, sizeof(_cfg));
  f.close();
}

void UITask::savePw() {
  uint8_t plain[PW_PLAIN], sealed[PW_SEALED + 16];
  memset(plain, 0, sizeof(plain)); memcpy(plain, _pw, sizeof(_pw));
  int n = the_mesh.uiSeal(sealed, plain, sizeof(plain));
  memset(plain, 0, sizeof(plain));
  if (n != PW_SEALED) return;
  File f = SPIFFS.open(FILE_PW, "w");
  if (!f) return;
  uint8_t h[3] = { 'P', '2', (uint8_t)_pw_n };
  f.write(h, 3);
  f.write(sealed, PW_SEALED);
  f.close();
}

// Zapis wiadomości w kawałkach, po kilka na przejście pętli. Zapis całego pliku naraz zatrzymywał urządzenie
// na kilka sekund i aplikacja w telefonie dostawała wtedy „timeout”.
static File sv_file;
static bool sv_active = false;
static int sv_idx = 0, sv_n = 0;

bool UITask::saveStep() {
  if (!sv_active) {
    if (_msgs == NULL || _snap == NULL) { _msgs_dirty = false; return false; }
    sv_file = SPIFFS.open(FILE_MSGS_TMP, "w");
    if (!sv_file) { _save_at = millis() + SAVE_DELAY_MS; return false; }
    // zapisujemy kopię zrobioną w tej chwili: nowe wiadomości przychodzące w trakcie nie przerywają zapisu,
    // tylko oznaczają, że potrzebny będzie następny
    sv_n = _msg_n; sv_idx = 0;
    memcpy(_snap, _msgs, sizeof(UiMsg) * sv_n);
    uint8_t h[6] = { 'M', 'B', (uint8_t)(sizeof(UiMsg) & 0xFF), 0, (uint8_t)(sv_n & 0xFF), (uint8_t)(sv_n >> 8) };
    if (sv_file.write(h, 6) != 6) { sv_file.close(); SPIFFS.remove(FILE_MSGS_TMP); _save_at = millis() + SAVE_DELAY_MS; return false; }
    sv_active = true;
    _msgs_dirty = false;
    return true;
  }
  int n = sv_n - sv_idx; if (n > SAVE_CHUNK) n = SAVE_CHUNK;
  bool ok = n <= 0 || sv_file.write((const uint8_t*)&_snap[sv_idx], sizeof(UiMsg) * n) == sizeof(UiMsg) * n;
  sv_idx += n;
  if (!ok) { sv_file.close(); SPIFFS.remove(FILE_MSGS_TMP); sv_active = false; return false; }     // brak miejsca: zostaje poprzedni plik
  if (sv_idx >= sv_n) {
    sv_file.close(); sv_active = false;
    swapMsgFiles();
    _msg_saved = sv_n; _msg_saved_at = the_mesh.getRTCClock()->getCurrentTime();
    if (_msgs_dirty) _save_at = millis() + SAVE_DELAY_MS;
    return false;
  }
  return true;
}

void UITask::saveMsgs() {
  if (sv_active) { sv_file.close(); SPIFFS.remove(FILE_MSGS_TMP); sv_active = false; }
  _msgs_dirty = false;
  if (_msgs == NULL) return;
  File f = SPIFFS.open(FILE_MSGS_TMP, "w");
  if (!f) return;
  uint8_t h[6] = { 'M', 'B', (uint8_t)(sizeof(UiMsg) & 0xFF), 0, (uint8_t)(_msg_n & 0xFF), (uint8_t)(_msg_n >> 8) };
  bool ok = f.write(h, 6) == 6 && f.write((const uint8_t*)_msgs, sizeof(UiMsg) * _msg_n) == sizeof(UiMsg) * _msg_n;
  f.close();
  if (!ok) { SPIFFS.remove(FILE_MSGS_TMP); return; }  // brak miejsca: zostaje poprzedni plik
  swapMsgFiles();
  _msg_saved = _msg_n; _msg_saved_at = the_mesh.getRTCClock()->getCurrentTime();
}

const char* UITask::pwGet(const uint8_t* key) {
  for (int i = 0; i < _pw_n; i++) if (memcmp(_pw[i].key, key, 6) == 0) return _pw[i].pw;
  return NULL;
}

void UITask::pwSet(const uint8_t* key, const char* pw) {
  int k = -1;
  for (int i = 0; i < _pw_n; i++) if (memcmp(_pw[i].key, key, 6) == 0) k = i;
  if (pw[0] == 0) {                                  // usuń
    if (k < 0) return;
    memmove(&_pw[k], &_pw[k + 1], sizeof(Pw) * (_pw_n - k - 1)); _pw_n--;
  } else {
    if (k < 0) {
      if (_pw_n == UI_PW_MAX) { memmove(&_pw[0], &_pw[1], sizeof(Pw) * (UI_PW_MAX - 1)); _pw_n--; }
      k = _pw_n++;
    }
    memcpy(_pw[k].key, key, 6);
    scopy(_pw[k].pw, sizeof(_pw[k].pw), pw);
  }
  savePw();
}

// ---------------- wiadomości ----------------

UiMsg* UITask::addMsg(const char* thread, const char* who, const char* text, bool chan, bool out, uint8_t hops) {
  if (_msgs == NULL) return NULL;
  if (_msg_n == UI_MSG_MAX) {                       // najstarsza wypada
    memmove(&_msgs[0], &_msgs[1], sizeof(UiMsg) * (UI_MSG_MAX - 1));
    _msg_n--;
    if (_msg_idx >= 0) _msg_idx--;
    if (_th_new >= 0) _th_new--;
  }
  UiMsg& m = _msgs[_msg_n++];
  memset(&m, 0, sizeof(m));
  scopy(m.thread, sizeof(m.thread), thread);
  scopy(m.who, sizeof(m.who), who);
  scopy(m.text, sizeof(m.text), text);
  m.ts = the_mesh.getRTCClock()->getCurrentTime();
  m.chan = chan; m.out = out; m.hops = hops;
  // przeczytana od razu, jeśli właśnie patrzymy na tę rozmowę
  m.unread = !out && !(_awake && scr() == S_THREAD && _input_mode == IN_MSG && _cur_chan == chan && strcmp(_cur, thread) == 0);
  _dirty = true; _msgs_dirty = true;
  _save_at = millis() + SAVE_DELAY_MS;
  return &m;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  // Kanał czy kontakt mówi MyMesh. Nie zgadujemy po nazwie: kontakt nazwany tak jak kanał
  // mógłby wtedy podrzucać wiadomości do rozmowy kanału (także prywatnego).
  bool chan = the_mesh.ui_rx_chan;
  char who[24]; who[0] = 0;
  const char* body = text;
  if (chan) {                                       // na kanale tekst ma postać „Nadawca: treść”
    const char* sep = strstr(text, ": ");
    if (sep && sep - text < (int)sizeof(who)) {
      memcpy(who, text, sep - text); who[sep - text] = 0;
      body = sep + 2;
    }
  }
  char tname[32];
  if (chan) scopy(tname, sizeof(tname), from_name);
  else dmThreadName(the_mesh.ui_rx_key, from_name, tname, sizeof(tname));   // rozmowa należy do klucza kontaktu, nie do nazwy
  from_name = tname;
  UiMsg* m = addMsg(from_name, who, body, chan, false, the_mesh.ui_rx_hops);
  if (m) {
    if (!chan) memcpy(m->key, the_mesh.ui_rx_key, 6);
    m->direct = the_mesh.ui_rx_direct;
    m->hash_size = the_mesh.ui_rx_hash_size;
    m->path_bytes = the_mesh.ui_rx_path_bytes > sizeof(m->path) ? sizeof(m->path) : the_mesh.ui_rx_path_bytes;
    memcpy(m->path, the_mesh.ui_rx_path, m->path_bytes);
    m->snr_x4 = the_mesh.ui_rx_snr_x4; m->rssi = the_mesh.ui_rx_rssi;
  }
  bool muted = isMuted(from_name);                   // wyciszona rozmowa: bez dźwięku i bez budzenia ekranu
  if (!muted && !_prefs->buzzer_quiet && !_cfg.no_msg_snd && !_buzzer.isPlaying()) _buzzer.play(chan ? "ch:d=16,o=6,b=200:e,g" : "dm:d=16,o=6,b=200:e,g,c7");
  if (_awake) _dirty = true;
  else if (!muted && !_cfg.no_wake) wake();
}

void UITask::msgRead(int msgcount) { _msgcount = msgcount; }

// liczniki powtórzeń (kanał) i potwierdzenia dostarczenia (rozmowa prywatna) dla naszych wiadomości
void UITask::pollSent() {
  for (int i = 0; i < _msg_n; i++) {
    UiMsg& m = _msgs[i];
    if (!m.out || !m.sent_id) continue;
    MyMesh::UiSent* u = the_mesh.uiSent(m.sent_id);
    if (!u) continue;
    if (u->repeats != m.repeats || u->delivered != (m.delivered != 0) || u->rep_n != m.rep_n) {
      if (!_prefs->buzzer_quiet && !_cfg.no_msg_snd && !_buzzer.isPlaying() && ((u->repeats > 0 && m.repeats == 0) || (u->delivered && !m.delivered))) _buzzer.play("ack:d=32,o=8,b=120:c");
      m.repeats = u->repeats; m.delivered = u->delivered;
      m.rep_n = u->rep_n; m.rep_hs = u->rep_hs;
      memcpy(m.rep_id, u->rep_id, sizeof(m.rep_id)); memcpy(m.rep_snr, u->rep_snr, sizeof(m.rep_snr));
      _dirty = true; _msgs_dirty = true; _save_at = millis() + SAVE_DELAY_MS;
    }
  }
}

void UITask::buildThreads() {
  _thread_n = 0;
  ChannelDetails cd;
  for (int i = 0; i < MAX_GROUP_CHANNELS && _thread_n < UI_THREAD_MAX; i++) {     // kanały są zawsze na liście
    if (!the_mesh.getChannel(i, cd)) break;
    if (!cd.name[0]) continue;
    Thread& t = _threads[_thread_n++];
    scopy(t.name, sizeof(t.name), cd.name); t.chan = 1; t.unread = 0; t.last = 0; t.last_msg = -1;
  }
  for (int i = 0; i < _msg_n; i++) {
    UiMsg& m = _msgs[i];
    int k = -1;
    for (int j = 0; j < _thread_n; j++) if (_threads[j].chan == m.chan && strcmp(_threads[j].name, m.thread) == 0) { k = j; break; }
    if (k < 0) {
      if (_thread_n == UI_THREAD_MAX) continue;
      k = _thread_n++;
      scopy(_threads[k].name, sizeof(_threads[k].name), m.thread); _threads[k].chan = m.chan; _threads[k].unread = 0;
    }
    _threads[k].last = m.ts; _threads[k].last_msg = i;
    if (m.unread && _threads[k].unread < 99) _threads[k].unread++;
  }
  // nieprzeczytane na górze, potem najnowsze
  for (int i = 1; i < _thread_n; i++) {
    Thread t = _threads[i];
    int j = i - 1;
    while (j >= 0 && ((_threads[j].unread > 0) < (t.unread > 0) || ((_threads[j].unread > 0) == (t.unread > 0) && _threads[j].last < t.last))) { _threads[j + 1] = _threads[j]; j--; }
    _threads[j + 1] = t;
  }
}

int UITask::unreadTotal() {
  int n = 0;
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].unread) n++;
  return n;
}

void UITask::markRead(const char* name, bool chan) {
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].unread && _msgs[i].chan == chan && strcmp(_msgs[i].thread, name) == 0) { _msgs[i].unread = 0; _msgs_dirty = true; _save_at = millis() + SAVE_DELAY_MS; }
}

static bool keySet(const uint8_t* k) { for (int i = 0; i < 6; i++) if (k[i]) return true; return false; }

bool UITask::ctByPrefix(const uint8_t* key6, ContactInfo& c) {
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total; i++) {
    if (the_mesh.getContactByIdx(i, c) && c.type != ADV_TYPE_NONE && memcmp(c.id.pub_key, key6, 6) == 0) return true;
  }
  return false;
}

// Nazwa rozmowy prywatnej dla kontaktu o danym kluczu. Rozmowę wyznacza klucz: drugi kontakt o tej samej nazwie
// (pomyłka albo podszywanie się) dostaje osobną rozmowę z dopiskiem identyfikatora, np. „Marek ·A3F2”.
void UITask::dmThreadName(const uint8_t* key, const char* name, char* out, size_t n) {
  char cur[32]; cur[0] = 0;
  bool have = false, taken = false;
  if (keySet(key)) {
    for (int i = _msg_n - 1; i >= 0; i--) if (!_msgs[i].chan && memcmp(_msgs[i].key, key, 6) == 0) { scopy(cur, sizeof(cur), _msgs[i].thread); have = true; break; }
  }
  // czy tę nazwę zajmuje rozmowa z kimś o innym kluczu
  for (int i = 0; i < _msg_n && !taken; i++) {
    const UiMsg& m = _msgs[i];
    if (!m.chan && strcmp(m.thread, name) == 0 && keySet(m.key) && keySet(key) && memcmp(m.key, key, 6) != 0) taken = true;
  }
  char want[32];
  if (!taken) scopy(want, sizeof(want), name);
  else snprintf(want, sizeof(want), "%.22s ·%02X%02X", name, key[0], key[1]);
  if (have && strcmp(cur, want) != 0) {              // kontakt zmienił nazwę: jego rozmowa idzie za nim
    for (int i = 0; i < _msg_n; i++) if (!_msgs[i].chan && memcmp(_msgs[i].key, key, 6) == 0) scopy(_msgs[i].thread, sizeof(_msgs[i].thread), want);
    if (!_cur_chan && strcmp(_cur, cur) == 0) scopy(_cur, sizeof(_cur), want);
    _msgs_dirty = true; _save_at = millis() + SAVE_DELAY_MS;
  }
  if (keySet(key)) {                                 // starsze wiadomości tej rozmowy bez klucza dostają go teraz
    for (int i = 0; i < _msg_n; i++) if (!_msgs[i].chan && !keySet(_msgs[i].key) && strcmp(_msgs[i].thread, want) == 0) memcpy(_msgs[i].key, key, 6);
  }
  scopy(out, n, want);
}

void UITask::openThread(const char* name, bool chan, const uint8_t* key) {
  scopy(_cur, sizeof(_cur), name);
  _cur_chan = chan;
  memset(_cur_key, 0, sizeof(_cur_key));
  if (!chan) {                                     // klucz rozmówcy: podany, z wiadomości tej rozmowy, a na końcu z kontaktu o tej nazwie
    if (key && keySet(key)) memcpy(_cur_key, key, 6);
    else {
      for (int i = _msg_n - 1; i >= 0; i--) if (!_msgs[i].chan && strcmp(_msgs[i].thread, name) == 0 && keySet(_msgs[i].key)) { memcpy(_cur_key, _msgs[i].key, 6); break; }
      if (!keySet(_cur_key)) {
        ContactInfo c; int total = the_mesh.getTotalContactSlots();
        for (int i = MAX_ANON_CONTACTS; i < total; i++) if (the_mesh.getContactByIdx(i, c) && c.type != ADV_TYPE_NONE && strcmp(c.name, name) == 0) { memcpy(_cur_key, c.id.pub_key, 6); break; }
      }
    }
  }
  scrollReset();
  _th_new = -1;
  _scroll_unread = -1;                             // pierwsza nieprzeczytana: po wejściu rozmowa ustawi się na niej
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].unread && _msgs[i].chan == chan && strcmp(_msgs[i].thread, name) == 0) {
    _scroll_unread = i;
    for (int k = 0; k < i; k++) if (_msgs[k].chan == chan && strcmp(_msgs[k].thread, name) == 0) { _th_new = i; break; }   // kreska tylko, gdy wyżej są starsze wiadomości
    break;
  }
  _input[0] = 0; _input_chars = 0; _polish = false;
  _input_mode = IN_MSG;
  markRead(name, chan);
  push(S_THREAD);
}

void UITask::openInput(uint8_t mode, const char* initial) {
  _input_mode = mode;
  scopy(_input, sizeof(_input), initial);
  _input_chars = 0;
  for (const char* p = _input; *p; p++) if (((uint8_t)*p & 0xC0) != 0x80) _input_chars++;
  _polish = false;
  push(S_THREAD);
}

bool UITask::sendText(const char* text) {
  uint16_t id;
  if (_cur_chan) { int idx = the_mesh.uiChannelIndex(_cur); id = idx >= 0 ? the_mesh.uiSendChannelText(idx, text) : 0; }
  else if (keySet(_cur_key)) id = the_mesh.uiSendDirectKey(_cur_key, text);      // do kontaktu o tym kluczu, nie „o tej nazwie”
  else { popup("Nie znam tego kontaktu", "dodaj go do kontaktów i napisz z jego menu", true); return false; }
  if (!id) { popup("Nie udało się wysłać", "spróbuj jeszcze raz", true); return false; }
  UiMsg* m = addMsg(_cur, "", text, _cur_chan, true, 0);
  if (m) { m->sent_id = id; if (!_cur_chan) memcpy(m->key, _cur_key, 6); }
  scrollReset();
  return true;
}

// ---------------- listy kontaktów ----------------

bool UITask::ctByKey(const uint8_t* key, ContactInfo& c) {
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total; i++) {                 // pierwsze miejsca tabeli są techniczne
    if (the_mesh.getContactByIdx(i, c) && c.type != ADV_TYPE_NONE && memcmp(c.id.pub_key, key, 32) == 0) return true;
  }
  return false;
}

void UITask::setCur(const ContactInfo& c) {
  memcpy(_ck, c.id.pub_key, 32);
  scopy(_cname, sizeof(_cname), c.name);
  _ctype = c.type; _cfav = (c.flags & 1) != 0;
  _chas_pos = c.gps_lat != 0 || c.gps_lon != 0;
  _clat = c.gps_lat / 1e6; _clon = c.gps_lon / 1e6;
}

// nazwa repeatera po początku klucza (identyfikator z trasy); gdy nieznany — sam identyfikator
void UITask::rptName(const uint8_t* id, int n, char* dest, size_t size) {
  char hex[20]; hex[0] = 0;
  for (int i = 0; i < n && i < 8; i++) { char x[4]; snprintf(x, sizeof(x), "%02x", id[i]); strcat(hex, x); }
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total; i++) {
    if (the_mesh.getContactByIdx(i, c) && c.type == ADV_TYPE_REPEATER && memcmp(c.id.pub_key, id, n) == 0) { snprintf(dest, size, "%s  %s", hex, c.name); return; }
  }
  scopy(dest, size, hex);
}

void UITask::sortList(bool by_dist) {
  static uint32_t keys[UI_LIST_MAX];
  ContactInfo c;
  double lat = 0, lon = 0; int sats;
  bool fix = by_dist && gpsFix(&lat, &lon, &sats);
  for (int i = 0; i < _list_n; i++) {
    if (!the_mesh.getContactByIdx(_list[i], c)) { keys[i] = 0; continue; }
    uint32_t k;
    if (fix) {                                        // bliższe wyżej; bez pozycji na końcu
      if (c.gps_lat || c.gps_lon) { double d = distKm(lat, lon, c.gps_lat / 1e6, c.gps_lon / 1e6); k = 0x7FFFFFFF - (uint32_t)(d * 100 > 2e9 ? 2e9 : d * 100); }
      else k = 0;
    } else k = c.lastmod > 0x7FFFFFFF ? 0x7FFFFFFF : c.lastmod;
    keys[i] = ((c.flags & 1) ? 0x80000000UL : 0) | k;   // ulubione zawsze na górze
  }
  for (int i = 1; i < _list_n; i++) {
    uint32_t k = keys[i]; uint16_t v = _list[i];
    int j = i - 1;
    while (j >= 0 && keys[j] < k) { keys[j + 1] = keys[j]; _list[j + 1] = _list[j]; j--; }
    keys[j + 1] = k; _list[j + 1] = v;
  }
}

static bool nameHas(const char* name, const char* frag) {     // bez rozróżniania wielkości liter (ASCII)
  if (!frag[0]) return true;
  size_t n = strlen(frag);
  for (const char* p = name; *p; p++) if (strncasecmp(p, frag, n) == 0) return true;
  return false;
}

void UITask::buildContacts() {
  _list_n = 0;
  if (_list == NULL) return;
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total && _list_n < UI_LIST_MAX; i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE) continue;
    bool ok = c.type != ADV_TYPE_SENSOR;              // czujników nie pokazujemy
    switch (_cfg.ct_filter) {
      case 1: ok = c.type == ADV_TYPE_CHAT; break;
      case 2: ok = c.type == ADV_TYPE_REPEATER; break;
      case 3: ok = c.type == ADV_TYPE_ROOM; break;
      case 4: ok = ok && (c.flags & 1) != 0; break;
    }
    if (ok && nameHas(c.name, _search)) _list[_list_n++] = (uint16_t)i;
  }
  sortList(false);
}

void UITask::buildRpts() {
  _list_n = 0;
  if (_list == NULL) return;
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total && _list_n < UI_LIST_MAX; i++) {
    if (the_mesh.getContactByIdx(i, c) && c.type == ADV_TYPE_REPEATER) _list[_list_n++] = (uint16_t)i;
  }
  sortList(_cfg.rpt_sort == 1);
}

// ---------------- nawigacja ----------------

void UITask::prepare(uint8_t s) {
  switch (s) {
    case S_MSGS: buildThreads(); break;
    case S_CONTACTS: buildContacts(); break;
    case S_RPTS: buildRpts(); break;
    case S_DISC:
      if (the_mesh.uiDiscoverStart()) _disc_until = millis() + 8000UL;
      else { _disc_until = 0; popup("Nie udało się wysłać pingu", "", true); }
      break;
  }
  _dirty = true;
}

void UITask::tab(uint8_t s) {
  if (_depth >= 1 && _nav[0].scr < 5) _tabsel[_nav[0].scr] = _nav[0].sel;
  if (s != S_CONTACTS) { _search[0] = 0; _sel_mode = false; }     // zaznaczanie kończy się po wyjściu z Kontaktów
  _depth = 1;
  _bar = false;
  _nav[0].scr = s; _nav[0].sel = _tabsel[s]; _nav[0].top = 0;
  prepare(s);
  if (s == S_CONTACTS && _nav[0].sel == 0 && listCount(s) > 1) _nav[0].sel = 1;     // kursor na pierwszym kontakcie, filtry są wiersz wyżej
}

void UITask::push(uint8_t s) {
  if (_depth >= (int)(sizeof(_nav) / sizeof(_nav[0]))) return;
  _nav[_depth].scr = s; _nav[_depth].sel = 0; _nav[_depth].top = 0;
  _depth++;
  _bar = false;
  prepare(s);
}

void UITask::pop() {
  if (_depth > 1) {
    _depth--;
    uint8_t s = scr();
    if (s == S_DISC) _dirty = true; else prepare(s);       // powrót do wyników discovery nie wysyła nowego pingu
  } else if (scr() != S_START) tab(S_START);
}

// ---------------- Kontakty: zaznaczanie wielu ----------------
bool UITask::selHas(const uint8_t* key) const {
  for (int i = 0; i < _sel_n; i++) if (memcmp(_selk[i], key, 6) == 0) return true;
  return false;
}
void UITask::selAdd(const uint8_t* key) {
  if (_selk == NULL || _sel_n >= UI_LIST_MAX || selHas(key)) return;
  memcpy(_selk[_sel_n++], key, 6);
}
void UITask::selToggle(const uint8_t* key) {
  if (_selk == NULL) return;
  for (int i = 0; i < _sel_n; i++) if (memcmp(_selk[i], key, 6) == 0) { memmove(_selk[i], _selk[i + 1], (_sel_n - i - 1) * 6); _sel_n--; return; }
  selAdd(key);
}

void UITask::back() {
  if (scr() == S_CONTACTS && _sel_mode) { _sel_mode = false; _sel_n = 0; return; }     // Wstecz kończy zaznaczanie bez kasowania
  if (scr() == S_THREAD && _input_mode != IN_MSG) _input_mode = IN_MSG;   // anulowane pole do wpisania
  if (scr() == S_ADMIN) the_mesh.uiAdminLogout();
  if (scr() == S_MAP_NET && m9dlActive()) { m9dlStop(); _map.scan(); }
  if (scr() == S_MAP && _route_n) _route_n = 0;     // koniec oglądania trasy
  if (scr() == S_OTA && m9otaState() == 4) { popup("Trwa instalacja", "poczekaj do końca"); return; }
  if (scr() == S_OTA && m9otaActive()) m9dlStop();
  if ((scr() == S_MAP_NET || scr() == S_OTA) && _bt_killed) { pop(); ask(C_BT_RESTART, "Zrestartować, żeby wrócił Bluetooth?", ""); return; }
  pop();
}

void UITask::popup(const char* a, const char* b, bool danger, int ms) {
  scopy(_pop1, sizeof(_pop1), a);
  scopy(_pop2, sizeof(_pop2), b);
  _pop_danger = danger;
  _popup_until = millis() + ms;
  _dirty = true;
}

void UITask::ask(uint8_t what, const char* a, const char* name) {
  _confirm = what;
  scopy(_confirm_name, sizeof(_confirm_name), name);
  popup(a, "Enter = tak · inny klawisz = anuluj", true, 15000);
}

int UITask::brightPercent() { return (_cfg.bright >= 1 && _cfg.bright <= 5) ? BRIGHT_STEPS[_cfg.bright - 1] : 100; }

void UITask::wake() {
  uint32_t ms = SLEEP_STEPS[_cfg.sleep_idx];
  _sleep_at = ms ? millis() + ms : 0;
  _last_key = millis();                            // od teraz liczy się czas do przyciemnienia
  if (!_awake) {
    _awake = true;
    m9cpuSave(false);                                // pełna moc procesora przy włączonym ekranie
    if (_display) _display->turnOn();
    m9lcdBacklight(brightPercent());
  } else if (_dimmed) m9lcdBacklight(brightPercent());
  _dimmed = false;
  _kb.setBacklight(_cfg.kb_light != 0 && !_locked);
  _dirty = true;
}

// ---------------- szczegóły (ekran z wierszami nazwa–wartość) ----------------

void UITask::kvOpen(const char* title, const char* right) {
  _kv_n = 0;
  _kv_more = false; _kv_pkt = 0;
  scopy(_kv_title, sizeof(_kv_title), title);
  scopy(_kv_right, sizeof(_kv_right), right);
}

void UITask::kvAdd(const char* k, const char* v) {
  if (_kv == NULL || _kv_n >= UI_KVS_MAX) return;
  scopy(_kv[_kv_n].k, sizeof(_kv[0].k), k);
  scopy(_kv[_kv_n].v, sizeof(_kv[0].v), v);
  _kv_n++;
}

// dłuższy tekst: dzielony na wiersze (po spacjach, co ok. 44 znaki)
void UITask::kvText(const char* text) {
  char line[56];
  int n = 0, chars = 0, lastSpace = -1;
  for (const char* p = text; ; p++) {
    bool end = *p == 0, nl = *p == '\n';
    if (!end && !nl) {
      if (n < (int)sizeof(line) - 1) { if (*p == ' ') lastSpace = n; line[n++] = *p; }
      if (((uint8_t)*p & 0xC0) != 0x80) chars++;
    }
    if (end || nl || chars >= 44) {
      int cut = n;
      if (!end && !nl && lastSpace > 10) cut = lastSpace;
      char rest[56]; int rn = n - cut;
      memcpy(rest, line + cut, rn);
      line[cut] = 0;
      if (cut > 0 || nl) kvAdd(line, "");
      n = 0; chars = 0; lastSpace = -1;
      for (int i = 0; i < rn; i++) { if (rest[i] == ' ' && n == 0) continue; line[n++] = rest[i]; if (((uint8_t)rest[i] & 0xC0) != 0x80) chars++; }
      if (end) { if (n > 0) { line[n] = 0; kvAdd(line, ""); } break; }
    }
  }
}

void UITask::detailContact() {
  ContactInfo c;
  char t[48];
  kvOpen(_cname, typeName(_ctype));
  if (!ctByKey(_ck, c)) { kvAdd("Stan", "nie ma go już w kontaktach"); push(S_KV); return; }
  kvAdd("Ulubiony", yesNo(c.flags & 1));
  fmtAgo(t, sizeof(t), c.lastmod); kvAdd("Ostatnio słyszany", t);
  if (c.gps_lat || c.gps_lon) {
    snprintf(t, sizeof(t), "%.5f, %.5f", c.gps_lat / 1e6, c.gps_lon / 1e6); kvAdd("Pozycja", t);
    double lat, lon; int sats;
    if (gpsFix(&lat, &lon, &sats)) { snprintf(t, sizeof(t), "%.1f km", distKm(lat, lon, c.gps_lat / 1e6, c.gps_lon / 1e6)); comma(t); kvAdd("Odległość", t); }
  } else kvAdd("Pozycja", "nie podaje");
  int hops = c.out_path_len & 63;
  if (c.out_path_len == 0xFF) kvAdd("Droga", "nieznana (przez całą sieć)");
  else if (hops == 0) kvAdd("Droga", "bezpośrednio");
  else {
    snprintf(t, sizeof(t), "skoki: %d", hops); kvAdd("Droga", t);
    int hs = (c.out_path_len >> 6) + 1;
    for (int h = 0; h < hops && h < 8; h++) { char nm[48], k[56]; rptName(&c.out_path[h * hs], hs, nm, sizeof(nm)); snprintf(k, sizeof(k), "  %d. %s", h + 1, nm); kvAdd(k, ""); }
  }
  snprintf(t, sizeof(t), "%02x%02x%02x%02x %02x%02x%02x%02x", c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2], c.id.pub_key[3], c.id.pub_key[4], c.id.pub_key[5], c.id.pub_key[6], c.id.pub_key[7]);
  kvAdd("Klucz (początek)", t);
  push(S_KV);
}

void UITask::detailMsg(int idx) {
  if (idx < 0 || idx >= _msg_n) return;
  UiMsg& m = _msgs[idx];
  char t[48], k[56];
  kvOpen(m.out ? "Wiadomość wysłana" : "Wiadomość odebrana", m.chan ? "kanał" : "prywatna");
  struct tm lt; localTime(m.ts, &lt);
  snprintf(t, sizeof(t), "%d.%02d.%04d %d:%02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900, lt.tm_hour, lt.tm_min, lt.tm_sec);
  kvAdd(m.out ? "Wysłana" : "Odebrana", t);
  snprintf(t, sizeof(t), "%s%s", m.chan ? "# " : "", m.thread); kvAdd(m.chan ? "Kanał" : "Rozmowa", t);
  if (m.out) {
    if (m.chan) {
      snprintf(t, sizeof(t), "%d", m.repeats); kvAdd("Usłyszane powtórzenia", t);
      if (m.rep_n == 0) kvAdd(m.repeats ? "Repeatery: brak danych" : "Nikt jeszcze nie powtórzył", "");
      for (int i = 0; i < m.rep_n && i < 6; i++) {
        char nm[48], sn[16];
        rptName(m.rep_id[i], m.rep_hs, nm, sizeof(nm)); fmtSnr(sn, sizeof(sn), m.rep_snr[i]);
        snprintf(k, sizeof(k), "  %s", nm); kvAdd(k, sn);
      }
    } else kvAdd("Dostarczona", m.delivered ? "tak, jest potwierdzenie" : "brak potwierdzenia");
  } else {
    if (m.who[0]) kvAdd("Od", m.who);
    fmtSnr(t, sizeof(t), m.snr_x4); kvAdd("SNR odbioru", t);
    snprintf(t, sizeof(t), "%d dBm", m.rssi); kvAdd("RSSI odbioru", t);
    snprintf(t, sizeof(t), "%d (identyfikator: %d znaki)", m.hash_size ? m.hash_size : 1, (m.hash_size ? m.hash_size : 1) * 2); kvAdd("Bajty trasy nadawcy", t);
    if (m.direct) kvAdd("Trasa", "trasa stała (bez listy hopów)");
    else if (m.hops == 0) kvAdd("Trasa", "bezpośrednio");
    else {
      hopsTxt(t, sizeof(t), m.hops, false); kvAdd("Trasa", t);
      int hs = m.hash_size ? m.hash_size : 1, shown = m.path_bytes / hs;
      for (int h = 0; h < shown; h++) { char nm[48]; rptName(&m.path[h * hs], hs, nm, sizeof(nm)); snprintf(k, sizeof(k), "  %d. %s", h + 1, nm); kvAdd(k, ""); }
      if (m.hops > shown) { snprintf(k, sizeof(k), "  … i jeszcze %d", m.hops - shown); kvAdd(k, ""); }
    }
  }
  kvAdd("Treść:", "");
  kvText(m.text);
  push(S_KV);
}

void UITask::detailInfo() {
  char t[48];
  kvOpen("Informacje", "MC SF6 · ThinkNode M9");
  kvAdd("Nazwa", _prefs->node_name);
  kvAdd("Wersja", M9_FW_VERSION);
  snprintf(t, sizeof(t), "%.3f MHz · BW %.4g", _prefs->freq, _prefs->bw); kvAdd("Radio", t);
  snprintf(t, sizeof(t), "SF%d · CR 4/%d · %d dBm", _prefs->sf, _prefs->cr, _prefs->tx_power_dbm); kvAdd("Parametry", t);
  uint16_t mv = getBattMilliVolts();
  snprintf(t, sizeof(t), "%d mV · %d%%", mv, battPercent(mv)); kvAdd("Bateria", t);
  snprintf(t, sizeof(t), "%d z %d", the_mesh.getNumContacts(), MAX_CONTACTS); kvAdd("Kontakty", t);
  snprintf(t, sizeof(t), "%d z %d", _msg_n, UI_MSG_MAX); kvAdd("Zapisane wiadomości", t);
  snprintf(t, sizeof(t), "0x%02X", _kb.addr()); kvAdd("Klawiatura", _kb.isFound() ? t : "nie znaleziona");
  snprintf(t, sizeof(t), "%lu kB · PSRAM %lu kB", (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024)); kvAdd("Wolna pamięć RAM", t);
  snprintf(t, sizeof(t), "%lu z %lu kB", (unsigned long)(SPIFFS.usedBytes() / 1024), (unsigned long)(SPIFFS.totalBytes() / 1024)); kvAdd("Pamięć na dane", t);
  {
    static const char* const SRC[3] = { "plik główny", "kopia", "plik tymczasowy" };
    snprintf(t, sizeof(t), "%d (%s)", _msg_loaded, _msg_loaded ? SRC[_msg_src % 3] : "brak pliku"); kvAdd("Wiadomości po starcie", t);
    if (_msg_saved_at) { char c[8]; fmtClock(c, sizeof(c), _msg_saved_at, false); snprintf(t, sizeof(t), "%d, o %s", _msg_saved, c); }
    else strcpy(t, _msgs_dirty ? "jeszcze nie (czeka)" : "nie było zmian");
    kvAdd("Ostatni zapis wiadomości", t);
  }
  unsigned long up = millis() / 60000UL;
  snprintf(t, sizeof(t), "%lu godz. %lu min", up / 60, up % 60); kvAdd("Czas pracy", t);
  const uint8_t* pk = the_mesh.self_id.pub_key;
  snprintf(t, sizeof(t), "%02x%02x%02x%02x %02x%02x%02x%02x", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], pk[6], pk[7]); kvAdd("Mój klucz (początek)", t);
  push(S_KV);
}

// ---------------- menu kontaktu i wiadomości ----------------

void UITask::openMenuContact() {
  _menu_n = 0; _menu_msg = false; _menu_thread = false;
  scopy(_menu_title, sizeof(_menu_title), _cname);
  if (_ctype == ADV_TYPE_CHAT || _ctype == ADV_TYPE_ROOM) _menu_act[_menu_n++] = A_WRITE;
  if (_ctype == ADV_TYPE_REPEATER || _ctype == ADV_TYPE_ROOM) _menu_act[_menu_n++] = A_MANAGE;
  _menu_act[_menu_n++] = A_FAV;
  _menu_act[_menu_n++] = A_DETAIL;
  if (_chas_pos) _menu_act[_menu_n++] = A_SHOWMAP;
  _menu_act[_menu_n++] = A_DELETE;
  if (scr() == S_CONTACTS) _menu_act[_menu_n++] = A_MULTI;      // tylko z listy Kontakty
  push(S_MENU);
}

void UITask::openMenuThread(int idx) {
  _menu_n = 0; _menu_msg = false; _menu_thread = true;
  if (idx >= 0 && idx < _thread_n) {
    scopy(_tname, sizeof(_tname), _threads[idx].name); _tchan = _threads[idx].chan;
    snprintf(_menu_title, sizeof(_menu_title), "%s%s", _tchan ? "# " : "", _tname);
    _menu_act[_menu_n++] = A_OPEN;
    if (_tchan) _menu_act[_menu_n++] = A_CHINFO;
    _menu_act[_menu_n++] = A_MUTE;
    _menu_act[_menu_n++] = A_MARKREAD;
    _menu_act[_menu_n++] = _tchan ? A_DELCHAN : A_DELTHREAD;
  } else { _tname[0] = 0; _tchan = false; strcpy(_menu_title, "Wiadomości"); }
  _menu_act[_menu_n++] = A_ADDCHAN;
  push(S_MENU);
}

void UITask::openMenuMsg(int idx) {
  if (idx < 0 || idx >= _msg_n) return;
  _msg_idx = idx;
  _menu_n = 0; _menu_msg = true; _menu_thread = false;
  UiMsg& m = _msgs[idx];
  scopy(_menu_title, sizeof(_menu_title), m.out ? "Moja wiadomość" : (m.who[0] ? m.who : m.thread));
  // kolejność: Odpowiedz @, Odpowiedz ack, Szczegóły, Trasa na mapie, reszta
  if (!m.out && m.chan && m.who[0]) _menu_act[_menu_n++] = A_REPLY;
  if (!m.out) _menu_act[_menu_n++] = A_REPLY_ACK;
  _menu_act[_menu_n++] = A_DETAIL;
  if ((!m.out && !m.direct && m.path_bytes > 0) || (m.out && m.chan && m.rep_n > 0)) _menu_act[_menu_n++] = A_ROUTE;
  if (m.out) _menu_act[_menu_n++] = A_RESEND;
  _menu_act[_menu_n++] = A_COPY;
  _menu_act[_menu_n++] = A_DELMSG;
  push(S_MENU);
}

void UITask::doAct(uint8_t act) {
  if (act == A_SPEC) { pop(); push(S_SPEC); return; }
  if (act == A_LOG) { pop(); push(S_LOG); return; }
  if (act == A_MULTI) { pop(); _sel_mode = true; _sel_n = 0; selAdd(_ck); return; }
  if (act == A_SELNONE) { _sel_n = 0; pop(); return; }
  if (act == A_SELALL) {                              // wszystko z bieżącego filtra oprócz ulubionych
    ContactInfo q; int added = 0;
    for (int i = 0; i < _list_n; i++) if (the_mesh.getContactByIdx(_list[i], q) && !(q.flags & 1) && !selHas(q.id.pub_key)) { selAdd(q.id.pub_key); added++; }
    pop();
    char t[40]; snprintf(t, sizeof(t), "dodano: %d · ulubione pominięte", added); popup("Zaznaczono", t);
    return;
  }
  if (act == A_SELOLD) {
    pop();
    if (the_mesh.getRTCClock()->getCurrentTime() < 1735689600UL) { popup("Najpierw ustaw zegar", "bez daty nie wiem, co jest stare", true); return; }
    pickBegin(P_SELOLD, "Niewidziane przez urządzenie od"); pickAdd("7 dni"); pickAdd("30 dni"); pickAdd("90 dni"); pickOpen(-1);
    return;
  }
  if (act == A_SELDEL) { char t[32]; snprintf(t, sizeof(t), "kontaktów: %d", _sel_n); ask(C_DEL_MULTI, "Usunąć zaznaczone kontakty?", t); return; }
  if (_menu_thread) {
    switch (act) {
      case A_OPEN: pop(); openThread(_tname, _tchan); break;
      case A_ADDCHAN: pop(); openInput(IN_CHANNEL, "#"); break;
      case A_CHINFO: pop(); if (scr() != S_CHINFO) push(S_CHINFO); break;
      case A_MUTE: toggleMute(_tname); popup(isMuted(_tname) ? "Wyciszono" : "Dźwięk włączony", _tname); break;
      case A_MARKREAD: markRead(_tname, _tchan); pop(); break;
      case A_DELCHAN: ask(C_DEL_CHANNEL, "Usunąć kanał?", _tname); break;
      case A_DELTHREAD: ask(C_DEL_THREAD, "Usunąć rozmowę?", _tname); break;
    }
    return;
  }
  if (_menu_msg) {
    if (_msg_idx < 0 || _msg_idx >= _msg_n) { pop(); return; }
    UiMsg& m = _msgs[_msg_idx];
    switch (act) {
      case A_DETAIL: detailMsg(_msg_idx); break;
      case A_ROUTE: if (buildRoute(_msg_idx)) { fitRoute(); push(S_MAP); } break;
      case A_REPLY: pop(); snprintf(_input, sizeof(_input), "@[%s] ", m.who); _input_chars = 0; for (const char* p = _input; *p; p++) if (((uint8_t)*p & 0xC0) != 0x80) _input_chars++; break;
      case A_REPLY_ACK: {                            // odpowiedź z potwierdzeniem: nick, „ack” i liczba hopów, którymi wiadomość do nas przyszła
        char h[24]; hopsTxt(h, sizeof(h), m.hops, m.direct);
        bool chanWho = m.chan && m.who[0];
        char who[24]; scopy(who, sizeof(who), m.who);
        pop();
        if (chanWho) snprintf(_input, sizeof(_input), "@[%s] ack, %s ", who, h); else snprintf(_input, sizeof(_input), "ack, %s ", h);
        _input_chars = 0; for (const char* p = _input; *p; p++) if (((uint8_t)*p & 0xC0) != 0x80) _input_chars++;
        break;
      }
      case A_COPY: pop(); scopy(_input, sizeof(_input), m.text); _input_chars = 0; for (const char* p = _input; *p; p++) if (((uint8_t)*p & 0xC0) != 0x80) _input_chars++; break;
      case A_RESEND: { char t[164]; scopy(t, sizeof(t), m.text); pop(); sendText(t); break; }
      case A_DELMSG:
        memmove(&_msgs[_msg_idx], &_msgs[_msg_idx + 1], sizeof(UiMsg) * (_msg_n - _msg_idx - 1)); _th_new = -1; _msg_n--;
        _msgs_dirty = true; _save_at = millis() + SAVE_DELAY_MS; scrollReset(); _msg_idx = -1;
        pop();
        break;
    }
    return;
  }
  ContactInfo c;
  switch (act) {
    case A_WRITE: { char tn[32]; dmThreadName(_ck, _cname, tn, sizeof(tn)); pop(); openThread(tn, false, _ck); break; }
    case A_FAV: {
      int r = the_mesh.uiToggleFavourite(_ck);
      if (r >= 0) { _cfav = r == 1; popup(r == 1 ? "Dodano do ulubionych" : "Usunięto z ulubionych", _cname); }
      break;
    }
    case A_DETAIL: detailContact(); break;
    case A_SHOWMAP: _map_lat = _clat; _map_lon = _clon; _map_follow = false; if (_map_zoom < 12) _map_zoom = 13; while (!_map.hasZoom(_map_zoom) && _map_zoom > _map.minZoom()) _map_zoom--; openMap(); break;
    case A_MANAGE: if (ctByKey(_ck, c)) push(S_LOGIN); break;
    case A_DELETE: { char t[40]; snprintf(t, sizeof(t), "Usunąć kontakt?"); ask(C_DEL_CONTACT, t, _cname); break; }
  }
}

void UITask::doConfirm(uint8_t what) {
  switch (what) {
    case C_DEL_CHANNEL: {
      bool ok = the_mesh.uiRemoveChannel(_confirm_name);
      for (int i = 0; i < _msg_n; ) {                 // razem z kanałem znikają jego wiadomości
        if (_msgs[i].chan && strcmp(_msgs[i].thread, _confirm_name) == 0) { memmove(&_msgs[i], &_msgs[i + 1], sizeof(UiMsg) * (_msg_n - i - 1)); _th_new = -1; _msg_n--; }
        else i++;
      }
      _msgs_dirty = true; _save_at = millis() + SAVE_DELAY_MS;
      if (scr() == S_MENU) pop(); else prepare(S_MSGS);
      popup(ok ? "Kanał usunięty" : "Nie udało się usunąć", _confirm_name, !ok);
      break;
    }
    case C_DEL_THREAD:
      for (int i = 0; i < _msg_n; ) {
        if (!_msgs[i].chan && strcmp(_msgs[i].thread, _confirm_name) == 0) { memmove(&_msgs[i], &_msgs[i + 1], sizeof(UiMsg) * (_msg_n - i - 1)); _th_new = -1; _msg_n--; }
        else i++;
      }
      _msgs_dirty = true; _save_at = millis() + SAVE_DELAY_MS;
      if (scr() == S_MENU) pop();
      popup("Rozmowa usunięta", _confirm_name);
      break;
    case C_DEL_MULTI: {                               // usuń zaznaczone kontakty razem z zapamiętanymi hasłami repeaterów
      int done = 0;
      for (int i = 0; i < _sel_n; i++) {
        ContactInfo q;
        if (!ctByPrefix(_selk[i], q)) continue;
        if (the_mesh.uiRemoveContact(q.id.pub_key)) { done++; pwSet(q.id.pub_key, ""); }
      }
      _sel_n = 0; _sel_mode = false;
      if (scr() == S_MENU) pop();
      buildContacts();
      if (sel() > _list_n) sel() = _list_n > 0 ? 1 : 0;
      char t[32]; snprintf(t, sizeof(t), "kontaktów: %d", done); popup("Usunięto", t);
      break;
    }
    case C_DEL_CONTACT: {
      bool ok = the_mesh.uiRemoveContact(_ck);
      pwSet(_ck, "");
      pop();                                          // z menu kontaktu na listę
      popup(ok ? "Kontakt usunięty" : "Nie udało się usunąć", _confirm_name, !ok);
      break;
    }
    case C_REBOOT: if (_msgs_dirty) saveMsgs(); the_mesh.uiReboot(); break;
    case C_FACTORY:
      if (!the_mesh.uiFactoryReset()) popup("Nie udało się wyczyścić pamięci", "", true);
      break;
    case C_RPT_REBOOT: adminCli("reboot"); break;
    case C_WIFI_DL:
      wifiPrepare();
      m9dlStart(_wifi.ssid, _wifi.pass, _wifi.url, 13);
      break;
    case C_WIFI_OTA:
      wifiPrepare();
      m9otaCheck(_wifi.ssid, _wifi.pass, OTA_URL, M9_FW_VERSION);
      break;
    case C_OTA_INSTALL: m9otaInstall(); break;
    case C_WIFI_FORGET: memset(_wifi.ssid, 0, sizeof(_wifi.ssid)); memset(_wifi.pass, 0, sizeof(_wifi.pass)); saveWifi(); popup("Sieć WiFi zapomniana"); break;
    case C_BT_RESTART: if (_msgs_dirty || true) saveMsgs(); the_mesh.uiReboot(); break;
    case C_CLEAR_MSGS: _msg_n = 0; _msg_idx = -1; saveMsgs(); popup("Wiadomości usunięte"); break;
    case C_CLEAR_PW: _pw_n = 0; savePw(); popup("Hasła repeaterów usunięte"); break;
    case C_CLEAR_BONDS: {
      int n = m9bleClearBonds();
      char t[64]; snprintf(t, sizeof(t), "usunięto: %d · w telefonie też usuń urządzenie", n);
      popup("Zapomniano telefony", t, false, 6000);
      break;
    }
    case C_RPT_SF6: adminCli("set radio 869.618,62.5,6,8"); break;
  }
}

// ---------------- zarządzanie repeaterem ----------------

void UITask::adminWait(uint8_t kind) {
  _wait_kind = kind;
  uint32_t est = the_mesh.ui_admin.est_timeout;
  if (est < 6000) est = 6000;
  if (est > 25000) est = 25000;
  _wait_until = millis() + est + 2000;
  static const char* const W[5] = { "", "Loguję…", "Pytam o stan…", "Pytam o sąsiadów…", "Wysyłam komendę…" };
  popup(W[kind], "czekam na odpowiedź · Wstecz przerywa", false, (int)(est + 2500));
}

void UITask::adminCli(const char* cmd) {
  scopy(_cli_cmd, sizeof(_cli_cmd), cmd);
  if (the_mesh.uiAdminCli(cmd)) adminWait(4); else popup("Nie udało się wysłać", "", true);
}

void UITask::adminLoop() {
  if (!_wait_kind) return;
  MyMesh::UiAdmin& a = the_mesh.ui_admin;
  char t[48], k[56];
  if (_wait_kind == 1 && a.login >= 2) {
    _wait_kind = 0; _popup_until = 0;
    if (a.login == 2) {
      if (_pw_new && _pw_try[0]) pwSet(_ck, _pw_try);     // hasło zadziałało: zapamiętaj
      _nav[_depth - 1].scr = S_ADMIN; _nav[_depth - 1].sel = 0; _nav[_depth - 1].top = 0;   // ekran logowania zamienia się w menu
      popup("Zalogowano", a.is_admin ? "jako administrator" : "jako gość");
    } else {
      if (!_pw_new && _pw_try[0]) pwSet(_ck, "");         // zapamiętane hasło już nie pasuje
      popup("Logowanie nieudane", _pw_try[0] ? "złe hasło?" : "gość niedozwolony?", true);
    }
    return;
  }
  if (_wait_kind == 2 && a.status_ready) {
    _wait_kind = 0; _popup_until = 0;
    struct __attribute__((packed)) St { uint16_t mv, queue; int16_t noise, rssi; uint32_t recv, sent, air, up, sf, sd, rf, rd; uint16_t err; int16_t snr; } st;
    memset(&st, 0, sizeof(st)); memcpy(&st, a.status, a.status_len < sizeof(st) ? a.status_len : sizeof(st));
    kvOpen(_cname, "stan");
    snprintf(t, sizeof(t), "%lu dni %lu godz.", (unsigned long)(st.up / 86400), (unsigned long)((st.up / 3600) % 24)); kvAdd("Czas pracy", t);
    snprintf(t, sizeof(t), "%u,%02u V", st.mv / 1000, (st.mv % 1000) / 10); kvAdd("Bateria", t);
    snprintf(t, sizeof(t), "%d dBm", st.noise); kvAdd("Szum tła", t);
    char sn[16]; fmtSnr(sn, sizeof(sn), st.snr);
    snprintf(t, sizeof(t), "%d dBm / %s", st.rssi, sn); kvAdd("Ostatni RSSI / SNR", t);
    snprintf(t, sizeof(t), "%lu / %lu", (unsigned long)st.recv, (unsigned long)st.sent); kvAdd("Odebrane / wysłane", t);
    snprintf(t, sizeof(t), "%lu s", (unsigned long)st.air); kvAdd("Czas nadawania", t);
    snprintf(t, sizeof(t), "%lu / %lu", (unsigned long)st.sf, (unsigned long)st.rf); kvAdd("Zalewowe wysł. / odebr.", t);
    snprintf(t, sizeof(t), "%lu / %lu", (unsigned long)st.sd, (unsigned long)st.rd); kvAdd("Bezpośr. wysł. / odebr.", t);
    snprintf(t, sizeof(t), "%u / %u", st.queue, st.err); kvAdd("Kolejka / błędy", t);
    push(S_KV);
    return;
  }
  if (_wait_kind == 3 && a.nb_ready) {
    _wait_kind = 0; _popup_until = 0;
    uint16_t total = 0, cnt = 0;
    if (a.nb_len >= 4) { memcpy(&total, a.nb, 2); memcpy(&cnt, a.nb + 2, 2); }
    bool more_page = _kv_more && scr() == S_KV;       // dociągamy kolejną porcję do otwartej listy
    if (more_page) { if (_kv_n > 0) _kv_n--; }        // zdejmij wiersz „pobierz następnych”
    else { kvOpen(_cname, ""); _nb_got = 0; }
    _nb_total = total;
    for (int i = 0; i < cnt; i++) {
      int o = 4 + i * 11; if (o + 11 > a.nb_len) break;
      char nm[48], sn[16];
      rptName(a.nb + o, 3, nm, sizeof(nm));
      fmtSnr(sn, sizeof(sn), (int8_t)a.nb[o + 10]);
      snprintf(k, sizeof(k), "%s", nm); kvAdd(k, sn);
      _nb_got++;
    }
    snprintf(_kv_right, sizeof(_kv_right), "sąsiedzi: %u z %u", _nb_got, total);
    _kv_more = cnt > 0 && _nb_got < total && _kv_n < UI_KVS_MAX - 1;
    if (_kv_more) { snprintf(k, sizeof(k), "Enter: pobierz następnych (%u z %u)", _nb_got, total); kvAdd(k, ""); }
    if (_kv_n == 0) kvAdd("Brak sąsiadów", "");
    if (!more_page) push(S_KV);
    return;
  }
  if (_wait_kind == 4 && a.cli_ready) {
    a.cli_ready = false;
    _wait_kind = 0; _popup_until = 0;
    snprintf(t, sizeof(t), "> %s", _cli_cmd);
    kvOpen(_cname, "odpowiedź");
    kvAdd(t, "");
    if (a.cli[0]) kvText(a.cli); else kvAdd("(pusta odpowiedź)", "");
    push(S_KV);
    return;
  }
  if ((long)(millis() - _wait_until) >= 0) {
    uint8_t was = _wait_kind;
    _wait_kind = 0; a.cli_wait = false;
    if (was == 1) a.login = 3;
    popup("Brak odpowiedzi", was == 4 && strcmp(_cli_cmd, "reboot") == 0 ? "to normalne po restarcie repeatera" : "repeater poza zasięgiem?", true);
  }
}

// ---------------- pole do wpisywania ----------------

void UITask::radioLoad() { _rf = _prefs->freq; _rbw = _prefs->bw; _rsf = _prefs->sf; _rcr = _prefs->cr; }
bool UITask::radioChanged() { return fabsf(_rf - _prefs->freq) > 0.0005f || fabsf(_rbw - _prefs->bw) > 0.01f || _rsf != _prefs->sf || _rcr != _prefs->cr; }

bool UITask::timeFromGps() {
  LocationProvider* lp = _sensors ? _sensors->getLocationProvider() : NULL;
  if (lp == NULL || !lp->isValid()) return false;
  long ts = lp->getTimestamp();
  if (ts < 1700000000L) return false;
  the_mesh.getRTCClock()->setCurrentTime((uint32_t)ts);
  return true;
}

void UITask::submitInput() {
  if (_input[0] == 0 && _input_mode != IN_PASS) return;
  uint8_t mode = _input_mode;
  switch (mode) {
    case IN_MSG:
      if (sendText(_input)) { _input[0] = 0; _input_chars = 0; }
      return;
    case IN_CHANNEL: {                               // nowy kanał z hasztagiem
      char name[32];
      snprintf(name, sizeof(name), "%s%s", _input[0] == '#' ? "" : "#", _input);
      for (char* p = name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;      // nazwy kanałów z hasztagiem są małymi literami
      if (strlen(name) < 2) return;
      int r = the_mesh.uiAddHashtagChannel(name);
      _input_mode = IN_MSG; pop();
      if (r >= 0) popup("Dodano kanał", name);
      else if (r == -2) popup("Ten kanał już jest", name);
      else popup("Brak miejsca na kanał", "usuń któryś z istniejących", true);
      return;
    }
    case IN_FREQ: {
      for (char* p = _input; *p; p++) if (*p == ',') *p = '.';
      float f = atof(_input);
      _input_mode = IN_MSG; pop();
      if (f >= 150.0f && f <= 960.0f) _rf = f; else popup("Zła częstotliwość", "podaj w MHz, np. 869.618", true);
      return;
    }
    case IN_NAME:
      scopy(_prefs->node_name, sizeof(_prefs->node_name), _input);
      the_mesh.savePrefs();
      _input_mode = IN_MSG; pop();
      popup("Nazwa zmieniona", "w Bluetooth będzie widoczna po restarcie");
      return;
    case IN_TIME: {                                  // RRRR-MM-DD GG:MM, czas polski
      int Y, M, D, h, mi;
      if (sscanf(_input, "%d-%d-%d %d:%d", &Y, &M, &D, &h, &mi) != 5 || Y < 2024 || Y > 2099 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || h < 0 || mi < 0) {
        popup("Zły format", "wpisz np. 2026-11-13 14:32", true);
        return;
      }
      int y = Y - (M <= 2), era = y / 400, yoe = y - era * 400, mp = (M + 9) % 12;
      int doy = (153 * mp + 2) / 5 + D - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      long days = (long)era * 146097 + doe - 719468;
      uint32_t local = (uint32_t)(days * 86400L + h * 3600L + mi * 60L);
      uint32_t utc = local - 3600;
      struct tm chk; localTime(utc, &chk);
      if (chk.tm_hour != h) utc -= 3600;             // czas letni
      the_mesh.getRTCClock()->setCurrentTime(utc);
      char shown[24]; scopy(shown, sizeof(shown), _input);
      _input_mode = IN_MSG; pop();
      popup("Zegar ustawiony", shown);
      return;
    }
    case IN_PASS: {
      scopy(_pw_try, sizeof(_pw_try), _input);
      _pw_new = true;
      _input_mode = IN_MSG; pop();                    // z powrotem na ekran logowania
      if (the_mesh.uiAdminLogin(_ck, _pw_try)) adminWait(1); else popup("Nie udało się wysłać", "", true);
      return;
    }
    case IN_CLI: {
      char cmd[48]; scopy(cmd, sizeof(cmd), _input);
      _input_mode = IN_MSG; pop();
      adminCli(cmd);
      return;
    }
    case IN_LAT: case IN_LON: {
      for (char* p = _input; *p; p++) if (*p == ',') *p = '.';
      double v = atof(_input);
      double lat = _sensors->node_lat, lon = _sensors->node_lon;
      bool ok = mode == IN_LAT ? (v >= -90 && v <= 90) : (v >= -180 && v <= 180);
      _input_mode = IN_MSG; pop();
      if (!ok) { popup("Zła wartość", mode == IN_LAT ? "szerokość: od -90 do 90" : "długość: od -180 do 180", true); return; }
      if (mode == IN_LAT) lat = v; else lon = v;
      the_mesh.uiSetLatLon(lat, lon);
      popup("Pozycja zapisana");
      return;
    }
    case IN_WIFI_SSID: scopy(_wifi.ssid, sizeof(_wifi.ssid), _input); saveWifi(); _input_mode = IN_MSG; pop(); return;
    case IN_WIFI_PASS:
      if (_input[0]) { scopy(_wifi.pass, sizeof(_wifi.pass), _input); saveWifi(); }
      memset(_input, 0, sizeof(_input));               // hasło nie zostaje w polu pisania
      _input_mode = IN_MSG; pop(); return;
    case IN_MAP_URL: scopy(_wifi.url, sizeof(_wifi.url), _input); saveWifi(); _input_mode = IN_MSG; pop(); return;
    case IN_LOCKPIN: {
      if (strlen(_input) < 4) { popup("PIN ma mieć 4–8 cyfr", "", true); return; }
      for (int i = 0; i < 8; i++) _pin_salt[i] = (uint8_t)(esp_random_m9() >> (i % 3 * 8));
      the_mesh.uiPinHash(_pin_hash, _pin_salt, _input);
      _pin_have = true; _pin_fails = 0; savePin();
      _cfg.lock = 2; saveCfg();
      memset(_input, 0, sizeof(_input));
      _input_mode = IN_MSG; pop();
      popup("PIN blokady ustawiony", "blokada po wygaszeniu i po włączeniu", false, 5000);
      return;
    }
    case IN_PIN: {
      long pin = atol(_input);
      if (strlen(_input) != 6 || pin < 100000) { popup("PIN ma mieć 6 cyfr", "pierwsza cyfra nie może być zerem", true); return; }
      _prefs->ble_pin = (uint32_t)pin;
      the_mesh.savePrefs();
      _input_mode = IN_MSG; pop();
      popup("PIN zapisany", "zacznie działać po restarcie urządzenia");
      return;
    }
  }
}

void UITask::keyThread(uint8_t k) {
  uint8_t mode = _input_mode;
  if (k == M9K_ENTER) {
    if (mode == IN_MSG && _input[0] == 0) push(S_QUICK);       // pusty Enter: szybkie odpowiedzi
    else submitInput();
    return;
  }
  if (k == M9K_CTRL) { _polish = !_polish; return; }
  if (k == M9K_BKSP) {
    int n = strlen(_input);
    if (n > 0) {
      n--;
      while (n > 0 && ((uint8_t)_input[n] & 0xC0) == 0x80) n--;     // cały znak UTF-8
      _input[n] = 0; _input_chars--;
    }
    _polish = false;
    return;
  }
  if (k == M9K_BKSP_LONG) { _input[0] = 0; _input_chars = 0; return; }
  if (mode == IN_MSG) {
    // ▲▼: płynne przewinięcie o 1/3 obszaru rozmowy (animacja w loop)
    int step = uiThreadViewH() / 3, maxs = _th_total - uiThreadViewH();
    if (maxs < 0) maxs = 0;
    if (k == M9K_UP) { _scroll_tgt += step; if (_scroll_tgt > maxs) _scroll_tgt = maxs; return; }
    if (k == M9K_DOWN) { _scroll_tgt -= step; if (_scroll_tgt < 0) _scroll_tgt = 0; return; }
    if (k == M9K_RIGHT || k == M9K_F3) {              // Menu (albo ►): opcje zaznaczonej wiadomości
      if (_th_sel >= 0 && _th_sel < _msg_n && _msgs[_th_sel].chan == _cur_chan && strcmp(_msgs[_th_sel].thread, _cur) == 0) openMenuMsg(_th_sel);
      return;
    }
  }
  if (mode == IN_TIME && k == M9K_F3) {               // Menu: zegar z GPS
    if (timeFromGps()) { _input_mode = IN_MSG; pop(); popup("Zegar ustawiony z GPS"); }
    else popup("GPS nie ma jeszcze czasu", "włącz GPS i wyjdź pod niebo", true);
    return;
  }
  int limit = UI_INPUT_MAX;
  bool digits = false;
  switch (mode) {
    case IN_CHANNEL: limit = 24; if (k == ' ' || (k == '#' && _input_chars > 0)) return; break;
    case IN_FREQ: limit = 8; digits = true; break;
    case IN_NAME: limit = 22; break;
    case IN_TIME: limit = 16; if (!((k >= '0' && k <= '9') || k == '-' || k == ':' || k == ' ')) return; break;
    case IN_PASS: limit = 15; break;
    case IN_CLI: limit = 46; break;
    case IN_LAT: case IN_LON: limit = 11; digits = true; break;
    case IN_PIN: limit = 6; if (k < '0' || k > '9') return; break;
    case IN_LOCKPIN: limit = 8; if (k < '0' || k > '9') return; break;
    case IN_WIFI_SSID: limit = 32; break;
    case IN_WIFI_PASS: limit = 63; break;
    case IN_MAP_URL: limit = 120; if (k == ' ') return; break;
  }
  if (digits && !((k >= '0' && k <= '9') || k == '.' || k == ',' || k == '-')) return;
  if (k >= 32 && k < 127 && _input_chars < limit) {
    const char* pl = (_polish && mode != IN_PASS && mode != IN_CLI && mode < IN_WIFI_SSID) ? polishFor(k) : NULL;
    if (pl) strcat(_input, pl);
    else { int n = strlen(_input); _input[n] = (char)k; _input[n + 1] = 0; }
    _input_chars++;
    _polish = false;
  }
}

// ---------------- listy: liczba wierszy, treść, Enter, dodatkowe klawisze ----------------

#define AA_OVERWRITE (1 << 0)
#define AA_CHAT      (1 << 1)
#define AA_REPEATER  (1 << 2)
#define AA_ROOM      (1 << 3)
#define AA_SENSOR    (1 << 4)
static const uint8_t AA_BITS[4] = { AA_CHAT, AA_REPEATER, AA_ROOM, AA_SENSOR };
static const float AIRTIME_STEPS[] = { 0, 1, 2, 3, 9 };
static const char* const AIRTIME_NAMES[] = { "bez limitu", "do 50% czasu", "do 33% czasu", "do 25% czasu", "do 10% czasu" };
static const char* const TELEM_NAMES[] = { "nikomu", "wybranym kontaktom", "każdemu, kto zapyta" };
#define FILTER_HEARD 5
#define N_CLI ((int)(sizeof(CLI_CMDS) / sizeof(CLI_CMDS[0])))

int UITask::listCount(uint8_t s) {
  switch (s) {
    case S_MSGS: return _thread_n;
    case S_CONTACTS: return 1 + (_cfg.ct_filter == FILTER_HEARD ? the_mesh.ui_heard_n : _list_n);   // wiersz 0 = filtry
    case S_RPTS: return _list_n + 1;
    case S_HEARD: return the_mesh.ui_heard_n;
    case S_MENU: return _menu_n;
    case S_QUICK: return N_QUICK_TEXT + 2;
    case S_DISC: return the_mesh.ui_disc_n + 1;
    case S_LOGIN: return pwGet(_ck) ? 3 : 2;
    case S_ADMIN: return 5;
    case S_ADMIN_ACT: return 4;
    case S_ADMIN_CLI: return N_CLI + 1;
    case S_RADIO: return 9;
    case S_SET_BT: return 7;
    case S_PICK: return _pick_n;
    case S_MAP_NET: return 7;      // wyśrodkuj, pobierz, sieć, hasło, zapomnij sieć, adres, karta
    case S_OTA: return 4;
    case S_SET_POS: return 10;
    case S_SET_ADV: return 3;
    case S_SET_CT: return 6;
    case S_SET_MSG: return 3;
    case S_SET_SCR: return 11;
    case S_SET_SYS: return 7;
  }
  return 0;
}

void UITask::listHead(uint8_t s) {
  UiView& v = *_v;
  auto hint = [&](const char* k, const char* t) { if (v.nhints < 4) { scopy(v.hint[v.nhints].key, sizeof(v.hint[0].key), k); scopy(v.hint[v.nhints].txt, sizeof(v.hint[0].txt), t); v.nhints++; } };
  v.tab = s < 5 ? (int8_t)s : -1;
  switch (s) {
    case S_MSGS:
      strcpy(v.title, "Wiadomości");
      if (v.unread > 0) snprintf(v.title_right, sizeof(v.title_right), "nieprzeczytane: %d · Menu: opcje", v.unread); else strcpy(v.title_right, "Menu: opcje rozmowy");
      strcpy(v.empty, "Brak rozmów. Kanał dodasz klawiszem Menu.");
      break;
    case S_CONTACTS: {
      strcpy(v.title, "Kontakty");
      if (_sel_mode) snprintf(v.title_right, sizeof(v.title_right), "zaznaczone: %d · Menu: usuń, zaznacz", _sel_n);
      else if (_search[0]) snprintf(v.title_right, sizeof(v.title_right), "szukam: %s · %d", _search, _list_n);
      else if (_cfg.ct_filter == FILTER_HEARD) strcpy(v.title_right, "Enter: dodaj do kontaktów");
      else snprintf(v.title_right, sizeof(v.title_right), "%d · pisz, aby szukać", _list_n);
      static const char* const C[6] = { "Wszyscy", "Osoby", "RPT", "Pokoje", "Ulubione", "Usłyszane" };
      for (int i = 0; i < 6; i++) scopy(v.chips[i], sizeof(v.chips[0]), C[i]);
      v.nchips = 6; v.chip_active = _cfg.ct_filter; v.chip_focus = sel() == 0;
      strcpy(v.empty, _cfg.ct_filter == FILTER_HEARD ? "Od włączenia urządzenie nie usłyszało jeszcze żadnego advertu." : "Brak kontaktów dla tego filtra.");
      break;
    }
    case S_RPTS:
      strcpy(v.title, "Repeatery");
      snprintf(v.title_right, sizeof(v.title_right), "%d · %s · Menu: sortuj", _list_n, _cfg.rpt_sort ? "wg odległości" : "ostatnio słyszane");
      break;
    case S_MENU: scopy(v.title, sizeof(v.title), _menu_title); hint("Enter", "wybierz"); hint("Wstecz", ""); break;
    case S_OTA:
      strcpy(v.title, "Aktualizacja przez WiFi");
      if (m9dlStatus()[0] && m9otaState()) scopy(v.title_right, sizeof(v.title_right), m9dlStatus()); else snprintf(v.title_right, sizeof(v.title_right), "wersja %s", M9_FW_VERSION);
      hint("Enter", "wybierz"); hint("Wstecz", "");
      break;
    case S_MAP_NET:
      strcpy(v.title, "Mapa");
      if (m9dlStatus()[0]) scopy(v.title_right, sizeof(v.title_right), m9dlStatus());
      else strcpy(v.title_right, "pobieranie i pliki");
      hint("Enter", "wybierz"); hint("Wstecz", m9dlActive() ? "zatrzymaj" : "");
      break;
    case S_PICK: scopy(v.title, sizeof(v.title), _pick_title); strcpy(v.title_right, "wybierz wartość"); hint("Enter", "wybierz"); hint("Wstecz", "bez zmian"); break;
    case S_QUICK: strcpy(v.title, "Szybkie odpowiedzi"); snprintf(v.title_right, sizeof(v.title_right), "do: %s", _cur); hint("Enter", "wyślij"); hint("Wstecz", ""); break;
    case S_DISC: {
      strcpy(v.title, "Co tu słychać?");
      bool waiting = _disc_until && (long)(millis() - _disc_until) < 0;
      snprintf(v.title_right, sizeof(v.title_right), waiting ? "czekam… odpowiedzi: %d" : "odpowiedzi: %d", the_mesh.ui_disc_n);
      hint("Enter", "otwórz / dodaj"); hint("Wstecz", "");
      break;
    }
    case S_LOGIN: scopy(v.title, sizeof(v.title), _cname); strcpy(v.title_right, "logowanie"); hint("Enter", "zaloguj"); hint("Wstecz", ""); break;
    case S_ADMIN: scopy(v.title, sizeof(v.title), _cname); strcpy(v.title_right, the_mesh.ui_admin.is_admin ? "administrator" : "gość"); hint("Enter", "wybierz"); hint("Wstecz", "wyloguj"); break;
    case S_ADMIN_ACT: scopy(v.title, sizeof(v.title), _cname); strcpy(v.title_right, "akcje"); hint("Enter", "wykonaj"); hint("Wstecz", ""); break;
    case S_ADMIN_CLI: scopy(v.title, sizeof(v.title), _cname); strcpy(v.title_right, "konsola"); hint("Enter", "wyślij"); hint("Wstecz", ""); break;
    case S_RADIO:
      strcpy(v.title, "Radio");
      snprintf(v.title_right, sizeof(v.title_right), "teraz: %.3f · %.4g · SF%d · CR 4/%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
      comma(v.title_right);
      hint("Enter", "wybierz"); hint("Wstecz", "");
      break;
    default: {
      static const char* const T[] = { "Bluetooth", "Pozycja i prywatność", "Advert", "Kontakty", "Wiadomości", "Ekran i dźwięk", "System" };
      if (s >= S_SET_BT && s <= S_SET_SYS) strcpy(v.title, T[s - S_SET_BT]);
      strcpy(v.title_right, "Ustawienia");
      hint("Enter", "wybierz"); hint("Wstecz", "");
    }
  }
}

void UITask::listRow(uint8_t s, int i, UiRow& r) {
  ContactInfo c;
  char t[64];
  switch (s) {
    case S_MSGS: {
      if (i == _thread_n) { setRow(r, "+ Dodaj kanał", "kanał z hasztagiem, np. #beskidy"); return; }
      Thread& th = _threads[i];
      bool muted = isMuted(th.name);
      setRow(r, th.name, "", muted ? UII_MUTE : (th.chan ? UII_HASH : UII_USER));
      r.badge = th.unread;
      if (th.last_msg >= 0) {
        UiMsg& m = _msgs[th.last_msg];
        if (m.out) snprintf(r.sub, sizeof(r.sub), "Ty: %s", m.text);
        else if (m.who[0]) snprintf(r.sub, sizeof(r.sub), "%s: %s", m.who, m.text);
        else scopy(r.sub, sizeof(r.sub), m.text);
        fmtClock(r.right, sizeof(r.right), m.ts, true);
      }
      return;
    }
    case S_CONTACTS:
      if (i == 0) return;                             // wiersz filtrów rysuje się osobno
      if (_cfg.ct_filter == FILTER_HEARD) { listRow(S_HEARD, i - 1, r); return; }
      if (!the_mesh.getContactByIdx(_list[i - 1], c)) return;
      fmtAgo(t, sizeof(t), c.lastmod);
      if (_sel_mode && selHas(c.id.pub_key)) setRow(r, c.name, t, UII_CHECK, true);          // zaznaczony do usunięcia
      else setRow(r, c.name, t, typeIcon(c.type, c.flags & 1), c.flags & 1);
      if (_cfg.ct_filter == 0 || _cfg.ct_filter == 4) scopy(r.sub, sizeof(r.sub), typeName(c.type));
      return;
    case S_RPTS: {
      if (i == 0) { setRow(r, "Co tu słychać?", "ping do sąsiadów ›", UII_TOWER, true); return; }
      if (!the_mesh.getContactByIdx(_list[i - 1], c)) return;
      char ago[16]; fmtAgo(ago, sizeof(ago), c.lastmod);
      double lat, lon; int sats;
      if ((c.gps_lat || c.gps_lon) && gpsFix(&lat, &lon, &sats)) snprintf(t, sizeof(t), "%.1f km · %s", distKm(lat, lon, c.gps_lat / 1e6, c.gps_lon / 1e6), ago);
      else scopy(t, sizeof(t), ago);
      comma(t);
      setRow(r, c.name, t, typeIcon(c.type, c.flags & 1), c.flags & 1);
      return;
    }
    case S_HEARD: {
      ContactInfo& h = the_mesh.ui_heard[i];
      char ago[16]; fmtAgo(ago, sizeof(ago), the_mesh.ui_heard_at[i]);
      bool known = the_mesh.uiIsContact(h.id.pub_key);
      snprintf(t, sizeof(t), "%s · %s", known ? "w kontaktach" : "nowy", ago);
      setRow(r, h.name, t, typeIcon(h.type, false), !known);
      scopy(r.sub, sizeof(r.sub), typeName(h.type));
      return;
    }
    case S_MENU:
      switch (_menu_act[i]) {
        case A_WRITE: setRow(r, "Napisz", "", UII_MAIL); break;
        case A_FAV: setRow(r, "Ulubiony", yesNo(_cfav), UII_STAR, _cfav); break;
        case A_DETAIL: setRow(r, "Szczegóły", _menu_msg ? "trasa, sygnał, czas" : "pozycja, droga, klucz", _menu_msg ? UII_SIGNAL : UII_USER); break;
        case A_MANAGE: setRow(r, "Zarządzaj", pwGet(_ck) ? "hasło zapamiętane" : "logowanie", UII_LOCK); break;
        case A_SHOWMAP: setRow(r, "Pokaż na mapie", "", UII_GPS); break;
        case A_ROUTE: setRow(r, "Trasa na mapie", _menu_msg && _msg_idx >= 0 && _msgs[_msg_idx].out ? "kto powtórzył" : "skąd przyszła", UII_GPS); break;
        case A_DELETE: setRow(r, "Usuń kontakt"); break;
        case A_MULTI: setRow(r, "Zaznacz kilka…", "do usunięcia naraz", UII_CHECK); break;
        case A_SELDEL: { char q[40]; snprintf(q, sizeof(q), "kontaktów: %d", _sel_n); setRow(r, "Usuń zaznaczone", q, UII_CHECK, true); break; }
        case A_SELALL: setRow(r, "Zaznacz wszystkie z tej listy", "bez ulubionych"); break;
        case A_SELOLD: setRow(r, "Zaznacz niewidziane od…", "z tej listy, bez ulubionych"); break;
        case A_SELNONE: setRow(r, "Odznacz wszystko"); break;
        case A_REPLY: setRow(r, "Odpowiedz @", "wstawia @[nadawca]", UII_MAIL); break;
        case A_REPLY_ACK: setRow(r, "Odpowiedz ack", "@[nadawca] ack, ile hopów", UII_CHECK); break;
        case A_RESEND: setRow(r, "Wyślij ponownie"); break;
        case A_COPY: setRow(r, "Kopiuj treść do pola pisania"); break;
        case A_DELMSG: setRow(r, "Usuń wiadomość", "tylko z tego urządzenia"); break;
        case A_OPEN: setRow(r, "Otwórz rozmowę", "", UII_MAIL); break;
        case A_MUTE: setRow(r, "Powiadomienia", isMuted(_tname) ? "wyciszone" : "włączone", UII_MUTE, isMuted(_tname)); break;
        case A_MARKREAD: setRow(r, "Oznacz jako przeczytane"); break;
        case A_DELCHAN: setRow(r, "Usuń kanał", "z wiadomościami"); break;
        case A_DELTHREAD: setRow(r, "Usuń rozmowę", "wiadomości z tego urządzenia"); break;
        case A_ADDCHAN: setRow(r, "Dodaj kanał", "z hasztagiem, np. #beskidy", UII_HASH); break;
        case A_CHINFO: setRow(r, "Info o kanale", "statystyki, kod QR", UII_HASH); break;
        case A_SPEC: setRow(r, "Widmo", "pasmo i szum na naszym kanale", UII_SPEC); break;
        case A_LOG: setRow(r, "Log pakietów", "wszystko, co odebrało radio", UII_SIGNAL); break;
      }
      return;
    case S_QUICK: setRow(r, QUICK[i], "", i >= N_QUICK_TEXT ? UII_GPS : UII_NONE); return;
    case S_PICK: setRow(r, _pick_opt[i], i == _pick_cur ? "teraz" : "", i == _pick_cur ? UII_CHECK : UII_NONE, true); return;
    case S_OTA: {
      int os = m9otaState();
      if (i == 0) {
        switch (os) {
          case 1: setRow(r, "Sprawdzam…", m9dlStatus(), UII_SIGNAL, true); break;
          case 2: { char a[40]; snprintf(a, sizeof(a), "Zainstaluj %s", m9otaVersion()); snprintf(t, sizeof(t), "teraz %s", M9_FW_VERSION); setRow(r, a, t, UII_SIGNAL, true); break; }
          case 3: snprintf(t, sizeof(t), "%s · Enter: wgraj ponownie", m9otaVersion()); setRow(r, "Masz aktualną wersję", t, UII_CHECK); break;
          case 4: { char a[40]; snprintf(a, sizeof(a), "Instaluję %s", m9otaVersion()); snprintf(t, sizeof(t), "%d%% · nie wyłączaj", m9otaPercent()); setRow(r, a, t, UII_SIGNAL, true); break; }
          case 5: setRow(r, "Zainstalowano", "restart…", UII_CHECK, true); break;
          case 6: setRow(r, "Błąd — spróbuj ponownie", m9dlDetail(), UII_SIGNAL); break;
          default: snprintf(t, sizeof(t), "teraz %s", M9_FW_VERSION); setRow(r, "Sprawdź aktualizację", t, UII_SIGNAL); break;
        }
      }
      else if (i == 1) setRow(r, "Sieć WiFi", _wifi.ssid[0] ? _wifi.ssid : "nie ustawiona");
      else if (i == 2) setRow(r, "Hasło WiFi", _wifi.pass[0] ? "zapisane (zaszyfrowane)" : "brak", UII_LOCK);
      else setRow(r, "Zapomnij sieć WiFi", "kasuje nazwę i hasło");
      return;
    }
    case S_MAP_NET:
      t[0] = 0;
      switch (i) {
        case 0: setRow(r, "Wyśrodkuj na mojej pozycji", "", UII_GPS); break;
        case 1: scopy(t, sizeof(t), m9dlStatus()[0] ? m9dlDetail() : "przez sieć WiFi poniżej"); setRow(r, m9dlActive() ? "Przerwij" : "Pobierz z internetu", t, UII_SIGNAL, m9dlActive()); break;
        case 2: setRow(r, "Sieć WiFi", _wifi.ssid[0] ? _wifi.ssid : "nie ustawiona"); break;
        case 3: setRow(r, "Hasło WiFi", _wifi.pass[0] ? "zapisane (zaszyfrowane)" : "brak", UII_LOCK); break;
        case 4: setRow(r, "Zapomnij sieć WiFi", "kasuje nazwę i hasło"); break;
        case 5: {
          const char* u = _wifi.url; if (strncmp(u, "https://", 8) == 0) u += 8;
          int n = strlen(u);
          if (n > 30) snprintf(t, sizeof(t), "%.13s…%s", u, u + n - 14); else scopy(t, sizeof(t), u);
          setRow(r, "Adres mapy", t);
          break;
        }
        case 6:
          if (!_map.sdOk()) strcpy(t, _sd_pending ? "sprawdzam kartę…" : "brak karty SD");
          else if (_map.numPacks() == 0) snprintf(t, sizeof(t), "karta %lu GB · brak mapy", (unsigned long)(_map.sdSizeMB() / 1024));
          else snprintf(t, sizeof(t), "plików %d · zoom %d–%d", _map.numPacks(), _map.minZoom(), _map.maxZoom());
          setRow(r, "Karta: test i pliki", t, UII_SD);
          break;
        case 7: break;
          break;
      }
      return;
    case S_DISC: {
      if (i == 0) { bool waiting = _disc_until && (long)(millis() - _disc_until) < 0; setRow(r, "Ponów ping", waiting ? "czekam na odpowiedzi…" : "wyślij jeszcze raz"); return; }
      MyMesh::UiDisc& d = the_mesh.ui_disc[i - 1];
      char a[16], b[16];
      fmtSnr(a, sizeof(a), d.snr_rx); fmtSnr(b, sizeof(b), d.snr_tx);
      a[strlen(a) - 3] = 0;                           // bez „ dB” w pierwszej wartości
      snprintf(t, sizeof(t), "u mnie %s · u niego %s", a, b);
      bool known = ctByKey(d.key, c) ;
      if (known) setRow(r, c.name, t, typeIcon(c.type, c.flags & 1), c.flags & 1);
      else { char nm[24]; snprintf(nm, sizeof(nm), "%02x%02x%02x · nowy", d.key[0], d.key[1], d.key[2]); setRow(r, nm, t, UII_TOWER, true); }
      return;
    }
    case S_LOGIN: {
      int k = i + (pwGet(_ck) ? 0 : 1);
      if (k == 0) setRow(r, "Zaloguj zapamiętanym hasłem", "", UII_LOCK, true);
      else if (k == 1) setRow(r, "Gość", "bez hasła", UII_USER);
      else setRow(r, "Wpisz hasło…", "zostanie zapamiętane");
      return;
    }
    case S_ADMIN: {
      static const char* const N[5] = { "Stan", "Sąsiedzi", "Akcje", "Konsola", "Wyloguj" };
      static const char* const R[5] = { "bateria, licznik pakietów, szum", "kogo słyszy i z jakim SNR", "advert, zegar, preset, restart", "komendy tekstowe", "" };
      bool adm = the_mesh.ui_admin.is_admin;
      setRow(r, N[i], (i == 2 || i == 3) && !adm ? "tylko dla administratora" : R[i]);
      return;
    }
    case S_ADMIN_ACT: {
      static const char* const N[4] = { "Wyślij advert", "Synchronizuj zegar", "Ustaw preset SF6", "Restart repeatera" };
      static const char* const R[4] = { "do całej sieci", "z zegara sieci", "869,618 · 62,5 · SF6 · CR 4/8", "z potwierdzeniem" };
      setRow(r, N[i], R[i]);
      return;
    }
    case S_ADMIN_CLI:
      if (i < N_CLI) setRow(r, CLI_CMDS[i]); else setRow(r, "Własna komenda…", "wpisz z klawiatury");
      return;
    case S_RADIO: {
      static const char* const N[9] = { "Preset", "Częstotliwość", "Pasmo (BW)", "Rozpraszanie (SF)", "Kodowanie (CR)", "Zastosuj własne ustawienia", "Moc nadawania", "Bajty trasy", "Limit nadawania" };
      bool sf6 = the_mesh.uiRadioIsSF6();
      bool narrow = fabsf(_prefs->freq - 869.618f) < 0.005f && fabsf(_prefs->bw - 62.5f) < 0.1f && _prefs->sf == 8 && _prefs->cr == 8;
      t[0] = 0;
      switch (i) {
        case 0: strcpy(t, sf6 ? "SF6 — MeshCore Południe" : narrow ? "EU/UK Narrow (SF8)" : "własne ustawienia"); break;
        case 1: snprintf(t, sizeof(t), "%.3f MHz", _rf); break;
        case 2: snprintf(t, sizeof(t), "%.4g kHz", _rbw); break;
        case 3: snprintf(t, sizeof(t), "SF%d", _rsf); break;
        case 4: snprintf(t, sizeof(t), "4/%d", _rcr); break;
        case 5: strcpy(t, radioChanged() ? "Enter: zapisz i włącz" : "brak zmian"); break;
        case 6: snprintf(t, sizeof(t), "%d dBm", _prefs->tx_power_dbm); break;
        case 7: snprintf(t, sizeof(t), "%d (identyfikator: %d znaki)", _prefs->path_hash_mode + 1, (_prefs->path_hash_mode + 1) * 2); break;
        case 8: { int k = 1; for (int q = 0; q < 5; q++) if (fabsf(AIRTIME_STEPS[q] - _prefs->airtime_factor) < 0.4f) k = q; strcpy(t, AIRTIME_NAMES[k]); break; }
      }
      comma(t);
      setRow(r, N[i], t, i == 0 ? UII_STAR : UII_NONE, i == 0 && (sf6 || narrow));
      return;
    }
    case S_SET_BT: {
      if (i == 0) {
        if (isBluetoothEnabled() && hasConnection()) snprintf(t, sizeof(t), "połączony z %s", the_mesh.ui_app_name[0] ? the_mesh.ui_app_name : "aplikacją");
        setRow(r, "Bluetooth", _bt_killed ? "wyłączony do restartu (WiFi)" : isBluetoothEnabled() ? (hasConnection() ? t : "włączony · czeka na telefon") : _bt_auto_off ? "wyłączony (brak połączenia)" : "wyłączony", UII_BT);
      }
      else if (i == 6) setRow(r, "Wyłącz bez połączenia", BTA_NAMES[_cfg.bt_auto]);
      else if (i == 1) { snprintf(t, sizeof(t), "%06lu", (unsigned long)the_mesh.getBLEPin()); setRow(r, "PIN do parowania", t, UII_LOCK); }
      else if (i == 2) { int n = m9bleBondCount(); if (n < 0) strcpy(t, "—"); else snprintf(t, sizeof(t), "%d", n); setRow(r, "Zapamiętane telefony", t); }
      else if (i == 3) setRow(r, "Zapomnij wszystkie telefony", "gdy parowanie nie działa");
      else if (i == 4) {
        if (m9ble_auth_fail) snprintf(t, sizeof(t), "%lu / %lu / %lu (kod %02X)", (unsigned long)m9ble_connects, (unsigned long)m9ble_auth_ok, (unsigned long)m9ble_auth_fail, m9ble_fail_reason);
        else snprintf(t, sizeof(t), "%lu / %lu / 0", (unsigned long)m9ble_connects, (unsigned long)m9ble_auth_ok);
        setRow(r, "Połączenia / parowania / błędy", t);
      } else if (i == 5) { snprintf(t, sizeof(t), "%lu / %lu", (unsigned long)m9ble_rx, (unsigned long)m9ble_tx); setRow(r, "Ramki odebrane / wysłane", t); }
      return;
    }
    case S_SET_POS: {
      static const char* const N[10] = { "GPS", "Pozycja w advercie", "Szerokość (ręcznie)", "Długość (ręcznie)", "Zapisz pozycję z GPS", "Teraz z GPS",
                                        "Stan baterii udostępniaj", "Pozycję udostępniaj", "Dane czujników udostępniaj", "Oszczędzanie GPS" };
      double lat, lon; int sats; bool fix = gpsFix(&lat, &lon, &sats);
      t[0] = 0;
      switch (i) {
        case 0: strcpy(t, onOff(_prefs->gps_enabled)); break;
        case 1: strcpy(t, _prefs->advert_loc_policy ? "wysyłaj" : "nie wysyłaj"); break;
        case 2: snprintf(t, sizeof(t), "%.5f", _sensors->node_lat); break;
        case 3: snprintf(t, sizeof(t), "%.5f", _sensors->node_lon); break;
        case 4: strcpy(t, fix ? "Enter: zapisz" : "brak pozycji"); break;
        case 5: if (fix) snprintf(t, sizeof(t), "%.5f, %.5f · sat. %d", lat, lon, sats); else snprintf(t, sizeof(t), "szukam… satelity: %d", sats); break;
        case 6: strcpy(t, TELEM_NAMES[_prefs->telemetry_mode_base % 3]); break;
        case 7: strcpy(t, TELEM_NAMES[_prefs->telemetry_mode_loc % 3]); break;
        case 8: strcpy(t, TELEM_NAMES[_prefs->telemetry_mode_env % 3]); break;
        case 9: strcpy(t, GPS_NAMES[_cfg.gps_mode]); if (_gps_off_phase) strcat(t, " · teraz wył."); break;
      }
      setRow(r, N[i], t, i == 0 ? UII_GPS : i == 9 ? UII_BATT : (i >= 6 ? UII_SHIELD : UII_NONE));
      return;
    }
    case S_SET_ADV:
      if (i == 0) setRow(r, "Wyślij do sąsiadów", "bez przekazywania");
      else if (i == 1) setRow(r, "Wyślij do całej sieci", "przez repeatery");
      else { if (ADV_HOURS[_cfg.adv_idx]) snprintf(t, sizeof(t), "co %d godz. (do sąsiadów)", ADV_HOURS[_cfg.adv_idx]); else strcpy(t, "wyłączony"); setRow(r, "Automatyczny advert", t); }
      return;
    case S_SET_CT: {
      static const char* const N[6] = { "Dodawanie z advertów", "Osoby", "Repeatery", "Pokoje", "Gdy brak miejsca", "Z jak daleka" };
      bool sel = _prefs->manual_add_contacts & 1;
      t[0] = 0;
      if (i == 0) strcpy(t, sel ? "tylko wybrane typy" : "wszystko automatycznie");
      else if (i <= 3) strcpy(t, sel ? ((_prefs->autoadd_config & AA_BITS[i - 1]) ? "dodawaj" : "nie dodawaj") : "dodawaj (wszystko)");
      else if (i == 4) strcpy(t, (_prefs->autoadd_config & AA_OVERWRITE) ? "nadpisuj najstarsze" : "nie dodawaj nowych");
      else if (_prefs->autoadd_max_hops == 0) strcpy(t, "bez limitu skoków");
      else if (_prefs->autoadd_max_hops == 1) strcpy(t, "tylko słyszane bezpośrednio");
      else snprintf(t, sizeof(t), "do %d skoków", _prefs->autoadd_max_hops - 1);
      setRow(r, N[i], t);
      return;
    }
    case S_SET_MSG:
      if (i == 0) setRow(r, "Trasa pod wiadomością", _cfg.show_path ? "identyfikatory repeaterów" : "tylko liczba skoków");
      else if (i == 1) setRow(r, "Dodatkowe potwierdzenia", _prefs->multi_acks ? "włączone (pewniej, więcej ruchu)" : "wyłączone");
      else { snprintf(t, sizeof(t), "zapisanych: %d", _msg_n); setRow(r, "Usuń wszystkie wiadomości", t); }
      return;
    case S_SET_SCR:
      if (i == 0) setRow(r, "Wygaszanie ekranu", SLEEP_NAMES[_cfg.sleep_idx], UII_SCREEN);
      else if (i == 1) setRow(r, "Podświetlenie klawiatury", _cfg.kb_light ? "świeci, gdy ekran świeci" : "wyłączone");
      else if (i == 2) setRow(r, "Nowa wiadomość budzi ekran", yesNo(!_cfg.no_wake));
      else if (i == 3) setRow(r, "Blokada ekranu", _cfg.lock == 2 ? "po wygaszeniu, z PIN-em" : _cfg.lock ? "po wygaszeniu (Wstecz, Enter)" : "wyłączona", UII_LOCK);
      else if (i == 4) { snprintf(t, sizeof(t), "%d%%", brightPercent()); setRow(r, "Jasność ekranu", t, UII_SCREEN); }
      else if (i == 5) setRow(r, "Dźwięki urządzenia", _prefs->buzzer_quiet ? "wyciszone (wszystkie)" : "włączone", UII_MUTE, _prefs->buzzer_quiet);
      else if (i == 6) setRow(r, "Dźwięk wiadomości", _cfg.no_msg_snd ? "wyłączony" : "włączony");
      else if (i == 7) setRow(r, "Dźwięk advertu repeatera", _cfg.adv_snd == 0 ? "wyłączony" : _cfg.adv_snd == 1 ? "każdy repeater" : "tylko ulubione");
      else if (i == 8) setRow(r, "Przyciemnianie", DIM_NAMES[_cfg.dim_idx], UII_BATT);
      else if (i == 9) setRow(r, "Procesor po wygaszeniu", _cfg.cpu_full ? "pełna moc (240 MHz)" : "oszczędny (80 MHz)", UII_BATT);
      else setRow(r, "PIN blokady ekranu", _pin_have ? "ustawiony · Enter: zmień" : "brak · Enter: ustaw", UII_LOCK);
      return;
    case S_SET_SYS: {
      if (i == 0) setRow(r, "Informacje", "wersja, pamięć, klucz");
      else if (i == 1) { snprintf(t, sizeof(t), "teraz %s", M9_FW_VERSION); setRow(r, "Aktualizacja przez WiFi", t, UII_SIGNAL); }
      else if (i == 2) setRow(r, "Nazwa urządzenia", _prefs->node_name, UII_USER);
      else if (i == 3) { struct tm lt; localTime(the_mesh.getRTCClock()->getCurrentTime(), &lt); snprintf(t, sizeof(t), "%d.%02d.%04d %d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900, lt.tm_hour, lt.tm_min); setRow(r, "Data i godzina", t); }
      else if (i == 4) { snprintf(t, sizeof(t), "zapamiętanych: %d", _pw_n); setRow(r, "Usuń hasła repeaterów", t, UII_LOCK); }
      else if (i == 5) setRow(r, "Restart urządzenia", "z potwierdzeniem");
      else setRow(r, "Ustawienia fabryczne", "kasuje wszystko");
      return;
    }
  }
}

void UITask::listEnter(uint8_t s, int i) {
  ContactInfo c;
  switch (s) {
    case S_MSGS:
      if (i < _thread_n) openThread(_threads[i].name, _threads[i].chan);
      return;
    case S_CONTACTS:
      if (i == 0) { sel() = 1; return; }              // Enter na filtrach: przejdź do listy
      if (_cfg.ct_filter == FILTER_HEARD) { listEnter(S_HEARD, i - 1); return; }
      if (_sel_mode) { if (the_mesh.getContactByIdx(_list[i - 1], c)) selToggle(c.id.pub_key); return; }      // zaznacz / odznacz
      if (the_mesh.getContactByIdx(_list[i - 1], c)) { setCur(c); openMenuContact(); }
      return;
    case S_RPTS:
      if (i == 0) { push(S_DISC); return; }
      if (the_mesh.getContactByIdx(_list[i - 1], c)) { setCur(c); openMenuContact(); }
      return;
    case S_HEARD:
      if (the_mesh.ui_heard[i].type == ADV_TYPE_SENSOR) { popup("Czujników nie dodajemy", the_mesh.ui_heard[i].name); return; }
      if (the_mesh.uiIsContact(the_mesh.ui_heard[i].id.pub_key)) { if (ctByKey(the_mesh.ui_heard[i].id.pub_key, c)) { setCur(c); openMenuContact(); } }
      else if (the_mesh.uiAddHeard(i)) popup("Dodano do kontaktów", the_mesh.ui_heard[i].name);
      else popup("Nie udało się dodać", "brak miejsca w kontaktach?", true);
      return;
    case S_MENU: doAct(_menu_act[i]); return;
    case S_PICK: { uint8_t id = _pick_id; pop(); applyPick(id, i); return; }
    case S_MAP_NET:
      if (i == 0) { _map_follow = true; pop(); }
      else if (i == 1) {
        if (m9dlActive()) { m9dlStop(); _map.scan(); }
        else if (!_map.sdOk()) popup("Brak karty SD", "", true);
        else if (!_wifi.ssid[0]) popup("Najpierw ustaw sieć WiFi", "pozycje niżej: sieć i hasło");
        else if (!_bt_killed) ask(C_WIFI_DL, "WiFi wyłączy Bluetooth do restartu", "");
        else doConfirm(C_WIFI_DL);
      }
      else if (i == 2) openInput(IN_WIFI_SSID, _wifi.ssid);
      else if (i == 3) openInput(IN_WIFI_PASS, "");   // zapisanego hasła nie pokazujemy; puste pole = bez zmian
      else if (i == 4) ask(C_WIFI_FORGET, "Zapomnieć sieć WiFi?", _wifi.ssid);
      else if (i == 5) openInput(IN_MAP_URL, _wifi.url);
      else if (i == 6) {                              // test karty: zapis próbny, zawartość, wolne miejsce
        if (m9dlActive()) popup("Najpierw zatrzymaj pobieranie", "");
        else { _sd_pending = false; popup("Sprawdzam kartę SD…", "to potrwa do minuty, ekran stoi", false, 120000); _sd_test_at = millis() + 500; }
      }
      return;
    case S_OTA: {
      int os = m9otaState();
      if (i == 0) {
        if (os == 0 || os == 6) { if (!_wifi.ssid[0]) popup("Najpierw ustaw sieć WiFi", "pozycje niżej: sieć i hasło"); else if (!_bt_killed) ask(C_WIFI_OTA, "WiFi wyłączy Bluetooth do restartu", ""); else doConfirm(C_WIFI_OTA); }
        else if (os == 2 || os == 3) { char t[48]; snprintf(t, sizeof(t), os == 2 ? "Zainstalować wersję %s?" : "Wgrać ponownie %s?", m9otaVersion()); ask(C_OTA_INSTALL, t, ""); }
      }
      else if (i == 1) openInput(IN_WIFI_SSID, _wifi.ssid);
      else if (i == 2) openInput(IN_WIFI_PASS, "");
      else if (i == 3) ask(C_WIFI_FORGET, "Zapomnieć sieć WiFi?", _wifi.ssid);
      return;
    }
    case S_QUICK: {
      char t[100];
      if (i < N_QUICK_TEXT) scopy(t, sizeof(t), QUICK[i]);
      else {
        double lat, lon; int sats;
        if (!gpsFix(&lat, &lon, &sats)) { popup("Brak pozycji GPS", "włącz GPS w Ustawieniach i wyjdź pod niebo"); return; }
        if (i == N_QUICK_TEXT) snprintf(t, sizeof(t), "Moja pozycja: %.5f, %.5f", lat, lon);
        else snprintf(t, sizeof(t), "Moja pozycja: https://maps.google.com/?q=%.5f,%.5f", lat, lon);
      }
      pop();
      sendText(t);
      return;
    }
    case S_DISC: {
      if (i == 0) { prepare(S_DISC); return; }
      MyMesh::UiDisc& d = the_mesh.ui_disc[i - 1];
      if (!ctByKey(d.key, c)) {                       // nowy: dodaj do kontaktów i od razu otwórz
        if (!the_mesh.uiAddDiscovered(i - 1) || !ctByKey(d.key, c)) { popup("Nie udało się dodać", "brak miejsca w kontaktach?", true); return; }
        popup("Dodano do kontaktów", c.name);
      }
      setCur(c); openMenuContact();
      return;
    }
    case S_LOGIN: {
      int k = i + (pwGet(_ck) ? 0 : 1);
      if (k == 2) { openInput(IN_PASS, ""); return; }
      scopy(_pw_try, sizeof(_pw_try), k == 0 ? pwGet(_ck) : "");
      _pw_new = false;
      if (the_mesh.uiAdminLogin(_ck, _pw_try)) adminWait(1); else popup("Nie udało się wysłać", "", true);
      return;
    }
    case S_ADMIN:
      if ((i == 2 || i == 3) && !the_mesh.ui_admin.is_admin) { popup("Tylko dla administratora", "zaloguj się hasłem administratora"); return; }
      if (i == 0) { if (the_mesh.uiAdminStatus()) adminWait(2); else popup("Nie udało się wysłać", "", true); }
      if (i == 1) { _kv_more = false; if (the_mesh.uiAdminNeighbours(0)) adminWait(3); else popup("Nie udało się wysłać", "", true); }
      if (i == 2) push(S_ADMIN_ACT);
      if (i == 3) push(S_ADMIN_CLI);
      if (i == 4) { the_mesh.uiAdminLogout(); pop(); popup("Wylogowano"); }
      return;
    case S_ADMIN_ACT:
      if (i == 0) adminCli("advert");
      if (i == 1) adminCli("clock sync");
      if (i == 2) ask(C_RPT_SF6, "Ustawić na repeaterze preset SF6?", _cname);
      if (i == 3) ask(C_RPT_REBOOT, "Zrestartować repeater?", _cname);
      return;
    case S_ADMIN_CLI:
      if (i < N_CLI) adminCli(CLI_CMDS[i]); else openInput(IN_CLI, "");
      return;
    case S_RADIO:
      if (i == 1) { char t[16]; snprintf(t, sizeof(t), "%.3f", _rf); openInput(IN_FREQ, t); }
      else if (i == 5) {
        if (!radioChanged()) popup("Brak zmian do zastosowania");
        else { the_mesh.uiSetRadio(_rf, _rbw, _rsf, _rcr); radioLoad(); popup("Radio: ustawienia własne", "zapisane i włączone"); }
      } else settingPick(s, i);
      return;
    case S_SET_BT:
      if (i == 1) { char t[12]; snprintf(t, sizeof(t), "%06lu", (unsigned long)the_mesh.getBLEPin()); openInput(IN_PIN, t); }
      else if (i == 3) ask(C_CLEAR_BONDS, "Zapomnieć wszystkie telefony?", "");
      else settingPick(s, i);
      return;
    case S_SET_POS:
      if (i == 2) { char t[16]; snprintf(t, sizeof(t), "%.5f", _sensors->node_lat); openInput(IN_LAT, t); }
      else if (i == 3) { char t[16]; snprintf(t, sizeof(t), "%.5f", _sensors->node_lon); openInput(IN_LON, t); }
      else if (i == 4) {
        double lat, lon; int sats;
        if (gpsFix(&lat, &lon, &sats)) { the_mesh.uiSetLatLon(lat, lon); popup("Pozycja zapisana z GPS"); } else popup("Brak pozycji GPS", "wyjdź pod niebo i poczekaj", true);
      } else settingPick(s, i);
      return;
    case S_SET_ADV:
      if (i == 0) popup(the_mesh.advert() ? "Advert wysłany" : "Błąd advertu", "do sąsiadów (bez przekazywania)");
      else if (i == 1) popup(the_mesh.uiAdvertFlood() ? "Advert wysłany" : "Błąd advertu", "do całej sieci");
      else settingPick(s, i);
      return;
    case S_SET_MSG:
      if (i == 2) ask(C_CLEAR_MSGS, "Usunąć wszystkie wiadomości?", "");
      else settingPick(s, i);
      return;
    case S_SET_SYS:
      if (i == 0) detailInfo();
      else if (i == 1) push(S_OTA);
      else if (i == 2) openInput(IN_NAME, _prefs->node_name);
      else if (i == 3) openTime();
      else if (i == 4) ask(C_CLEAR_PW, "Usunąć zapamiętane hasła?", "");
      else if (i == 5) ask(C_REBOOT, "Zrestartować urządzenie?", "");
      else ask(C_FACTORY, "Skasować WSZYSTKO?", "");
      return;
    case S_SET_SCR:
      if (i == 10) openInput(IN_LOCKPIN, "");
      else settingPick(s, i);
      return;
    default: settingPick(s, i); return;               // pozostałe ustawienia: lista wartości do wyboru
  }
}

// ---------------- wybór wartości ustawienia ----------------

void UITask::pickBegin(uint8_t id, const char* title) { _pick_id = id; _pick_n = 0; scopy(_pick_title, sizeof(_pick_title), title); }
void UITask::pickAdd(const char* text) { if (_pick_n < UI_PICK_MAX) scopy(_pick_opt[_pick_n++], sizeof(_pick_opt[0]), text); }
void UITask::pickOpen(int cur) { _pick_cur = cur; push(S_PICK); sel() = cur >= 0 && cur < _pick_n ? cur : 0; }

// otwiera listę wartości dla wiersza ustawień; false = ten wiersz nie ma listy
bool UITask::settingPick(uint8_t s, int i) {
  char t[48];
  auto two = [&](uint8_t id, const char* title, const char* a, const char* b, int cur) { pickBegin(id, title); pickAdd(a); pickAdd(b); pickOpen(cur); };
  auto telem = [&](uint8_t id, const char* title, int cur) { pickBegin(id, title); for (int q = 0; q < 3; q++) pickAdd(TELEM_NAMES[q]); pickOpen(cur % 3); };
  switch (s) {
    case S_RADIO:
      if (i == 0) {
        bool narrow = fabsf(_prefs->freq - 869.618f) < 0.005f && fabsf(_prefs->bw - 62.5f) < 0.1f && _prefs->sf == 8 && _prefs->cr == 8;
        pickBegin(P_PRESET, "Preset radia");
        pickAdd("SF6 — MeshCore Południe"); pickAdd("EU/UK Narrow (SF8)");
        pickOpen(the_mesh.uiRadioIsSF6() ? 0 : narrow ? 1 : -1);
      } else if (i == 2) {
        pickBegin(P_BW, "Pasmo (BW)"); int cur = 2;
        for (int q = 0; q < 6; q++) { snprintf(t, sizeof(t), "%.4g kHz", BW_STEPS[q]); comma(t); pickAdd(t); if (fabsf(BW_STEPS[q] - _rbw) < 0.5f) cur = q; }
        pickOpen(cur);
      } else if (i == 3) { pickBegin(P_SF, "Rozpraszanie (SF)"); for (int q = 5; q <= 12; q++) { snprintf(t, sizeof(t), "SF%d", q); pickAdd(t); } pickOpen(_rsf - 5); }
      else if (i == 4) { pickBegin(P_CR, "Kodowanie (CR)"); for (int q = 5; q <= 8; q++) { snprintf(t, sizeof(t), "4/%d", q); pickAdd(t); } pickOpen(_rcr - 5); }
      else if (i == 6) {
        pickBegin(P_TX, "Moc nadawania"); int cur = 0;
        for (int q = 0; q < (int)sizeof(TX_STEPS); q++) { if (TX_STEPS[q] > the_mesh.uiMaxTxPower()) break; snprintf(t, sizeof(t), "%d dBm", TX_STEPS[q]); pickAdd(t); if (TX_STEPS[q] <= _prefs->tx_power_dbm) cur = q; }
        pickOpen(cur);
      } else if (i == 7) { pickBegin(P_HASH, "Bajty trasy"); pickAdd("1 bajt (identyfikator 2 znaki)"); pickAdd("2 bajty (identyfikator 4 znaki)"); pickAdd("3 bajty (identyfikator 6 znaków)"); pickOpen(_prefs->path_hash_mode % 3); }
      else if (i == 8) { pickBegin(P_AIR, "Limit nadawania"); int cur = 1; for (int q = 0; q < 5; q++) { pickAdd(AIRTIME_NAMES[q]); if (fabsf(AIRTIME_STEPS[q] - _prefs->airtime_factor) < 0.4f) cur = q; } pickOpen(cur); }
      else return false;
      return true;
    case S_SET_BT:
      if (i == 0) { two(P_BT, "Bluetooth", "włączony", "wyłączony", isBluetoothEnabled() ? 0 : 1); return true; }
      if (i == 6) { pickBegin(P_BTAUTO, "Wyłącz Bluetooth bez połączenia"); for (int q = 0; q < 5; q++) pickAdd(BTA_NAMES[q]); pickOpen(_cfg.bt_auto); return true; }
      return false;
    case S_SET_POS:
      if (i == 0) two(P_GPS, "GPS", "włączony", "wyłączony", _prefs->gps_enabled ? 0 : 1);
      else if (i == 1) two(P_ADVLOC, "Pozycja w advercie", "wysyłaj", "nie wysyłaj", _prefs->advert_loc_policy ? 0 : 1);
      else if (i == 6) telem(P_TEL0, "Stan baterii udostępniaj", _prefs->telemetry_mode_base);
      else if (i == 7) telem(P_TEL1, "Pozycję udostępniaj", _prefs->telemetry_mode_loc);
      else if (i == 8) telem(P_TEL2, "Dane czujników udostępniaj", _prefs->telemetry_mode_env);
      else if (i == 9) { pickBegin(P_GPSMODE, "GPS: kiedy szukać pozycji"); for (int q = 0; q < 5; q++) pickAdd(GPS_NAMES[q]); pickOpen(_cfg.gps_mode); }
      else return false;
      return true;
    case S_SET_ADV:
      if (i != 2) return false;
      pickBegin(P_AUTOADV, "Automatyczny advert"); pickAdd("wyłączony");
      for (int q = 1; q < (int)sizeof(ADV_HOURS); q++) { snprintf(t, sizeof(t), "co %d godz. (do sąsiadów)", ADV_HOURS[q]); pickAdd(t); }
      pickOpen(_cfg.adv_idx);
      return true;
    case S_SET_CT: {
      bool sel_mode = _prefs->manual_add_contacts & 1;
      if (i == 0) two(P_AA_MODE, "Dodawanie z advertów", "wszystko automatycznie", "tylko wybrane typy", sel_mode ? 1 : 0);
      else if (i <= 3) { static const char* const T[3] = { "Osoby", "Repeatery", "Pokoje" }; two(P_AA_T0 + (i - 1), T[i - 1], "dodawaj", "nie dodawaj", (!sel_mode || (_prefs->autoadd_config & AA_BITS[i - 1])) ? 0 : 1); }
      else if (i == 4) two(P_AA_FULL, "Gdy brak miejsca", "nadpisuj najstarsze (nieulubione)", "nie dodawaj nowych", (_prefs->autoadd_config & AA_OVERWRITE) ? 0 : 1);
      else {
        pickBegin(P_AA_HOPS, "Z jak daleka dodawać"); pickAdd("bez limitu skoków"); pickAdd("tylko słyszane bezpośrednio");
        for (int q = 2; q < 6; q++) { snprintf(t, sizeof(t), "do %d skoków", q - 1); pickAdd(t); }
        pickOpen(_prefs->autoadd_max_hops < 6 ? _prefs->autoadd_max_hops : 0);
      }
      return true;
    }
    case S_SET_MSG:
      if (i == 0) two(P_SHOWPATH, "Trasa pod wiadomością", "identyfikatory repeaterów", "tylko liczba skoków", _cfg.show_path ? 0 : 1);
      else if (i == 1) two(P_MULTIACK, "Dodatkowe potwierdzenia", "włączone (pewniej, więcej ruchu)", "wyłączone", _prefs->multi_acks ? 0 : 1);
      else return false;
      return true;
    case S_SET_SCR:
      if (i == 0) { pickBegin(P_SLEEP, "Wygaszanie ekranu"); for (int q = 0; q < 4; q++) pickAdd(SLEEP_NAMES[q]); pickOpen(_cfg.sleep_idx); }
      else if (i == 1) two(P_KBLIGHT, "Podświetlenie klawiatury", "świeci, gdy ekran świeci", "wyłączone", _cfg.kb_light ? 0 : 1);
      else if (i == 2) two(P_WAKE, "Nowa wiadomość budzi ekran", "tak", "nie", _cfg.no_wake ? 1 : 0);
      else if (i == 3) { pickBegin(P_LOCK, "Blokada ekranu"); pickAdd("wyłączona"); pickAdd("po wygaszeniu (Wstecz, potem Enter)"); pickAdd("po wygaszeniu i włączeniu, z PIN-em"); pickOpen(_cfg.lock > 2 ? 0 : _cfg.lock); }
      else if (i == 4) { pickBegin(P_BRIGHT, "Jasność ekranu"); for (int q = 0; q < 5; q++) { snprintf(t, sizeof(t), "%d%%", BRIGHT_STEPS[q]); pickAdd(t); } pickOpen((_cfg.bright >= 1 && _cfg.bright <= 5) ? _cfg.bright - 1 : 4); }
      else if (i == 5) two(P_SOUND, "Dźwięki urządzenia", "włączone", "wyciszone (wszystkie)", _prefs->buzzer_quiet ? 1 : 0);
      else if (i == 6) two(P_MSGSND, "Dźwięk wiadomości", "włączony", "wyłączony", _cfg.no_msg_snd ? 1 : 0);
      else if (i == 7) { pickBegin(P_ADVSND, "Dźwięk advertu repeatera"); pickAdd("wyłączony"); pickAdd("każdy repeater"); pickAdd("tylko ulubione"); pickOpen(_cfg.adv_snd < 3 ? _cfg.adv_snd : 0); }
      else if (i == 8) { pickBegin(P_DIM, "Przyciemnianie ekranu"); for (int q = 0; q < 4; q++) pickAdd(DIM_NAMES[q]); pickOpen(_cfg.dim_idx); }
      else if (i == 9) two(P_CPU, "Procesor po wygaszeniu ekranu", "oszczędny (80 MHz)", "pełna moc (240 MHz)", _cfg.cpu_full ? 1 : 0);
      else return false;
      return true;
  }
  return false;
}

void UITask::applyPick(uint8_t id, int i) {
  switch (id) {
    case P_PRESET:
      if (i == 0) { the_mesh.uiSetRadioSF6(); popup("Radio: SF6 — MeshCore Południe", "869,618 MHz · BW 62,5 · SF6 · CR 4/8"); }
      else { the_mesh.uiSetRadio(869.618f, 62.5f, 8, 8); popup("Radio: EU/UK Narrow", "869,618 MHz · BW 62,5 · SF8 · CR 4/8"); }
      radioLoad();
      break;
    case P_BW: _rbw = BW_STEPS[i]; break;             // BW, SF i CR działają dopiero po „Zastosuj”
    case P_SF: _rsf = 5 + i; break;
    case P_CR: _rcr = 5 + i; break;
    case P_TX: the_mesh.uiSetTxPower(TX_STEPS[i]); break;
    case P_HASH: _prefs->path_hash_mode = i; the_mesh.savePrefs(); break;
    case P_AIR: _prefs->airtime_factor = AIRTIME_STEPS[i]; the_mesh.savePrefs(); break;
    case P_BT:
      if (_bt_killed) { popup("Bluetooth wróci po restarcie", "był wyłączony na czas WiFi"); break; }
      _cfg.bt_off = i == 1; saveCfg();               // stan Bluetooth przeżywa restart
      _bt_auto_off = false; _bt_idle_since = 0;
      if (i == 0 && !isBluetoothEnabled()) { char t[40]; snprintf(t, sizeof(t), "PIN do parowania: %06lu", (unsigned long)the_mesh.getBLEPin()); enableBluetooth(); popup("Bluetooth włączony", t, false, 5000); }
      if (i == 1 && isBluetoothEnabled()) { disableBluetooth(); popup("Bluetooth wyłączony"); }
      break;
    case P_GPS: the_mesh.uiSetGps(i == 0); _gps_off_phase = false; _gps_on_at = millis(); _gps_fix_at = 0; break;
    case P_GPSMODE: _cfg.gps_mode = i; saveCfg(); if (_gps_off_phase && _sensors && _prefs->gps_enabled) _sensors->setSettingValue("gps", "1"); _gps_off_phase = false; _gps_on_at = millis(); _gps_fix_at = 0; break;
    case P_DIM: _cfg.dim_idx = i; saveCfg(); break;
    case P_CPU: _cfg.cpu_full = i == 1; saveCfg(); break;
    case P_BTAUTO: _cfg.bt_auto = i; saveCfg(); _bt_idle_since = 0; break;
    case P_SELOLD: {                                  // kontakty z bieżącej listy, których urządzenie nie słyszało od N dni (bez ulubionych)
      static const uint16_t DAYS[3] = { 7, 30, 90 };
      uint32_t now = the_mesh.getRTCClock()->getCurrentTime(), lim = now - (uint32_t)DAYS[i % 3] * 86400UL;
      ContactInfo q; int added = 0;
      buildContacts();
      for (int k = 0; k < _list_n; k++) if (the_mesh.getContactByIdx(_list[k], q) && !(q.flags & 1) && q.lastmod < lim && !selHas(q.id.pub_key)) { selAdd(q.id.pub_key); added++; }
      char t[40]; snprintf(t, sizeof(t), "dodano: %d · ulubione pominięte", added); popup("Zaznaczono", t);
      break;
    }
    case P_ADVLOC: _prefs->advert_loc_policy = i == 0 ? 1 : 0; the_mesh.savePrefs(); break;
    case P_TEL0: _prefs->telemetry_mode_base = i; the_mesh.savePrefs(); break;
    case P_TEL1: _prefs->telemetry_mode_loc = i; the_mesh.savePrefs(); break;
    case P_TEL2: _prefs->telemetry_mode_env = i; the_mesh.savePrefs(); break;
    case P_AUTOADV:
      _cfg.adv_idx = i; saveCfg();
      _next_advert = ADV_HOURS[i] ? millis() + ADV_HOURS[i] * 3600000UL : 0;
      break;
    case P_AA_MODE:
      if (i == 1) { _prefs->manual_add_contacts |= 1; if ((_prefs->autoadd_config & (AA_CHAT | AA_REPEATER | AA_ROOM | AA_SENSOR)) == 0) _prefs->autoadd_config |= AA_CHAT | AA_REPEATER | AA_ROOM | AA_SENSOR; }
      else _prefs->manual_add_contacts &= ~1;
      the_mesh.savePrefs();
      break;
    case P_AA_T0: case P_AA_T1: case P_AA_T2: case P_AA_T3: {
      uint8_t bit = AA_BITS[id - P_AA_T0];
      if (!(_prefs->manual_add_contacts & 1)) { _prefs->manual_add_contacts |= 1; _prefs->autoadd_config |= AA_CHAT | AA_REPEATER | AA_ROOM | AA_SENSOR; }   // przejście na „wybrane typy”
      if (i == 0) _prefs->autoadd_config |= bit; else _prefs->autoadd_config &= ~bit;
      the_mesh.savePrefs();
      break;
    }
    case P_AA_FULL: if (i == 0) _prefs->autoadd_config |= AA_OVERWRITE; else _prefs->autoadd_config &= ~AA_OVERWRITE; the_mesh.savePrefs(); break;
    case P_AA_HOPS: _prefs->autoadd_max_hops = i; the_mesh.savePrefs(); break;
    case P_SHOWPATH: _cfg.show_path = i == 0; saveCfg(); break;
    case P_MULTIACK: _prefs->multi_acks = i == 0 ? 1 : 0; the_mesh.savePrefs(); break;
    case P_SOUND:
      _prefs->buzzer_quiet = i == 0 ? 0 : 1; the_mesh.savePrefs(); _buzzer.quiet(_prefs->buzzer_quiet);
      if (!_prefs->buzzer_quiet) _buzzer.play("dm:d=16,o=6,b=200:e,g,c7");
      break;
    case P_SLEEP: _cfg.sleep_idx = i; saveCfg(); wake(); break;
    case P_KBLIGHT: _cfg.kb_light = i == 0; saveCfg(); _kb.setBacklight(_cfg.kb_light != 0); break;
    case P_WAKE: _cfg.no_wake = i == 1; saveCfg(); break;
    case P_RPTSORT: _cfg.rpt_sort = i; saveCfg(); break;
    case P_MSGSND: _cfg.no_msg_snd = i == 1; saveCfg(); if (i == 0 && !_prefs->buzzer_quiet) _buzzer.play("dm:d=16,o=6,b=200:e,g,c7"); break;
    case P_ADVSND: _cfg.adv_snd = i; saveCfg(); if (i > 0 && !_prefs->buzzer_quiet) _buzzer.play("adv:d=32,o=7,b=160:g,p,g"); break;
    case P_MAPRANGE: _cfg.map_range = i; saveCfg(); break;
    case P_BRIGHT: _cfg.bright = i + 1; saveCfg(); m9lcdBacklight(brightPercent()); break;
    case P_LOCK:
      if (i == 2 && !_pin_have) { openInput(IN_LOCKPIN, ""); break; }      // najpierw trzeba ustawić PIN
      _cfg.lock = (uint8_t)i; saveCfg();
      if (i == 1) popup("Blokada włączona", "odblokowanie: Wstecz, potem Enter", false, 5000);
      if (i == 2) popup("Blokada z PIN-em włączona", "także po włączeniu urządzenia", false, 5000);
      break;
  }
  _dirty = true;
}

// Del i Pinezka na listach głównych, chodzenie po filtrach; true = klawisz obsłużony
bool UITask::listKey(uint8_t s, int i, uint8_t k) {
  int dir = k == M9K_LEFT ? -1 : k == M9K_RIGHT ? 1 : 0;
  switch (s) {
    case S_MSGS:
      if (k == M9K_F3) { openMenuThread(i < _thread_n ? i : -1); return true; }
      if ((k == M9K_BKSP || k == M9K_BKSP_LONG) && i < _thread_n && _threads[i].chan) { ask(C_DEL_CHANNEL, "Usunąć kanał?", _threads[i].name); return true; }
      return false;
    case S_CONTACTS:
      if (i == 0 && dir) {                            // chodzenie po filtrach; z brzegu przechodzi na sąsiedni ekran
        int f = (int)_cfg.ct_filter + dir;
        if (f < 0 || f > FILTER_HEARD) return false;
        _cfg.ct_filter = (uint8_t)f; saveCfg(); buildContacts(); return true;
      }
      if (k == M9K_F3 && _sel_mode) {                 // Menu w trybie zaznaczania
        _menu_n = 0; _menu_msg = _menu_thread = false; snprintf(_menu_title, sizeof(_menu_title), "Zaznaczone: %d", _sel_n);
        if (_sel_n > 0) _menu_act[_menu_n++] = A_SELDEL;
        if (_cfg.ct_filter != FILTER_HEARD) { _menu_act[_menu_n++] = A_SELALL; _menu_act[_menu_n++] = A_SELOLD; }
        if (_sel_n > 0) _menu_act[_menu_n++] = A_SELNONE;
        push(S_MENU);
        return true;
      }
      if (k == M9K_F3) { if (i > 0) listEnter(s, i); return true; }
      if (k == M9K_BKSP) { int n = strlen(_search); if (n > 0) { _search[n - 1] = 0; buildContacts(); } return true; }
      if (k == M9K_BKSP_LONG) { _search[0] = 0; buildContacts(); return true; }
      if (k > 32 && k < 127 && _cfg.ct_filter != FILTER_HEARD) { int n = strlen(_search); if (n < (int)sizeof(_search) - 1) { _search[n] = (char)k; _search[n + 1] = 0; buildContacts(); if (sel() > 1) sel() = 1; } return true; }
      return false;
    case S_RPTS:
      if (k == M9K_F3) { pickBegin(P_RPTSORT, "Sortowanie repeaterów"); pickAdd("ostatnio słyszane"); pickAdd("wg odległości (potrzebny GPS)"); pickOpen(_cfg.rpt_sort ? 1 : 0); return true; }
      return false;
  }
  if (s >= S_RADIO && s <= S_MAP_NET && dir) return true;   // w ustawieniach ◄ ► nic nie zmieniają (wartości wybiera się z listy)
  return false;
}

// ---------------- klawisze ----------------

// siatka kafelków: strzałki chodzą po kafelkach; z lewej i prawej krawędzi przechodzi się na sąsiedni ekran
bool UITask::gridKey(uint8_t k, int count, int cols) {
  int16_t& i = sel();
  if (i >= count) i = count - 1;
  int col = i % cols;
  if (k == M9K_LEFT) { if (col == 0) return false; i--; return true; }
  if (k == M9K_RIGHT) { if (col == cols - 1 || i + 1 >= count) return false; i++; return true; }
  if (k == M9K_UP) { if (i - cols >= 0) i -= cols; return true; }
  if (k == M9K_DOWN) { if (i + cols < count) { i += cols; return true; } return false; }    // z dolnego rzędu ▼ schodzi na pasek
  return false;
}

// ekran blokady: odblokowuje Wstecz, a zaraz po nim Enter
void UITask::lockKey(uint8_t k) {
  unsigned long now = millis();
  _dirty = true;
  if (_cfg.lock == 2 && _pin_have) {                 // blokada z PIN-em: cyfry, Del kasuje, Enter sprawdza
    int n = strlen(_pin_in);
    if (k >= '0' && k <= '9') { if (n < 8) { _pin_in[n] = (char)k; _pin_in[n + 1] = 0; } return; }
    if (k == M9K_BKSP || k == M9K_BKSP_LONG || k == M9K_BACK) { if (k == M9K_BKSP && n > 0) _pin_in[n - 1] = 0; else _pin_in[0] = 0; return; }
    if (k != M9K_ENTER || n == 0) return;
    if (_pin_wait && (long)(now - _pin_wait) < 0) { _pin_in[0] = 0; return; }      // kara za złe próby jeszcze trwa
    uint8_t h[32];
    the_mesh.uiPinHash(h, _pin_salt, _pin_in);
    memset(_pin_in, 0, sizeof(_pin_in));
    uint8_t diff = 0; for (int i = 0; i < 32; i++) diff |= h[i] ^ _pin_hash[i];
    if (diff == 0) {
      _locked = false; _pin_wait = 0;
      if (_pin_fails) { _pin_fails = 0; savePin(); }
      _kb.setBacklight(_cfg.kb_light != 0);
    } else {
      if (_pin_fails < 250) _pin_fails++;
      savePin();                                       // licznik złych prób przeżywa restart
      pinPenalty(now);
    }
    return;
  }
  if (k == M9K_BACK) { _ul_back = true; _ul_last = now; }
  else if (k == M9K_ENTER && _ul_back && now - _ul_last < 4000) {
    _locked = false; _ul_back = false;
    _kb.setBacklight(_cfg.kb_light != 0);
  } else _ul_back = false;
  _dirty = true;
}

// WiFi i Bluetooth naraz nie mieszczą się w pamięci urządzenia: przed WiFi Bluetooth jest wyłączany i wraca po restarcie
void UITask::wifiPrepare() {
  if (_bt_killed) return;
  if (isBluetoothEnabled()) disableBluetooth();
  m9bleShutdown();
  _bt_killed = true;
}

// ---------------- data i godzina ----------------

static int daysInMonth(int Y, int M) {
  static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (M == 2 && ((Y % 4 == 0 && Y % 100 != 0) || Y % 400 == 0)) return 29;
  return D[(M - 1) % 12];
}

// ustawia zegar urządzenia z czasu polskiego (zegar wewnątrz chodzi w UTC)
void UITask::setLocalTime(int Y, int M, int D, int h, int mi) {
  int y = Y - (M <= 2), era = y / 400, yoe = y - era * 400, mp = (M + 9) % 12;
  int doy = (153 * mp + 2) / 5 + D - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + doe - 719468;
  uint32_t local = (uint32_t)(days * 86400L + h * 3600L + mi * 60L);
  uint32_t utc = local - 3600;
  struct tm chk; localTime(utc, &chk);
  if (chk.tm_hour != h) utc -= 3600;                 // czas letni
  the_mesh.getRTCClock()->setCurrentTime(utc);
}

void UITask::openTime() {
  struct tm lt; localTime(the_mesh.getRTCClock()->getCurrentTime(), &lt);
  _tf[0] = lt.tm_year + 1900; _tf[1] = lt.tm_mon + 1; _tf[2] = lt.tm_mday; _tf[3] = lt.tm_hour; _tf[4] = lt.tm_min;
  if (_tf[0] < 2025) _tf[0] = 2026;
  _tf_sel = 0;
  push(S_TIME);
}

void UITask::keyTime(uint8_t k) {
  static const int LO[5] = { 2025, 1, 1, 0, 0 }, HI[5] = { 2099, 12, 31, 23, 59 };
  if (k == M9K_LEFT) { _tf_sel = (_tf_sel + 4) % 5; return; }
  if (k == M9K_RIGHT) { _tf_sel = (_tf_sel + 1) % 5; return; }
  if (k == M9K_UP || k == M9K_DOWN) {
    int hi = _tf_sel == 2 ? daysInMonth(_tf[0], _tf[1]) : HI[_tf_sel];
    int v = _tf[_tf_sel] + (k == M9K_UP ? 1 : -1);
    if (v > hi) v = LO[_tf_sel];
    if (v < LO[_tf_sel]) v = hi;
    _tf[_tf_sel] = v;
    int dm = daysInMonth(_tf[0], _tf[1]);
    if (_tf[2] > dm) _tf[2] = dm;                    // np. 31 → 30 po zmianie miesiąca
    return;
  }
  if (k == M9K_F3) {                                 // Menu: zegar z GPS
    if (timeFromGps()) { pop(); popup("Zegar ustawiony z GPS"); } else popup("GPS nie ma jeszcze czasu", "włącz GPS i wyjdź pod niebo", true);
    return;
  }
  if (k == M9K_ENTER) {
    setLocalTime(_tf[0], _tf[1], _tf[2], _tf[3], _tf[4]);
    char t[32]; snprintf(t, sizeof(t), "%d.%02d.%04d %d:%02d", _tf[2], _tf[1], _tf[0], _tf[3], _tf[4]);
    pop();
    popup("Zegar ustawiony", t);
  }
}

void UITask::fillTime() {
  UiView& v = *_v;
  v.screen = UIS_TIME; v.tab = -1;
  strcpy(v.title, "Data i godzina"); strcpy(v.title_right, "czas polski");
  static const char* const L[5] = { "rok", "miesiąc", "dzień", "godz.", "min" };
  for (int i = 0; i < 5; i++) { strcpy(v.tile[i].label, L[i]); snprintf(v.tile[i].value, sizeof(v.tile[i].value), i == 0 ? "%d" : "%02d", _tf[i]); }
  v.ntiles = 5; v.sel = _tf_sel;
  strcpy(v.line1, "Zegar ustawia się też sam: z aplikacji w telefonie i z GPS.");
  static const char* const H[4][2] = { { "◄►", "pole" }, { "▲▼", "zmień" }, { "Enter", "zapisz" }, { "Menu", "z GPS" } };
  for (int i = 0; i < 4; i++) { strcpy(v.hint[i].key, H[i][0]); strcpy(v.hint[i].txt, H[i][1]); }
  v.nhints = 4;
}

// ---------------- trasa wiadomości na mapie ----------------

// repeater o identyfikatorze z trasy (początek klucza); przy kilku pasujących — najbliższy poprzedniego punktu
bool UITask::findRepeater(const uint8_t* id, int n, double plat, double plon, bool have_prev, ContactInfo& out) {
  ContactInfo c;
  bool found = false;
  double best = 1e18;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total; i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_REPEATER) continue;
    if (memcmp(c.id.pub_key, id, n) != 0 || (c.gps_lat == 0 && c.gps_lon == 0)) continue;
    double d = have_prev ? distKm(plat, plon, c.gps_lat / 1e6, c.gps_lon / 1e6) : 0;
    if (!found || d < best) { best = d; out = c; found = true; }
  }
  return found;
}

bool UITask::buildRoute(int idx) {
  if (idx < 0 || idx >= _msg_n) return false;
  UiMsg& m = _msgs[idx];
  _route_n = 0; _route_unknown = 0; _route_star = false;
  auto add = [&](double lat, double lon, const char* label, uint8_t kind) {
    if (_route_n >= (int)(sizeof(_route) / sizeof(_route[0]))) return;
    RoutePt& p = _route[_route_n++];
    p.lat = lat; p.lon = lon; p.kind = kind; scopy(p.label, sizeof(p.label), label);
  };
  // moja pozycja: GPS, a bez niego pozycja zapisana w ustawieniach
  double mlat = 0, mlon = 0; int sats;
  bool me = gpsFix(&mlat, &mlon, &sats);
  if (!me && (_sensors->node_lat != 0 || _sensors->node_lon != 0)) { mlat = _sensors->node_lat; mlon = _sensors->node_lon; me = true; }
  ContactInfo c;
  char lab[24];

  if (m.out) {                                       // wysłana na kanale: ja i repeatery, które ją powtórzyły (gwiazda)
    _route_star = true;
    if (me) add(mlat, mlon, "ja", UIM_ME);
    for (int i = 0; i < m.rep_n && i < 6; i++) {
      if (findRepeater(m.rep_id[i], m.rep_hs, mlat, mlon, me, c)) { snprintf(lab, sizeof(lab), "%02X%02X", c.id.pub_key[0], c.id.pub_key[1]); add(c.gps_lat / 1e6, c.gps_lon / 1e6, lab, UIM_RPT); }
      else _route_unknown++;
    }
    snprintf(_route_title, sizeof(_route_title), "powtórzyli: %d", m.rep_n);
  } else {                                           // odebrana: nadawca → kolejne repeatery → ja
    const char* who = m.chan ? m.who : m.thread;
    bool sender = false;
    int total = the_mesh.getTotalContactSlots();
    for (int i = MAX_ANON_CONTACTS; i < total && who[0]; i++) {
      if (the_mesh.getContactByIdx(i, c) && c.type != ADV_TYPE_REPEATER && strcmp(c.name, who) == 0 && (c.gps_lat || c.gps_lon)) {
        snprintf(lab, sizeof(lab), "%.10s", c.name);
        add(c.gps_lat / 1e6, c.gps_lon / 1e6, lab, UIM_PERSON); sender = true; break;
      }
    }
    if (!sender) _route_unknown++;
    int hs = m.hash_size ? m.hash_size : 1, cnt = m.path_bytes / hs;
    double plat = sender ? _route[0].lat : 0, plon = sender ? _route[0].lon : 0;
    bool prev = sender;
    for (int h = 0; h < cnt; h++) {
      if (findRepeater(&m.path[h * hs], hs, plat, plon, prev, c)) {
        snprintf(lab, sizeof(lab), "%d %02X%02X", h + 1, c.id.pub_key[0], c.id.pub_key[1]);
        add(c.gps_lat / 1e6, c.gps_lon / 1e6, lab, UIM_RPT);
        plat = c.gps_lat / 1e6; plon = c.gps_lon / 1e6; prev = true;
      } else _route_unknown++;
    }
    if (m.hops > cnt) _route_unknown += m.hops - cnt;
    if (me) add(mlat, mlon, "ja", UIM_ME);
    char h[24]; hopsTxt(h, sizeof(h), m.hops, m.direct);
    snprintf(_route_title, sizeof(_route_title), "%s", h);
  }
  if (_route_n < 2) {
    _route_n = 0;
    popup("Za mało punktów z pozycją", "repeatery z trasy nie podają pozycji albo nie ma ich w kontaktach", true, 5000);
    return false;
  }
  return true;
}

// dobiera środek i zoom tak, żeby cała trasa zmieściła się na ekranie
void UITask::fitRoute() {
  if (_route_n == 0) return;
  double la0 = _route[0].lat, la1 = la0, lo0 = _route[0].lon, lo1 = lo0;
  for (int i = 1; i < _route_n; i++) {
    if (_route[i].lat < la0) la0 = _route[i].lat; if (_route[i].lat > la1) la1 = _route[i].lat;
    if (_route[i].lon < lo0) lo0 = _route[i].lon; if (_route[i].lon > lo1) lo1 = _route[i].lon;
  }
  _map_lat = (la0 + la1) / 2; _map_lon = (lo0 + lo1) / 2;
  _map_follow = false;
  int lo = _map.numPacks() ? _map.minZoom() : 6, hi = _map.numPacks() ? _map.maxZoom() : 13;
  _map_zoom = lo;
  for (int z = hi; z >= lo; z--) {
    if (_map.numPacks() && !_map.hasZoom(z)) continue;
    double x0, y0, x1, y1;
    mcmLatLonToPixel(la1, lo0, z, &x0, &y0);
    mcmLatLonToPixel(la0, lo1, z, &x1, &y1);
    if (x1 - x0 <= GFX_W - 110 && y1 - y0 <= UI_MAP_H - 70) { _map_zoom = z; break; }
  }
}

// ---------------- mapa ----------------

void UITask::openMap() {
  if (_depth >= 1 && _nav[0].scr < 5) _tabsel[_nav[0].scr] = _nav[0].sel;
  _depth = 1; _bar = false;
  _nav[0].scr = S_MAP; _nav[0].sel = 0; _nav[0].top = 0;
  if (!_map.hasZoom(_map_zoom) && _map.numPacks() > 0) { _map_zoom = 12; while (!_map.hasZoom(_map_zoom) && _map_zoom > _map.minZoom()) _map_zoom--; }
  _dirty = true;
}

void UITask::keyMap(uint8_t k) {
  double px, py;
  mcmLatLonToPixel(_map_lat, _map_lon, _map_zoom, &px, &py);
  int dx = 0, dy = 0;
  if (k == M9K_LEFT) dx = -80;
  if (k == M9K_RIGHT) dx = 80;
  if (k == M9K_UP) dy = -60;
  if (k == M9K_DOWN) dy = 60;
  if (dx || dy) { mcmPixelToLatLon(px + dx, py + dy, _map_zoom, &_map_lat, &_map_lon); _map_follow = false; return; }
  int lo = _map.numPacks() ? _map.minZoom() : 6, hi = _map.numPacks() ? _map.maxZoom() : 15;
  if (k == 'i' || k == 'I' || k == '+') { for (int z = _map_zoom + 1; z <= hi; z++) if (_map.numPacks() == 0 || _map.hasZoom(z)) { _map_zoom = z; break; } return; }
  if (k == 'o' || k == 'O' || k == '-') { for (int z = _map_zoom - 1; z >= lo; z--) if (_map.numPacks() == 0 || _map.hasZoom(z)) { _map_zoom = z; break; } return; }
  if (k == M9K_ENTER) { if (_route_n) fitRoute(); else _map_follow = true; return; }
  if (k == M9K_F3) { if (!_route_n) push(S_MAP_NET); return; }
}

void UITask::drawMap() {
  UiView& v = *_v;
  memset(&v, 0, sizeof(v));
  fillStatus();
  v.tab = -1;
  static const char* const H[4][2] = { { "I", "bliżej" }, { "O", "dalej" }, { "Enter", "do mnie" }, { "Menu", "pobierz" } };
  static const char* const HR[4][2] = { { "I", "bliżej" }, { "O", "dalej" }, { "Enter", "cała trasa" }, { "Wstecz", "koniec" } };
  for (int i = 0; i < 4; i++) { strcpy(v.hint[i].key, (_route_n ? HR : H)[i][0]); strcpy(v.hint[i].txt, (_route_n ? HR : H)[i][1]); }
  v.nhints = 4;
  if (_popup_until) { v.popup = true; v.popup_danger = _pop_danger; scopy(v.popup1, sizeof(v.popup1), _pop1); scopy(v.popup2, sizeof(v.popup2), _pop2); }

  double lat = 0, lon = 0; int sats = 0;
  bool fix = gpsFix(&lat, &lon, &sats);
  if (fix && _map_follow) { _map_lat = lat; _map_lon = lon; }

  Gfx g(_fb);
  g.clear(uiColorBg());
  int tiles = 0;
  if (_map_ready) {
    tiles = _map.render(_map_lat, _map_lon, _map_zoom);
    // podkład: środek bufora mapy (320x216, bajty w kolejności panelu) trafia w środek pola mapy na ekranie
    const uint16_t* src = _map.fb() + ((M9MAP_H - UI_MAP_H) / 2) * M9MAP_W;
    uint16_t* dst = _fb + UI_MAP_TOP * GFX_W;
    for (int i = 0; i < UI_MAP_H * GFX_W; i++) { uint16_t p = src[i]; dst[i] = (uint16_t)((p >> 8) | (p << 8)); }
  }

  // znaczniki: repeatery i osoby z pozycją, na końcu ja
  double cx, cy;
  mcmLatLonToPixel(_map_lat, _map_lon, _map_zoom, &cx, &cy);
  const int mx = GFX_W / 2, my = UI_MAP_TOP + UI_MAP_H / 2;
  bool names = _map_zoom >= 11 && !_route_n;
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots(), drawn = 0;
  if (_route_n) {                                    // trasa: linie, potem punkty z podpisami
    int px[16], py[16];
    for (int i = 0; i < _route_n; i++) {
      double qx, qy;
      mcmLatLonToPixel(_route[i].lat, _route[i].lon, _map_zoom, &qx, &qy);
      px[i] = mx + (int)(qx - cx); py[i] = my + (int)(qy - cy);
    }
    for (int i = 1; i < _route_n; i++) {
      int a = _route_star ? 0 : i - 1;
      uiMapLine(g, px[a], py[a], px[i], py[i]);
    }
    for (int i = 0; i < _route_n; i++) uiMapMarker(g, px[i], py[i], _route[i].kind, _route[i].label);
    drawn = 999;                                     // bez pozostałych znaczników
  }
  for (int pass = 0; pass < 2 && !_route_n; pass++) {                   // najpierw zwykłe, potem ulubione (na wierzchu)
    for (int i = MAX_ANON_CONTACTS; i < total && drawn < 60; i++) {
      if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE || c.type == ADV_TYPE_SENSOR) continue;
      if (c.gps_lat == 0 && c.gps_lon == 0) continue;
      bool fav = c.flags & 1;
      if ((pass == 1) != fav) continue;
      double qx, qy;
      mcmLatLonToPixel(c.gps_lat / 1e6, c.gps_lon / 1e6, _map_zoom, &qx, &qy);
      int x = mx + (int)(qx - cx), y = my + (int)(qy - cy);
      if (x < 8 || x > GFX_W - 8 || y < UI_MAP_TOP + 10 || y > UI_MAP_TOP + UI_MAP_H - 10) continue;
      char lab[16];
      if (c.type == ADV_TYPE_REPEATER) snprintf(lab, sizeof(lab), "%02X%02X", c.id.pub_key[0], c.id.pub_key[1]);
      else snprintf(lab, sizeof(lab), "%.10s", c.name);
      uiMapMarker(g, x, y, c.type == ADV_TYPE_REPEATER ? (fav ? UIM_RPT_FAV : UIM_RPT) : UIM_PERSON, names || fav ? lab : "");
      drawn++;
    }
  }
  if (fix && !_route_n) {
    double qx, qy;
    mcmLatLonToPixel(lat, lon, _map_zoom, &qx, &qy);
    int x = mx + (int)(qx - cx), y = my + (int)(qy - cy);
    if (x >= 8 && x <= GFX_W - 8 && y >= UI_MAP_TOP + 10 && y <= UI_MAP_TOP + UI_MAP_H - 10) uiMapMarker(g, x, y, UIM_ME, "");
  }
  if ((!fix || !_map_follow) && !_route_n) uiMapMarker(g, mx, my, UIM_CROSS, "");

  char tr[24], bl[40], note[160];
  snprintf(tr, sizeof(tr), "zoom %d", _map_zoom);
  snprintf(bl, sizeof(bl), "%.4f, %.4f%s", _map_lat, _map_lon, fix ? (_map_follow ? " · GPS" : "") : " · bez GPS");
  note[0] = 0;
  if (!_map.sdOk()) strcpy(note, _sd_pending ? "Sprawdzam kartę SD…" : "Brak karty SD. Bez niej widać tylko znaczniki na pustym tle.");
  else if (_map.numPacks() == 0) strcpy(note, "Na karcie nie ma jeszcze mapy. Klawisz Menu: pobierz z internetu albo wgraj z telefonu.");
  else if (tiles == 0) strcpy(note, "Dla tego miejsca i zoomu nie ma mapy na karcie.");
  if (_route_n) {
    if (_route_unknown) snprintf(bl, sizeof(bl), "trasa: %s · bez pozycji: %d", _route_title, _route_unknown);
    else snprintf(bl, sizeof(bl), "trasa: %s", _route_title);
    if (tiles > 0 || _map.numPacks() > 0) note[0] = 0;    // na trasie okienko u góry zasłaniałoby punkty na północy
  }
  uiMapLabels(g, tr, bl, note);
  uiRenderOverlay(g, v);
  g.swapBytes();
  m9lcdBlit(0, 0, GFX_W, GFX_H, _fb);
}

void UITask::key(uint8_t k) {
  if (_locked) { lockKey(k); return; }
  if (_wait_kind) {                                   // czekamy na odpowiedź repeatera: tylko Wstecz przerywa
    if (k == M9K_BACK) { _wait_kind = 0; _popup_until = 0; the_mesh.ui_admin.cli_wait = false; }
    return;
  }
  if (_confirm) {                                     // pytanie w okienku: Enter potwierdza, inny klawisz anuluje
    uint8_t what = _confirm;
    _confirm = C_NONE; _popup_until = 0;
    if (k == M9K_ENTER) doConfirm(what);
    return;
  }
  if (_popup_until) _popup_until = 0;                 // dowolny klawisz zamyka okienko (i działa dalej)
  uint8_t s = scr();
  if (s == S_BOOT) { _boot_until = 0; tab(S_START); return; }

  // klawisze funkcyjne działają wszędzie
  if (k == M9K_HOME) { _tabsel[S_START] = 0; if (scr() == S_START) sel() = 0; tab(S_START); sel() = 0; return; }
  if (k == M9K_MSG)  { tab(S_MSGS); return; }
  if (k == M9K_F4)   { tab(S_RPTS); return; }           // pinezka: lista repeaterów
  if (k == M9K_MAP)  { openMap(); return; }             // mapa z karty SD
  // M9K_F3 (mała wiadomość pod Home) = klawisz „Menu”: dodatkowe opcje na bieżącym ekranie
  if (k == M9K_BACK) { if (_bar) _bar = false; else back(); return; }

  if (_bar) {                                         // chodzenie po dolnym pasku
    if (k == M9K_LEFT || k == M9K_RIGHT) {            // przejście po pasku od razu pokazuje wybrany ekran
      uint8_t t = (uint8_t)((_bar_sel + (k == M9K_LEFT ? 4 : 1)) % 5);
      tab(t);
      _bar = true; _bar_sel = (int8_t)t;
    } else if (k == M9K_UP || k == M9K_ENTER) _bar = false;       // wejście w zawartość ekranu
    return;
  }

  if (s == S_THREAD) { keyThread(k); return; }
  if (s == S_MAP) { keyMap(k); return; }
  if (s == S_TIME) { keyTime(k); return; }
  if (s == S_SPEC) { keySpec(k); return; }
  if (s == S_LOG) { keyLog(k); return; }
  if (s == S_CHINFO) {
    if (k == M9K_ENTER) push(S_QR);
    else if (k == M9K_F3) { buildThreads(); for (int i = 0; i < _thread_n; i++) if (_threads[i].chan && strcmp(_threads[i].name, _tname) == 0) { openMenuThread(i); break; } }
    return;
  }
  if (s == S_QR) return;
  if (s == S_KV && _kv_pkt) {                          // szczegóły pakietu: ◄► poprzedni / następny, Menu: mapa
    if (k == M9K_LEFT || k == M9K_RIGHT) {
      buildLog();
      for (int i = 0; i < _lg_n; i++) if (m9logGet(_lg_idx[i])->seq == _kv_pkt) {
        int j = i + (k == M9K_RIGHT ? 1 : -1);
        if (j >= 0 && j < _lg_n) { detailPkt(m9logGet(_lg_idx[j])->seq); if (_depth >= 2 && _nav[_depth - 2].scr == S_LOG) _nav[_depth - 2].sel = j; }
        break;
      }
      return;
    }
    if (k == M9K_F3 && _chas_pos) {
      _map_lat = _clat; _map_lon = _clon; _map_follow = false;
      if (_map_zoom < 12) _map_zoom = 13;
      while (!_map.hasZoom(_map_zoom) && _map_zoom > _map.minZoom()) _map_zoom--;
      openMap();
      return;
    }
  }
  if (s == S_KV) {
    int16_t& top = _nav[_depth - 1].top;
    if (k == M9K_UP && top > 0) top--;
    if (k == M9K_DOWN && top + KV_VISIBLE < _kv_n) top++;
    if (k == M9K_ENTER && _kv_more) {                 // sąsiedzi: następna porcja
      if (the_mesh.uiAdminNeighbours(_nb_got)) adminWait(3); else popup("Nie udało się wysłać", "", true);
      return;
    }
    if (k == M9K_ENTER || k == M9K_LEFT) back();
    return;
  }
  if (s == S_START) {
    if (gridKey(k, 6, 3)) return;
    if (k == M9K_DOWN) { _bar = true; _bar_sel = S_START; return; }
    if (k == M9K_LEFT) { tab(S_SETTINGS); return; }
    if (k == M9K_RIGHT) { tab(S_MSGS); return; }
    if (k == M9K_F3) {                                 // Menu na Start: Widmo i log pakietów
      _menu_n = 0; _menu_msg = _menu_thread = false; strcpy(_menu_title, "Start");
      _menu_act[_menu_n++] = A_SPEC; _menu_act[_menu_n++] = A_LOG;
      push(S_MENU); return;
    }
    if (k == M9K_ENTER) {
      switch (sel()) {
        case 0: tab(S_MSGS); break;
        case 1: tab(S_CONTACTS); break;
        case 2: tab(S_SETTINGS); sel() = 0; radioLoad(); push(S_RADIO); break;
        case 3: tab(S_SETTINGS); sel() = 1; push(S_SET_BT); break;
        case 4: push(S_SPEC); break;
        case 5: detailBattery(); break;
      }
    }
    return;
  }
  if (s == S_SETTINGS) {
    if (gridKey(k, 8, 2)) return;
    if (k == M9K_DOWN) { _bar = true; _bar_sel = S_SETTINGS; return; }
    if (k == M9K_LEFT) { tab(S_RPTS); return; }
    if (k == M9K_RIGHT) { tab(S_START); return; }
    if (k == M9K_ENTER) {
      static const uint8_t TO[8] = { S_RADIO, S_SET_BT, S_SET_POS, S_SET_ADV, S_SET_CT, S_SET_MSG, S_SET_SCR, S_SET_SYS };
      if (sel() == 0) radioLoad();
      push(TO[sel()]);
    }
    return;
  }

  int count = listCount(s);
  int16_t& i = sel();
  if (i >= count) i = count > 0 ? count - 1 : 0;
  if (k == M9K_UP) { if (i > 0) i--; return; }
  if (k == M9K_DOWN) {
    if (i < count - 1) i++;
    else if (s < 5 && _depth == 1) { _bar = true; _bar_sel = (int8_t)s; }      // z ostatniego wiersza ▼ schodzi na pasek
    return;
  }
  if (listKey(s, i, k)) return;
  if (s < 5 && _depth == 1) {                         // ekrany z zakładkami: ◄ ► zmieniają ekran
    if (k == M9K_LEFT) { tab((s + 4) % 5); return; }
    if (k == M9K_RIGHT) { tab((s + 1) % 5); return; }
  }
  if (k == M9K_ENTER && count > 0) listEnter(s, i);
}

// ---------------- dane na ekran ----------------

bool UITask::gpsFix(double* lat, double* lon, int* sats) {
  LocationProvider* lp = _sensors ? _sensors->getLocationProvider() : NULL;
  *sats = 0;
  if (lp == NULL) return false;
  *sats = (int)lp->satellitesCount();
  if (!lp->isValid()) {                              // GPS oszczędny (chwilowo wyłączony): ostatnia znana pozycja
    if (_gps_off_phase && _gps_cache) { *lat = _gps_lat; *lon = _gps_lon; return true; }
    return false;
  }
  *lat = lp->getLatitude() / 1000000.0;
  *lon = lp->getLongitude() / 1000000.0;
  _gps_lat = *lat; _gps_lon = *lon; _gps_cache = true;
  return true;
}

void UITask::fmtAgo(char* dest, size_t n, uint32_t ts) {
  uint32_t now = the_mesh.getRTCClock()->getCurrentTime();
  if (ts == 0 || ts > now + 300) { scopy(dest, n, "—"); return; }
  uint32_t d = now > ts ? now - ts : 0;
  if (d < 60) scopy(dest, n, "teraz");
  else if (d < 3600) snprintf(dest, n, "%lu min", (unsigned long)(d / 60));
  else if (d < 86400) snprintf(dest, n, "%lu godz.", (unsigned long)(d / 3600));
  else snprintf(dest, n, "%lu dni", (unsigned long)(d / 86400));
}

void UITask::fmtClock(char* dest, size_t n, uint32_t ts, bool date_if_old) {
  struct tm t; localTime(ts, &t);
  uint32_t now = the_mesh.getRTCClock()->getCurrentTime();
  if (date_if_old && now > ts && now - ts > 20 * 3600) snprintf(dest, n, "%d.%02d", t.tm_mday, t.tm_mon + 1);
  else snprintf(dest, n, "%d:%02d", t.tm_hour, t.tm_min);
}

void UITask::fillStatus() {
  UiView& v = *_v;
  fmtClock(v.time, sizeof(v.time), the_mesh.getRTCClock()->getCurrentTime(), false);
  v.unread = unreadTotal();
  double lat, lon; int sats;
  gpsFix(&lat, &lon, &sats);
  v.sats = sats;
  v.gps_on = _prefs->gps_enabled != 0;
  v.bt = isBluetoothEnabled() ? (hasConnection() ? 2 : 1) : 0;
  v.wifi = (uint8_t)(m9wifiBars() + 1);
  v.batt = battPercent(getBattMilliVolts());
  v.charging = digitalRead(M9_PIN_EXT_PWR) == LOW;
}

void UITask::fillStart() {
  UiView& v = *_v;
  v.screen = UIS_START; v.tab = 0;
  v.grid_cols = 3; v.ntiles = 6; v.sel = sel();
  scopy(v.big, sizeof(v.big), v.time);
  scopy(v.name, sizeof(v.name), _prefs->node_name);
  static const char* const DNI[7] = { "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota" };
  struct tm lt; localTime(the_mesh.getRTCClock()->getCurrentTime(), &lt);
  snprintf(v.line1, sizeof(v.line1), "%s, %d.%02d.%04d", DNI[lt.tm_wday % 7], lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);

  buildThreads();
  int unread = v.unread;
  UiTile& a = v.tile[0];
  strcpy(a.label, "Wiadomości"); a.icon = UII_MAIL; a.accent = unread > 0;
  if (unread > 0) snprintf(a.value, sizeof(a.value), unread == 1 ? "1 nowa" : "%d nowe", unread); else strcpy(a.value, "brak nowych");
  if (_thread_n > 0 && _threads[0].unread) scopy(a.sub, sizeof(a.sub), _threads[0].name);
  else snprintf(a.sub, sizeof(a.sub), "rozmów: %d", _thread_n);

  // wszystkie kontakty (bez czujników), osobno policzone repeatery
  int people = 0, rpts = 0;
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total; i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE) continue;
    if (c.type == ADV_TYPE_SENSOR) continue;
    people++;
    if (c.type == ADV_TYPE_REPEATER) rpts++;
  }
  UiTile& b = v.tile[1];
  strcpy(b.label, "Kontakty"); b.icon = UII_USER;
  snprintf(b.value, sizeof(b.value), "%d", people);
  // ostatnio usłyszany węzeł: nazwa pod liczbą, a obok liczby — jak dawno
  (void)rpts;
  if (the_mesh.ui_heard_n > 0) { fmtAgo(b.side, sizeof(b.side), the_mesh.ui_heard_at[0]); scopy(b.sub, sizeof(b.sub), the_mesh.ui_heard[0].name); }
  else strcpy(b.sub, "nikogo nie słychać");

  UiTile& d = v.tile[2];
  strcpy(d.label, "Radio"); d.icon = UII_SIGNAL;
  bool narrow = fabsf(_prefs->freq - 869.618f) < 0.005f && fabsf(_prefs->bw - 62.5f) < 0.1f && _prefs->sf == 8 && _prefs->cr == 8;
  if (the_mesh.uiRadioIsSF6()) strcpy(d.value, "SF6"); else if (narrow) strcpy(d.value, "SF8"); else snprintf(d.value, sizeof(d.value), "SF%d", _prefs->sf);
  snprintf(d.sub, sizeof(d.sub), "%.3f MHz", _prefs->freq); comma(d.sub);

  // Sieć: Bluetooth (nazwa połączonej aplikacji) i WiFi
  UiTile& n = v.tile[3];
  strcpy(n.label, "Sieć"); n.icon = UII_NET; n.kind = UIT_NET;
  if (!isBluetoothEnabled()) { v.bt_state = 0; strcpy(v.bt_txt, "BT OFF"); }
  else if (hasConnection()) { v.bt_state = 2; scopy(v.bt_txt, sizeof(v.bt_txt), the_mesh.ui_app_name[0] ? the_mesh.ui_app_name : "połączony"); }
  else { v.bt_state = 1; strcpy(v.bt_txt, "BT ON"); }
  v.wifi_dbm = m9wifiBars() >= 1 && m9wifiBars() <= 4 ? m9wifiRssi() : 0;

  // Widmo: szum na naszym kanale z ostatnich minut
  UiTile& w = v.tile[4];
  strcpy(w.label, "Widmo"); w.icon = UII_SPEC; w.kind = UIT_NOISE;
  snprintf(w.value, sizeof(w.value), "%d", m9radioNoiseFloor());
  {
    unsigned long now = millis(); uint32_t air = 0;
    for (int i = 0; i < m9logCount(); i++) { const M9Pkt* p = m9logGet(i); if (now - p->ms > 60000UL) break; air += m9radioAirtime(p->len); }
    int busy = (int)(air * 100 / 60000); if (busy > 100) busy = 100;
    snprintf(w.sub, sizeof(w.sub), "zajęte %d %%", busy);
    int from = _nh_n > UI_SPARK_N * 3 ? _nh_n - UI_SPARK_N * 3 : 0;      // ok. 5 ostatnich minut, co trzeci punkt
    for (int i = from; i < _nh_n && v.nspark < UI_SPARK_N; i += 3) v.spark[v.nspark++] = _nh[i];
  }

  // Bateria: procent, wykres napięcia z ostatnich godzin i czas pracy
  UiTile& e = v.tile[5];
  strcpy(e.label, "Bateria"); e.icon = UII_BATT; e.kind = UIT_BATT;
  uint16_t mv = getBattMilliVolts();
  bool chg = digitalRead(M9_PIN_EXT_PWR) == LOW;
  snprintf(e.value, sizeof(e.value), "%d %%", battPercent(mv));
  e.accent = chg;
  battEstimate(e.sub, sizeof(e.sub));
  memcpy(v.bhist, _bh, _bh_n * sizeof(uint16_t)); v.nbhist = _bh_n; v.bchg = chg;
}

void UITask::fillSettings() {
  UiView& v = *_v;
  v.screen = UIS_GRID; v.tab = 4;
  v.grid_cols = 2; v.ntiles = 8; v.sel = sel();
  strcpy(v.title, "Ustawienia"); snprintf(v.title_right, sizeof(v.title_right), "wersja %s", M9_FW_VERSION);
  static const char* const N[8] = { "Radio", "Bluetooth", "Pozycja", "Advert", "Kontakty", "Wiadomości", "Ekran i dźwięk", "System" };
  static const uint8_t IC[8] = { UII_SIGNAL, UII_BT, UII_GPS, UII_TOWER, UII_USER, UII_MAIL, UII_SCREEN, UII_GEAR };
  for (int i = 0; i < 8; i++) {
    UiTile& t = v.tile[i];
    strcpy(t.label, N[i]); t.icon = IC[i];
    switch (i) {
      case 0: if (the_mesh.uiRadioIsSF6()) snprintf(t.sub, sizeof(t.sub), "SF6 · %d dBm", _prefs->tx_power_dbm); else { snprintf(t.sub, sizeof(t.sub), "%.3f · SF%d", _prefs->freq, _prefs->sf); comma(t.sub); } break;
      case 1: if (!isBluetoothEnabled()) strcpy(t.sub, "wyłączony"); else if (hasConnection()) strcpy(t.sub, "połączony"); else snprintf(t.sub, sizeof(t.sub), "PIN %06lu", (unsigned long)the_mesh.getBLEPin()); break;
      case 2: snprintf(t.sub, sizeof(t.sub), "GPS %s · prywatność", _prefs->gps_enabled ? "wł." : "wył."); break;
      case 3: if (ADV_HOURS[_cfg.adv_idx]) snprintf(t.sub, sizeof(t.sub), "co %d godz.", ADV_HOURS[_cfg.adv_idx]); else strcpy(t.sub, "wyślij · auto wył."); break;
      case 4: strcpy(t.sub, (_prefs->manual_add_contacts & 1) ? "dodaje wybrane typy" : "dodaje wszystko"); break;
      case 5: strcpy(t.sub, "trasa · potwierdzenia"); break;
      case 6: snprintf(t.sub, sizeof(t.sub), "%s · %s", SLEEP_NAMES[_cfg.sleep_idx], _prefs->buzzer_quiet ? "wyciszone" : "dźwięk wł."); break;
      case 7: strcpy(t.sub, "informacje · zegar"); break;
    }
  }
}

void UITask::fillList() {
  UiView& v = *_v;
  uint8_t s = scr();
  v.screen = UIS_LIST;
  if (s == S_CONTACTS) buildContacts(); else if (s == S_RPTS) buildRpts(); else if (s == S_MSGS) buildThreads();
  listHead(s);
  int base = s == S_CONTACTS ? 1 : 0;                 // Kontakty: wiersz 0 to filtry, lista zaczyna się od 1
  int vis = ROWS_VISIBLE - base;
  int count = listCount(s) - base;
  Nav& n = _nav[_depth - 1];
  if (n.sel >= count + base) n.sel = count + base > 0 ? count + base - 1 : 0;
  int selr = n.sel - base;                            // -1 = zaznaczone filtry
  if (selr < 0) n.top = 0;
  else {
    if (selr < n.top) n.top = selr;
    if (selr >= n.top + vis) n.top = selr - vis + 1;
  }
  if (n.top > count - vis) n.top = count - vis;
  if (n.top < 0) n.top = 0;
  v.sel = selr < 0 ? -1 : selr - n.top;
  v.more_up = n.top > 0;
  v.more_dn = n.top + vis < count;
  for (int i = n.top; i < count && v.nrows < vis; i++) listRow(s, i + base, v.rows[v.nrows++]);
}

void UITask::fillKv() {
  UiView& v = *_v;
  v.screen = UIS_INFO; v.tab = -1;
  scopy(v.title, sizeof(v.title), _kv_title);
  scopy(v.title_right, sizeof(v.title_right), _kv_right);
  Nav& n = _nav[_depth - 1];
  if (n.top > _kv_n - KV_VISIBLE) n.top = _kv_n - KV_VISIBLE;
  if (n.top < 0) n.top = 0;
  for (int i = n.top; i < _kv_n && v.nkv < KV_VISIBLE; i++) v.kv[v.nkv++] = _kv[i];
  v.more_up = n.top > 0;
  v.more_dn = n.top + KV_VISIBLE < _kv_n;
  if (_kv_n > KV_VISIBLE) { strcpy(v.hint[0].key, "▲▼"); strcpy(v.hint[0].txt, "przewiń"); v.nhints = 1; }
  strcpy(v.hint[v.nhints].key, "Wstecz"); strcpy(v.hint[v.nhints].txt, ""); v.nhints++;
}

void UITask::makeBubble(const UiMsg& m, UiBubble& b) {
  scopy(b.who, sizeof(b.who), m.who[0] ? m.who : (m.out ? "" : _cur));
  scopy(b.text, sizeof(b.text), m.text);
  b.out = m.out;
  b.sep = _th_new >= 0 && &m == &_msgs[_th_new];
  char t[8]; fmtClock(t, sizeof(t), m.ts, false);
  char h[24]; hopsTxt(h, sizeof(h), m.hops, m.direct);
  if (m.out && m.chan && m.repeats) snprintf(b.meta, sizeof(b.meta), "%s · Powtórzone ×%d", t, m.repeats);
  else if (m.out && !m.chan && m.delivered) snprintf(b.meta, sizeof(b.meta), "%s · Doręczono", t);
  else if (m.out) snprintf(b.meta, sizeof(b.meta), "%s · Wysłano", t);
  else snprintf(b.meta, sizeof(b.meta), "%s · %s", t, h);
  if (_cfg.show_path && m.out && m.rep_n > 0 && m.rep_hs > 0) {
    // pod wysłaną: repeater, który pierwszy ją usłyszał i powtórzył
    for (int q = 0; q < m.rep_hs && q < 3; q++) { char x[4]; snprintf(x, sizeof(x), "%02X", m.rep_id[0][q]); strcat(b.path, x); }
  }
  // pod odebraną: identyfikatory kolejnych repeaterów (2 znaki na bajt)
  if (_cfg.show_path && !m.out && !m.direct && m.path_bytes > 0 && m.hash_size > 0) {
    int cnt = m.path_bytes / m.hash_size, shown = 0;
    for (int hh = 0; hh < cnt; hh++) {
      char id[12]; id[0] = 0;
      for (int q = 0; q < m.hash_size; q++) { char x[4]; snprintf(x, sizeof(x), "%02X", m.path[hh * m.hash_size + q]); strcat(id, x); }
      if (strlen(b.path) + strlen(id) + 4 > sizeof(b.path) - 10) break;
      if (b.path[0]) strcat(b.path, "→");
      strcat(b.path, id);
      shown++;
    }
    if (m.hops > shown) { char more[12]; snprintf(more, sizeof(more), "→+%d", m.hops - shown); strcat(b.path, more); }
  }
}

void UITask::fillThread() {
  UiView& v = *_v;
  v.screen = UIS_THREAD; v.tab = -1;
  if (_input_mode != IN_MSG) {                       // pole do wpisania czegoś innego niż wiadomość
    static const char* const T[14] = { "", "Nowy kanał", "Częstotliwość", "Nazwa urządzenia", "Data i godzina", "Hasło repeatera", "Komenda", "Szerokość geograficzna", "Długość geograficzna", "PIN Bluetooth",
                                       "Sieć WiFi", "Hasło WiFi", "Adres mapy", "PIN blokady ekranu" };
    static const char* const R[14] = { "", "kanał z hasztagiem", "MHz", "do 22 znaków", "czas polski", "", "konsola repeatera", "stopnie", "stopnie", "6 cyfr", "nazwa sieci", "puste = bez zmian", "internet", "4–8 cyfr" };
    static const char* const N[14] = { "", "Wpisz nazwę, np. #beskidy. Kto wpisze tę samą nazwę, ten jest na kanale.", "Wpisz częstotliwość w MHz, np. 869.618",
      "Pod tą nazwą widzą Cię inni w sieci. Polskie litery: ctrl, potem litera.",
      "Format: RRRR-MM-DD GG:MM. Zegar ustawia się też sam po połączeniu z aplikacją w telefonie i z GPS po złapaniu pozycji.",
      "Hasło administratora albo gościa. Po udanym logowaniu zostanie zapamiętane w urządzeniu. Puste = gość.",
      "Komenda tekstowa dla repeatera, np. get radio albo set tx 20.",
      "Szerokość w stopniach, np. 49.82250 (ujemna = południowa).", "Długość w stopniach, np. 19.04690 (ujemna = zachodnia).",
      "Nowy PIN do parowania. Zacznie działać po restarcie. W telefonie trzeba będzie sparować urządzenie od nowa.",
      "Nazwa sieci WiFi, przez którą urządzenie pobierze mapę: domowa albo hotspot z telefonu. Wielkość liter ma znaczenie.",
      "Hasło do tej sieci WiFi. Zapisane hasło jest zaszyfrowane i nie jest tu pokazywane; wpisz nowe albo zostaw puste pole.",
      "Adres w internecie, spod którego urządzenie pobiera pliki mapy (musi tam być plik lista.txt).",
      "Nowy PIN do odblokowania ekranu. Zapamiętaj go: bez PIN-u pomoże tylko wgranie firmware kablem z kasowaniem pamięci." };
    strcpy(v.title, T[_input_mode]); strcpy(v.title_right, R[_input_mode]); strcpy(v.line1, N[_input_mode]);
    scopy(v.input, sizeof(v.input), _input);
    v.input_chars = _input_chars;
    v.polish_next = _polish;
    strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, "zatwierdź");
    v.nhints = 1;
    if (_input_mode == IN_TIME) { strcpy(v.hint[1].key, "Menu"); strcpy(v.hint[1].txt, "z GPS"); v.nhints = 2; }
    strcpy(v.hint[v.nhints].key, "Wstecz"); strcpy(v.hint[v.nhints].txt, "anuluj"); v.nhints++;
    return;
  }
  snprintf(v.title, sizeof(v.title), "%s%s", _cur_chan ? "# " : "", _cur);
  markRead(_cur, _cur_chan);

  // wiadomości tej rozmowy od najnowszej i wysokości ich dymków (w czasie animacji przewijania — z poprzedniej klatki)
  const int viewH = uiThreadViewH();
  if (_th_idx && _th_h && (_scroll_px == _scroll_tgt || _th_n == 0)) {
    static UiBubble tmp;
    _th_n = 0; _th_total = 0;
    for (int i = _msg_n - 1; i >= 0; i--) {
      if (_msgs[i].chan != _cur_chan || strcmp(_msgs[i].thread, _cur) != 0) continue;
      memset(&tmp, 0, sizeof(tmp));
      makeBubble(_msgs[i], tmp);
      int h = uiBubbleHeight(tmp);
      _th_idx[_th_n] = i; _th_h[_th_n] = h; _th_n++;
      _th_total += h + (_th_total ? 4 : 0);
    }
  }
  int maxs = _th_total - viewH; if (maxs < 0) maxs = 0;
  if (_scroll_unread >= 0) {                       // po wejściu: pierwsza nieprzeczytana na górze ekranu
    int p = 0;
    for (int j = 0; j < _th_n; j++) {
      if (_th_idx[j] == _scroll_unread) { int sp = p + _th_h[j] - viewH; _scroll_px = _scroll_tgt = sp < 0 ? 0 : sp; break; }
      p += _th_h[j] + 4;
    }
    _scroll_unread = -1;
  }
  if (_scroll_tgt > maxs) _scroll_tgt = maxs;
  if (_scroll_px > maxs) _scroll_px = maxs;
  if (_cur_chan) snprintf(v.title_right, sizeof(v.title_right), "kanał · %d", _th_n);
  else if (keySet(_cur_key)) snprintf(v.title_right, sizeof(v.title_right), "prywatna %02X%02X · %d", _cur_key[0], _cur_key[1], _th_n);   // identyfikator kontaktu
  else snprintf(v.title_right, sizeof(v.title_right), "prywatna · %d", _th_n);

  // które dymki są widoczne: od pierwszego, który sięga w okno [_scroll_px, _scroll_px + viewH)
  int first = 0, pos = 0;
  while (first < _th_n && pos + _th_h[first] <= _scroll_px) { pos += _th_h[first] + 4; first++; }
  v.bub_off = _scroll_px - pos;
  // wybrana (do opcji): najniższa, której dół widać; gdy żadnej — ta, która wypełnia ekran
  _th_sel = -1;
  int sel = first, p2 = pos;
  for (int j = first; j < _th_n && p2 < _scroll_px + viewH; j++) {
    if (p2 >= _scroll_px - 4) { sel = j; break; }
    p2 += _th_h[j] + 4;
  }
  int last = first, p3 = pos;
  while (last < _th_n && last - first < UI_BUB_MAX && p3 < _scroll_px + viewH) { p3 += _th_h[last] + 4; last++; }
  for (int j = last - 1; j >= first; j--) {
    int i = _th_idx[j];
    if (i < 0 || i >= _msg_n) continue;
    UiBubble& b = v.bub[v.nbub++];
    makeBubble(_msgs[i], b);
    b.sel = j == sel;                                // jej dotyczy ► / Menu (opcje)
    if (b.sel) _th_sel = i;
  }
  v.sb_total = _th_total; v.sb_view = viewH; v.sb_pos = _scroll_px;
  scopy(v.input, sizeof(v.input), _input);
  v.input_chars = _input_chars;
  v.polish_next = _polish;
  strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, _input[0] ? "wyślij" : "szybkie");
  strcpy(v.hint[1].key, "ctrl"); strcpy(v.hint[1].txt, "ąęł");
  strcpy(v.hint[2].key, "▲▼"); strcpy(v.hint[2].txt, "przewiń");
  strcpy(v.hint[3].key, "Menu"); strcpy(v.hint[3].txt, "opcje");
  v.nhints = 4;
}

void UITask::fill() {
  UiView& v = *_v;
  memset(&v, 0, sizeof(v));
  uint8_t s = scr();
  if (s == S_BOOT) {
    v.screen = UIS_BOOT;
    snprintf(v.line1, sizeof(v.line1), "%s · ThinkNode M9 · wersja %s", _prefs->node_name, M9_FW_VERSION);
    snprintf(v.line2, sizeof(v.line2), "%.3f MHz · BW %.4g · SF%d · CR 4/%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
    return;
  }
  fillStatus();
  if (_locked) { fillLock(); return; }
  if (s == S_START) fillStart();
  else if (s == S_TIME) fillTime();
  else if (s == S_SETTINGS) fillSettings();
  else if (s == S_THREAD) fillThread();
  else if (s == S_KV) fillKv();
  else if (s == S_SPEC) fillSpec();
  else if (s == S_LOG) fillLog();
  else if (s == S_CHINFO) fillChInfo();
  else if (s == S_QR) fillQR();
  else fillList();
  if (_bar && v.tab >= 0) { v.bar_focus = true; v.tab = _bar_sel; }
  if (_popup_until) {
    v.popup = true; v.popup_danger = _pop_danger;
    scopy(v.popup1, sizeof(v.popup1), _pop1); scopy(v.popup2, sizeof(v.popup2), _pop2);
  }
}

void UITask::fillLock() {
  UiView& v = *_v;
  v.screen = UIS_LOCK;
  scopy(v.big, sizeof(v.big), v.time);
  static const char* const DNI[7] = { "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota" };
  struct tm lt; localTime(the_mesh.getRTCClock()->getCurrentTime(), &lt);
  snprintf(v.line1, sizeof(v.line1), "%s · %s, %d.%02d", _prefs->node_name, DNI[lt.tm_wday % 7], lt.tm_mday, lt.tm_mon + 1);
  if (_popup_until && _pin_popup) snprintf(v.line2, sizeof(v.line2), "Bluetooth: %s", _pop2);
  else if (v.unread > 0) snprintf(v.line2, sizeof(v.line2), v.unread == 1 ? "1 nowa wiadomość" : "nowe wiadomości: %d", v.unread);
  if (_cfg.lock == 2 && _pin_have) {
    unsigned long now = millis();
    if (_popup_until && _pin_popup) strcpy(v.line2, "Bluetooth: odblokuj, żeby sparować");   // PIN parowania nie na zablokowanym ekranie
    if (_pin_wait && (long)(now - _pin_wait) < 0) { snprintf(v.footer, sizeof(v.footer), "Zły PIN (%d). Poczekaj %lu s", _pin_fails, (unsigned long)((_pin_wait - now) / 1000 + 1)); }
    else if (_pin_fails) snprintf(v.footer, sizeof(v.footer), "Zły PIN (%d). Wpisz PIN i naciśnij Enter", _pin_fails);
    else strcpy(v.footer, "Wpisz PIN i naciśnij Enter");
    int n = strlen(_pin_in); char* d = v.popup1;
    for (int i = 0; i < n; i++) { *d++ = '*'; *d++ = ' '; }
    *d = 0;
    return;
  }
  strcpy(v.footer, "Odblokowanie: Wstecz, potem Enter");
  if (_ul_back && millis() - _ul_last < 4000) strcpy(v.popup1, "teraz naciśnij Enter");
}

void UITask::draw() {
  if (_fb == NULL || _v == NULL) return;
  if (scr() == S_MAP && !_locked) { drawMap(); return; }
  fill();
  Gfx g(_fb);
  uiRender(g, *_v);
  g.swapBytes();                                  // panel chce starszy bajt pierwszy
  m9lcdBlit(0, 0, GFX_W, GFX_H, _fb);
}

// ---------------- pętla ----------------

void UITask::loop() {
  unsigned long now = millis();

  for (int n = 0; n < 8; n++) {
    uint8_t k = _kb.readKey();
    if (k == 0) break;
    _last_key = now;
    if (!_awake || _dimmed) { wake(); continue; }  // pierwszy klawisz tylko budzi (albo rozjaśnia) ekran
    uint32_t ms = SLEEP_STEPS[_cfg.sleep_idx];
    _sleep_at = ms ? now + ms : 0;
    key(k);
    _dirty = true;
  }

  // karta SD i mapa: chwilę po starcie, żeby nie wydłużać uruchamiania
  if (_sd_pending && now > 6000) {
    _sd_pending = false;
    _map_ready = _map.begin();
    if (_map.mount()) _map.scan();
    _dirty = true;
  }
  m9dlLoop();
  if (m9dlChanged()) _map.scan();
  if (_sd_test_at && (long)(now - _sd_test_at) >= 0) {
    _sd_test_at = 0;
    char* t = (char*)ps_malloc(1600);
    if (t) {
      _map.diag(t, 1600);
      _map.scan();
      _popup_until = 0;
      kvOpen("Test karty SD", M9_FW_VERSION);
      kvText(t);
      free(t);
      if (scr() != S_KV) push(S_KV);
    }
    uint32_t ms = SLEEP_STEPS[_cfg.sleep_idx]; if (ms) _sleep_at = millis() + ms;
  }
  // zegar z serwera mapy (nagłówek Date): poprawka, gdy różni się o więcej niż 3 s
  if (uint32_t st = m9dlServerTime()) {
    uint32_t mine = the_mesh.getRTCClock()->getCurrentTime();
    // Połączenie nie potwierdza tożsamości serwera, więc poprawiamy najwyżej o 6 godzin;
    // większą różnicę przyjmujemy tylko wtedy, gdy zegar urządzenia w ogóle nie jest ustawiony (przed 2025 r.).
    uint32_t diff = st > mine ? st - mine : mine - st;
    if (diff > 3 && (diff <= 6 * 3600UL || mine < 1735689600UL)) the_mesh.getRTCClock()->setCurrentTime(st);
  }
  if (m9otaState() == 5 && !_ota_reboot_at) { _ota_reboot_at = now + 3000; wake(); popup("Aktualizacja zainstalowana", "restart za 3 s", false, 5000); }
  if (_ota_reboot_at && (long)(now - _ota_reboot_at) >= 0) { _ota_reboot_at = 0; saveMsgs(); the_mesh.uiReboot(); }
  if (m9dlFinished()) { _map.scan(); wake(); popup("Mapa pobrana", "możesz wyjść z menu (Wstecz)", false, 20000); }
  if (m9dlFailed()) {                                // błąd pobierania: ekran ze szczegółami (zostaje do naciśnięcia Wstecz)
    wake();
    kvOpen("Pobieranie: błąd", M9_FW_VERSION);
    kvText(m9dlDiag());
    if (scr() != S_KV) push(S_KV);
  }
  if (m9dlActive() || m9otaActive()) { uint32_t ms = SLEEP_STEPS[_cfg.sleep_idx]; if (ms) _sleep_at = now + ms; }     // przy przesyłaniu mapy ekran nie gaśnie

  adminLoop();
  if (_disc_until && (long)(now - _disc_until) >= 0) { _disc_until = 0; _dirty = true; }
  if (scr() == S_BOOT && (long)(now - _boot_until) >= 0) { tab(S_START); }
  if (_popup_until && (long)(now - _popup_until) >= 0) { _popup_until = 0; _confirm = C_NONE; _dirty = true; }

  if (_awake && _sleep_at && (long)(now - _sleep_at) >= 0) {    // wygaszenie: gaśnie ekran i klawiatura, radio i Bluetooth działają
    _awake = false;
    if (_cfg.lock) { _locked = true; _ul_first = 0; _ul_back = false; _pin_in[0] = 0; }
    if (_msgs_dirty) _save_at = now;                 // ekran zgasł: dobry moment na zapis
    m9lcdBacklight(0);
    if (_display) _display->turnOff();
    _kb.setBacklight(false);
    _dimmed = false;
    if (!_cfg.cpu_full && m9wifiBars() < 0) m9cpuSave(true);      // ekran zgasł: procesor 80 MHz (nie przy włączonym WiFi)
  }
  if (!_awake && m9wifiBars() >= 0) m9cpuSave(false);

  // Bluetooth: nowe połączenie, które po 1,5 s nadal nie jest uwierzytelnione, to parowanie — pokaż PIN
  if (m9ble_connects != _ble_conn_seen) { _ble_conn_seen = m9ble_connects; _ble_auth_seen = m9ble_auth_ok; _ble_pin_at = now + 1500; }
  if (_ble_pin_at && (long)(now - _ble_pin_at) >= 0) {
    _ble_pin_at = 0;
    if (m9ble_auth_ok == _ble_auth_seen && !hasConnection()) {
      char t[40]; snprintf(t, sizeof(t), "wpisz w telefonie PIN: %06lu", (unsigned long)the_mesh.getBLEPin());
      wake(); popup("Parowanie Bluetooth", t, false, 45000); _pin_popup = true;
    }
  }
  if (_pin_popup && (m9ble_auth_ok != _ble_auth_seen || !_popup_until)) { if (_popup_until) { _popup_until = 0; _dirty = true; } _pin_popup = false; }

  // dźwięk przy advercie repeatera (jeśli włączony w Ustawieniach › Ekran i dźwięk)
  if (the_mesh.ui_heard_n > 0 && the_mesh.ui_heard_at[0] != _adv_seen) {
    bool first = _adv_seen == 0;
    _adv_seen = the_mesh.ui_heard_at[0];
    ContactInfo& h = the_mesh.ui_heard[0];
    ContactInfo c;
    bool fav = _cfg.adv_snd == 2 && ctByKey(h.id.pub_key, c) && (c.flags & 1);
    if (!first && h.type == ADV_TYPE_REPEATER && !_prefs->buzzer_quiet && !_buzzer.isPlaying() && (_cfg.adv_snd == 1 || fav)) _buzzer.play("adv:d=32,o=7,b=160:g,p,g");
  }

  _buzzer.loop();
  if (the_mesh.ui_sent_changed) { the_mesh.ui_sent_changed = false; pollSent(); }

  if ((long)(now - _next_tick) >= 0) {             // zegar, bateria, „… min temu”, odpowiedzi discovery
    _next_tick = now + ((scr() == S_DISC || scr() == S_MAP_NET || scr() == S_OTA) ? 1000 : scr() == S_MAP ? 3000 : 5000);
    // zegar niewiarygodny (przed 2025 r.), a GPS ma czas: ustaw sam
    if (the_mesh.getRTCClock()->getCurrentTime() < 1735689600UL) timeFromGps();
    _dirty = true;
  }

  if (_next_advert && (long)(now - _next_advert) >= 0) {        // automatyczny advert do sąsiadów
    uint8_t h = ADV_HOURS[_cfg.adv_idx];
    _next_advert = h ? now + h * 3600000UL : 0;
    if (h) the_mesh.advert();
  }

  // zapis wiadomości: w kawałkach, kilka minut po zmianie (albo zaraz po wygaszeniu ekranu) i nie w trakcie pisania
  if (sv_active || (_msgs_dirty && (long)(now - _save_at) >= 0 && now - _last_key > 5000)) saveStep();

  // Bluetooth wyłączony w ustawieniach: po starcie (gdy MeshCore już go włączył) wyłącz
  if (!_bt_boot_done && now > 2000) {
    _bt_boot_done = true;
    if (_cfg.bt_off && isBluetoothEnabled()) { disableBluetooth(); _dirty = true; }
  }
  // ikona WiFi na pasku stanu: odświeżaj, gdy zmieni się stan albo zasięg
  { int w = m9wifiBars(); if (w != _wifi_bars) { _wifi_bars = w; _dirty = true; } }

  sampleLoop(now);
  powerLoop(now);

  // płynne przewijanie rozmowy: kilka klatek do celu
  if (_scroll_px != _scroll_tgt && (long)(now - _scroll_next) >= 0) {
    if (scr() != S_THREAD) _scroll_px = _scroll_tgt;
    else {
      int d = _scroll_tgt - _scroll_px, st = d * 2 / 5;
      if (st == 0 || (d > 0 ? d : -d) <= 3) st = d;
      _scroll_px += st;
      _scroll_next = now + 25;
      _dirty = true;
    }
  }

  if (_dirty && _awake) {
    _dirty = false;
    draw();
  }
}

// ======================= pomiary w tle, oszczędzanie energii =======================

void UITask::sampleLoop(unsigned long now) {
  // szum na naszym kanale: próbka co 0,5 s, do historii najmniejsza z każdych ~2 s (pakiety nie zawyżają)
  if ((long)(now - _nh_next) >= 0) {
    _nh_next = now + 500;
    int r = m9radioRssi();
    if (r && r < _nh_min) _nh_min = r;
    if ((long)(now - _nh_slot_end) >= 0) {
      _nh_slot_end = now + NOISE_SLOT_MS;
      if (_nh_min <= 0) {
        if (_nh_n == UI_NOISE_N) { memmove(_nh, _nh + 1, UI_NOISE_N - 1); _nh_n--; }
        _nh[_nh_n++] = (int8_t)(_nh_min < -128 ? -128 : _nh_min);
        if (_awake && (scr() == S_SPEC || scr() == S_START)) _dirty = true;
      }
      _nh_min = 1;
    }
  }
  // bateria: napięcie co 7,5 min (6 godzin w 48 punktach); zmiana ładowania zaczyna nowy odcinek do szacowania
  if ((long)(now - _bh_next) >= 0) {
    _bh_next = now + BATT_SAMPLE_MS;
    uint16_t mv = getBattMilliVolts();
    bool chg = digitalRead(M9_PIN_EXT_PWR) == LOW;
    if (_bh_n == UI_SPARK_N) { memmove(_bh, _bh + 1, (UI_SPARK_N - 1) * sizeof(uint16_t)); _bh_n--; if (_bh_seg > 0) _bh_seg--; }
    if (chg != _bh_chg || _bh_n == 0) { _bh_seg = _bh_n; _bh_chg = chg; }
    _bh[_bh_n++] = mv;
  }
  // widmo: przemiatanie pasma, gdy ekran Widmo jest otwarty
  if (_awake && scr() == S_SPEC && _sp_tab == 0 && !_sp_pause && (long)(now - _sp_next) >= 0) {
    float f0, step; sweepRange(f0, step);
    int8_t d[UI_SPEC_BINS];
    unsigned long t0 = millis();
    if (m9radioSweep(f0, step, UI_SPEC_BINS, d)) {
      _sp_ms = millis() - t0;
      memcpy(_sp_cur, d, sizeof(d));
      for (int i = 0; i < UI_SPEC_BINS; i++) { if (_sp_peak[i] > -128) _sp_peak[i]--; if (d[i] > _sp_peak[i]) _sp_peak[i] = d[i]; }
      memmove(_sp_wf[1], _sp_wf[0], (UI_SPEC_WF - 1) * UI_SPEC_BINS);
      memcpy(_sp_wf[0], d, UI_SPEC_BINS);
      if (_sp_wf_n < UI_SPEC_WF) _sp_wf_n++;
      _sp_have = true; _dirty = true;
      _sp_next = millis() + 600;                      // przerwa na odbiór pakietów MeshCore
    } else _sp_next = millis() + 150;                 // radio zajęte (odbiera lub nadaje): spróbuj za chwilę
  }
}

void UITask::powerLoop(unsigned long now) {
  // przyciemnienie ekranu po bezczynności (przed wygaszeniem); na ekranie Widmo nie przyciemniamy
  uint32_t dm = DIM_STEPS[_cfg.dim_idx];
  bool busyNet = m9dlActive() || m9otaActive();
  if (_awake && !_dimmed && dm && now - _last_key >= dm && !busyNet && scr() != S_SPEC && !_locked) {
    _dimmed = true;
    int b = brightPercent();
    m9lcdBacklight(b > 10 ? 10 : b);
    _kb.setBacklight(false);
  }
  // GPS oszczędny: włącz co X minut, po ustaleniu pozycji (+20 s) albo po 3 min bez niej wyłącz
  if (_sensors && _prefs->gps_enabled && _cfg.gps_mode > 0) {
    if (!_gps_off_phase) {
      LocationProvider* lp = _sensors->getLocationProvider();
      bool fix = lp && lp->isValid();
      if (fix && !_gps_fix_at) _gps_fix_at = now;
      if ((_gps_fix_at && now - _gps_fix_at > 20000) || now - _gps_on_at > 180000) {
        double la, lo; int sats; gpsFix(&la, &lo, &sats);   // zapamiętaj ostatnią pozycję
        _sensors->setSettingValue("gps", "0");
        _gps_off_phase = true; _gps_fix_at = 0;
        _gps_next = now + GPS_MIN[_cfg.gps_mode] * 60000UL;
      }
    } else if ((long)(now - _gps_next) >= 0) {
      _sensors->setSettingValue("gps", "1");
      _gps_off_phase = false; _gps_on_at = now; _gps_fix_at = 0;
    }
  } else if (_gps_off_phase) {                        // tryb stały albo GPS wyłączony: koniec oszczędzania
    _gps_off_phase = false;
    if (_sensors && _prefs->gps_enabled) _sensors->setSettingValue("gps", "1");
  }
  // Bluetooth: wyłącz, gdy przez X minut nie połączył się telefon
  if (isBluetoothEnabled() && !hasConnection()) { if (!_bt_idle_since) _bt_idle_since = now ? now : 1; }
  else _bt_idle_since = 0;
  uint16_t bm = BTA_MIN[_cfg.bt_auto];
  if (bm && _bt_idle_since && now - _bt_idle_since >= bm * 60000UL && !_bt_killed) {
    disableBluetooth(); _bt_auto_off = true; _bt_idle_since = 0; _dirty = true;
  }
}

// szacowanie czasu pracy z napięć od ostatniej zmiany ładowania (regresja liniowa z ostatnich 2 godzin)
bool UITask::battEstimate(char* dest, size_t n) {
  int a = _bh_seg, cnt;
  if (_bh_n - a > 16) a = _bh_n - 16;
  cnt = _bh_n - a;
  bool chg = digitalRead(M9_PIN_EXT_PWR) == LOW;
  if (cnt < 5 || chg != _bh_chg) { scopy(dest, n, chg ? "ładuje · mierzę…" : "mierzę…"); return false; }
  double sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (int i = 0; i < cnt; i++) { double x = i, y = _bh[a + i]; sx += x; sy += y; sxy += x * y; sxx += x * x; }
  double den = cnt * sxx - sx * sx;
  double slope = den != 0 ? (cnt * sxy - sx * sy) / den : 0;      // mV na próbkę
  double perH = slope * 3600000.0 / BATT_SAMPLE_MS;              // mV na godzinę
  uint16_t mv = _bh[_bh_n - 1];
  if (chg) {
    if (mv >= 4120) scopy(dest, n, "prawie pełna");
    else if (perH > 2) { double h = (4150 - mv) / perH; if (h < 1) snprintf(dest, n, "pełna za %d min", (int)(h * 60) + 1); else snprintf(dest, n, "pełna za %.0f h", h); }
    else scopy(dest, n, "ładuje");
    return true;
  }
  if (perH > -1) { scopy(dest, n, "zostało ponad 99 h"); return true; }
  double pctH = -perH * 100.0 / (4150 - 3300);
  double h = battPercent(mv) / pctH;
  if (h < 1) snprintf(dest, n, "zostało ok. %d min", (int)(h * 60));
  else if (h > 99) scopy(dest, n, "zostało ponad 99 h");
  else snprintf(dest, n, "zostało ok. %.0f h", h);
  return true;
}

void UITask::detailBattery() {
  char t[64];
  uint16_t mv = getBattMilliVolts();
  bool chg = digitalRead(M9_PIN_EXT_PWR) == LOW;
  kvOpen("Bateria", chg ? "ładowanie" : "na baterii");
  snprintf(t, sizeof(t), "%d%% · %d,%02d V", battPercent(mv), mv / 1000, (mv % 1000) / 10); kvAdd("Poziom", t);
  battEstimate(t, sizeof(t)); kvAdd(chg ? "Ładowanie" : "Czas pracy", t);
  int a = _bh_seg; if (_bh_n - a > 16) a = _bh_n - 16;
  if (_bh_n - a >= 2) {
    int d = (int)_bh[_bh_n - 1] - (int)_bh[a];
    double hrs = (_bh_n - 1 - a) * (BATT_SAMPLE_MS / 3600000.0);
    snprintf(t, sizeof(t), "%+d mV w %.1f h", d, hrs); comma(t); kvAdd("Zmiana napięcia", t);
  } else kvAdd("Zmiana napięcia", "mierzę… (pomiar co 7,5 min)");
  snprintf(t, sizeof(t), "%d MHz%s", m9cpuMhz(), _cfg.cpu_full ? "" : " (80 MHz po wygaszeniu)"); kvAdd("Procesor", t);
  kvAdd("Przyciemnianie", DIM_NAMES[_cfg.dim_idx]);
  kvAdd("Wygaszanie", SLEEP_NAMES[_cfg.sleep_idx]);
  kvAdd("GPS", _prefs->gps_enabled ? GPS_NAMES[_cfg.gps_mode] : "wyłączony");
  kvAdd("Bluetooth bez połączenia", BTA_MIN[_cfg.bt_auto] ? BTA_NAMES[_cfg.bt_auto] : "nie wyłącza się");
  push(S_KV);
}

// ======================= widmo =======================

void UITask::sweepRange(float& f0, float& step) {
  switch (_sp_range) {
    case 1: f0 = 869.3f; step = 0.6f / UI_SPEC_BINS; break;      // okolica naszego kanału
    default: f0 = 867.0f; step = 3.0f / UI_SPEC_BINS; break;     // kanały LoRa w Europie (867,1–869,65 MHz)
  }
}

void UITask::fillSpec() {
  UiView& v = *_v;
  v.screen = UIS_SPEC; v.tab = -1;
  v.spec_tab = _sp_tab; v.spec_pause = _sp_pause;
  strcpy(v.title, "Widmo");
  float f0, step; sweepRange(f0, step);
  v.spec_f0 = f0; v.spec_step = step; v.spec_mark = _prefs->freq;
  if (_sp_tab == 0) {
    if (_sp_range == 0) strcpy(v.title_right, "LoRa 867–870 MHz · co 54 kHz");
    else strcpy(v.title_right, "869,3–869,9 MHz · co 11 kHz");
    v.spec_have = _sp_have;
    memcpy(v.spec_cur, _sp_cur, sizeof(_sp_cur)); memcpy(v.spec_peak, _sp_peak, sizeof(_sp_peak));
    memcpy(v.spec_wf, _sp_wf, sizeof(_sp_wf)); v.spec_wf_n = _sp_wf_n;
    if (_sp_pause) strcpy(v.spec_info, "pauza · Enter wznawia pomiar");
    else if (_sp_have) {
      int best = 0; for (int i = 1; i < UI_SPEC_BINS; i++) if (_sp_peak[i] > _sp_peak[best]) best = i;
      int lo = 0; long sum = 0; for (int i = 0; i < UI_SPEC_BINS; i++) sum += _sp_cur[i]; lo = (int)(sum / UI_SPEC_BINS);
      char f[16]; snprintf(f, sizeof(f), "%.3f", f0 + step * (best + 0.5f)); comma(f);
      snprintf(v.spec_info, sizeof(v.spec_info), "średnio %d dBm · najsilniej %s MHz %d dBm", lo, f, _sp_peak[best]);
    } else strcpy(v.spec_info, "na czas pomiaru odbiór pakietów jest przerywany");
    strcpy(v.hint[0].key, "◄►"); strcpy(v.hint[0].txt, "karta");
    strcpy(v.hint[1].key, "▲▼"); strcpy(v.hint[1].txt, "zakres");
    strcpy(v.hint[2].key, "Enter"); strcpy(v.hint[2].txt, _sp_pause ? "wznów" : "pauza");
    strcpy(v.hint[3].key, "Menu"); strcpy(v.hint[3].txt, "log");
    v.nhints = 4;
  } else {
    snprintf(v.title_right, sizeof(v.title_right), "%.3f MHz · %.4g kHz", _prefs->freq, _prefs->bw); comma(v.title_right);
    v.noise_dbm = m9radioNoiseFloor();
    memcpy(v.nhist, _nh, _nh_n); v.nnhist = _nh_n;
    unsigned long now = millis();
    uint32_t air = 0;
    for (int i = 0; i < m9logCount(); i++) {
      const M9Pkt* p = m9logGet(i);
      unsigned long age = now - p->ms;
      if (age > 600000UL) break;
      if (i == 0) { v.have_last = true; v.last_rssi = p->rssi; snprintf(v.last_snr, sizeof(v.last_snr), "SNR %.1f", p->snr_x4 / 4.0f); comma(v.last_snr); }
      if (age < 60000UL) air += m9radioAirtime(p->len);
      if (v.npk < UI_PKDOT_MAX) { v.pk_x[v.npk] = (int16_t)(UI_NOISE_N - 1 - age / NOISE_SLOT_MS); v.pk_dbm[v.npk] = (int8_t)(p->rssi < -128 ? -128 : p->rssi); v.npk++; }
    }
    v.busy_pct = (int)(air * 100 / 60000); if (v.busy_pct > 100) v.busy_pct = 100;
    strcpy(v.hint[0].key, "◄►"); strcpy(v.hint[0].txt, "karta");
    strcpy(v.hint[1].key, "Menu"); strcpy(v.hint[1].txt, "log pakietów");
    v.nhints = 2;
  }
}

void UITask::keySpec(uint8_t k) {
  if (k == M9K_LEFT || k == M9K_RIGHT) { _sp_tab ^= 1; return; }
  if (k == M9K_F3) { push(S_LOG); return; }
  if (_sp_tab != 0) return;
  if (k == M9K_UP || k == M9K_DOWN) {
    _sp_range ^= 1;
    _sp_have = false; _sp_wf_n = 0; memset(_sp_peak, -128, sizeof(_sp_peak)); _sp_next = 0;
    return;
  }
  if (k == M9K_ENTER) { _sp_pause = !_sp_pause; _sp_next = 0; }
}

// ======================= log pakietów =======================

// Dane advertu z pakietu (klucz 32 B, czas 4 B, podpis 64 B, potem: flagi, pozycja, nazwa). Każdy odczyt w granicach pakietu.
static bool advParse(const M9PktInfo& in, char* name, int name_n, int32_t* lat, int32_t* lon, uint8_t* kind) {
  name[0] = 0; *lat = *lon = 0; *kind = 0;
  int i = 100, n = in.payload_len;
  if (n <= i) return false;
  uint8_t fl = in.payload[i++];
  *kind = fl & 15;
  if (fl & 0x10) { if (i + 8 > n) return false; memcpy(lat, &in.payload[i], 4); memcpy(lon, &in.payload[i + 4], 4); i += 8; }
  if (fl & 0x20) i += 2;
  if (fl & 0x40) i += 2;
  if ((fl & 0x80) && i < n) {
    int nl = n - i; if (nl > name_n - 1) nl = name_n - 1;
    memcpy(name, &in.payload[i], nl); name[nl] = 0;
    for (char* c = name; *c; c++) if ((uint8_t)*c < 32) *c = ' ';       // znaki sterujące z sieci nie trafiają na ekran
  }
  return true;
}

static bool logMatch(uint8_t filter, uint8_t type) {
  switch (filter) {
    case 1: return type == 4;
    case 2: return type == 2 || type == 5 || type == 6;
    case 3: return !(type == 4 || type == 2 || type == 5 || type == 6);
  }
  return true;
}

void UITask::buildLog() {
  _lg_n = 0;
  for (int i = 0; i < m9logCount() && _lg_n < 200; i++) {
    M9PktInfo in;
    const M9Pkt* p = m9logGet(i);
    uint8_t type = m9logParse(*p, in) ? in.type : 0xFF;
    if (_lg_filter == 0 || (type != 0xFF && logMatch(_lg_filter, type)) || (type == 0xFF && _lg_filter == 3)) _lg_idx[_lg_n++] = (int16_t)i;
  }
}

static uint8_t logColor(uint8_t type) {
  switch (type) {
    case 4: return ULC_GREEN;
    case 5: case 6: return ULC_BLUE;
    case 2: return ULC_PINK;
    case 8: return ULC_ACC;
    case 3: case 9: return ULC_DIM;
  }
  return ULC_LINE;
}

void UITask::fillLog() {
  UiView& v = *_v;
  v.screen = UIS_LOG; v.tab = -1;
  buildLog();
  strcpy(v.title, "Log pakietów");
  unsigned long now = millis(); int perMin = 0;
  for (int i = 0; i < m9logCount(); i++) { if (now - m9logGet(i)->ms > 60000UL) break; perMin++; }
  snprintf(v.title_right, sizeof(v.title_right), "%lu · %d/min", (unsigned long)m9logTotal(), perMin);
  static const char* const F[4] = { "Wszystkie", "Adverty", "Wiadomości", "Inne" };
  for (int i = 0; i < 4; i++) strcpy(v.chips[i], F[i]);
  v.nchips = 4; v.chip_active = _lg_filter;
  strcpy(v.empty, m9logCount() ? "Brak pakietów tego rodzaju." : "Nic jeszcze nie odebrano.");
  int16_t& s = sel(); int16_t& top = _nav[_depth - 1].top;
  if (s >= _lg_n) s = _lg_n ? _lg_n - 1 : 0;
  if (s < top) top = s;
  if (s >= top + UI_LOG_ROWS) top = s - UI_LOG_ROWS + 1;
  if (top < 0) top = 0;
  for (int r = 0; r < UI_LOG_ROWS && top + r < _lg_n; r++) {
    const M9Pkt* p = m9logGet(_lg_idx[top + r]);
    UiLogRow& w = v.lrow[v.nlrow++];
    struct tm lt; localTime(p->ts, &lt);
    snprintf(w.t, sizeof(w.t), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    snprintf(w.snr, sizeof(w.snr), "%.1f", p->snr_x4 / 4.0f); comma(w.snr);
    snprintf(w.rssi, sizeof(w.rssi), "%d", p->rssi);
    M9PktInfo in;
    if (!m9logParse(*p, in)) { strcpy(w.type, "?"); w.col = ULC_LINE; snprintf(w.who, sizeof(w.who), "nieczytelny · %d B", p->len); continue; }
    scopy(w.type, sizeof(w.type), m9logTypeShort(in.type)); w.col = logColor(in.type);
    bool direct = in.route == 2 || in.route == 3;
    if (direct) strcpy(w.hop, "D"); else snprintf(w.hop, sizeof(w.hop), "%dh", in.hops);
    char path[40]; m9logPath(in, path, sizeof(path), 3);
    char who[40]; who[0] = 0;
    int32_t alat, alon; uint8_t akind;
    if (in.type == 4 && advParse(in, who, 31, &alat, &alon, &akind)) {     // advert: nazwa z danych aplikacji
      if (!who[0]) snprintf(who, sizeof(who), "%02X%02X", in.payload[0], in.payload[1]);
    } else if ((in.type == 5 || in.type == 6) && in.payload_len > 3) {
      char ch[32], nm[24];
      if (the_mesh.uiDecodeGroup(in.payload, in.payload_len, ch, sizeof(ch), nm, sizeof(nm))) snprintf(who, sizeof(who), nm[0] ? "%s · %s" : "%s", ch, nm);
      else snprintf(who, sizeof(who), "kanał %02X", in.payload[0]);
    } else if ((in.type <= 2 || in.type == 8) && in.payload_len > 2) {
      char nm[32]; if (!the_mesh.uiNameByHash(in.payload[1], nm, sizeof(nm))) snprintf(nm, sizeof(nm), "%02X", in.payload[1]);
      snprintf(who, sizeof(who), "od: %s", nm);
    } else if (in.type == 7 && in.payload_len > 1) {
      char nm[32]; if (!the_mesh.uiNameByHash(in.payload[0], nm, sizeof(nm))) snprintf(nm, sizeof(nm), "%02X", in.payload[0]);
      snprintf(who, sizeof(who), "do: %s", nm);
    } else snprintf(who, sizeof(who), "%d B", p->len);
    if (path[0]) snprintf(w.who, sizeof(w.who), "%s · %s", who, path); else scopy(w.who, sizeof(w.who), who);
  }
  v.sel = s - top;
  v.sb_total = _lg_n; v.sb_view = UI_LOG_ROWS; v.sb_pos = top;
  strcpy(v.hint[0].key, "▲▼"); strcpy(v.hint[0].txt, "wybierz");
  strcpy(v.hint[1].key, "Enter"); strcpy(v.hint[1].txt, "szczegóły");
  strcpy(v.hint[2].key, "◄►"); strcpy(v.hint[2].txt, "filtr");
  v.nhints = 3;
}

void UITask::keyLog(uint8_t k) {
  buildLog();
  int16_t& s = sel();
  if (k == M9K_UP && s > 0) s--;
  else if (k == M9K_DOWN && s + 1 < _lg_n) s++;
  else if (k == M9K_LEFT || k == M9K_RIGHT) { _lg_filter = (uint8_t)((_lg_filter + (k == M9K_RIGHT ? 1 : 3)) % 4); s = 0; _nav[_depth - 1].top = 0; }
  else if (k == M9K_ENTER && s < _lg_n) detailPkt(m9logGet(_lg_idx[s])->seq);
}

void UITask::detailPkt(uint32_t seq) {
  const M9Pkt* p = NULL; int pos = -1;
  buildLog();
  for (int i = 0; i < _lg_n; i++) if (m9logGet(_lg_idx[i])->seq == seq) { p = m9logGet(_lg_idx[i]); pos = i; break; }
  if (p == NULL) { popup("Pakietu już nie ma w logu"); return; }
  char t[80], r[40];
  struct tm lt; localTime(p->ts, &lt);
  snprintf(t, sizeof(t), "Pakiet %02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
  snprintf(r, sizeof(r), "%d z %d", pos + 1, _lg_n);
  bool wasKv = scr() == S_KV && _kv_pkt;
  kvOpen(t, r);
  _kv_pkt = seq;
  _chas_pos = false;
  M9PktInfo in;
  if (!m9logParse(*p, in)) { kvAdd("Typ", "nieczytelny"); snprintf(t, sizeof(t), "%d B", p->len); kvAdd("Długość", t); }
  else {
    kvAdd("Typ", m9logTypeLong(in.type));
    char nm[32]; int32_t lat, lon; uint8_t akind;
    if (in.type == 4 && advParse(in, nm, sizeof(nm), &lat, &lon, &akind)) {
      static const char* const KIND[5] = { "?", "osoba", "repeater", "pokój", "czujnik" };
      snprintf(t, sizeof(t), "%s (%s)", nm[0] ? nm : "bez nazwy", KIND[akind < 5 ? akind : 0]); kvAdd("Nadawca", t);
      snprintf(t, sizeof(t), "%02x%02x %02x%02x …", in.payload[0], in.payload[1], in.payload[2], in.payload[3]); kvAdd("Klucz", t);
      if (lat || lon) {
        _clat = lat / 1e6; _clon = lon / 1e6; _chas_pos = true;
        double ml, mo; int sats; char d[24] = "";
        if (gpsFix(&ml, &mo, &sats)) { snprintf(d, sizeof(d), " · %.0f km", distKm(ml, mo, _clat, _clon)); }
        snprintf(t, sizeof(t), "%.3f N %.3f E%s", _clat, _clon, d); comma(t); kvAdd("Pozycja", t);
      }
    } else if ((in.type == 5 || in.type == 6) && in.payload_len > 3) {
      char ch[32], nm[24];
      if (the_mesh.uiDecodeGroup(in.payload, in.payload_len, ch, sizeof(ch), nm, sizeof(nm))) { kvAdd("Kanał", ch); if (nm[0]) kvAdd("Nadawca", nm); }
      else { snprintf(t, sizeof(t), "nieznany (hash %02X)", in.payload[0]); kvAdd("Kanał", t); }
    } else if ((in.type <= 2 || in.type == 8) && in.payload_len > 2) {
      char nm[32];
      if (!the_mesh.uiNameByHash(in.payload[1], nm, sizeof(nm))) snprintf(nm, sizeof(nm), "nieznany (%02X)", in.payload[1]); kvAdd("Od", nm);
      if (!the_mesh.uiNameByHash(in.payload[0], nm, sizeof(nm))) snprintf(nm, sizeof(nm), "%02X", in.payload[0]); kvAdd("Do", nm);
    }
    bool direct = in.route == 2 || in.route == 3;
    if (direct) snprintf(t, sizeof(t), "bezpośrednio%s", in.hops ? " (wyznaczona trasa)" : "");
    else if (in.hops == 0) strcpy(t, "flood · prosto od nadawcy");
    else snprintf(t, sizeof(t), "flood · %d %s", in.hops, in.hops == 1 ? "hop" : in.hops < 5 ? "hopy" : "hopów");
    kvAdd("Trasa", t);
    if (in.hops) { char path[80]; m9logPath(in, path, sizeof(path), 8); kvAdd("Ścieżka", path); }
  }
  snprintf(t, sizeof(t), "SNR %.1f dB · %d dBm", p->snr_x4 / 4.0f, p->rssi); comma(t); kvAdd("Sygnał", t);
  snprintf(t, sizeof(t), "%d B · w eterze %lu ms", p->len, (unsigned long)m9radioAirtime(p->len)); kvAdd("Długość", t);
  char ago[16]; fmtAgo(ago, sizeof(ago), p->ts); kvAdd("Odebrano", ago);
  if (!wasKv) push(S_KV); else _dirty = true;
}

// ======================= info o kanale i kod QR =======================

static void chTitle(char* dest, size_t n, const char* name) { snprintf(dest, n, "%s%s", name[0] == '#' ? "" : "# ", name); }

void UITask::fillChInfo() {
  UiView& v = *_v;
  v.screen = UIS_CHINFO; v.tab = -1;
  chTitle(v.title, sizeof(v.title), _tname);
  strcpy(v.title_right, "Info o kanale");
  char t[52];
  int idx = the_mesh.uiChannelIndex(_tname);
  ChannelDetails cd; memset(&cd, 0, sizeof(cd));
  if (idx >= 0) the_mesh.getChannel(idx, cd);
  static const uint8_t PUB[16] = { 0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a, 0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72 };
  auto kv = [&](const char* k, const char* val) { if (v.nkv < UI_KV_MAX) { scopy(v.kv[v.nkv].k, sizeof(v.kv[0].k), k); scopy(v.kv[v.nkv].v, sizeof(v.kv[0].v), val); v.nkv++; } };
  kv("Rodzaj", _tname[0] == '#' ? "hasztag · klucz z nazwy" : memcmp(cd.channel.secret, PUB, 16) == 0 ? "publiczny" : "prywatny · własny klucz");
  if (idx >= 0) snprintf(t, sizeof(t), "%02X · miejsce %d z %d", cd.channel.hash[0], idx + 1, MAX_GROUP_CHANNELS); else strcpy(t, "—");
  kv("Hash kanału", t);
  int cnt = 0, unread = 0, hopsN = 0, hopsSum = 0, snrN = 0, snrSum = 0, last = -1;
  struct Top { char who[24]; int n; } top[24]; int ntop = 0;
  uint32_t now = the_mesh.getRTCClock()->getCurrentTime();
  for (int i = 0; i < _msg_n; i++) {
    const UiMsg& m = _msgs[i];
    if (!m.chan || strcmp(m.thread, _tname) != 0) continue;
    cnt++; if (m.unread) unread++;
    if (m.out) continue;
    last = i;
    hopsN++; hopsSum += m.direct ? 0 : m.hops;
    if (m.snr_x4) { snrN++; snrSum += m.snr_x4; }
    if (m.who[0] && m.ts + 86400 >= now) {
      int k = 0; while (k < ntop && strcmp(top[k].who, m.who) != 0) k++;
      if (k == ntop && ntop < 24) { scopy(top[ntop].who, sizeof(top[0].who), m.who); top[ntop].n = 0; ntop++; }
      if (k < ntop) top[k].n++;
    }
  }
  snprintf(t, sizeof(t), unread ? "%d · %d nowych" : "%d", cnt, unread); kv("Wiadomości", t);
  if (last >= 0) { char c[8]; fmtClock(c, sizeof(c), _msgs[last].ts, true); snprintf(t, sizeof(t), "%s · %s", c, _msgs[last].who[0] ? _msgs[last].who : "?"); } else strcpy(t, "—");
  kv("Ostatnia", t);
  if (hopsN) { if (snrN) snprintf(t, sizeof(t), "%.1f hopa · SNR %.1f", hopsSum / (double)hopsN, snrSum / 4.0 / snrN); else snprintf(t, sizeof(t), "%.1f hopa", hopsSum / (double)hopsN); comma(t); } else strcpy(t, "—");
  kv("Średnio do nas", t);
  for (int r = 0; r < 3; r++) {                     // trzech najaktywniejszych
    int b = -1; for (int k = 0; k < ntop; k++) if (top[k].n > 0 && (b < 0 || top[k].n > top[b].n)) b = k;
    if (b < 0) break;
    scopy(v.top_name[v.ntop], sizeof(v.top_name[0]), top[b].who); v.top_cnt[v.ntop] = top[b].n; v.ntop++;
    top[b].n = 0;
  }
  strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, "kod QR");
  strcpy(v.hint[1].key, "Menu"); strcpy(v.hint[1].txt, "opcje");
  v.nhints = 2;
}

void UITask::fillQR() {
  UiView& v = *_v;
  v.screen = UIS_QR; v.tab = -1;
  chTitle(v.title, sizeof(v.title), _tname);
  strcpy(v.title_right, "Udostępnij kanał");
  int idx = the_mesh.uiChannelIndex(_tname);
  ChannelDetails cd; memset(&cd, 0, sizeof(cd));
  if (idx >= 0) the_mesh.getChannel(idx, cd);
  int klen = 16; for (int i = 16; i < 32; i++) if (cd.channel.secret[i]) klen = 32;
  char hex[72]; for (int i = 0; i < klen; i++) snprintf(hex + i * 2, 3, "%02x", cd.channel.secret[i]);
  char url[200]; int n = snprintf(url, sizeof(url), "meshcore://channel/add?name=");
  for (const char* p = _tname; *p && n < 150; p++) {
    uint8_t c = (uint8_t)*p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') url[n++] = c;
    else n += snprintf(url + n, sizeof(url) - n, "%%%02X", c);
  }
  url[n] = 0;
  snprintf(url + n, sizeof(url) - n, "&secret=%s", hex);
  static uint8_t tmp[UI_QR_BYTES];
  v.qr_ok = qrcodegen_encodeText(url, tmp, v.qr, qrcodegen_Ecc_LOW, 1, 10, qrcodegen_Mask_AUTO, true);
  for (int l = 0; l < 4 && l * 16 < klen * 2; l++) {
    char* d = v.qr_line[l];
    for (int g = 0; g < 4; g++) { memcpy(d, hex + l * 16 + g * 4, 4); d += 4; *d++ = g < 3 ? ' ' : 0; }
  }
  strcpy(v.hint[0].key, "Wstecz"); strcpy(v.hint[0].txt, ""); v.nhints = 1;
}

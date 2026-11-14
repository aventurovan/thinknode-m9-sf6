#pragma once
// Interfejs „MC SF6” dla ThinkNode M9 — część sprzętowa: klawisze, dane z sieci MeshCore, ekrany.
// Rysowanie jest w UiView.cpp (przenośne), tu tylko wypełniamy UiView i reagujemy na klawisze.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#include <AbstractUITask.h>
#include <NodePrefs.h>
#include "M9Keyboard.h"
#include "M9Map.h"
#include "UiView.h"

struct ContactInfo;

#define UI_MSG_MAX     300
#define UI_THREAD_MAX  48
#define UI_LIST_MAX    520
#define UI_KVS_MAX     64
#define UI_MUTED_MAX   16
#define UI_PICK_MAX    12
#define UI_PW_MAX      12
#define UI_MENU_MAX    8

struct UiMsg {
  char thread[32];        // nazwa kanału albo kontaktu
  char who[24];           // nadawca (na kanale)
  char text[164];
  uint32_t ts;            // czas (zegar urządzenia)
  uint8_t chan, out, unread, hops;
  uint16_t sent_id;       // numer wysyłki (wiadomości własne, tylko do restartu)
  uint8_t repeats, delivered;
  uint8_t direct, hash_size, path_bytes;   // trasa odebranej wiadomości
  uint8_t path[24];
  int8_t snr_x4; int16_t rssi;             // odbiór
  uint8_t rep_n, rep_hs; uint8_t rep_id[6][3]; int8_t rep_snr[6];   // kto powtórzył naszą wiadomość
};

class UITask : public AbstractUITask {
  enum Scr { S_START = 0, S_MSGS = 1, S_CONTACTS = 2, S_RPTS = 3, S_SETTINGS = 4,
             S_THREAD, S_KV, S_BOOT, S_MENU, S_HEARD, S_QUICK, S_DISC, S_LOGIN, S_ADMIN, S_ADMIN_ACT, S_ADMIN_CLI,
             S_RADIO, S_SET_BT, S_SET_POS, S_SET_ADV, S_SET_CT, S_SET_MSG, S_SET_SCR, S_SET_SYS, S_PICK, S_MAP, S_MAP_NET, S_TIME, S_OTA };
  enum InputMode { IN_MSG = 0, IN_CHANNEL, IN_FREQ, IN_NAME, IN_TIME, IN_PASS, IN_CLI, IN_LAT, IN_LON, IN_PIN, IN_WIFI_SSID, IN_WIFI_PASS, IN_MAP_URL };
  enum Act { A_WRITE, A_FAV, A_DETAIL, A_MANAGE, A_DELETE, A_REPLY, A_RESEND, A_COPY, A_DELMSG, A_OPEN, A_MUTE, A_MARKREAD, A_DELCHAN, A_DELTHREAD, A_REPLY_ACK, A_SHOWMAP, A_ROUTE, A_ADDCHAN };
  enum Pick { P_PRESET, P_BW, P_SF, P_CR, P_TX, P_HASH, P_AIR, P_BT, P_GPS, P_ADVLOC, P_TEL0, P_TEL1, P_TEL2, P_AUTOADV, P_AA_MODE, P_AA_T0, P_AA_T1, P_AA_T2, P_AA_T3,
              P_AA_FULL, P_AA_HOPS, P_SHOWPATH, P_MULTIACK, P_SOUND, P_SLEEP, P_KBLIGHT, P_WAKE, P_LOCK, P_RPTSORT, P_BRIGHT, P_MSGSND, P_ADVSND, P_MAPRANGE };
  enum Confirm { C_NONE = 0, C_DEL_CHANNEL, C_DEL_CONTACT, C_REBOOT, C_FACTORY, C_RPT_REBOOT, C_RPT_SF6, C_CLEAR_MSGS, C_CLEAR_PW, C_CLEAR_BONDS, C_DEL_THREAD, C_WIFI_DL, C_WIFI_AP, C_BT_RESTART, C_WIFI_OTA, C_OTA_INSTALL };
  struct Thread { char name[32]; uint8_t chan; uint8_t unread; uint32_t last; int16_t last_msg; };
  struct Nav { uint8_t scr; int16_t sel, top; };
  struct Cfg { uint32_t magic; uint8_t show_path, sleep_idx, kb_light, adv_idx, ct_filter, rpt_sort, no_wake, lock, bright, no_msg_snd, adv_snd, map_range, bt_off; };   // map_range: 0 = cała mapa, 1 = bez zoomu 15, 2 = do zoomu 13;   // bright: 0 = nie ustawiono (pełna jasność), 1..5 = poziom
  struct Pw { uint8_t key[6]; char pw[16]; };
  struct WifiCfg { char ssid[33]; char pass[64]; char url[128]; };

  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _prefs;
  M9Keyboard _kb;
  genericBuzzer _buzzer;

  uint16_t* _fb;
  UiView* _v;
  UiMsg* _msgs; int _msg_n;
  Thread _threads[UI_THREAD_MAX]; int _thread_n;
  uint16_t* _list; int _list_n;          // indeksy kontaktów dla bieżącej listy
  UiKV* _kv; int _kv_n; char _kv_title[48], _kv_right[80];

  Nav _nav[12]; int _depth; int16_t _tabsel[5];
  Cfg _cfg;
  Pw _pw[UI_PW_MAX]; int _pw_n;

  // rozmowa i pole pisania
  char _cur[32]; bool _cur_chan; int _scroll_unread;   // pierwsza nieprzeczytana (indeks w _msgs) do pokazania po wejściu, -1 = dół
  int _scroll_px, _scroll_tgt;           // przewinięcie rozmowy od dołu w pikselach: bieżące i docelowe (animacja)
  unsigned long _scroll_next;            // następna klatka animacji
  int _th_total, _th_n, _th_sel;         // wysokość całej rozmowy, liczba wiadomości, wybrana (indeks w _msgs)
  int16_t* _th_idx; uint16_t* _th_h;     // wiadomości rozmowy od najnowszej i wysokości ich dymków
  char _input[UI_INPUT_MAX * 2 + 4]; int _input_chars; bool _polish; uint8_t _input_mode;
  // wybrany kontakt / wiadomość / menu
  uint8_t _ck[32]; char _cname[32]; uint8_t _ctype; bool _cfav;
  bool _chas_pos; double _clat, _clon;
  // mapa z karty SD
  M9Map _map; bool _map_ready, _sd_pending;
  double _map_lat, _map_lon; int _map_zoom; bool _map_follow;
  WifiCfg _wifi;
  // trasa wiadomości na mapie
  struct RoutePt { double lat, lon; char label[24]; uint8_t kind; };
  RoutePt _route[16]; int _route_n; bool _route_star; int _route_unknown; char _route_title[40];
  unsigned long _ota_reboot_at;
  unsigned long _sd_test_at;             // zaplanowany test karty SD (po narysowaniu okienka)
  bool _bt_boot_done; int _wifi_bars;
  bool _bt_killed;                       // Bluetooth wyłączony na czas WiFi (do restartu)
  uint32_t _adv_seen;                    // ostatni advert, dla którego zagrał dźwięk
  int _tf[5]; int _tf_sel;               // ustawianie daty i godziny: rok, miesiąc, dzień, godzina, minuta
  UiMsg* _snap;                          // kopia wiadomości na czas zapisu
  int _msg_idx;
  uint8_t _menu_act[UI_MENU_MAX]; int _menu_n; char _menu_title[32]; bool _menu_msg, _menu_thread;
  char _tname[32]; bool _tchan;          // rozmowa wybrana w menu listy Wiadomości
  // wybór wartości ustawienia
  uint8_t _pick_id; char _pick_opt[UI_PICK_MAX][48]; int _pick_n, _pick_cur; char _pick_title[48];
  // wyciszone kanały i rozmowy
  char _muted[UI_MUTED_MAX][32]; int _muted_n;
  // dolny pasek, blokada, sąsiedzi repeatera
  bool _bar; int8_t _bar_sel;
  bool _locked; unsigned long _ul_first, _ul_last; bool _ul_back;
  uint16_t _nb_total, _nb_got; bool _kv_more;
  uint32_t _ble_conn_seen, _ble_auth_seen; unsigned long _ble_pin_at; bool _pin_popup;
  char _search[16];
  // radio: wartości przed „Zastosuj”
  float _rf, _rbw; uint8_t _rsf, _rcr;
  // repeater: logowanie i czekanie na odpowiedź
  uint8_t _wait_kind; unsigned long _wait_until; char _cli_cmd[48]; char _pw_try[16]; bool _pw_new;
  unsigned long _disc_until;

  bool _dirty, _awake, _msgs_dirty;
  unsigned long _next_tick, _popup_until, _sleep_at, _boot_until, _save_at, _next_advert, _last_key;
  char _pop1[56], _pop2[80]; bool _pop_danger;
  uint8_t _confirm; char _confirm_name[32];
  int _msgcount;

  // nawigacja
  uint8_t scr() const { return _nav[_depth - 1].scr; }
  int16_t& sel() { return _nav[_depth - 1].sel; }
  void tab(uint8_t s);
  void push(uint8_t s);
  void pop();
  void prepare(uint8_t s);
  void popup(const char* a, const char* b = "", bool danger = false, int ms = 2500);
  void ask(uint8_t what, const char* a, const char* name);
  void wake();

  // dane
  UiMsg* addMsg(const char* thread, const char* who, const char* text, bool chan, bool out, uint8_t hops);
  void buildThreads();
  void buildContacts();
  void buildRpts();
  void sortList(bool by_dist);
  void openThread(const char* name, bool chan);
  void openInput(uint8_t mode, const char* initial);
  void markRead(const char* name, bool chan);
  int  unreadTotal();
  void submitInput();
  bool sendText(const char* text);
  void pollSent();
  bool timeFromGps();
  void radioLoad();
  bool radioChanged();
  bool ctByKey(const uint8_t* key, ContactInfo& c);
  void setCur(const ContactInfo& c);
  void rptName(const uint8_t* id, int n, char* dest, size_t size);
  void openMenuContact();
  void openMenuMsg(int idx);
  void doAct(uint8_t act);
  void doConfirm(uint8_t what);
  void kvOpen(const char* title, const char* right);
  void kvAdd(const char* k, const char* v = "");
  void kvText(const char* text);
  void detailContact();
  void detailMsg(int idx);
  void detailInfo();

  // repeater
  void adminWait(uint8_t kind);
  void adminLoop();
  void adminCli(const char* cmd);
  const char* pwGet(const uint8_t* key);
  void pwSet(const uint8_t* key, const char* pw);

  // zapis
  void loadAll();
  void saveCfg();
  void savePw();
  void saveMsgs();
  int  readMsgFile(const char* path);
  int _msg_loaded, _msg_src, _msg_saved; uint32_t _msg_saved_at;   // diagnostyka zapisu wiadomości
  bool saveStep();

  // klawisze
  void key(uint8_t k);
  void keyThread(uint8_t k);
  int  listCount(uint8_t s);
  void listRow(uint8_t s, int i, UiRow& r);
  void listHead(uint8_t s);
  void listEnter(uint8_t s, int i);
  bool listKey(uint8_t s, int i, uint8_t k);
  void back();

  // rysowanie
  void fill();
  void fillStatus();
  void fillStart();
  void openMap();
  void drawMap();
  void keyMap(uint8_t k);
  void saveWifi();
  void openTime();
  bool buildRoute(int idx);
  void fitRoute();
  bool findRepeater(const uint8_t* id, int n, double plat, double plon, bool have_prev, ContactInfo& out);
  void wifiPrepare();
  void keyTime(uint8_t k);
  void fillTime();
  void setLocalTime(int Y, int M, int D, int h, int mi);
  int  brightPercent();
  void fillLock();
  void lockKey(uint8_t k);
  void pickBegin(uint8_t id, const char* title);
  void pickAdd(const char* text);
  void pickOpen(int cur);
  void applyPick(uint8_t id, int i);
  bool settingPick(uint8_t s, int i);
  bool isMuted(const char* name);
  void toggleMute(const char* name);
  void saveMuted();
  void openMenuThread(int idx);
  void fillSettings();
  bool gridKey(uint8_t k, int count, int cols);
  void fillThread();
  void makeBubble(const UiMsg& m, UiBubble& b);
  void scrollReset() { _scroll_px = _scroll_tgt = 0; }
  void fillList();
  void fillKv();
  void draw();

  void fmtAgo(char* dest, size_t n, uint32_t ts);
  void fmtClock(char* dest, size_t n, uint32_t ts, bool date_if_old);
  bool gpsFix(double* lat, double* lon, int* sats);

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial);
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override { _dirty = true; }
  void loop() override;
};

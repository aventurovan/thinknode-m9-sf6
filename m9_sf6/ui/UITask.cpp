#include "UITask.h"
#include "M9Lcd.h"
#include "M9Ble.h"
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

static const int8_t TX_STEPS[] = { 10, 14, 17, 20, 22 };
static const float BW_STEPS[] = { 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };
static const uint32_t SLEEP_STEPS[] = { 30000UL, 60000UL, 300000UL, 0 };            // 0 = nigdy
static const char* const SLEEP_NAMES[] = { "30 s", "1 min", "5 min", "nigdy" };
static const uint8_t ADV_HOURS[] = { 0, 1, 3, 6, 12, 24 };
static const char* const FILTER_NAMES[] = { "wszystkie", "osoby", "repeatery", "pokoje", "czujniki", "ulubione" };
static const char* const QUICK[] = { "OK", "Tak", "Nie", "Zaraz będę", "Oddzwonię później", "Moja pozycja", "Moja pozycja + link do mapy" };
static const char* const CLI_CMDS[] = { "ver", "clock", "neighbors", "get radio", "get tx", "get name", "get repeat" };

// ---------------- pomocnicze ----------------

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
  _cur[0] = 0; _cur_chan = false; _scroll = 0;
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

  loadAll();
  _kb.begin();
  _buzzer.begin();
  _buzzer.quiet(_prefs->buzzer_quiet);
  if (_display) _display->turnOn();
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
    Cfg c;
    if (f.read((uint8_t*)&c, sizeof(c)) == sizeof(c) && c.magic == CFG_MAGIC) _cfg = c;
    f.close();
  }
  if (_cfg.sleep_idx > 3) _cfg.sleep_idx = 1;
  if (_cfg.adv_idx >= sizeof(ADV_HOURS)) _cfg.adv_idx = 0;
  if (_cfg.ct_filter > 6) _cfg.ct_filter = 0;

  f = SPIFFS.open(FILE_PW, "r");
  if (f) {
    uint8_t n = 0;
    if (f.read(&n, 1) == 1 && n <= UI_PW_MAX && f.read((uint8_t*)_pw, sizeof(Pw) * n) == sizeof(Pw) * n) _pw_n = n;
    f.close();
  }

  f = SPIFFS.open(FILE_MSGS, "r");
  if (f && _msgs) {
    uint8_t h[4];
    if (f.read(h, 4) == 4 && h[0] == 'M' && h[1] == '9' && h[2] == (uint8_t)(sizeof(UiMsg) & 0xFF) && h[3] <= UI_MSG_MAX) {
      if (f.read((uint8_t*)_msgs, sizeof(UiMsg) * h[3]) == sizeof(UiMsg) * h[3]) _msg_n = h[3];
    }
    for (int i = 0; i < _msg_n; i++) _msgs[i].sent_id = 0;       // numery wysyłek nie przeżywają restartu
  }
  if (f) f.close();
}

void UITask::saveCfg() {
  File f = SPIFFS.open(FILE_CFG, "w");
  if (!f) return;
  f.write((const uint8_t*)&_cfg, sizeof(_cfg));
  f.close();
}

void UITask::savePw() {
  File f = SPIFFS.open(FILE_PW, "w");
  if (!f) return;
  uint8_t n = (uint8_t)_pw_n;
  f.write(&n, 1);
  f.write((const uint8_t*)_pw, sizeof(Pw) * _pw_n);
  f.close();
}

void UITask::saveMsgs() {
  _msgs_dirty = false;
  if (_msgs == NULL) return;
  File f = SPIFFS.open(FILE_MSGS, "w");
  if (!f) return;
  uint8_t h[4] = { 'M', '9', (uint8_t)(sizeof(UiMsg) & 0xFF), (uint8_t)_msg_n };
  bool ok = f.write(h, 4) == 4 && f.write((const uint8_t*)_msgs, sizeof(UiMsg) * _msg_n) == sizeof(UiMsg) * _msg_n;
  f.close();
  if (!ok) SPIFFS.remove(FILE_MSGS);                 // brak miejsca: lepiej bez pliku niż z uszkodzonym
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
  _save_at = millis() + 30000UL;
  return &m;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  bool chan = false;
  ChannelDetails cd;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!the_mesh.getChannel(i, cd)) break;
    if (cd.name[0] && strcmp(cd.name, from_name) == 0) { chan = true; break; }
  }
  char who[24]; who[0] = 0;
  const char* body = text;
  if (chan) {                                       // na kanale tekst ma postać „Nadawca: treść”
    const char* sep = strstr(text, ": ");
    if (sep && sep - text < (int)sizeof(who)) {
      memcpy(who, text, sep - text); who[sep - text] = 0;
      body = sep + 2;
    }
  }
  UiMsg* m = addMsg(from_name, who, body, chan, false, the_mesh.ui_rx_hops);
  if (m) {
    m->direct = the_mesh.ui_rx_direct;
    m->hash_size = the_mesh.ui_rx_hash_size;
    m->path_bytes = the_mesh.ui_rx_path_bytes > sizeof(m->path) ? sizeof(m->path) : the_mesh.ui_rx_path_bytes;
    memcpy(m->path, the_mesh.ui_rx_path, m->path_bytes);
    m->snr_x4 = the_mesh.ui_rx_snr_x4; m->rssi = the_mesh.ui_rx_rssi;
  }
  if (!_prefs->buzzer_quiet && !_buzzer.isPlaying()) _buzzer.play(chan ? "ch:d=16,o=6,b=200:e,g" : "dm:d=16,o=6,b=200:e,g,c7");
  if (!_cfg.no_wake || _awake) wake();
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
      if (!_prefs->buzzer_quiet && !_buzzer.isPlaying() && ((u->repeats > 0 && m.repeats == 0) || (u->delivered && !m.delivered))) _buzzer.play("ack:d=32,o=8,b=120:c");
      m.repeats = u->repeats; m.delivered = u->delivered;
      m.rep_n = u->rep_n; m.rep_hs = u->rep_hs;
      memcpy(m.rep_id, u->rep_id, sizeof(m.rep_id)); memcpy(m.rep_snr, u->rep_snr, sizeof(m.rep_snr));
      _dirty = true; _msgs_dirty = true; _save_at = millis() + 30000UL;
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
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].unread && _msgs[i].chan == chan && strcmp(_msgs[i].thread, name) == 0) { _msgs[i].unread = 0; _msgs_dirty = true; _save_at = millis() + 30000UL; }
}

void UITask::openThread(const char* name, bool chan) {
  scopy(_cur, sizeof(_cur), name);
  _cur_chan = chan;
  _scroll = 0;
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
  else id = the_mesh.uiSendDirectText(_cur, text);
  if (!id) { popup("Nie udało się wysłać", "spróbuj jeszcze raz", true); return false; }
  UiMsg* m = addMsg(_cur, "", text, _cur_chan, true, 0);
  if (m) m->sent_id = id;
  _scroll = 0;
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
    the_mesh.getContactByIdx(_list[i], c);
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
    bool ok = true;
    switch (_cfg.ct_filter) {
      case 1: ok = c.type == ADV_TYPE_CHAT; break;
      case 2: ok = c.type == ADV_TYPE_REPEATER; break;
      case 3: ok = c.type == ADV_TYPE_ROOM; break;
      case 4: ok = c.type == ADV_TYPE_SENSOR; break;
      case 5: ok = (c.flags & 1) != 0; break;
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
  if (s != S_CONTACTS) _search[0] = 0;
  _depth = 1;
  _nav[0].scr = s; _nav[0].sel = _tabsel[s]; _nav[0].top = 0;
  prepare(s);
}

void UITask::push(uint8_t s) {
  if (_depth >= (int)(sizeof(_nav) / sizeof(_nav[0]))) return;
  _nav[_depth].scr = s; _nav[_depth].sel = 0; _nav[_depth].top = 0;
  _depth++;
  prepare(s);
}

void UITask::pop() {
  if (_depth > 1) {
    _depth--;
    uint8_t s = scr();
    if (s == S_DISC) _dirty = true; else prepare(s);       // powrót do wyników discovery nie wysyła nowego pingu
  } else if (scr() != S_START) tab(S_START);
}

void UITask::back() {
  if (scr() == S_THREAD && _input_mode != IN_MSG) _input_mode = IN_MSG;   // anulowane pole do wpisania
  if (scr() == S_ADMIN) the_mesh.uiAdminLogout();
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

void UITask::wake() {
  uint32_t ms = SLEEP_STEPS[_cfg.sleep_idx];
  _sleep_at = ms ? millis() + ms : 0;
  if (!_awake) {
    _awake = true;
    if (_display) _display->turnOn();
  }
  _kb.setBacklight(_cfg.kb_light != 0);
  _dirty = true;
}

// ---------------- szczegóły (ekran z wierszami nazwa–wartość) ----------------

void UITask::kvOpen(const char* title, const char* right) {
  _kv_n = 0;
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
    if (m.direct) kvAdd("Trasa", "znaną drogą (bez listy skoków)");
    else if (m.hops == 0) kvAdd("Trasa", "bezpośrednio, bez skoków");
    else {
      snprintf(t, sizeof(t), "skoki: %d", m.hops); kvAdd("Trasa", t);
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
  unsigned long up = millis() / 60000UL;
  snprintf(t, sizeof(t), "%lu godz. %lu min", up / 60, up % 60); kvAdd("Czas pracy", t);
  const uint8_t* pk = the_mesh.self_id.pub_key;
  snprintf(t, sizeof(t), "%02x%02x%02x%02x %02x%02x%02x%02x", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], pk[6], pk[7]); kvAdd("Mój klucz (początek)", t);
  push(S_KV);
}

// ---------------- menu kontaktu i wiadomości ----------------

void UITask::openMenuContact() {
  _menu_n = 0; _menu_msg = false;
  scopy(_menu_title, sizeof(_menu_title), _cname);
  if (_ctype == ADV_TYPE_CHAT || _ctype == ADV_TYPE_ROOM) _menu_act[_menu_n++] = A_WRITE;
  if (_ctype == ADV_TYPE_REPEATER || _ctype == ADV_TYPE_ROOM) _menu_act[_menu_n++] = A_MANAGE;
  _menu_act[_menu_n++] = A_FAV;
  _menu_act[_menu_n++] = A_DETAIL;
  _menu_act[_menu_n++] = A_DELETE;
  push(S_MENU);
}

void UITask::openMenuMsg(int idx) {
  if (idx < 0 || idx >= _msg_n) return;
  _msg_idx = idx;
  _menu_n = 0; _menu_msg = true;
  UiMsg& m = _msgs[idx];
  scopy(_menu_title, sizeof(_menu_title), m.out ? "Moja wiadomość" : (m.who[0] ? m.who : m.thread));
  _menu_act[_menu_n++] = A_DETAIL;
  if (!m.out && m.chan && m.who[0]) _menu_act[_menu_n++] = A_REPLY;
  if (m.out) _menu_act[_menu_n++] = A_RESEND;
  _menu_act[_menu_n++] = A_COPY;
  _menu_act[_menu_n++] = A_DELMSG;
  push(S_MENU);
}

void UITask::doAct(uint8_t act) {
  if (_menu_msg) {
    if (_msg_idx < 0 || _msg_idx >= _msg_n) { pop(); return; }
    UiMsg& m = _msgs[_msg_idx];
    switch (act) {
      case A_DETAIL: detailMsg(_msg_idx); break;
      case A_REPLY: pop(); snprintf(_input, sizeof(_input), "@[%s] ", m.who); _input_chars = strlen(_input); break;
      case A_COPY: pop(); scopy(_input, sizeof(_input), m.text); _input_chars = 0; for (const char* p = _input; *p; p++) if (((uint8_t)*p & 0xC0) != 0x80) _input_chars++; break;
      case A_RESEND: { char t[164]; scopy(t, sizeof(t), m.text); pop(); sendText(t); break; }
      case A_DELMSG:
        memmove(&_msgs[_msg_idx], &_msgs[_msg_idx + 1], sizeof(UiMsg) * (_msg_n - _msg_idx - 1)); _msg_n--;
        _msgs_dirty = true; _save_at = millis() + 30000UL; _scroll = 0; _msg_idx = -1;
        pop();
        break;
    }
    return;
  }
  ContactInfo c;
  switch (act) {
    case A_WRITE: pop(); openThread(_cname, false); break;
    case A_FAV: {
      int r = the_mesh.uiToggleFavourite(_ck);
      if (r >= 0) { _cfav = r == 1; popup(r == 1 ? "Dodano do ulubionych" : "Usunięto z ulubionych", _cname); }
      break;
    }
    case A_DETAIL: detailContact(); break;
    case A_MANAGE: if (ctByKey(_ck, c)) push(S_LOGIN); break;
    case A_DELETE: { char t[40]; snprintf(t, sizeof(t), "Usunąć kontakt?"); ask(C_DEL_CONTACT, t, _cname); break; }
  }
}

void UITask::doConfirm(uint8_t what) {
  switch (what) {
    case C_DEL_CHANNEL: {
      bool ok = the_mesh.uiRemoveChannel(_confirm_name);
      for (int i = 0; i < _msg_n; ) {                 // razem z kanałem znikają jego wiadomości
        if (_msgs[i].chan && strcmp(_msgs[i].thread, _confirm_name) == 0) { memmove(&_msgs[i], &_msgs[i + 1], sizeof(UiMsg) * (_msg_n - i - 1)); _msg_n--; }
        else i++;
      }
      _msgs_dirty = true; _save_at = millis() + 30000UL;
      prepare(S_MSGS);
      popup(ok ? "Kanał usunięty" : "Nie udało się usunąć", _confirm_name, !ok);
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
    snprintf(t, sizeof(t), "sąsiedzi: %u", total);
    kvOpen(_cname, t);
    for (int i = 0; i < cnt; i++) {
      int o = 4 + i * 11; if (o + 11 > a.nb_len) break;
      char nm[48], sn[16];
      rptName(a.nb + o, 3, nm, sizeof(nm));
      fmtSnr(sn, sizeof(sn), (int8_t)a.nb[o + 10]);
      snprintf(k, sizeof(k), "%s", nm); kvAdd(k, sn);
    }
    if (_kv_n == 0) kvAdd("Brak sąsiadów", "");
    push(S_KV);
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
    if (k == M9K_UP) { _scroll++; return; }
    if (k == M9K_DOWN) { if (_scroll > 0) _scroll--; return; }
    if (k == M9K_RIGHT) {                             // menu wiadomości pokazanej na dole (po ▲ jest zaznaczona)
      int skip = _scroll;
      for (int i = _msg_n - 1; i >= 0; i--) {
        if (_msgs[i].chan != _cur_chan || strcmp(_msgs[i].thread, _cur) != 0) continue;
        if (skip > 0) { skip--; continue; }
        openMenuMsg(i);
        break;
      }
      return;
    }
    if (k == M9K_F4) {                                // pinezka: wstaw moją pozycję
      double lat, lon; int sats;
      if (gpsFix(&lat, &lon, &sats)) {
        char t[48]; snprintf(t, sizeof(t), "Moja pozycja: %.5f, %.5f", lat, lon);
        if (_input_chars + (int)strlen(t) <= UI_INPUT_MAX) { strcat(_input, t); _input_chars += strlen(t); }
      } else popup("Brak pozycji GPS", "włącz GPS w Ustawieniach i wyjdź pod niebo");
      return;
    }
  }
  if (mode == IN_TIME && k == M9K_F4) {               // pinezka: zegar z GPS
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
  }
  if (digits && !((k >= '0' && k <= '9') || k == '.' || k == ',' || k == '-')) return;
  if (k >= 32 && k < 127 && _input_chars < limit) {
    const char* pl = (_polish && mode != IN_PASS && mode != IN_CLI) ? polishFor(k) : NULL;
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
#define FILTER_HEARD 6
#define N_CLI ((int)(sizeof(CLI_CMDS) / sizeof(CLI_CMDS[0])))

int UITask::listCount(uint8_t s) {
  switch (s) {
    case S_MSGS: return _thread_n + 1;
    case S_CONTACTS: return 1 + (_cfg.ct_filter == FILTER_HEARD ? the_mesh.ui_heard_n : _list_n);   // wiersz 0 = filtry
    case S_RPTS: return _list_n + 1;
    case S_HEARD: return the_mesh.ui_heard_n;
    case S_MENU: return _menu_n;
    case S_QUICK: return 7;
    case S_DISC: return the_mesh.ui_disc_n + 1;
    case S_LOGIN: return pwGet(_ck) ? 3 : 2;
    case S_ADMIN: return 5;
    case S_ADMIN_ACT: return 4;
    case S_ADMIN_CLI: return N_CLI + 1;
    case S_RADIO: return 10;
    case S_SET_BT: return 4;
    case S_SET_POS: return 9;
    case S_SET_ADV: return 3;
    case S_SET_CT: return 7;
    case S_SET_MSG: return 4;
    case S_SET_SCR: return 3;
    case S_SET_SYS: return 6;
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
      if (v.unread > 0) snprintf(v.title_right, sizeof(v.title_right), "nieprzeczytane: %d", v.unread); else strcpy(v.title_right, "Del: usuń kanał");
      break;
    case S_CONTACTS: {
      strcpy(v.title, "Kontakty");
      if (_search[0]) snprintf(v.title_right, sizeof(v.title_right), "szukam: %s · %d", _search, _list_n);
      else if (_cfg.ct_filter == FILTER_HEARD) strcpy(v.title_right, "Enter: dodaj do kontaktów");
      else snprintf(v.title_right, sizeof(v.title_right), "%d · pisz, aby szukać", _list_n);
      static const char* const C[7] = { "Wszyscy", "Osoby", "RPT", "Pokoje", "Czujn.", "Ulub.", "Słyszane" };
      for (int i = 0; i < 7; i++) scopy(v.chips[i], sizeof(v.chips[0]), C[i]);
      v.nchips = 7; v.chip_active = _cfg.ct_filter; v.chip_focus = sel() == 0;
      strcpy(v.empty, _cfg.ct_filter == FILTER_HEARD ? "Od włączenia urządzenie nie usłyszało jeszcze żadnego advertu." : "Brak kontaktów dla tego filtra.");
      break;
    }
    case S_RPTS:
      strcpy(v.title, "Repeatery");
      snprintf(v.title_right, sizeof(v.title_right), "%d · %s · Pinezka: zmień", _list_n, _cfg.rpt_sort ? "wg odległości" : "ostatnio słyszane");
      break;
    case S_MENU: scopy(v.title, sizeof(v.title), _menu_title); hint("Enter", "wybierz"); hint("Wstecz", ""); break;
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
      hint("Enter", "wybierz"); hint("◄►", "zmień"); hint("Wstecz", "");
      break;
    default: {
      static const char* const T[] = { "Bluetooth", "Pozycja i prywatność", "Advert", "Kontakty", "Wiadomości", "Ekran i dźwięk", "System" };
      if (s >= S_SET_BT && s <= S_SET_SYS) strcpy(v.title, T[s - S_SET_BT]);
      strcpy(v.title_right, "Ustawienia");
      hint("Enter", "zmień"); hint("◄►", "zmień"); hint("Wstecz", "");
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
      setRow(r, th.name, "", th.chan ? UII_HASH : UII_USER);
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
      setRow(r, c.name, t, typeIcon(c.type, c.flags & 1), c.flags & 1);
      if (_cfg.ct_filter == 0 || _cfg.ct_filter == 5) scopy(r.sub, sizeof(r.sub), typeName(c.type));
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
        case A_DETAIL: setRow(r, "Szczegóły", _menu_msg ? "trasa, sygnał, czas" : "pozycja, droga, klucz"); break;
        case A_MANAGE: setRow(r, "Zarządzaj", pwGet(_ck) ? "hasło zapamiętane" : "logowanie", UII_LOCK); break;
        case A_DELETE: setRow(r, "Usuń kontakt"); break;
        case A_REPLY: setRow(r, "Odpowiedz", "wstawia @[nadawca]", UII_MAIL); break;
        case A_RESEND: setRow(r, "Wyślij ponownie"); break;
        case A_COPY: setRow(r, "Kopiuj treść do pola pisania"); break;
        case A_DELMSG: setRow(r, "Usuń wiadomość", "tylko z tego urządzenia"); break;
      }
      return;
    case S_QUICK: setRow(r, QUICK[i], "", i >= 5 ? UII_GPS : UII_NONE); return;
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
      static const char* const N[10] = { "SF6 — MeshCore Południe", "EU/UK Narrow (SF8)", "Częstotliwość", "Pasmo (BW)", "Rozpraszanie (SF)", "Kodowanie (CR)", "Zastosuj własne ustawienia",
                                         "Moc nadawania", "Bajty trasy", "Limit nadawania" };
      bool sf6 = the_mesh.uiRadioIsSF6();
      bool narrow = fabsf(_prefs->freq - 869.618f) < 0.005f && fabsf(_prefs->bw - 62.5f) < 0.1f && _prefs->sf == 8 && _prefs->cr == 8;
      t[0] = 0;
      switch (i) {
        case 0: strcpy(t, sf6 ? "aktywny" : "Enter: włącz"); break;
        case 1: strcpy(t, narrow ? "aktywny" : "Enter: włącz"); break;
        case 2: snprintf(t, sizeof(t), "%.3f MHz", _rf); break;
        case 3: snprintf(t, sizeof(t), "%.4g kHz", _rbw); break;
        case 4: snprintf(t, sizeof(t), "SF%d", _rsf); break;
        case 5: snprintf(t, sizeof(t), "4/%d", _rcr); break;
        case 6: strcpy(t, radioChanged() ? "Enter: zapisz i włącz" : "brak zmian"); break;
        case 7: snprintf(t, sizeof(t), "%d dBm", _prefs->tx_power_dbm); break;
        case 8: snprintf(t, sizeof(t), "%d (identyfikator: %d znaki)", _prefs->path_hash_mode + 1, (_prefs->path_hash_mode + 1) * 2); break;
        case 9: { int k = 1; for (int q = 0; q < 5; q++) if (fabsf(AIRTIME_STEPS[q] - _prefs->airtime_factor) < 0.4f) k = q; strcpy(t, AIRTIME_NAMES[k]); break; }
      }
      if (i != 9) comma(t);
      setRow(r, N[i], t, i < 2 ? UII_STAR : UII_NONE, (i == 0 && sf6) || (i == 1 && narrow));
      return;
    }
    case S_SET_BT: {
      if (i == 0) setRow(r, "Bluetooth", isBluetoothEnabled() ? (hasConnection() ? "włączony · połączony z aplikacją" : "włączony · czeka na telefon") : "wyłączony", UII_BT);
      else if (i == 1) { snprintf(t, sizeof(t), "%06lu", (unsigned long)the_mesh.getBLEPin()); setRow(r, "PIN do parowania", t, UII_LOCK); }
      else if (i == 2) { int n = m9bleBondCount(); if (n < 0) strcpy(t, "—"); else snprintf(t, sizeof(t), "%d", n); setRow(r, "Zapamiętane telefony", t); }
      else setRow(r, "Zapomnij wszystkie telefony", "gdy parowanie nie działa");
      return;
    }
    case S_SET_POS: {
      static const char* const N[9] = { "GPS", "Pozycja w advercie", "Szerokość (ręcznie)", "Długość (ręcznie)", "Zapisz pozycję z GPS", "Teraz z GPS",
                                        "Stan baterii udostępniaj", "Pozycję udostępniaj", "Dane czujników udostępniaj" };
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
      }
      setRow(r, N[i], t, i == 0 ? UII_GPS : (i >= 6 ? UII_SHIELD : UII_NONE));
      return;
    }
    case S_SET_ADV:
      if (i == 0) setRow(r, "Wyślij do sąsiadów", "bez przekazywania");
      else if (i == 1) setRow(r, "Wyślij do całej sieci", "przez repeatery");
      else { if (ADV_HOURS[_cfg.adv_idx]) snprintf(t, sizeof(t), "co %d godz. (do sąsiadów)", ADV_HOURS[_cfg.adv_idx]); else strcpy(t, "wyłączony"); setRow(r, "Automatyczny advert", t); }
      return;
    case S_SET_CT: {
      static const char* const N[7] = { "Dodawanie z advertów", "Osoby", "Repeatery", "Pokoje", "Czujniki", "Gdy brak miejsca", "Z jak daleka" };
      bool sel = _prefs->manual_add_contacts & 1;
      t[0] = 0;
      if (i == 0) strcpy(t, sel ? "tylko wybrane typy" : "wszystko automatycznie");
      else if (i <= 4) strcpy(t, sel ? ((_prefs->autoadd_config & AA_BITS[i - 1]) ? "dodawaj" : "nie dodawaj") : "dodawaj (wszystko)");
      else if (i == 5) strcpy(t, (_prefs->autoadd_config & AA_OVERWRITE) ? "nadpisuj najstarsze" : "nie dodawaj nowych");
      else if (_prefs->autoadd_max_hops == 0) strcpy(t, "bez limitu skoków");
      else if (_prefs->autoadd_max_hops == 1) strcpy(t, "tylko słyszane bezpośrednio");
      else snprintf(t, sizeof(t), "do %d skoków", _prefs->autoadd_max_hops - 1);
      setRow(r, N[i], t);
      return;
    }
    case S_SET_MSG:
      if (i == 0) setRow(r, "Trasa pod wiadomością", _cfg.show_path ? "identyfikatory repeaterów" : "tylko liczba skoków");
      else if (i == 1) setRow(r, "Dodatkowe potwierdzenia", _prefs->multi_acks ? "włączone (pewniej, więcej ruchu)" : "wyłączone");
      else if (i == 2) setRow(r, "Dźwięk wiadomości", onOff(!_prefs->buzzer_quiet));
      else { snprintf(t, sizeof(t), "zapisanych: %d", _msg_n); setRow(r, "Usuń wszystkie wiadomości", t); }
      return;
    case S_SET_SCR:
      if (i == 0) setRow(r, "Wygaszanie ekranu", SLEEP_NAMES[_cfg.sleep_idx], UII_SCREEN);
      else if (i == 1) setRow(r, "Podświetlenie klawiatury", _cfg.kb_light ? "świeci, gdy ekran świeci" : "wyłączone");
      else setRow(r, "Nowa wiadomość budzi ekran", yesNo(!_cfg.no_wake));
      return;
    case S_SET_SYS: {
      if (i == 0) setRow(r, "Informacje", "wersja, pamięć, klucz");
      else if (i == 1) setRow(r, "Nazwa urządzenia", _prefs->node_name, UII_USER);
      else if (i == 2) { struct tm lt; localTime(the_mesh.getRTCClock()->getCurrentTime(), &lt); snprintf(t, sizeof(t), "%d.%02d.%04d %d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900, lt.tm_hour, lt.tm_min); setRow(r, "Data i godzina", t); }
      else if (i == 3) { snprintf(t, sizeof(t), "zapamiętanych: %d", _pw_n); setRow(r, "Usuń hasła repeaterów", t, UII_LOCK); }
      else if (i == 4) setRow(r, "Restart urządzenia", "z potwierdzeniem");
      else setRow(r, "Ustawienia fabryczne", "kasuje wszystko");
      return;
    }
  }
}

void UITask::listEnter(uint8_t s, int i) {
  ContactInfo c;
  switch (s) {
    case S_MSGS:
      if (i < _thread_n) openThread(_threads[i].name, _threads[i].chan); else openInput(IN_CHANNEL, "#");
      return;
    case S_CONTACTS:
      if (i == 0) { sel() = 1; return; }              // Enter na filtrach: przejdź do listy
      if (_cfg.ct_filter == FILTER_HEARD) { listEnter(S_HEARD, i - 1); return; }
      if (the_mesh.getContactByIdx(_list[i - 1], c)) { setCur(c); openMenuContact(); }
      return;
    case S_RPTS:
      if (i == 0) { push(S_DISC); return; }
      if (the_mesh.getContactByIdx(_list[i - 1], c)) { setCur(c); openMenuContact(); }
      return;
    case S_HEARD:
      if (the_mesh.uiIsContact(the_mesh.ui_heard[i].id.pub_key)) { if (ctByKey(the_mesh.ui_heard[i].id.pub_key, c)) { setCur(c); openMenuContact(); } }
      else if (the_mesh.uiAddHeard(i)) popup("Dodano do kontaktów", the_mesh.ui_heard[i].name);
      else popup("Nie udało się dodać", "brak miejsca w kontaktach?", true);
      return;
    case S_MENU: doAct(_menu_act[i]); return;
    case S_QUICK: {
      char t[100];
      if (i < 5) scopy(t, sizeof(t), QUICK[i]);
      else {
        double lat, lon; int sats;
        if (!gpsFix(&lat, &lon, &sats)) { popup("Brak pozycji GPS", "włącz GPS w Ustawieniach i wyjdź pod niebo"); return; }
        if (i == 5) snprintf(t, sizeof(t), "Moja pozycja: %.5f, %.5f", lat, lon);
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
      if (i == 1) { if (the_mesh.uiAdminNeighbours()) adminWait(3); else popup("Nie udało się wysłać", "", true); }
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
      if (i == 0) { the_mesh.uiSetRadioSF6(); radioLoad(); popup("Radio: SF6 — MeshCore Południe", "869,618 MHz · BW 62,5 · SF6 · CR 4/8"); }
      else if (i == 1) { the_mesh.uiSetRadio(869.618f, 62.5f, 8, 8); radioLoad(); popup("Radio: EU/UK Narrow", "869,618 MHz · BW 62,5 · SF8 · CR 4/8"); }
      else if (i == 2) { char t[16]; snprintf(t, sizeof(t), "%.3f", _rf); openInput(IN_FREQ, t); }
      else if (i == 6) {
        if (!radioChanged()) popup("Brak zmian do zastosowania");
        else { the_mesh.uiSetRadio(_rf, _rbw, _rsf, _rcr); radioLoad(); popup("Radio: ustawienia własne", "zapisane i włączone"); }
      } else listKey(s, i, M9K_RIGHT);
      return;
    case S_SET_BT:
      if (i == 0) {
        if (isBluetoothEnabled()) { disableBluetooth(); popup("Bluetooth wyłączony"); }
        else { char t[40]; snprintf(t, sizeof(t), "PIN do parowania: %06lu", (unsigned long)the_mesh.getBLEPin()); enableBluetooth(); popup("Bluetooth włączony", t, false, 6000); }
      } else if (i == 1) { char t[12]; snprintf(t, sizeof(t), "%06lu", (unsigned long)the_mesh.getBLEPin()); openInput(IN_PIN, t); }
      else if (i == 3) ask(C_CLEAR_BONDS, "Zapomnieć wszystkie telefony?", "");
      return;
    case S_SET_POS:
      if (i == 2) { char t[16]; snprintf(t, sizeof(t), "%.5f", _sensors->node_lat); openInput(IN_LAT, t); }
      else if (i == 3) { char t[16]; snprintf(t, sizeof(t), "%.5f", _sensors->node_lon); openInput(IN_LON, t); }
      else if (i == 4) {
        double lat, lon; int sats;
        if (gpsFix(&lat, &lon, &sats)) { the_mesh.uiSetLatLon(lat, lon); popup("Pozycja zapisana z GPS"); } else popup("Brak pozycji GPS", "wyjdź pod niebo i poczekaj", true);
      } else if (i != 5) listKey(s, i, M9K_RIGHT);
      return;
    case S_SET_ADV:
      if (i == 0) popup(the_mesh.advert() ? "Advert wysłany" : "Błąd advertu", "do sąsiadów (bez przekazywania)");
      else if (i == 1) popup(the_mesh.uiAdvertFlood() ? "Advert wysłany" : "Błąd advertu", "do całej sieci");
      else listKey(s, i, M9K_RIGHT);
      return;
    case S_SET_MSG:
      if (i == 3) ask(C_CLEAR_MSGS, "Usunąć wszystkie wiadomości?", "");
      else listKey(s, i, M9K_RIGHT);
      return;
    case S_SET_SYS:
      if (i == 0) detailInfo();
      else if (i == 1) openInput(IN_NAME, _prefs->node_name);
      else if (i == 2) {
        struct tm lt; localTime(the_mesh.getRTCClock()->getCurrentTime(), &lt);
        char b[24]; snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
        openInput(IN_TIME, b);
      }
      else if (i == 3) ask(C_CLEAR_PW, "Usunąć zapamiętane hasła?", "");
      else if (i == 4) ask(C_REBOOT, "Zrestartować urządzenie?", "");
      else ask(C_FACTORY, "Skasować WSZYSTKO?", "");
      return;
    default: listKey(s, i, M9K_RIGHT); return;        // pozostałe listy ustawień: Enter = następna wartość
  }
}

// ◄ ► (i Enter przez listEnter) na listach ustawień, Del i Pinezka na listach głównych; true = klawisz obsłużony
bool UITask::listKey(uint8_t s, int i, uint8_t k) {
  int dir = k == M9K_LEFT ? -1 : k == M9K_RIGHT ? 1 : 0;
  switch (s) {
    case S_MSGS:
      if ((k == M9K_BKSP || k == M9K_BKSP_LONG) && i < _thread_n && _threads[i].chan) { ask(C_DEL_CHANNEL, "Usunąć kanał?", _threads[i].name); return true; }
      return false;
    case S_CONTACTS:
      if (i == 0 && dir) {                            // chodzenie po filtrach; z brzegu przechodzi na sąsiedni ekran
        int f = (int)_cfg.ct_filter + dir;
        if (f < 0 || f > FILTER_HEARD) return false;
        _cfg.ct_filter = (uint8_t)f; saveCfg(); buildContacts(); return true;
      }
      if (k == M9K_F4) { _cfg.ct_filter = (_cfg.ct_filter + 1) % 7; saveCfg(); buildContacts(); if (sel() > 1) sel() = 1; return true; }
      if (k == M9K_BKSP) { int n = strlen(_search); if (n > 0) { _search[n - 1] = 0; buildContacts(); } return true; }
      if (k == M9K_BKSP_LONG) { _search[0] = 0; buildContacts(); return true; }
      if (k > 32 && k < 127 && _cfg.ct_filter != FILTER_HEARD) { int n = strlen(_search); if (n < (int)sizeof(_search) - 1) { _search[n] = (char)k; _search[n + 1] = 0; buildContacts(); if (sel() > 1) sel() = 1; } return true; }
      return false;
    case S_RPTS:
      if (k == M9K_F4) { _cfg.rpt_sort = !_cfg.rpt_sort; saveCfg(); buildRpts(); return true; }
      return false;
    case S_RADIO:
      if (!dir) return false;
      if (i == 3) { int n = sizeof(BW_STEPS) / sizeof(BW_STEPS[0]), cur = 2; for (int q = 0; q < n; q++) if (fabsf(BW_STEPS[q] - _rbw) < 0.5f) cur = q; _rbw = BW_STEPS[(cur + dir + n) % n]; }
      else if (i == 4) { int v = _rsf + dir; if (v > 12) v = 5; if (v < 5) v = 12; _rsf = v; }
      else if (i == 5) { int v = _rcr + dir; if (v > 8) v = 5; if (v < 5) v = 8; _rcr = v; }
      else if (i == 7) {
        int n = sizeof(TX_STEPS), cur = 0;
        for (int q = 0; q < n; q++) if (TX_STEPS[q] <= _prefs->tx_power_dbm) cur = q;
        cur = (cur + dir + n) % n;
        if (TX_STEPS[cur] > the_mesh.uiMaxTxPower()) cur = dir > 0 ? 0 : n - 1;
        the_mesh.uiSetTxPower(TX_STEPS[cur]);
      } else if (i == 8) { _prefs->path_hash_mode = (_prefs->path_hash_mode + dir + 3) % 3; the_mesh.savePrefs(); }
      else if (i == 9) { int cur = 1; for (int q = 0; q < 5; q++) if (fabsf(AIRTIME_STEPS[q] - _prefs->airtime_factor) < 0.4f) cur = q; _prefs->airtime_factor = AIRTIME_STEPS[(cur + dir + 5) % 5]; the_mesh.savePrefs(); }
      return true;
    case S_SET_BT:
      return dir != 0;
    case S_SET_POS:
      if (!dir) return false;
      if (i == 0) { bool on = !_prefs->gps_enabled; the_mesh.uiSetGps(on); }
      else if (i == 1) { _prefs->advert_loc_policy = _prefs->advert_loc_policy ? 0 : 1; the_mesh.savePrefs(); }
      else if (i == 6) { _prefs->telemetry_mode_base = (_prefs->telemetry_mode_base + dir + 3) % 3; the_mesh.savePrefs(); }
      else if (i == 7) { _prefs->telemetry_mode_loc = (_prefs->telemetry_mode_loc + dir + 3) % 3; the_mesh.savePrefs(); }
      else if (i == 8) { _prefs->telemetry_mode_env = (_prefs->telemetry_mode_env + dir + 3) % 3; the_mesh.savePrefs(); }
      return true;
    case S_SET_ADV:
      if (!dir) return false;
      if (i == 2) {
        _cfg.adv_idx = (_cfg.adv_idx + dir + sizeof(ADV_HOURS)) % sizeof(ADV_HOURS);
        saveCfg();
        _next_advert = ADV_HOURS[_cfg.adv_idx] ? millis() + ADV_HOURS[_cfg.adv_idx] * 3600000UL : 0;
      }
      return true;
    case S_SET_CT:
      if (!dir) return false;
      if (i == 0) {
        _prefs->manual_add_contacts ^= 1;
        if ((_prefs->manual_add_contacts & 1) && (_prefs->autoadd_config & (AA_CHAT | AA_REPEATER | AA_ROOM | AA_SENSOR)) == 0)
          _prefs->autoadd_config |= AA_CHAT | AA_REPEATER | AA_ROOM | AA_SENSOR;       // start od „wszystkie”, potem wyłączasz zbędne
      } else if (i <= 4) {
        if (!(_prefs->manual_add_contacts & 1)) { _prefs->manual_add_contacts |= 1; _prefs->autoadd_config |= AA_CHAT | AA_REPEATER | AA_ROOM | AA_SENSOR; }
        _prefs->autoadd_config ^= AA_BITS[i - 1];
      } else if (i == 5) _prefs->autoadd_config ^= AA_OVERWRITE;
      else _prefs->autoadd_max_hops = (_prefs->autoadd_max_hops + dir + 6) % 6;
      the_mesh.savePrefs();
      return true;
    case S_SET_MSG:
      if (!dir) return false;
      if (i == 0) { _cfg.show_path = !_cfg.show_path; saveCfg(); }
      else if (i == 1) { _prefs->multi_acks = _prefs->multi_acks ? 0 : 1; the_mesh.savePrefs(); }
      else if (i == 2) { _prefs->buzzer_quiet = _prefs->buzzer_quiet ? 0 : 1; the_mesh.savePrefs(); _buzzer.quiet(_prefs->buzzer_quiet); if (!_prefs->buzzer_quiet) _buzzer.play("dm:d=16,o=6,b=200:e,g,c7"); }
      return true;
    case S_SET_SCR:
      if (!dir) return false;
      if (i == 0) { _cfg.sleep_idx = (_cfg.sleep_idx + dir + 4) % 4; saveCfg(); wake(); }
      else if (i == 1) { _cfg.kb_light = !_cfg.kb_light; saveCfg(); _kb.setBacklight(_cfg.kb_light != 0); }
      else { _cfg.no_wake = !_cfg.no_wake; saveCfg(); }
      return true;
    case S_SET_SYS:
      return dir != 0;
  }
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
  if (k == M9K_DOWN) { if (i + cols < count) i += cols; return true; }
  return false;
}

void UITask::key(uint8_t k) {
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
  if (k == M9K_F3)   { tab(S_CONTACTS); return; }
  if (k == M9K_MAP)  { tab(S_RPTS); return; }
  if (k == M9K_BACK) { back(); return; }

  if (s == S_THREAD) { keyThread(k); return; }
  if (s == S_KV) {
    int16_t& top = _nav[_depth - 1].top;
    if (k == M9K_UP && top > 0) top--;
    if (k == M9K_DOWN && top + KV_VISIBLE < _kv_n) top++;
    if (k == M9K_ENTER || k == M9K_LEFT) back();
    return;
  }
  if (s == S_START) {
    if (gridKey(k, 6, 3)) return;
    if (k == M9K_LEFT) { tab(S_SETTINGS); return; }
    if (k == M9K_RIGHT) { tab(S_MSGS); return; }
    if (k == M9K_ENTER) {
      switch (sel()) {
        case 0: tab(S_MSGS); break;
        case 1: tab(S_CONTACTS); break;
        case 2: tab(S_RPTS); break;
        case 3: tab(S_SETTINGS); sel() = 2; push(S_SET_POS); break;
        case 4: tab(S_SETTINGS); sel() = 0; radioLoad(); push(S_RADIO); break;
        case 5: tab(S_SETTINGS); sel() = 7; detailInfo(); break;
      }
    }
    return;
  }
  if (s == S_SETTINGS) {
    if (gridKey(k, 8, 2)) return;
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
  if (k == M9K_DOWN) { if (i < count - 1) i++; return; }
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
  if (!lp->isValid()) return false;
  *lat = lp->getLatitude() / 1000000.0;
  *lon = lp->getLongitude() / 1000000.0;
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

  // policz kontakty wg roli (bez repeaterów) i repeatery
  int people = 0, rpts = 0;
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total; i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE) continue;
    if (c.type == ADV_TYPE_REPEATER) rpts++; else people++;
  }
  UiTile& b = v.tile[1];
  strcpy(b.label, "Kontakty"); b.icon = UII_USER;
  snprintf(b.value, sizeof(b.value), "%d", people);
  snprintf(b.sub, sizeof(b.sub), "usłyszane: %d", the_mesh.ui_heard_n);

  UiTile& r = v.tile[2];
  strcpy(r.label, "Repeatery"); r.icon = UII_TOWER;
  snprintf(r.value, sizeof(r.value), "%d", rpts);
  if (the_mesh.ui_heard_n > 0) { char ago[16]; fmtAgo(ago, sizeof(ago), the_mesh.ui_heard_at[0]); snprintf(r.sub, sizeof(r.sub), "%s · %s", the_mesh.ui_heard[0].name, ago); }
  else strcpy(r.sub, "cisza w eterze");

  UiTile& p = v.tile[3];
  strcpy(p.label, "Pozycja"); p.icon = UII_GPS;
  double lat, lon; int sats;
  if (!_prefs->gps_enabled) { strcpy(p.value, "GPS wył."); strcpy(p.sub, "Enter: ustawienia"); }
  else if (gpsFix(&lat, &lon, &sats)) { snprintf(p.value, sizeof(p.value), "%.4f", lat); snprintf(p.sub, sizeof(p.sub), "%.4f · sat. %d", lon, sats); }
  else { strcpy(p.value, "szukam…"); snprintf(p.sub, sizeof(p.sub), "satelity: %d", sats); }

  UiTile& d = v.tile[4];
  strcpy(d.label, "Radio"); d.icon = UII_BOLT;
  bool narrow = fabsf(_prefs->freq - 869.618f) < 0.005f && fabsf(_prefs->bw - 62.5f) < 0.1f && _prefs->sf == 8 && _prefs->cr == 8;
  if (the_mesh.uiRadioIsSF6()) strcpy(d.value, "SF6"); else if (narrow) strcpy(d.value, "Narrow"); else snprintf(d.value, sizeof(d.value), "SF%d", _prefs->sf);
  snprintf(d.sub, sizeof(d.sub), "%.3f MHz", _prefs->freq); comma(d.sub);

  UiTile& e = v.tile[5];
  strcpy(e.label, "Urządzenie"); e.icon = UII_BT;
  uint16_t mv = getBattMilliVolts();
  snprintf(e.value, sizeof(e.value), "%d%%", battPercent(mv));
  if (!isBluetoothEnabled()) strcpy(e.sub, "Bluetooth wył.");
  else if (hasConnection()) strcpy(e.sub, "telefon połączony");
  else snprintf(e.sub, sizeof(e.sub), "PIN %06lu", (unsigned long)the_mesh.getBLEPin());
}

void UITask::fillSettings() {
  UiView& v = *_v;
  v.screen = UIS_GRID; v.tab = 4;
  v.grid_cols = 2; v.ntiles = 8; v.sel = sel();
  strcpy(v.title, "Ustawienia"); snprintf(v.title_right, sizeof(v.title_right), "wersja %s", M9_FW_VERSION);
  static const char* const N[8] = { "Radio", "Bluetooth", "Pozycja", "Advert", "Kontakty", "Wiadomości", "Ekran i dźwięk", "System" };
  static const uint8_t IC[8] = { UII_TOWER, UII_BT, UII_GPS, UII_BOLT, UII_USER, UII_MAIL, UII_SCREEN, UII_GEAR };
  for (int i = 0; i < 8; i++) {
    UiTile& t = v.tile[i];
    strcpy(t.label, N[i]); t.icon = IC[i];
    switch (i) {
      case 0: if (the_mesh.uiRadioIsSF6()) snprintf(t.sub, sizeof(t.sub), "SF6 · %d dBm", _prefs->tx_power_dbm); else { snprintf(t.sub, sizeof(t.sub), "%.3f · SF%d", _prefs->freq, _prefs->sf); comma(t.sub); } break;
      case 1: if (!isBluetoothEnabled()) strcpy(t.sub, "wyłączony"); else if (hasConnection()) strcpy(t.sub, "połączony"); else snprintf(t.sub, sizeof(t.sub), "PIN %06lu", (unsigned long)the_mesh.getBLEPin()); break;
      case 2: snprintf(t.sub, sizeof(t.sub), "GPS %s · prywatność", _prefs->gps_enabled ? "wł." : "wył."); break;
      case 3: if (ADV_HOURS[_cfg.adv_idx]) snprintf(t.sub, sizeof(t.sub), "co %d godz.", ADV_HOURS[_cfg.adv_idx]); else strcpy(t.sub, "wyślij · auto wył."); break;
      case 4: strcpy(t.sub, (_prefs->manual_add_contacts & 1) ? "dodaje wybrane typy" : "dodaje wszystko"); break;
      case 5: snprintf(t.sub, sizeof(t.sub), "dźwięk %s · trasa", _prefs->buzzer_quiet ? "wył." : "wł."); break;
      case 6: snprintf(t.sub, sizeof(t.sub), "gaśnie: %s", SLEEP_NAMES[_cfg.sleep_idx]); break;
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

void UITask::fillThread() {
  UiView& v = *_v;
  v.screen = UIS_THREAD; v.tab = -1;
  if (_input_mode != IN_MSG) {                       // pole do wpisania czegoś innego niż wiadomość
    static const char* const T[10] = { "", "Nowy kanał", "Częstotliwość", "Nazwa urządzenia", "Data i godzina", "Hasło repeatera", "Komenda", "Szerokość geograficzna", "Długość geograficzna", "PIN Bluetooth" };
    static const char* const R[10] = { "", "kanał z hasztagiem", "MHz", "do 22 znaków", "czas polski", "", "konsola repeatera", "stopnie", "stopnie", "6 cyfr" };
    static const char* const N[10] = { "", "Wpisz nazwę, np. #beskidy. Kto wpisze tę samą nazwę, ten jest na kanale.", "Wpisz częstotliwość w MHz, np. 869.618",
      "Pod tą nazwą widzą Cię inni w sieci. Polskie litery: ctrl, potem litera.",
      "Format: RRRR-MM-DD GG:MM. Zegar ustawia się też sam po połączeniu z aplikacją w telefonie i z GPS po złapaniu pozycji.",
      "Hasło administratora albo gościa. Po udanym logowaniu zostanie zapamiętane w urządzeniu. Puste = gość.",
      "Komenda tekstowa dla repeatera, np. get radio albo set tx 20.",
      "Szerokość w stopniach, np. 49.82250 (ujemna = południowa).", "Długość w stopniach, np. 19.04690 (ujemna = zachodnia).",
      "Nowy PIN do parowania. Zacznie działać po restarcie. W telefonie trzeba będzie sparować urządzenie od nowa." };
    strcpy(v.title, T[_input_mode]); strcpy(v.title_right, R[_input_mode]); strcpy(v.line1, N[_input_mode]);
    scopy(v.input, sizeof(v.input), _input);
    v.input_chars = _input_chars;
    v.polish_next = _polish;
    strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, "zatwierdź");
    v.nhints = 1;
    if (_input_mode == IN_TIME) { strcpy(v.hint[1].key, "Pinezka"); strcpy(v.hint[1].txt, "z GPS"); v.nhints = 2; }
    strcpy(v.hint[v.nhints].key, "Wstecz"); strcpy(v.hint[v.nhints].txt, "anuluj"); v.nhints++;
    return;
  }
  snprintf(v.title, sizeof(v.title), "%s%s", _cur_chan ? "# " : "", _cur);
  strcpy(v.title_right, _cur_chan ? "kanał" : "rozmowa prywatna");
  markRead(_cur, _cur_chan);

  // ostatnie wiadomości tej rozmowy (z pominięciem _scroll najnowszych)
  int idx[UI_BUB_MAX], n = 0, total = 0;
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].chan == _cur_chan && strcmp(_msgs[i].thread, _cur) == 0) total++;
  if (_scroll > total - 1) _scroll = total > 0 ? total - 1 : 0;
  int skip = _scroll;
  for (int i = _msg_n - 1; i >= 0 && n < UI_BUB_MAX; i--) {
    if (_msgs[i].chan != _cur_chan || strcmp(_msgs[i].thread, _cur) != 0) continue;
    if (skip > 0) { skip--; continue; }
    idx[n++] = i;
  }
  for (int k = n - 1; k >= 0; k--) {
    UiMsg& m = _msgs[idx[k]];
    UiBubble& b = v.bub[v.nbub++];
    scopy(b.who, sizeof(b.who), m.who[0] ? m.who : (m.out ? "" : _cur));
    scopy(b.text, sizeof(b.text), m.text);
    b.out = m.out;
    b.sel = k == 0 && _scroll > 0;                   // po przewinięciu dolna wiadomość jest zaznaczona
    char t[8]; fmtClock(t, sizeof(t), m.ts, false);
    if (m.out && m.chan && m.repeats) snprintf(b.meta, sizeof(b.meta), "%s · powtórzeń: %d", t, m.repeats);
    else if (m.out && !m.chan && m.delivered) snprintf(b.meta, sizeof(b.meta), "%s · dostarczono", t);
    else if (m.out) snprintf(b.meta, sizeof(b.meta), "%s · wysłano", t);
    else if (m.direct) snprintf(b.meta, sizeof(b.meta), "%s · znaną drogą", t);
    else if (m.hops == 0) snprintf(b.meta, sizeof(b.meta), "%s · bez skoków", t);
    else snprintf(b.meta, sizeof(b.meta), "%s · skoki: %d", t, m.hops);
    // trasa: identyfikatory kolejnych repeaterów (2 znaki na bajt)
    if (_cfg.show_path && !m.out && !m.direct && m.path_bytes > 0 && m.hash_size > 0) {
      int cnt = m.path_bytes / m.hash_size, shown = 0;
      for (int h = 0; h < cnt; h++) {
        char id[12]; id[0] = 0;
        for (int q = 0; q < m.hash_size; q++) { char x[4]; snprintf(x, sizeof(x), "%02x", m.path[h * m.hash_size + q]); strcat(id, x); }
        if (strlen(b.path) + strlen(id) + 4 > sizeof(b.path) - 8) break;
        if (b.path[0]) strcat(b.path, " > ");
        strcat(b.path, id);
        shown++;
      }
      if (m.hops > shown) { char more[12]; snprintf(more, sizeof(more), " > +%d", m.hops - shown); strcat(b.path, more); }
    }
  }
  scopy(v.input, sizeof(v.input), _input);
  v.input_chars = _input_chars;
  v.polish_next = _polish;
  strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, _input[0] ? "wyślij" : "szybkie");
  strcpy(v.hint[1].key, "ctrl"); strcpy(v.hint[1].txt, "ą ę ł…");
  strcpy(v.hint[2].key, "▲▼"); strcpy(v.hint[2].txt, "wybierz");
  strcpy(v.hint[3].key, "►"); strcpy(v.hint[3].txt, "opcje");
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
  if (s == S_START) fillStart();
  else if (s == S_SETTINGS) fillSettings();
  else if (s == S_THREAD) fillThread();
  else if (s == S_KV) fillKv();
  else fillList();
  if (_popup_until) {
    v.popup = true; v.popup_danger = _pop_danger;
    scopy(v.popup1, sizeof(v.popup1), _pop1); scopy(v.popup2, sizeof(v.popup2), _pop2);
  }
}

void UITask::draw() {
  if (_fb == NULL || _v == NULL) return;
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
    if (!_awake) { wake(); continue; }             // pierwszy klawisz tylko budzi ekran
    uint32_t ms = SLEEP_STEPS[_cfg.sleep_idx];
    _sleep_at = ms ? now + ms : 0;
    key(k);
    _dirty = true;
  }

  adminLoop();
  if (_disc_until && (long)(now - _disc_until) >= 0) { _disc_until = 0; _dirty = true; }
  if (scr() == S_BOOT && (long)(now - _boot_until) >= 0) { tab(S_START); }
  if (_popup_until && (long)(now - _popup_until) >= 0) { _popup_until = 0; _confirm = C_NONE; _dirty = true; }

  if (_awake && _sleep_at && (long)(now - _sleep_at) >= 0) {    // wygaszenie: gaśnie ekran i klawiatura, radio i Bluetooth działają
    _awake = false;
    if (_display) _display->turnOff();
    _kb.setBacklight(false);
  }

  _buzzer.loop();
  if (the_mesh.ui_sent_changed) { the_mesh.ui_sent_changed = false; pollSent(); }

  if ((long)(now - _next_tick) >= 0) {             // zegar, bateria, „… min temu”, odpowiedzi discovery
    _next_tick = now + (scr() == S_DISC ? 1000 : 5000);
    // zegar niewiarygodny (przed 2025 r.), a GPS ma czas: ustaw sam
    if (the_mesh.getRTCClock()->getCurrentTime() < 1735689600UL) timeFromGps();
    _dirty = true;
  }

  if (_next_advert && (long)(now - _next_advert) >= 0) {        // automatyczny advert do sąsiadów
    uint8_t h = ADV_HOURS[_cfg.adv_idx];
    _next_advert = h ? now + h * 3600000UL : 0;
    if (h) the_mesh.advert();
  }

  // zapis wiadomości: pół minuty po ostatniej zmianie i nie w trakcie pisania
  if (_msgs_dirty && (long)(now - _save_at) >= 0 && now - _last_key > 5000) saveMsgs();

  if (_dirty && _awake) {
    _dirty = false;
    draw();
  }
}

#include "UITask.h"
#include "M9Lcd.h"
#include "../Version.h"
#include "../companion/MyMesh.h"
#include <helpers/sensors/LocationProvider.h>
#include <math.h>

#define M9_PIN_EXT_PWR   1          // LOW = podłączone zasilanie zewnętrzne
#define SLEEP_MS         60000UL    // wygaszenie ekranu po minucie bez klawiszy
#define ROWS_VISIBLE     8

static const int8_t TX_STEPS[] = { 10, 14, 17, 20, 22 };
static const float BW_STEPS[] = { 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };

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

UITask::UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
  _prefs = NULL; _fb = NULL; _v = NULL; _msgs = NULL; _list = NULL;
  _msg_n = _thread_n = _list_n = 0;
  _scr = S_BOOT; _back_scr = S_MSGS;
  memset(_sel, 0, sizeof(_sel)); memset(_top, 0, sizeof(_top));
  _cur[0] = 0; _cur_chan = false; _scroll = 0;
  _input[0] = 0; _input_chars = 0; _polish = false;
  _dirty = true; _awake = true;
  _next_tick = _popup_until = _sleep_at = _boot_until = 0;
  _pop1[0] = _pop2[0] = 0; _pop_danger = false;
  _msgcount = 0;
  _input_mode = IN_MSG; _confirm = 0; _confirm_name[0] = 0;
  _rf = 869.618f; _rbw = 62.5f; _rsf = 6; _rcr = 8;
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

  _kb.begin();
  if (_display) _display->turnOn();
  _boot_until = millis() + 2500;
  _sleep_at = millis() + SLEEP_MS;
  draw();
}

// ---------------- wiadomości ----------------

void UITask::addMsg(const char* thread, const char* who, const char* text, bool chan, bool out, uint8_t hops) {
  if (_msgs == NULL) return;
  if (_msg_n == UI_MSG_MAX) {                       // najstarsza wypada
    memmove(&_msgs[0], &_msgs[1], sizeof(UiMsg) * (UI_MSG_MAX - 1));
    _msg_n--;
  }
  UiMsg& m = _msgs[_msg_n++];
  memset(&m, 0, sizeof(m));
  scopy(m.thread, sizeof(m.thread), thread);
  scopy(m.who, sizeof(m.who), who);
  scopy(m.text, sizeof(m.text), text);
  m.ts = the_mesh.getRTCClock()->getCurrentTime();
  m.chan = chan; m.out = out; m.hops = hops;
  // przeczytana od razu, jeśli właśnie patrzymy na tę rozmowę
  m.unread = !out && !(_awake && _scr == S_THREAD && _cur_chan == chan && strcmp(_cur, thread) == 0);
  _dirty = true;
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
  addMsg(from_name, who, body, chan, false, path_len);
  wake();
}

void UITask::msgRead(int msgcount) { _msgcount = msgcount; }

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
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].chan == chan && strcmp(_msgs[i].thread, name) == 0) _msgs[i].unread = 0;
}

void UITask::openThread(const char* name, bool chan) {
  scopy(_cur, sizeof(_cur), name);
  _cur_chan = chan;
  _scroll = 0;
  _input[0] = 0; _input_chars = 0; _polish = false;
  _input_mode = IN_MSG;
  markRead(name, chan);
  _back_scr = _scr < 5 ? _scr : S_MSGS;
  _scr = S_THREAD;
  _dirty = true;
}

void UITask::openInput(uint8_t mode, const char* initial) {
  _input_mode = mode;
  scopy(_input, sizeof(_input), initial);
  _input_chars = strlen(_input);
  _polish = false;
  _scr = S_THREAD;
  _dirty = true;
}

void UITask::radioLoad() { _rf = _prefs->freq; _rbw = _prefs->bw; _rsf = _prefs->sf; _rcr = _prefs->cr; }
bool UITask::radioChanged() { return fabsf(_rf - _prefs->freq) > 0.0005f || fabsf(_rbw - _prefs->bw) > 0.01f || _rsf != _prefs->sf || _rcr != _prefs->cr; }

void UITask::sendInput() {
  if (_input[0] == 0) return;
  if (_input_mode == IN_CHANNEL) {                   // nowy kanał z hasztagiem
    char name[32];
    snprintf(name, sizeof(name), "%s%s", _input[0] == '#' ? "" : "#", _input);
    for (char* p = name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;      // nazwy kanałów z hasztagiem są małymi literami
    if (strlen(name) < 2) return;
    int r = the_mesh.uiAddHashtagChannel(name);
    go(S_MSGS);
    if (r >= 0) popup("Dodano kanał", name);
    else if (r == -2) popup("Ten kanał już jest", name);
    else popup("Brak miejsca na kanał", "usuń któryś z istniejących", true);
    return;
  }
  if (_input_mode == IN_FREQ) {                      // częstotliwość w MHz
    for (char* p = _input; *p; p++) if (*p == ',') *p = '.';
    float f = atof(_input);
    _scr = S_RADIO;
    if (f >= 150.0f && f <= 960.0f) _rf = f; else popup("Zła częstotliwość", "podaj w MHz, np. 869.618", true);
    return;
  }
  bool ok;
  if (_cur_chan) { int idx = the_mesh.uiChannelIndex(_cur); ok = idx >= 0 && the_mesh.uiSendChannelText(idx, _input); }
  else ok = the_mesh.uiSendDirectText(_cur, _input);
  if (ok) {
    addMsg(_cur, "", _input, _cur_chan, true, 0);
    _input[0] = 0; _input_chars = 0; _scroll = 0;
  } else {
    popup("Nie udało się wysłać", "spróbuj jeszcze raz", true);
  }
}

// ---------------- listy kontaktów ----------------

void UITask::buildContacts(uint8_t kind) {
  _list_n = 0;
  if (_list == NULL) return;
  ContactInfo c;
  int total = the_mesh.getTotalContactSlots();
  for (int i = MAX_ANON_CONTACTS; i < total && _list_n < UI_LIST_MAX; i++) {       // pierwsze miejsca tabeli są techniczne
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE) continue;
    bool rpt = c.type == ADV_TYPE_REPEATER;
    if ((kind == 1) != rpt) continue;
    if (kind == 0 && c.type != ADV_TYPE_CHAT && c.type != ADV_TYPE_ROOM) continue;
    _list[_list_n++] = (uint16_t)i;
  }
  // ulubione na górze, potem ostatnio słyszane (sortowanie przez wstawianie na kluczach)
  static uint32_t keys[UI_LIST_MAX];
  for (int i = 0; i < _list_n; i++) {
    the_mesh.getContactByIdx(_list[i], c);
    uint32_t last = c.lastmod > 0x7FFFFFFF ? 0x7FFFFFFF : c.lastmod;
    keys[i] = ((c.flags & 1) ? 0x80000000UL : 0) | last;
  }
  for (int i = 1; i < _list_n; i++) {
    uint32_t k = keys[i]; uint16_t v = _list[i];
    int j = i - 1;
    while (j >= 0 && keys[j] < k) { keys[j + 1] = keys[j]; _list[j + 1] = _list[j]; j--; }
    keys[j + 1] = k; _list[j + 1] = v;
  }
}

// ---------------- nawigacja ----------------

void UITask::popup(const char* a, const char* b, bool danger, int ms) {
  scopy(_pop1, sizeof(_pop1), a);
  scopy(_pop2, sizeof(_pop2), b);
  _pop_danger = danger;
  _popup_until = millis() + ms;
  _dirty = true;
}

void UITask::go(uint8_t scr) {
  _scr = scr;
  if (scr == S_MSGS) buildThreads();
  if (scr == S_CONTACTS) buildContacts(0);
  if (scr == S_RPTS) buildContacts(1);
  _dirty = true;
}

void UITask::wake() {
  _sleep_at = millis() + SLEEP_MS;
  if (!_awake) {
    _awake = true;
    if (_display) _display->turnOn();
    _kb.setBacklight(true);
  }
  _dirty = true;
}

void UITask::back() {
  switch (_scr) {
    case S_THREAD: go(_input_mode == IN_CHANNEL ? (uint8_t)S_MSGS : _input_mode == IN_FREQ ? (uint8_t)S_RADIO : _back_scr); break;
    case S_INFO:   go(S_SETTINGS); break;
    case S_RADIO:  go(S_SETTINGS); break;
    case S_RPT:    go(S_RPTS); break;
    case S_START:  break;
    default:       go(S_START); break;
  }
}

void UITask::keyList(uint8_t k, int count) {
  int& sel = _sel[_scr];
  if (k == M9K_UP && sel > 0) sel--;
  if (k == M9K_DOWN && sel < count - 1) sel++;
  if (sel >= count) sel = count > 0 ? count - 1 : 0;
}

void UITask::enter() {
  ContactInfo c;
  switch (_scr) {
    case S_START: go(S_MSGS); break;
    case S_MSGS:
      if (_sel[S_MSGS] < _thread_n) openThread(_threads[_sel[S_MSGS]].name, _threads[_sel[S_MSGS]].chan);
      else openInput(IN_CHANNEL, "#");
      break;
    case S_CONTACTS:
      if (_sel[S_CONTACTS] < _list_n && the_mesh.getContactByIdx(_list[_sel[S_CONTACTS]], c)) openThread(c.name, false);
      break;
    case S_RPTS:
      if (_sel[S_RPTS] < _list_n && the_mesh.getContactByIdx(_list[_sel[S_RPTS]], c)) { memcpy(_rpt_key, c.id.pub_key, 32); _scr = S_RPT; }
      break;
    case S_SETTINGS:
      switch (_sel[S_SETTINGS]) {
        case 0: radioLoad(); _sel[S_RADIO] = 0; _scr = S_RADIO; break;
        case 1: popup(the_mesh.advert() ? "Advert wysłany" : "Błąd advertu", "do sąsiadów (bez przekazywania)"); break;
        case 2: popup(the_mesh.uiAdvertFlood() ? "Advert wysłany" : "Błąd advertu", "do całej sieci"); break;
        case 3: {
          bool on = !_prefs->gps_enabled;
          the_mesh.uiSetGps(on);
          popup(on ? "GPS włączony" : "GPS wyłączony");
          break;
        }
        case 4: {
          int k = 0;
          for (int i = 0; i < (int)sizeof(TX_STEPS); i++) if (TX_STEPS[i] <= _prefs->tx_power_dbm) k = i;
          k = (k + 1) % (int)sizeof(TX_STEPS);
          if (TX_STEPS[k] > the_mesh.uiMaxTxPower()) k = 0;
          the_mesh.uiSetTxPower(TX_STEPS[k]);
          break;
        }
        case 5:
          if (isBluetoothEnabled()) { disableBluetooth(); popup("Bluetooth wyłączony"); }
          else {
            char t[40]; snprintf(t, sizeof(t), "PIN do parowania: %06lu", (unsigned long)the_mesh.getBLEPin());
            enableBluetooth(); popup("Bluetooth włączony", t, false, 6000);
          }
          break;
        case 6: _scr = S_INFO; break;
      }
      break;
  }
  _dirty = true;
}

void UITask::keyRadio(uint8_t k) {
  int& sel = _sel[S_RADIO];
  if (k == M9K_UP && sel > 0) sel--;
  if (k == M9K_DOWN && sel < 6) sel++;
  int dir = k == M9K_LEFT ? -1 : (k == M9K_RIGHT || k == M9K_ENTER) ? 1 : 0;
  if (dir == 0) return;
  switch (sel) {
    case 0: if (k == M9K_ENTER) { the_mesh.uiSetRadioSF6(); radioLoad(); popup("Radio: SF6 — MeshCore Południe", "869,618 MHz · BW 62,5 · SF6 · CR 4/8"); } break;
    case 1: if (k == M9K_ENTER) { the_mesh.uiSetRadio(869.618f, 62.5f, 8, 8); radioLoad(); popup("Radio: EU/UK Narrow", "869,618 MHz · BW 62,5 · SF8 · CR 4/8"); } break;
    case 2: if (k == M9K_ENTER) { char t[16]; snprintf(t, sizeof(t), "%.3f", _rf); openInput(IN_FREQ, t); } break;
    case 3: {
      int n = sizeof(BW_STEPS) / sizeof(BW_STEPS[0]), cur = 2;
      for (int i = 0; i < n; i++) if (fabsf(BW_STEPS[i] - _rbw) < 0.5f) cur = i;
      _rbw = BW_STEPS[(cur + dir + n) % n];
      break;
    }
    case 4: { int v = _rsf + dir; if (v > 12) v = 5; if (v < 5) v = 12; _rsf = v; break; }
    case 5: { int v = _rcr + dir; if (v > 8) v = 5; if (v < 5) v = 8; _rcr = v; break; }
    case 6:
      if (k == M9K_ENTER) {
        if (!radioChanged()) popup("Brak zmian do zastosowania");
        else { the_mesh.uiSetRadio(_rf, _rbw, _rsf, _rcr); radioLoad(); popup("Radio: ustawienia własne", "zapisane i włączone"); }
      }
      break;
  }
}

void UITask::keyThread(uint8_t k) {
  if (k == M9K_ENTER) { sendInput(); return; }
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
  if (_input_mode == IN_MSG && k == M9K_UP) { _scroll++; return; }
  if (_input_mode == IN_MSG && k == M9K_DOWN) { if (_scroll > 0) _scroll--; return; }
  if (_input_mode == IN_MSG && k == M9K_F4) {                                 // pinezka: wstaw moją pozycję
    double lat, lon; int sats;
    if (gpsFix(&lat, &lon, &sats)) {
      char t[48]; snprintf(t, sizeof(t), "Moja pozycja: %.5f, %.5f", lat, lon);
      if (_input_chars + (int)strlen(t) <= UI_INPUT_MAX) { strcat(_input, t); _input_chars += strlen(t); }
    } else popup("Brak pozycji GPS", "włącz GPS w Ustawieniach i wyjdź pod niebo");
    return;
  }
  int limit = _input_mode == IN_CHANNEL ? 24 : _input_mode == IN_FREQ ? 8 : UI_INPUT_MAX;
  if (_input_mode == IN_CHANNEL && (k == ' ' || (k == '#' && _input_chars > 0))) return;      // nazwa kanału bez spacji
  if (_input_mode == IN_FREQ && !((k >= '0' && k <= '9') || k == '.' || k == ',')) return;
  if (k >= 32 && k < 127 && _input_chars < limit) {
    const char* pl = _polish ? polishFor(k) : NULL;
    if (pl) strcat(_input, pl);
    else { int n = strlen(_input); _input[n] = (char)k; _input[n + 1] = 0; }
    _input_chars++;
    _polish = false;
  }
}

void UITask::key(uint8_t k) {
  if (_confirm) {                                     // pytanie w okienku: Enter potwierdza, inny klawisz anuluje
    uint8_t what = _confirm;
    _confirm = 0; _popup_until = 0;
    if (k == M9K_ENTER && what == 1) {
      bool ok = the_mesh.uiRemoveChannel(_confirm_name);
      for (int i = 0; i < _msg_n; ) {                 // razem z kanałem znikają jego wiadomości
        if (_msgs[i].chan && strcmp(_msgs[i].thread, _confirm_name) == 0) { memmove(&_msgs[i], &_msgs[i + 1], sizeof(UiMsg) * (_msg_n - i - 1)); _msg_n--; }
        else i++;
      }
      go(S_MSGS);
      popup(ok ? "Kanał usunięty" : "Nie udało się usunąć", _confirm_name, !ok);
    }
    return;
  }
  if (_popup_until) { _popup_until = 0; }             // dowolny klawisz zamyka okienko (i działa dalej)
  if (_scr == S_BOOT) { _boot_until = 0; _scr = S_START; return; }

  // klawisze funkcyjne działają wszędzie
  if (k == M9K_HOME) { go(S_START); return; }
  if (k == M9K_MSG)  { go(S_MSGS); return; }
  if (k == M9K_F3)   { go(S_CONTACTS); return; }
  if (k == M9K_MAP)  { go(S_RPTS); return; }
  if (k == M9K_BACK) { back(); return; }

  if (_scr == S_THREAD) { keyThread(k); return; }
  if (_scr == S_RADIO) { keyRadio(k); return; }
  if (_scr == S_MSGS && (k == M9K_BKSP || k == M9K_BKSP_LONG) && _sel[S_MSGS] < _thread_n && _threads[_sel[S_MSGS]].chan) {
    scopy(_confirm_name, sizeof(_confirm_name), _threads[_sel[S_MSGS]].name);
    _confirm = 1;
    char t[40]; snprintf(t, sizeof(t), "Usunąć kanał %s?", _confirm_name);
    popup(t, "Enter = usuń · inny klawisz = anuluj", true, 15000);
    return;
  }
  if (_scr == S_INFO || _scr == S_RPT) { if (k == M9K_ENTER || k == M9K_LEFT) back(); return; }

  // ekrany z zakładkami
  if (k == M9K_LEFT)  { go((_scr + 4) % 5); return; }
  if (k == M9K_RIGHT) { go((_scr + 1) % 5); return; }
  if (k == M9K_ENTER) { enter(); return; }
  int count = _scr == S_MSGS ? _thread_n + 1 : (_scr == S_CONTACTS || _scr == S_RPTS) ? _list_n : _scr == S_SETTINGS ? 7 : 0;
  keyList(k, count);
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

// okno widocznych wierszy listy wokół zaznaczenia
void UITask::windowRows(int count, int s, int* first) {
  int& sel = _sel[s]; int& top = _top[s];
  if (sel >= count) sel = count > 0 ? count - 1 : 0;
  if (sel < top) top = sel;
  if (sel >= top + ROWS_VISIBLE) top = sel - ROWS_VISIBLE + 1;
  if (top > count - ROWS_VISIBLE) top = count - ROWS_VISIBLE;
  if (top < 0) top = 0;
  *first = top;
  _v->sel = sel - top;
  _v->more_up = top > 0;
  _v->more_dn = top + ROWS_VISIBLE < count;
}

void UITask::fillStart() {
  UiView& v = *_v;
  v.screen = UIS_START; v.tab = 0;
  scopy(v.name, sizeof(v.name), _prefs->node_name);
  snprintf(v.preset, sizeof(v.preset), "%.3f · BW %.4g · SF%d · CR 4/%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  for (char* p = v.preset; *p; p++) if (*p == '.') *p = ',';

  buildThreads();
  int unread = v.unread;
  UiTile& a = v.tile[0];
  strcpy(a.label, "Wiadomości"); a.icon = UII_MAIL; a.accent = unread > 0;
  if (unread > 0) snprintf(a.value, sizeof(a.value), unread == 1 ? "1 nowa" : "%d nowe", unread); else strcpy(a.value, "brak nowych");
  a.sub[0] = 0;
  for (int i = 0; i < _thread_n && i < 2; i++) if (_threads[i].unread) {
    char t[44]; snprintf(t, sizeof(t), "%s%s %d", a.sub[0] ? " · " : "", _threads[i].name, _threads[i].unread);
    strlcat(a.sub, t, sizeof(a.sub));
  }
  if (!a.sub[0]) strcpy(a.sub, "Enter: lista rozmów");

  UiTile& b = v.tile[1];
  strcpy(b.label, "Sieć"); b.icon = UII_TOWER; b.accent = false;
  if (the_mesh.ui_heard_n > 0) {
    char ago[16]; fmtAgo(ago, sizeof(ago), the_mesh.ui_heard_at[0]);
    scopy(b.value, sizeof(b.value), the_mesh.ui_heard[0].name);
    snprintf(b.sub, sizeof(b.sub), "słyszany: %s · kontaktów %d", ago, the_mesh.getNumContacts());
  } else {
    strcpy(b.value, the_mesh.uiRadioIsSF6() ? "nasłuch SF6" : "inny preset!");
    snprintf(b.sub, sizeof(b.sub), "jeszcze nikogo nie słychać · kontaktów %d", the_mesh.getNumContacts());
  }

  UiTile& c = v.tile[2];
  strcpy(c.label, "Pozycja"); c.icon = UII_GPS; c.accent = false;
  double lat, lon; int sats;
  if (!_prefs->gps_enabled) { strcpy(c.value, "GPS wyłączony"); strcpy(c.sub, "włącz w Ustawieniach"); }
  else if (gpsFix(&lat, &lon, &sats)) { snprintf(c.value, sizeof(c.value), "%.4f, %.4f", lat, lon); snprintf(c.sub, sizeof(c.sub), "satelity: %d", sats); }
  else { strcpy(c.value, "szukam…"); snprintf(c.sub, sizeof(c.sub), "satelity: %d", sats); }

  UiTile& d = v.tile[3];
  strcpy(d.label, "Urządzenie"); d.icon = UII_BOLT; d.accent = false;
  uint16_t mv = getBattMilliVolts();
  snprintf(d.value, sizeof(d.value), "%d%% · %d,%02d V", battPercent(mv), mv / 1000, (mv % 1000) / 10);
  if (!isBluetoothEnabled()) strcpy(d.sub, "Bluetooth wyłączony");
  else if (hasConnection()) strcpy(d.sub, "Bluetooth połączony");
  else snprintf(d.sub, sizeof(d.sub), "Bluetooth czeka · PIN %06lu", (unsigned long)the_mesh.getBLEPin());
  v.footer[0] = 0;
}

void UITask::fillMsgs() {
  UiView& v = *_v;
  v.screen = UIS_LIST; v.tab = 1;
  strcpy(v.title, "Wiadomości");
  if (v.unread > 0) snprintf(v.title_right, sizeof(v.title_right), "nieprzeczytane: %d", v.unread);
  strcpy(v.empty, "Nie ma jeszcze żadnych kanałów ani rozmów. Dodaj kanał w aplikacji MeshCore albo napisz do kogoś z listy Kontakty.");
  buildThreads();
  strcpy(v.title_right + strlen(v.title_right), v.title_right[0] ? "" : "Del: usuń kanał");
  int first; windowRows(_thread_n + 1, S_MSGS, &first);
  for (int i = first; i <= _thread_n && v.nrows < ROWS_VISIBLE; i++) {
    if (i == _thread_n) {
      UiRow& r = v.rows[v.nrows++];
      strcpy(r.text, "+ Dodaj kanał"); strcpy(r.right, "kanał z hasztagiem, np. #beskidy");
      continue;
    }
    Thread& t = _threads[i];
    UiRow& r = v.rows[v.nrows++];
    scopy(r.text, sizeof(r.text), t.name);
    r.icon = t.chan ? UII_HASH : UII_USER;
    r.badge = t.unread;
    if (t.last_msg >= 0) {
      UiMsg& m = _msgs[t.last_msg];
      if (m.out) snprintf(r.sub, sizeof(r.sub), "Ty: %s", m.text);
      else if (m.who[0]) snprintf(r.sub, sizeof(r.sub), "%s: %s", m.who, m.text);
      else scopy(r.sub, sizeof(r.sub), m.text);
      fmtClock(r.right, sizeof(r.right), m.ts, true);
    }
  }
}

void UITask::fillContacts() {
  UiView& v = *_v;
  v.screen = UIS_LIST; v.tab = 2;
  strcpy(v.title, "Kontakty");
  snprintf(v.title_right, sizeof(v.title_right), "%d · Enter: napisz", _list_n);
  strcpy(v.empty, "Brak kontaktów. Pojawią się, gdy urządzenie usłyszy czyjś advert albo dodasz je w aplikacji MeshCore.");
  int first; windowRows(_list_n, S_CONTACTS, &first);
  ContactInfo c;
  for (int i = first; i < _list_n && v.nrows < ROWS_VISIBLE; i++) {
    if (!the_mesh.getContactByIdx(_list[i], c)) continue;
    UiRow& r = v.rows[v.nrows++];
    scopy(r.text, sizeof(r.text), c.name);
    bool fav = c.flags & 1;
    r.icon = fav ? UII_STAR : (c.type == ADV_TYPE_ROOM ? UII_ROOM : UII_USER);
    r.icon_accent = fav;
    fmtAgo(r.right, sizeof(r.right), c.lastmod);
  }
}

void UITask::fillRpts() {
  UiView& v = *_v;
  v.screen = UIS_LIST; v.tab = 3;
  strcpy(v.title, "Repeatery");
  snprintf(v.title_right, sizeof(v.title_right), "%d · ostatnio słyszane", _list_n);
  strcpy(v.empty, "Brak repeaterów w kontaktach. Pojawią się po usłyszeniu ich advertów.");
  int first; windowRows(_list_n, S_RPTS, &first);
  double lat, lon; int sats;
  bool fix = gpsFix(&lat, &lon, &sats);
  ContactInfo c;
  for (int i = first; i < _list_n && v.nrows < ROWS_VISIBLE; i++) {
    if (!the_mesh.getContactByIdx(_list[i], c)) continue;
    UiRow& r = v.rows[v.nrows++];
    scopy(r.text, sizeof(r.text), c.name);
    bool fav = c.flags & 1;
    r.icon = fav ? UII_STAR : UII_TOWER; r.icon_accent = fav;
    char ago[16]; fmtAgo(ago, sizeof(ago), c.lastmod);
    if (fix && (c.gps_lat || c.gps_lon)) snprintf(r.right, sizeof(r.right), "%.1f km · %s", distKm(lat, lon, c.gps_lat / 1e6, c.gps_lon / 1e6), ago);
    else scopy(r.right, sizeof(r.right), ago);
  }
}

void UITask::fillSettings() {
  UiView& v = *_v;
  v.screen = UIS_LIST; v.tab = 4;
  strcpy(v.title, "Ustawienia");
  strcpy(v.title_right, "Enter: zmień / wykonaj");
  static const char* const names[7] = { "Radio", "Advert do sąsiadów", "Advert do całej sieci", "GPS", "Moc nadawania", "Bluetooth", "Informacje" };
  static const uint8_t icons[7] = { UII_TOWER, UII_NONE, UII_NONE, UII_GPS, UII_BOLT, UII_BT, UII_NONE };
  int first; windowRows(7, S_SETTINGS, &first);
  for (int i = first; i < 7 && v.nrows < ROWS_VISIBLE; i++) {
    UiRow& r = v.rows[v.nrows++];
    strcpy(r.text, names[i]); r.icon = icons[i];
    switch (i) {
      case 0:
        if (the_mesh.uiRadioIsSF6()) strcpy(r.right, "SF6 — MeshCore Południe");
        else snprintf(r.right, sizeof(r.right), "%.3f · %.4g · SF%d · CR 4/%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
        break;
      case 1: strcpy(r.right, "wyślij"); break;
      case 2: strcpy(r.right, "wyślij"); break;
      case 3: strcpy(r.right, _prefs->gps_enabled ? "włączony" : "wyłączony"); break;
      case 4: snprintf(r.right, sizeof(r.right), "%d dBm", _prefs->tx_power_dbm); break;
      case 5: if (isBluetoothEnabled()) snprintf(r.right, sizeof(r.right), "włączony · PIN %06lu", (unsigned long)the_mesh.getBLEPin()); else strcpy(r.right, "wyłączony"); break;
      case 6: snprintf(r.right, sizeof(r.right), "wersja %s", M9_FW_VERSION); break;
    }
  }
}

void UITask::fillThread() {
  UiView& v = *_v;
  v.screen = UIS_THREAD; v.tab = -1;
  if (_input_mode != IN_MSG) {                       // pole do wpisania nazwy kanału albo częstotliwości
    bool ch = _input_mode == IN_CHANNEL;
    strcpy(v.title, ch ? "Nowy kanał" : "Częstotliwość");
    strcpy(v.title_right, ch ? "kanał z hasztagiem" : "MHz");
    strcpy(v.line1, ch ? "Wpisz nazwę, np. #beskidy. Kto wpisze tę samą nazwę, ten jest na kanale." : "Wpisz częstotliwość w MHz, np. 869.618");
    scopy(v.input, sizeof(v.input), _input);
    v.input_chars = _input_chars;
    strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, ch ? "dodaj" : "zatwierdź");
    strcpy(v.hint[1].key, "Wstecz"); strcpy(v.hint[1].txt, "anuluj");
    v.nhints = 2;
    return;
  }
  snprintf(v.title, sizeof(v.title), "%s%s", _cur_chan ? "# " : "", _cur);
  strcpy(v.title_right, _cur_chan ? "kanał" : "rozmowa prywatna");
  markRead(_cur, _cur_chan);

  // ostatnie wiadomości tej rozmowy (z pominięciem _scroll najnowszych)
  int idx[UI_BUB_MAX], n = 0, skip = _scroll, total = 0;
  for (int i = 0; i < _msg_n; i++) if (_msgs[i].chan == _cur_chan && strcmp(_msgs[i].thread, _cur) == 0) total++;
  if (_scroll > total - 1) _scroll = total > 0 ? total - 1 : 0;
  skip = _scroll;
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
    char t[8]; fmtClock(t, sizeof(t), m.ts, false);
    if (m.out) snprintf(b.meta, sizeof(b.meta), "%s · wysłano", t);
    else if (m.hops == 0xFF) snprintf(b.meta, sizeof(b.meta), "%s · bezpośrednio", t);
    else if (m.hops == 0) scopy(b.meta, sizeof(b.meta), t);
    else snprintf(b.meta, sizeof(b.meta), "%s · skoki: %d", t, m.hops);
  }
  scopy(v.input, sizeof(v.input), _input);
  v.input_chars = _input_chars;
  v.polish_next = _polish;
  static const char* const H[4][2] = { { "Enter", "wyślij" }, { "ctrl", "ą ę ł…" }, { "Pinezka", "pozycja" }, { "Wstecz", "" } };
  for (int i = 0; i < 4; i++) { strcpy(v.hint[i].key, H[i][0]); strcpy(v.hint[i].txt, H[i][1]); }
  v.nhints = 4;
}

static void kvAdd(UiView& v, const char* k, const char* val) {
  if (v.nkv >= UI_KV_MAX) return;
  scopy(v.kv[v.nkv].k, sizeof(v.kv[0].k), k);
  scopy(v.kv[v.nkv].v, sizeof(v.kv[0].v), val);
  v.nkv++;
}

void UITask::fillRadio() {
  UiView& v = *_v;
  v.screen = UIS_LIST; v.tab = -1;
  strcpy(v.title, "Radio");
  snprintf(v.title_right, sizeof(v.title_right), "teraz: %.3f · %.4g · SF%d · CR 4/%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  bool sf6 = the_mesh.uiRadioIsSF6();
  bool narrow = fabsf(_prefs->freq - 869.618f) < 0.005f && fabsf(_prefs->bw - 62.5f) < 0.1f && _prefs->sf == 8 && _prefs->cr == 8;
  static const char* const names[7] = { "SF6 — MeshCore Południe", "EU/UK Narrow (SF8)", "Częstotliwość", "Pasmo (BW)", "Rozpraszanie (SF)", "Kodowanie (CR)", "Zastosuj własne ustawienia" };
  int first; windowRows(7, S_RADIO, &first);
  for (int i = first; i < 7 && v.nrows < ROWS_VISIBLE; i++) {
    UiRow& r = v.rows[v.nrows++];
    strcpy(r.text, names[i]);
    switch (i) {
      case 0: strcpy(r.right, sf6 ? "aktywny" : "Enter: włącz"); r.icon = UII_STAR; r.icon_accent = sf6; break;
      case 1: strcpy(r.right, narrow ? "aktywny" : "Enter: włącz"); r.icon = UII_STAR; r.icon_accent = narrow; break;
      case 2: snprintf(r.right, sizeof(r.right), "%.3f MHz", _rf); break;
      case 3: snprintf(r.right, sizeof(r.right), "%.4g kHz", _rbw); break;
      case 4: snprintf(r.right, sizeof(r.right), "SF%d", _rsf); break;
      case 5: snprintf(r.right, sizeof(r.right), "4/%d", _rcr); break;
      case 6: strcpy(r.right, radioChanged() ? "Enter: zapisz i włącz" : "brak zmian"); break;
    }
  }
  strcpy(v.hint[0].key, "Enter"); strcpy(v.hint[0].txt, "wybierz");
  strcpy(v.hint[1].key, "◄►"); strcpy(v.hint[1].txt, "zmień wartość");
  strcpy(v.hint[2].key, "Wstecz"); strcpy(v.hint[2].txt, "");
  v.nhints = 3;
}

void UITask::fillInfo() {
  UiView& v = *_v;
  v.screen = UIS_INFO; v.tab = -1;
  strcpy(v.title, "Informacje"); strcpy(v.title_right, "MC SF6 · ThinkNode M9");
  char t[48];
  kvAdd(v, "Nazwa", _prefs->node_name);
  kvAdd(v, "Wersja", M9_FW_VERSION);
  snprintf(t, sizeof(t), "%.3f MHz · BW %.4g", _prefs->freq, _prefs->bw); kvAdd(v, "Radio", t);
  snprintf(t, sizeof(t), "SF%d · CR 4/%d · %d dBm", _prefs->sf, _prefs->cr, _prefs->tx_power_dbm); kvAdd(v, "Parametry", t);
  uint16_t mv = getBattMilliVolts();
  snprintf(t, sizeof(t), "%d mV · %d%%", mv, battPercent(mv)); kvAdd(v, "Bateria", t);
  snprintf(t, sizeof(t), "%d z %d", the_mesh.getNumContacts(), MAX_CONTACTS); kvAdd(v, "Kontakty", t);
  snprintf(t, sizeof(t), "0x%02X", _kb.addr()); kvAdd(v, "Klawiatura", _kb.isFound() ? t : "nie znaleziona");
  snprintf(t, sizeof(t), "%lu kB · PSRAM %lu kB", (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024)); kvAdd(v, "Wolna pamięć", t);
  unsigned long up = millis() / 60000UL;
  snprintf(t, sizeof(t), "%lu godz. %lu min", up / 60, up % 60); kvAdd(v, "Czas pracy", t);
  strcpy(v.hint[0].key, "Wstecz"); strcpy(v.hint[0].txt, "ustawienia"); v.nhints = 1;
}

void UITask::fillRpt() {
  UiView& v = *_v;
  v.screen = UIS_INFO; v.tab = -1;
  ContactInfo c; bool found = false;
  for (int i = 0; i < _list_n; i++) if (the_mesh.getContactByIdx(_list[i], c) && memcmp(c.id.pub_key, _rpt_key, 32) == 0) { found = true; break; }
  if (!found) { strcpy(v.title, "Repeater"); kvAdd(v, "Stan", "nie ma go już w kontaktach"); }
  else {
    char t[48];
    scopy(v.title, sizeof(v.title), c.name); strcpy(v.title_right, (c.flags & 1) ? "repeater · ulubiony" : "repeater");
    fmtAgo(t, sizeof(t), c.lastmod); kvAdd(v, "Ostatnio słyszany", t);
    if (c.gps_lat || c.gps_lon) {
      snprintf(t, sizeof(t), "%.5f, %.5f", c.gps_lat / 1e6, c.gps_lon / 1e6); kvAdd(v, "Pozycja", t);
      double lat, lon; int sats;
      if (gpsFix(&lat, &lon, &sats)) { snprintf(t, sizeof(t), "%.1f km", distKm(lat, lon, c.gps_lat / 1e6, c.gps_lon / 1e6)); kvAdd(v, "Odległość", t); }
    } else kvAdd(v, "Pozycja", "nie podaje");
    if (c.out_path_len == 0xFF || c.out_path_len > 64) kvAdd(v, "Droga", "nieznana");
    else if (c.out_path_len == 0) kvAdd(v, "Droga", "bezpośrednio");
    else { snprintf(t, sizeof(t), "skoki: %d", c.out_path_len); kvAdd(v, "Droga", t); }
    snprintf(t, sizeof(t), "%02x%02x%02x%02x %02x%02x…", c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2], c.id.pub_key[3], c.id.pub_key[4], c.id.pub_key[5]); kvAdd(v, "Klucz", t);
    kvAdd(v, "Zarządzanie", "w następnej wersji");
  }
  strcpy(v.hint[0].key, "Wstecz"); strcpy(v.hint[0].txt, "lista"); v.nhints = 1;
}

void UITask::fill() {
  UiView& v = *_v;
  memset(&v, 0, sizeof(v));
  if (_scr == S_BOOT) {
    v.screen = UIS_BOOT;
    snprintf(v.line1, sizeof(v.line1), "%s · ThinkNode M9 · wersja %s", _prefs->node_name, M9_FW_VERSION);
    snprintf(v.line2, sizeof(v.line2), "%.3f MHz · BW %.4g · SF%d · CR 4/%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
    return;
  }
  fillStatus();
  switch (_scr) {
    case S_START: fillStart(); break;
    case S_MSGS: fillMsgs(); break;
    case S_CONTACTS: fillContacts(); break;
    case S_RPTS: fillRpts(); break;
    case S_SETTINGS: fillSettings(); break;
    case S_RADIO: fillRadio(); break;
    case S_THREAD: fillThread(); break;
    case S_INFO: fillInfo(); break;
    case S_RPT: fillRpt(); break;
  }
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
    if (!_awake) { wake(); continue; }             // pierwszy klawisz tylko budzi ekran
    _sleep_at = now + SLEEP_MS;
    key(k);
    _dirty = true;
  }

  if (_scr == S_BOOT && (long)(now - _boot_until) >= 0) { _scr = S_START; _dirty = true; }
  if (_popup_until && (long)(now - _popup_until) >= 0) { _popup_until = 0; _confirm = 0; _dirty = true; }

  if (_awake && (long)(now - _sleep_at) >= 0) {    // wygaszenie: gaśnie ekran i klawiatura, radio i Bluetooth działają
    _awake = false;
    if (_display) _display->turnOff();
    _kb.setBacklight(false);
  }

  if ((long)(now - _next_tick) >= 0) {             // zegar, bateria, „… min temu”
    _next_tick = now + 5000;
    _dirty = true;
  }

  if (_dirty && _awake) {
    _dirty = false;
    draw();
  }
}

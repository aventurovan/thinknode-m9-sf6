#include "UITask.h"
#include "M9Lcd.h"
#include "M9Upload.h"
#include "../Version.h"
#include <SPI.h>
#include <SD.h>
#include <helpers/sensors/LocationProvider.h>

#define M9_PIN_EXT_PWR   1       // LOW = podłączone zasilanie zewnętrzne
#define M9_PIN_SD_CS     48
#define M9_PIN_GPS_EN    11
#define M9_SD_FREQ       40000000
#define M9_SD_FREQ_SLOW  20000000

// kody klawiszy: M9Keyboard.h
#define KEY_UP     M9K_UP
#define KEY_DOWN   M9K_DOWN
#define KEY_LEFT   M9K_LEFT
#define KEY_RIGHT  M9K_RIGHT
#define KEY_ENTER  M9K_ENTER
#define KEY_HOME   M9K_HOME

// gdy nie ma pozycji z GPS: środek mapy w Bielsku-Białej
#define DEF_LAT  49.8225
#define DEF_LON  19.0469

// wiersze tekstu: czcionka 16 px, współrzędne logiczne ekranu 128x64 (1 jednostka w pionie = 3,75 px)
#define ROW(n)  ((n) * 5)


UITask::UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
  _node_prefs = NULL;
  _page = PAGE_KB;
  _next_refresh = _next_poll = _next_scan = _backlight_until = 0;
  _msgcount = 0; _reads = 0; _hist_len = 0;
  _last_key = -1; _same_count = 0; _last_key_ms = 0;
  _sd_ok = _sd_tested = false; _sd_type = 0;
  _sd_pending = true; _sd_free_done = false; _wifi_failed = false;
  _sd_size_mb = _sd_free_mb = _sd_write_kbs = _sd_read_kbs = 0;
  _next_sd_try = 0;
  _gps_detected = false; _gps_probe = false; _gps_probe_bytes = 0;
  _map_mem = false; _map_dirty = true; _map_follow = true;
  _map_lat = DEF_LAT; _map_lon = DEF_LON; _map_zoom = 12; _map_tiles = 0; _map_ms = 0; _next_map = 0;
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;

  pinMode(M9_PIN_EXT_PWR, INPUT);

  _kb.begin();
  _backlight_until = millis() + 5000;
  _next_scan = millis() + 5000;

  // GPS: jeśli MeshCore wykrył moduł, włączamy go na czas testu
  for (int i = 0; i < _sensors->getNumSettings(); i++) {
    const char* n = _sensors->getSettingName(i);
    if (n && strcmp(n, "gps") == 0) _gps_detected = true;
  }
  if (_gps_detected) {
    _sensors->setSettingValue("gps", "1");
  } else {
    // nie wykryty przy EN=LOW: próbujemy odwrotnej polaryzacji (wg Meshtastic tak ma wersja V2) i liczymy bajty z modułu
    pinMode(M9_PIN_GPS_EN, OUTPUT);
    digitalWrite(M9_PIN_GPS_EN, HIGH);
    _gps_probe = true;
  }

  _map_mem = _map.begin();
  // kartę montujemy dopiero w pętli (po pierwszym obrazie), żeby nie wydłużać startu

  Serial.printf("M9 test %s: klawiatura %s, SD %s, GPS %s\n", M9_FW_VERSION, _kb.isFound() ? "jest" : "BRAK",
                _sd_ok ? "jest" : "BRAK", _gps_detected ? "wykryty" : "nie wykryty");
}

// ---------------- karta SD ----------------

void UITask::mountSD() {
  _next_sd_try = millis() + 15000;
  SD.end();
  _sd_ok = SD.begin(M9_PIN_SD_CS, SPI, M9_SD_FREQ);
  if (!_sd_ok) { SD.end(); _sd_ok = SD.begin(M9_PIN_SD_CS, SPI, M9_SD_FREQ_SLOW); }
  if (!_sd_ok) return;
  _sd_type = SD.cardType();
  if (_sd_type == CARD_NONE) { _sd_ok = false; SD.end(); return; }
  _sd_size_mb = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
  _sd_free_done = false;      // wolne miejsce liczymy dopiero na stronie karty (na dużej karcie trwa to długo)
  if (!_sd_tested) testSD();
  _map.scan(SD);
  if (_map.numPacks() > 0 && !_map.hasZoom(_map_zoom)) _map_zoom = _map.maxZoom() < 12 ? _map.maxZoom() : _map.minZoom();
  _map_dirty = true;
}

// zapis i odczyt 256 kB — orientacyjna prędkość karty na wspólnej magistrali SPI
void UITask::testSD() {
  static uint8_t buf[4096];
  const int blocks = 64;
  const char* path = "/m9test.bin";
  memset(buf, 0xA5, sizeof(buf));

  unsigned long t = millis();
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  bool ok = true;
  for (int i = 0; i < blocks && ok; i++) ok = f.write(buf, sizeof(buf)) == sizeof(buf);
  f.close();
  unsigned long dt = millis() - t;
  if (ok && dt > 0) _sd_write_kbs = (uint32_t)(blocks * 4UL * 1000UL / dt);

  t = millis();
  f = SD.open(path, FILE_READ);
  if (f) {
    int n = 0;
    while (f.read(buf, sizeof(buf)) == sizeof(buf)) n++;
    f.close();
    dt = millis() - t;
    if (n == blocks && dt > 0) _sd_read_kbs = (uint32_t)(blocks * 4UL * 1000UL / dt);
  }
  SD.remove(path);
  _sd_tested = true;
}

// ---------------- GPS ----------------

bool UITask::gpsFix(double* lat, double* lon, long* sats) {
  LocationProvider* lp = _sensors->getLocationProvider();
  if (lp == NULL) { *sats = 0; return false; }
  *sats = lp->satellitesCount();
  if (!lp->isValid()) return false;
  *lat = lp->getLatitude() / 1000000.0;
  *lon = lp->getLongitude() / 1000000.0;
  return true;
}

// ---------------- klawisze ----------------

void UITask::addHistory(uint8_t key) {
  if (_hist_len == KBT_HISTORY) {
    memmove(&_hist[0], &_hist[1], sizeof(_hist[0]) * (KBT_HISTORY - 1));
    _hist_len--;
  }
  _hist[_hist_len++] = key;
}

void UITask::nextPage() {
  if (_page == PAGE_MAP && _map_mem) {      // sprzątamy kolorowy obraz, zanim wróci tekst
    _map.clear();
    m9lcdBlit(0, 240 - M9MAP_H, M9MAP_W, M9MAP_H, _map.fb());
  }
  if (_page == PAGE_WIFI) {                 // wychodzimy z wgrywania: wyłącz WiFi i odśwież listę map
    m9uploadStop();
    if (_sd_ok) _map.scan(SD);
  }
  _page = (_page + 1) % PAGE_COUNT;
  _same_count = 0;
  _map_dirty = true;
  if (_page == PAGE_WIFI) _wifi_failed = !_sd_ok || !m9uploadStart();
}

void UITask::handleKey(int16_t key) {
  #define IS(code) (key == (code))
  bool used = false;

  if (_page == PAGE_MAP) {
    double px, py;
    mcmLatLonToPixel(_map_lat, _map_lon, _map_zoom, &px, &py);
    int dx = 0, dy = 0;
    if (IS(KEY_LEFT)) dx = -80;
    if (IS(KEY_RIGHT)) dx = 80;
    if (IS(KEY_UP)) dy = -60;
    if (IS(KEY_DOWN)) dy = 60;
    if (dx || dy) {
      mcmPixelToLatLon(px + dx, py + dy, _map_zoom, &_map_lat, &_map_lon);
      _map_follow = false;
      used = true;
    }
    if (IS('i') || IS('I') || IS('+')) {              // bliżej: następny zoom, który jest na karcie
      for (int z = _map_zoom + 1; z <= _map.maxZoom(); z++) if (_map.numPacks() == 0 || _map.hasZoom(z)) { _map_zoom = z; break; }
      used = true;
    }
    if (IS('o') || IS('O') || IS('-')) {
      for (int z = _map_zoom - 1; z >= _map.minZoom(); z--) if (_map.numPacks() == 0 || _map.hasZoom(z)) { _map_zoom = z; break; }
      used = true;
    }
    if (IS(KEY_ENTER) || IS(' ')) { _map_follow = true; used = true; }
    if (used) _map_dirty = true;
  }

  // zmiana strony: klawisz Menu (0x82 wg Meshtastic) albo ten sam klawisz 5 razy pod rząd
  if (IS(KEY_HOME) || IS(M9K_MSG)) { nextPage(); return; }
  if (used) { _same_count = 0; return; }
  unsigned long now = millis();
  if (key >= 0 && key == _last_key && now - _last_key_ms < 3000) _same_count++; else _same_count = 1;
  _last_key = key;
  _last_key_ms = now;
  if (_same_count >= 5) nextPage();
  #undef IS
}

// ---------------- pętla ----------------

void UITask::loop() {
  unsigned long now = millis();
  bool dirty = false;

  // klawisze: odczyt po przerwaniu (i co 100 ms na wszelki wypadek); kontroler pamięta jeden klawisz naraz
  for (int n = 0; n < 4; n++) {
    uint8_t key = _kb.readKey();
    if (key == 0) break;
    _reads++;
    addHistory(key);
    _backlight_until = now + 5000;
    _kb.setBacklight(true);
    Serial.printf("KB key 0x%02X\n", key);
    handleKey(key);
    dirty = true;
  }

  if ((long)(now - _next_poll) >= 0) {
    _next_poll = now + 1000;
    dirty = true;
  }

  // podświetlenie klawiatury: włączone 5 s po naciśnięciu (test pinu KB_LED)
  if (_backlight_until && (long)(now - _backlight_until) >= 0) {
    _backlight_until = 0;
    _kb.setBacklight(false);
  }

  // pierwsze montowanie karty: chwilę po starcie; potem ponawiamy co 15 s tylko na stronie karty
  if (_sd_pending && now > 3000) {
    _sd_pending = false;
    mountSD();
    dirty = true;
  } else if (!_sd_pending && !_sd_ok && _page == PAGE_SD && (long)(now - _next_sd_try) >= 0) {
    mountSD();
    dirty = true;
  }

  // wgrywanie mapy przez WiFi
  if (m9uploadActive()) {
    m9uploadLoop();
    if (m9uploadChanged() && _sd_ok) _map.scan(SD);
  }

  // GPS nie wykryty: liczymy bajty przychodzące przy odwróconym EN
  if (_gps_probe) {
    while (Serial1.available()) { Serial1.read(); _gps_probe_bytes++; }
  }

  if (_display == NULL) return;

  if (_page == PAGE_MAP) {
    // mapa: rysujemy po zmianie albo co 3 s (śledzenie pozycji)
    if (_map_dirty || (long)(now - _next_map) >= 0) {
      _next_map = now + 3000;
      _map_dirty = false;
      renderMap();
    }
  } else if (dirty || (long)(now - _next_refresh) >= 0) {
    _next_refresh = now + (_page == PAGE_WIFI ? 1000 : 250);
    if (_page == PAGE_KB) renderKeyboard();
    else if (_page == PAGE_SD) renderSD();
    else renderWifi();
  }
}

// ---------------- strony ----------------

static const char* keyName(uint8_t k) {
  switch (k) {
    case M9K_ENTER: return "Enter"; case M9K_BKSP: return "Del"; case ' ': return "Spacja";
    case M9K_LEFT: return "Lewo"; case M9K_UP: return "Gora"; case M9K_DOWN: return "Dol"; case M9K_RIGHT: return "Prawo";
    case M9K_MSG: return "Czat"; case M9K_HOME: return "Home"; case M9K_F3: return "F3"; case M9K_F4: return "F4";
    case M9K_MAP: return "Mapa"; case M9K_BACK: return "Wstecz"; case M9K_GPS_LONG: return "GPS dl."; case M9K_CTRL: return "Ctrl";
    case M9K_ENTER_LONG: return "OK dl.";
  }
  return NULL;
}

void UITask::renderKeyboard() {
  char line[96], tmp[32];
  DisplayDriver& d = *_display;

  d.startFrame();
  d.setTextSize(1);

  snprintf(line, sizeof(line), "M9 TEST %s [1/4] KLAWIATURA", M9_FW_VERSION);
  d.setCursor(0, ROW(0)); d.print(line);

  if (_kb.isFound()) snprintf(line, sizeof(line), "0x%02X%c hw%02X fw%02X INT%d prz%lu bl%lu", _kb.addr(), _kb.bus() == 1 ? 'B' : 'A',
                              _kb.hwVersion() & 0xFF, _kb.fwVersion() & 0xFF, _kb.intLevel() ? 1 : 0, (unsigned long)_kb.irqCount(), (unsigned long)_kb.errors());
  else strcpy(line, "Klawiatura: NIE ZNALEZIONA (szukam...)");
  d.setCursor(0, ROW(1)); d.print(line);

  // ostatnie 3 naciśnięcia: rejestry 0-7 od razu / po 20 ms / po 100 ms
  for (int n = 0; n < 3 && n < _kb.pressCount(); n++) {
    M9Keyboard::Press p;
    _kb.getPress(n, &p);
    for (int s = 0; s < M9KB_SNAPS; s++) {
      line[0] = 0;
      if (s == 0) snprintf(line, sizeof(line), "%d>", n + 1); else strcpy(line, "  ");
      for (int r = 0; r < M9KB_SNAP_REGS; r++) {
        if (p.r[s][r] < 0) strcpy(tmp, " ER"); else snprintf(tmp, sizeof(tmp), " %02X", p.r[s][r]);
        strlcat(line, tmp, sizeof(line));
      }
      if (s == 0) { snprintf(tmp, sizeof(tmp), "  =%02X", p.key); strlcat(line, tmp, sizeof(line)); }
      d.setCursor(0, ROW(2 + n * 3 + s)); d.print(line);
    }
  }
  if (_kb.pressCount() == 0) { d.setCursor(0, ROW(3)); d.print("(nacisnij klawisz)"); }

  line[0] = 0;
  for (int i = _hist_len > 8 ? _hist_len - 8 : 0; i < _hist_len; i++) {
    uint8_t k = _hist[i];
    if (k > 32 && k < 127) snprintf(tmp, sizeof(tmp), "%02X%c ", k, (char)k); else snprintf(tmp, sizeof(tmp), "%02X ", k);
    strlcat(line, tmp, sizeof(line));
  }
  d.setCursor(0, 59); d.print(line[0] ? line : "Hist: pusta");
  d.endFrame();
}

void UITask::renderSD() {
  char line[96];
  DisplayDriver& d = *_display;

  d.startFrame();
  d.setTextSize(1);

  snprintf(line, sizeof(line), "M9 TEST %s  [2/4] KARTA SD + GPS", M9_FW_VERSION);
  d.setCursor(0, ROW(0)); d.print(line);

  if (_sd_pending) {
    d.setCursor(0, ROW(1)); d.print("Karta: sprawdzam...");
  } else if (_sd_ok) {
    if (!_sd_free_done) {          // pierwszy raz na tej stronie: policz wolne miejsce (może potrwać)
      d.setCursor(0, ROW(1)); d.print("Karta: licze wolne miejsce (do 1 min)...");
      d.endFrame();
      _sd_free_mb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024ULL * 1024ULL));
      _sd_free_done = true;
      return;
    }
    const char* type = _sd_type == CARD_MMC ? "MMC" : _sd_type == CARD_SD ? "SD" : _sd_type == CARD_SDHC ? "SDHC" : "?";
    snprintf(line, sizeof(line), "Karta: OK %s %lu MB, wolne %lu MB", type, (unsigned long)_sd_size_mb, (unsigned long)_sd_free_mb);
    d.setCursor(0, ROW(1)); d.print(line);
    snprintf(line, sizeof(line), "Zapis %lu kB/s   odczyt %lu kB/s", (unsigned long)_sd_write_kbs, (unsigned long)_sd_read_kbs);
    d.setCursor(0, ROW(2)); d.print(line);
    if (_map.numPacks() > 0) snprintf(line, sizeof(line), "Mapa: %d plikow, zoom %d-%d", _map.numPacks(), _map.minZoom(), _map.maxZoom());
    else strcpy(line, "Mapa: brak plikow .mcm w folderze /mapa");
    d.setCursor(0, ROW(3)); d.print(line);
  } else {
    d.setCursor(0, ROW(1)); d.print("Karta: NIE WYKRYTA (ponawiam co 15 s)");
    d.setCursor(0, ROW(2)); d.print("CS=48, SPI wspolne z ekranem i radiem");
  }

  double lat = 0, lon = 0; long sats = 0;
  bool fix = gpsFix(&lat, &lon, &sats);
  if (_gps_detected) {
    snprintf(line, sizeof(line), "GPS: wykryty, satelity %ld, pozycja %s", sats, fix ? "JEST" : "brak");
    d.setCursor(0, ROW(5)); d.print(line);
    if (fix) { snprintf(line, sizeof(line), "Poz: %.5f, %.5f", lat, lon); d.setCursor(0, ROW(6)); d.print(line); }
    else { d.setCursor(0, ROW(6)); d.print("(pierwsza pozycja: kilka minut pod niebem)"); }
  } else {
    d.setCursor(0, ROW(5)); d.print("GPS: NIE wykryty przy EN=LOW");
    snprintf(line, sizeof(line), "Proba EN=HIGH: %lu bajtow z modulu", (unsigned long)_gps_probe_bytes);
    d.setCursor(0, ROW(6)); d.print(line);
  }

  snprintf(line, sizeof(line), "Bateria ADC: %u mV   zasil. zewn.: %s", (unsigned)getBattMilliVolts(),
           digitalRead(M9_PIN_EXT_PWR) == LOW ? "TAK" : "nie");
  d.setCursor(0, ROW(8)); d.print(line);

  snprintf(line, sizeof(line), "Pamiec: %lu kB, PSRAM %lu kB wolne", (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024));
  d.setCursor(0, ROW(9)); d.print(line);

  snprintf(line, sizeof(line), "Bufor mapy: %s", _map_mem ? "OK" : "BRAK PAMIECI");
  d.setCursor(0, ROW(10)); d.print(line);

  d.setCursor(0, 59); d.print("Nast. strona: 5x ten sam klawisz");
  d.endFrame();
}

void UITask::renderMap() {
  char line[96];
  DisplayDriver& d = *_display;

  double lat = 0, lon = 0; long sats = 0;
  bool fix = gpsFix(&lat, &lon, &sats);
  if (fix && _map_follow) { _map_lat = lat; _map_lon = lon; }

  unsigned long t = millis();
  if (_map_mem) {
    _map_tiles = _map.render(_map_lat, _map_lon, _map_zoom);
    if (fix) _map.drawMarker(lat, lon, _map_lat, _map_lon, _map_zoom);
    if (!fix || !_map_follow) _map.drawCross();
  }
  _map_ms = millis() - t;

  // pasek tekstu u góry (jednokolorowy sterownik), pod nim kolorowa mapa
  d.startFrame();
  d.setTextSize(1);
  snprintf(line, sizeof(line), "[3/4] z%d %.4f %.4f %s k%d %lums", _map_zoom, _map_lat, _map_lon,
           fix ? (_map_follow ? "GPS" : "reka") : "bezGPS", _map_tiles, _map_ms);
  d.setCursor(0, 0); d.print(line);
  if (!_map_mem) { d.setCursor(0, ROW(3)); d.print("Brak pamieci na bufor mapy"); }
  d.endFrame();

  if (_map_mem) m9lcdBlit(0, 240 - M9MAP_H, M9MAP_W, M9MAP_H, _map.fb());
}

void UITask::renderWifi() {
  char line[96];
  DisplayDriver& d = *_display;

  d.startFrame();
  d.setTextSize(1);
  snprintf(line, sizeof(line), "M9 TEST %s  [4/4] WGRYWANIE MAPY", M9_FW_VERSION);
  d.setCursor(0, ROW(0)); d.print(line);

  if (_wifi_failed) {
    d.setCursor(0, ROW(2)); d.print(_sd_ok ? "Nie udalo sie uruchomic WiFi" : "Brak karty SD - nie ma dokad wgrywac");
  } else {
    d.setCursor(0, ROW(2)); d.print("1. Polacz komputer z siecia WiFi:");
    snprintf(line, sizeof(line), "    %s   haslo: %s", M9UP_SSID, M9UP_PASS);
    d.setCursor(0, ROW(3)); d.print(line);
    d.setCursor(0, ROW(4)); d.print("2. Otworz w przegladarce:");
    d.setCursor(0, ROW(5)); d.print("    http://192.168.4.1");

    snprintf(line, sizeof(line), "Polaczonych komputerow: %d", m9uploadClients());
    d.setCursor(0, ROW(7)); d.print(line);
    if (m9uploadFile()[0]) {
      snprintf(line, sizeof(line), "Plik: %s", m9uploadFile());
      d.setCursor(0, ROW(8)); d.print(line);
      snprintf(line, sizeof(line), "Na karcie: %.1f MB   %lu kB/s", m9uploadBytes() / 1048576.0, (unsigned long)m9uploadSpeed());
      d.setCursor(0, ROW(9)); d.print(line);
    } else {
      d.setCursor(0, ROW(8)); d.print("Czekam na pliki...");
    }
    snprintf(line, sizeof(line), "Mapa na karcie: %d plikow", _map.numPacks());
    d.setCursor(0, ROW(10)); d.print(line);
  }

  d.setCursor(0, 59); d.print("Wyjscie (wylacza WiFi): 5x ten sam klawisz");
  d.endFrame();
}

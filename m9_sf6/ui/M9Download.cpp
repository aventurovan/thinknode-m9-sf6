#include "M9Download.h"
#if defined(ESP32)
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <Update.h>

// Szyfrowanie połączenia (TLS) potrzebuje ok. 40 kB pamięci naraz, a w pamięci wewnętrznej zostaje ok. 30 kB.
// Dlatego jego bufory kierujemy do dużej pamięci dodatkowej (PSRAM); gdyby jej zabrakło — do wewnętrznej.
static void* tlsCalloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void tlsFree(void* p) { heap_caps_free(p); }

#define DIR        "/mapa"
#define MAX_FILES  80
#define CHUNK_SZ   (3UL * 1024UL * 1024UL)   // porcja pobierana do pamięci PSRAM, zanim trafi na kartę
#define MAX_TRIES  25                         // prób z rzędu bez żadnego postępu, zanim zgłosimy błąd
#define SD_CS      48

// Pobieranie porcjami. Na M9 zapis na kartę SD nie działa, gdy jest włączone WiFi (bez WiFi działa — sprawdzone testem karty).
// Dlatego: WiFi wł. → porcja do 3 MB do pamięci PSRAM → WiFi wył. → zapis porcji na kartę → WiFi wł. → następna porcja.
// Połączenia z serwerem (do kilkunastu sekund) robi zadanie w tle, więc ekran i klawisze działają przez cały czas.

enum { ST_IDLE, ST_CONNECT, ST_LIST, ST_NEXT, ST_FETCH, ST_WIFI_OFF, ST_WRITE, ST_DONE, ST_ERROR,
       ST_OTA_INFO, ST_OTA_READY, ST_OTA_FETCH, ST_OTA_DONE };
enum { N_IDLE, N_REQ, N_OPEN, N_STREAM, N_DONE, N_FAIL };

static uint8_t st = ST_IDLE;
static char base[160], status[64] = "", detail[96] = "";
static char w_ssid[33] = "", w_pass[64] = "";
static struct { char name[40]; uint32_t size; uint8_t zoom; } files[MAX_FILES];
static int n_files = 0, cur = -1, done_files = 0, retries = 0, max_zoom = 99, w_tries = 0, wifi_fails = 0;
static unsigned long t_wifi_wait = 0;          // przerwa przed kolejną próbą połączenia z WiFi
static unsigned long t0 = 0, t_retry = 0, t_fetch = 0;
static uint32_t on_card = 0, total = 0, chunk_off = 0, w_pos = 0, speed_kbs = 0;
static bool changed = false, failed_flag = false, done_flag = false, list_asked = false, after_wifi_list = false;

// wspólne z zadaniem w tle
static uint8_t* cbuf = NULL;
static volatile uint32_t c_len = 0;                    // ile bajtów porcji już przyszło
static volatile uint32_t session = 0;                  // zmiana numeru = zadanie ma się zakończyć
static volatile bool task_alive = false;
static volatile uint8_t net = N_IDLE;
static volatile int last_code = 0;                     // ostatni kod odpowiedzi serwera (ujemny = błąd połączenia)
static char req_url[220];
static volatile uint32_t req_from = 0, req_to = 0;     // zakres bajtów (włącznie); req_to = 0 → lista plików
// aktualizacja firmware: plik wersja.txt w wydaniu „latest-build”: „wersja nazwa_pliku rozmiar md5”
static bool ota_mode = false;
static volatile bool req_ota = false;
static volatile uint32_t ota_got = 0;
static char ota_cur[16] = "", ota_ver[16] = "", ota_file[72] = "", ota_md5[40] = "", ota_err[64] = "";
static uint32_t ota_size = 0;
static char list_body[4096];
// diagnostyka ostatniej nieudanej próby
static char d_tls[72] = "", diag[480] = "";
static volatile int d_hop = 0, d_code = 0, d_heap = 0, d_block = 0, d_rssi = 0, d_wifi = 0;
static char d_host[40] = "";
static volatile uint32_t srv_time = 0;
static volatile unsigned long srv_ms = 0;

// „Sat, 14 Nov 2026 06:31:00 GMT” → sekundy od 1970 (UTC)
static uint32_t parseHttpDate(const char* s) {
  static const char* const MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = ""; int D, Y, h, m, sec;
  if (sscanf(s, "%*3s, %d %3s %d %d:%d:%d", &D, mon, &Y, &h, &m, &sec) != 6) return 0;
  const char* p = strstr(MON, mon);
  if (!p || Y < 2025 || Y > 2099) return 0;
  int M = (int)(p - MON) / 3 + 1;
  int y = Y - (M <= 2), era = y / 400, yoe = y - era * 400, mp = (M + 9) % 12;
  int doy = (153 * mp + 2) / 5 + D - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + doe - 719468;
  return (uint32_t)(days * 86400L + h * 3600L + m * 60L + sec);
}

static void noteFailure(const String& u, int hop, int code) {
  d_hop = hop; d_code = code;
  d_heap = (int)(ESP.getFreeHeap() / 1024);
  d_block = (int)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  d_rssi = (int)WiFi.RSSI(); d_wifi = (int)WiFi.status();
  int a = u.indexOf("://"); a = a < 0 ? 0 : a + 3;
  int b = u.indexOf('/', a); if (b < 0) b = u.length();
  strncpy(d_host, u.substring(a, b).c_str(), sizeof(d_host) - 1); d_host[sizeof(d_host) - 1] = 0;
}

// ---- zadanie w tle: tylko sieć ----
static HTTPClient* http = NULL;
static WiFiClient* client = NULL;

static void closeHttp() {
  if (http) { http->end(); delete http; http = NULL; }
  if (client) { delete client; client = NULL; }
}

// Otwiera adres i zostawia otwarty strumień z treścią. Przekierowania (GitHub odsyła pliki wydań na inny serwer)
// obsługujemy sami, żeby nagłówek „Range” na pewno trafił do serwera, który oddaje plik.
static bool openUrl(const char* url, uint32_t from, uint32_t to) {
  String u = url;
  for (int hop = 0; hop < 4; hop++) {
    closeHttp();
    bool tls = u.startsWith("https://");
    if (tls) { WiFiClientSecure* c = new WiFiClientSecure(); c->setInsecure(); client = c; }
    else client = new WiFiClient();
    http = new HTTPClient();
    http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http->setTimeout(15000);
    http->setReuse(false);
    if (!http->begin(*client, u)) { last_code = -100; noteFailure(u, hop, -100); return false; }
    static const char* KEYS[] = { "Location", "Date" };
    http->collectHeaders(KEYS, 2);
    if (to > 0) { char r[48]; snprintf(r, sizeof(r), "bytes=%lu-%lu", (unsigned long)from, (unsigned long)to); http->addHeader("Range", r); }
    int code = http->GET();
    last_code = code;
    if (code > 0) {                                   // dokładny czas z serwera: urządzenie poprawia nim swój zegar
      uint32_t t = parseHttpDate(http->header("Date").c_str());
      if (t) { srv_ms = millis(); srv_time = t; }
    }
    if (code < 0) {                                   // nie doszło do odpowiedzi serwera: zapisz, co mówi warstwa szyfrowania
      d_tls[0] = 0;
      if (tls) ((WiFiClientSecure*)client)->lastError(d_tls, sizeof(d_tls));
      noteFailure(u, hop, code);
    }
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String loc = http->getLocation();
      if (loc.length() == 0) loc = http->header("Location");
      if (loc.length() == 0) return false;
      u = loc;
      continue;
    }
    if (to > 0) return code == 206 || (code == 200 && from == 0);    // 200 przy from = 0: serwer oddaje cały plik — weźmiemy początek
    return code == 200;
  }
  last_code = -101;                                   // za dużo przekierowań
  return false;
}

static void netTask(void* arg) {
  uint32_t my = (uint32_t)(uintptr_t)arg;
  while (session == my) {
    if (net != N_REQ) { vTaskDelay(10 / portTICK_PERIOD_MS); continue; }
    net = N_OPEN;
    uint32_t to = req_to, want = to ? to - req_from + 1 : 0;
    bool ok = openUrl(req_url, req_from, to);
    if (session != my) break;
    if (!ok) { closeHttp(); net = N_FAIL; continue; }
    if (req_ota) {                                    // aktualizacja: strumień prosto do drugiej partycji pamięci flash
      req_ota = false;
      net = N_STREAM;
      bool good = false;
      ota_err[0] = 0;
      if (!Update.begin(ota_size, U_FLASH)) snprintf(ota_err, sizeof(ota_err), "nie mogę zacząć zapisu (%s)", Update.errorString());
      else {
        if (ota_md5[0]) Update.setMD5(ota_md5);
        WiFiClient* s = http->getStreamPtr();
        static uint8_t ob[4096];
        unsigned long t_last = millis();
        while (ota_got < ota_size && session == my) {
          int av = s ? s->available() : 0;
          if (av <= 0) {
            if (!s || !s->connected()) { snprintf(ota_err, sizeof(ota_err), "połączenie zerwane"); break; }
            if (millis() - t_last > 20000) { snprintf(ota_err, sizeof(ota_err), "serwer przestał wysyłać"); break; }
            vTaskDelay(2 / portTICK_PERIOD_MS);
            continue;
          }
          int n = s->read(ob, av > (int)sizeof(ob) ? sizeof(ob) : av);
          if (n <= 0) { vTaskDelay(2 / portTICK_PERIOD_MS); continue; }
          if (Update.write(ob, n) != (size_t)n) { snprintf(ota_err, sizeof(ota_err), "błąd zapisu (%s)", Update.errorString()); break; }
          ota_got = ota_got + n; t_last = millis();
        }
        if (!ota_err[0] && ota_got >= ota_size) {
          if (Update.end(true)) good = true;           // sprawdza sumę MD5 i ustawia nową wersję do uruchomienia
          else snprintf(ota_err, sizeof(ota_err), "plik uszkodzony (%s)", Update.errorString());
        } else Update.abort();
      }
      closeHttp();
      net = good ? N_DONE : N_FAIL;
      continue;
    }
    if (to == 0) {                                    // lista plików
      String b = http->getString();
      strncpy(list_body, b.c_str(), sizeof(list_body) - 1); list_body[sizeof(list_body) - 1] = 0;
      closeHttp();
      net = N_DONE;
      continue;
    }
    net = N_STREAM;
    unsigned long t_last = millis();
    bool bad = false;
    WiFiClient* s = http->getStreamPtr();
    while (c_len < want && session == my) {
      int av = s ? s->available() : 0;
      if (av <= 0) {
        if (!s || !s->connected()) { last_code = -200; bad = true; break; }      // serwer zamknął połączenie
        if (millis() - t_last > 20000) { last_code = -201; bad = true; break; }  // nic nie przychodzi od 20 s
        vTaskDelay(2 / portTICK_PERIOD_MS);
        continue;
      }
      uint32_t n = want - c_len;
      if (n > (uint32_t)av) n = av;
      if (n > 16384) n = 16384;
      int r = s->read(cbuf + c_len, n);
      if (r <= 0) { vTaskDelay(2 / portTICK_PERIOD_MS); continue; }
      c_len = c_len + r; t_last = millis();
    }
    closeHttp();
    if (session != my) break;
    net = bad ? N_FAIL : N_DONE;                       // N_FAIL z częścią porcji: to, co przyszło, i tak zapiszemy
  }
  closeHttp();
  task_alive = false;
  vTaskDelete(NULL);
}

// ---- pętla główna ----

static void stopTask() { session = session + 1; net = N_IDLE; }

static void wifiOff() { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }

static void wifiOn() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(w_ssid, w_pass);
  t0 = millis();
}

static void fail(const char* why) {
  snprintf(diag, sizeof(diag),
           "%s\nEtap: %s, serwer %d: %s\nKod: %d\nSzyfrowanie: %s\nPamięć: %d kB wolne, blok %d kB\nWiFi: %d dBm, stan %d\nTeraz wolne: %u kB, dodatkowa %u kB",
           why, st == ST_CONNECT ? "WiFi" : st == ST_LIST ? "lista plików" : st == ST_WRITE ? "zapis na kartę" : "plik mapy", (int)d_hop + 1, d_host[0] ? d_host : "-", (int)d_code,
           d_tls[0] ? d_tls : "-", (int)d_heap, (int)d_block, (int)d_rssi, (int)d_wifi, (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  st = ST_ERROR;
  failed_flag = true;
  snprintf(status, sizeof(status), "błąd");
  snprintf(detail, sizeof(detail), "%s", why);
  stopTask();
  wifiOff();
}

static void request(const char* url, uint32_t from, uint32_t to) {
  strncpy(req_url, url, sizeof(req_url) - 1); req_url[sizeof(req_url) - 1] = 0;
  req_from = from; req_to = to;
  c_len = 0;
  t0 = millis();
  net = N_REQ;
}

void m9dlStart(const char* ssid, const char* pass, const char* base_url, int max_zoom_) {
  m9dlStop();
  if (cbuf == NULL) cbuf = (uint8_t*)ps_malloc(CHUNK_SZ);
  if (cbuf == NULL) { fail("brak pamięci PSRAM na porcję"); return; }
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  for (int i = 0; i < 60 && task_alive; i++) delay(50); // poprzednie zadanie kończy połączenie
  if (task_alive) { fail("poprzednie połączenie jeszcze się zamyka"); return; }
  snprintf(base, sizeof(base), "%s%s", base_url, base_url[strlen(base_url) - 1] == '/' ? "" : "/");
  strncpy(w_ssid, ssid, sizeof(w_ssid) - 1); w_ssid[sizeof(w_ssid) - 1] = 0;
  strncpy(w_pass, pass, sizeof(w_pass) - 1); w_pass[sizeof(w_pass) - 1] = 0;
  n_files = 0; cur = -1; done_files = 0; retries = 0; t_retry = 0; last_code = 0; list_asked = false; after_wifi_list = true;
  wifi_fails = 0; t_wifi_wait = 0;
  max_zoom = max_zoom_ > 0 ? max_zoom_ : 99;
  task_alive = true;
  if (xTaskCreatePinnedToCore(netTask, "m9dl", 16384, (void*)(uintptr_t)session, 1, NULL, 0) != pdPASS) { task_alive = false; fail("nie mogę uruchomić pobierania (pamięć)"); return; }
  wifiOn();
  st = ST_CONNECT;
  snprintf(status, sizeof(status), "łączę z WiFi…");
  snprintf(detail, sizeof(detail), "%s", ssid);
}

static int verNum(const char* v) { int a = 0, b = 0, c = 0; sscanf(v, "%d.%d.%d", &a, &b, &c); return a * 1000000 + b * 1000 + c; }

void m9otaCheck(const char* ssid, const char* pass, const char* base_url, const char* cur_ver) {
  m9dlStart(ssid, pass, base_url, 0);
  if (st == ST_ERROR) return;
  ota_mode = true;
  strncpy(ota_cur, cur_ver, sizeof(ota_cur) - 1); ota_cur[sizeof(ota_cur) - 1] = 0;
  ota_ver[0] = 0; ota_got = 0; ota_size = 0;
}

void m9otaInstall() {
  if (st != ST_OTA_READY) return;
  char url[220]; snprintf(url, sizeof(url), "%s%s", base, ota_file);
  ota_got = 0;
  req_ota = true;
  request(url, 0, 0);
  st = ST_OTA_FETCH;
  snprintf(status, sizeof(status), "pobieram %s", ota_ver);
}

int m9otaState() {                                     // 0 nic, 1 sprawdzam, 2 jest nowsza, 3 aktualna, 4 pobieram, 5 gotowe, 6 błąd
  switch (st) {
    case ST_CONNECT: case ST_OTA_INFO: return ota_mode ? 1 : 0;
    case ST_OTA_READY: return verNum(ota_ver) > verNum(ota_cur) ? 2 : 3;
    case ST_OTA_FETCH: return 4;
    case ST_OTA_DONE: return 5;
    case ST_ERROR: return ota_mode ? 6 : 0;
  }
  return 0;
}
const char* m9otaVersion() { return ota_ver; }
int m9otaPercent() { return ota_size ? (int)((uint64_t)ota_got * 100 / ota_size) : 0; }

void m9dlStop() {
  if (st == ST_OTA_FETCH) Update.abort();
  ota_mode = false;
  stopTask();
  if (st != ST_IDLE) wifiOff();
  st = ST_IDLE;
  status[0] = 0; detail[0] = 0;
}

// próba się nie udała: odczekaj i spróbuj jeszcze raz (to, co już jest na karcie, zostaje)
static void retryLater(const char* what) {
  if (++retries > MAX_TRIES) {
    char why[80]; snprintf(why, sizeof(why), "%s, kod %d", what, (int)last_code);
    fail(why);
    return;
  }
  snprintf(detail, sizeof(detail), "ponawiam (%d), kod %d", retries, (int)last_code);
  st = ST_NEXT; t_retry = millis() + (retries < 5 ? 2000 : 6000);
}

static uint32_t partSize() {
  char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
  File f = SD.open(part);
  uint32_t n = f ? f.size() : 0;
  if (f) f.close();
  return n;
}

// następna porcja: wybór pliku (z pominięciem gotowych) i zapytanie o kolejny zakres bajtów
static void nextChunk() {
  if (WiFi.status() != WL_CONNECTED) { wifiOn(); st = ST_CONNECT; after_wifi_list = false; return; }
  for (;;) {
    if (cur < 0 || cur >= n_files) cur = 0;
    // szukamy pierwszego niepełnego pliku od bieżącego
    int k = cur;
    bool found = false;
    for (; k < n_files; k++) {
      char path[64]; snprintf(path, sizeof(path), DIR "/%s", files[k].name);
      File f = SD.open(path);
      bool have = f && f.size() == files[k].size;
      if (f) f.close();
      if (!have) { found = true; break; }
    }
    if (!found) {
      st = ST_DONE;
      done_flag = true; changed = true;
      snprintf(status, sizeof(status), "gotowe");
      snprintf(detail, sizeof(detail), "pobrane pliki: %d", n_files);
      stopTask();
      wifiOff();
      return;
    }
    cur = k;
    break;
  }
  done_files = cur;
  if (!SD.exists(DIR)) SD.mkdir(DIR);
  total = files[cur].size;
  on_card = partSize();
  if (on_card > total) {
    char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
    SD.remove(part); on_card = 0;
  }
  chunk_off = on_card;
  uint32_t to = chunk_off + CHUNK_SZ - 1;
  if (to > total - 1) to = total - 1;
  char url[220]; snprintf(url, sizeof(url), "%s%s", base, files[cur].name);
  request(url, chunk_off, to);
  t_fetch = millis();
  st = ST_FETCH;
}

void m9dlLoop() {
  unsigned long now = millis();
  switch (st) {
    case ST_CONNECT:
      if (t_wifi_wait) {                               // przerwa po nieudanej próbie połączenia
        if ((long)(now - t_wifi_wait) < 0) { snprintf(status, sizeof(status), "WiFi: kolejna próba za %lu s (%d)", (t_wifi_wait - now) / 1000 + 1, wifi_fails); break; }
        t_wifi_wait = 0;
        wifiOn();
        break;
      }
      if (WiFi.status() == WL_CONNECTED) {
        wifi_fails = 0;
        WiFi.setSleep(false);                          // bez usypiania radia WiFi pobieranie jest szybsze
        if (ota_mode) { char url[220]; snprintf(url, sizeof(url), "%swersja.txt", base); request(url, 0, 0); st = ST_OTA_INFO; snprintf(status, sizeof(status), "sprawdzam wersję…"); }
        else if (after_wifi_list) { st = ST_LIST; list_asked = false; t_retry = 0; }
        else { st = ST_NEXT; t_retry = 0; }
      } else if (now - t0 > 25000) {
        int ws = (int)WiFi.status();
        // W trakcie pobierania (lista już jest) pojedyncza nieudana próba to zwykle chwilowy kłopot routera:
        // WiFi wyłączamy, czekamy i próbujemy dalej — błąd zgłaszamy dopiero po ok. 30 minutach prób.
        // Przy pierwszym łączeniu dajemy 3 próby (zła nazwa albo hasło nie powinny kazać czekać pół godziny).
        int limit = after_wifi_list ? 3 : 40;
        if (++wifi_fails < limit) {
          wifiOff();
          t_wifi_wait = now + (wifi_fails < 5 ? 10000UL : 45000UL);
          snprintf(detail, sizeof(detail), "WiFi nie odpowiada (stan %d), ponawiam", ws);
          break;
        }
        fail(ws == WL_NO_SSID_AVAIL ? "nie widzę takiej sieci WiFi (nazwa? 5 GHz?)" : ws == WL_CONNECT_FAILED ? "sieć odrzuciła połączenie (hasło?)" : "nie połączono z WiFi (nazwa, hasło?)");
      } else snprintf(status, sizeof(status), "%s łączę z WiFi… %lu s", after_wifi_list ? "" : "porcja:", (now - t0) / 1000);
      break;
    case ST_OTA_INFO:
      if (net == N_FAIL) { char why[64]; snprintf(why, sizeof(why), "nie mogę odczytać wersji, kod %d", (int)last_code); fail(why); break; }
      if (net != N_DONE) break;
      net = N_IDLE;
      {
        char fn[72] = "", md[40] = "", ver[16] = ""; unsigned long sz = 0;
        if (sscanf(list_body, "%15s %71s %lu %39s", ver, fn, &sz, md) < 3 || sz < 100000) { fail("zły plik wersja.txt"); break; }
        strcpy(ota_ver, ver); strcpy(ota_file, fn); strcpy(ota_md5, md); ota_size = sz;
      }
      st = ST_OTA_READY;
      snprintf(status, sizeof(status), "%s", verNum(ota_ver) > verNum(ota_cur) ? "jest nowsza wersja" : "masz aktualną wersję");
      snprintf(detail, sizeof(detail), "na serwerze %s, w urządzeniu %s", ota_ver, ota_cur);
      break;
    case ST_OTA_READY: break;
    case ST_OTA_FETCH:
      snprintf(detail, sizeof(detail), "%d%% · %lu z %lu kB", m9otaPercent(), (unsigned long)(ota_got / 1024), (unsigned long)(ota_size / 1024));
      if (net == N_DONE) { net = N_IDLE; st = ST_OTA_DONE; snprintf(status, sizeof(status), "zainstalowano %s", ota_ver); stopTask(); wifiOff(); }
      else if (net == N_FAIL) { net = N_IDLE; char why[96]; snprintf(why, sizeof(why), "aktualizacja: %s", ota_err[0] ? ota_err : "serwer nie oddaje pliku"); fail(why); }
      break;
    case ST_LIST: {
      if (!list_asked) {
        if (t_retry && (long)(now - t_retry) < 0) break;
        t_retry = 0;
        char url[220]; snprintf(url, sizeof(url), "%slista.txt", base);
        request(url, 0, 0);
        list_asked = true;
        break;
      }
      if (net == N_REQ || net == N_OPEN) { snprintf(status, sizeof(status), "lista plików… %lu s · %u kB", (now - t0) / 1000, (unsigned)(ESP.getFreeHeap() / 1024)); break; }
      if (net == N_FAIL) {
        list_asked = false;
        if (++retries > 5) { char why[64]; snprintf(why, sizeof(why), "nie mogę pobrać listy plików, kod %d", (int)last_code); fail(why); }
        else { snprintf(detail, sizeof(detail), "ponawiam (%d), kod %d", retries, (int)last_code); t_retry = now + 3000; }
        break;
      }
      if (net != N_DONE) break;
      net = N_IDLE;
      retries = 0;
      String body = list_body;
      int pos = 0;
      while (pos < (int)body.length() && n_files < MAX_FILES) {
        int nl = body.indexOf('\n', pos); if (nl < 0) nl = body.length();
        String line = body.substring(pos, nl); line.trim();
        pos = nl + 1;
        int sp = line.indexOf(' ');
        if (sp <= 4 || sp >= 39 || !line.substring(0, sp).endsWith(".mcm")) continue;
        bool ok = true;
        for (int i = 0; i < sp; i++) { char c = line[i]; if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) ok = false; }
        if (!ok) continue;
        int zoom = line[0] == 'z' ? atoi(line.c_str() + 1) : 0;
        if (zoom > max_zoom) continue;                 // poza wybranym zakresem
        strncpy(files[n_files].name, line.substring(0, sp).c_str(), sizeof(files[0].name) - 1);
        files[n_files].name[sizeof(files[0].name) - 1] = 0;
        files[n_files].size = strtoul(line.substring(sp + 1).c_str(), NULL, 10);
        files[n_files].zoom = (uint8_t)zoom;
        if (files[n_files].size > 0) n_files++;
      }
      if (n_files == 0) { fail("lista plików jest pusta"); break; }
      // od najmniejszego zoomu: ogólna mapa całego kraju jest na karcie najszybciej, szczegóły dochodzą później
      for (int i = 1; i < n_files; i++) {
        auto t = files[i];
        int k = i - 1;
        while (k >= 0 && files[k].zoom > t.zoom) { files[k + 1] = files[k]; k--; }
        files[k + 1] = t;
      }
      detail[0] = 0;
      after_wifi_list = false;
      cur = 0;
      st = ST_NEXT;
      break;
    }
    case ST_NEXT:
      if (t_retry && (long)(now - t_retry) < 0) break;
      t_retry = 0;
      nextChunk();
      break;
    case ST_FETCH: {
      uint32_t want = total - chunk_off; if (want > CHUNK_SZ) want = CHUNK_SZ;
      snprintf(status, sizeof(status), "%d z %d · WiFi %d dBm · %u kB", done_files + 1, n_files, (int)WiFi.RSSI(), (unsigned)(ESP.getFreeHeap() / 1024));
      if (net == N_REQ || net == N_OPEN) snprintf(detail, sizeof(detail), "z%d · łączę z serwerem… %lu s", files[cur].zoom, (now - t0) / 1000);
      else {
        uint32_t el = now - t_fetch;
        if (el > 500) speed_kbs = (uint32_t)((uint64_t)c_len * 1000 / 1024 / el);
        snprintf(detail, sizeof(detail), "z%d · %lu/%lu MB · porcja %lu%% · %lu kB/s", files[cur].zoom, (unsigned long)((chunk_off + c_len) >> 20), (unsigned long)(total >> 20),
                 (unsigned long)(want ? (uint64_t)c_len * 100 / want : 0), (unsigned long)speed_kbs);
      }
      if (net == N_DONE || net == N_FAIL) {
        bool ok = net == N_DONE;
        net = N_IDLE;
        if (c_len == 0) { retryLater(ok ? "pusta odpowiedź serwera" : "serwer nie oddaje pliku"); break; }
        // mamy dane: WiFi wyłączamy i zapisujemy na kartę
        if (!ok) retries++; else retries = 0;
        wifiOff();
        t0 = now;
        st = ST_WIFI_OFF;
      }
      break;
    }
    case ST_WIFI_OFF:                                   // chwila, żeby radio WiFi na pewno zgasło
      if (now - t0 < 300) break;
      {
        char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
        File f = SD.open(part, chunk_off > 0 ? FILE_APPEND : FILE_WRITE);
        if (f) f.close();
      }
      w_pos = 0; w_tries = 0;
      st = ST_WRITE;
      break;
    case ST_WRITE: {
      // zapis porcji: kawałki po 4 kB, najwyżej 64 kB na jedno wywołanie; prawdą jest rozmiar pliku na karcie
      char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
      File out = SD.open(part, FILE_APPEND);
      int werr = 0;
      if (out) {
        uint32_t sz = out.size();
        if (sz < chunk_off || sz > chunk_off + c_len) { out.close(); werr = -1; }
        else {
          w_pos = sz - chunk_off;
          uint32_t budget = 64 * 1024;
          while (w_pos < c_len && budget > 0) {
            uint32_t n = c_len - w_pos; if (n > 4096) n = 4096;
            errno = 0;
            size_t w = out.write(cbuf + w_pos, n);
            if (w != n) { werr = errno ? errno : -2; w_pos += w; break; }
            w_pos += n; budget -= n;
          }
          out.close();
        }
      } else werr = errno ? errno : -3;
      snprintf(status, sizeof(status), "%d z %d · zapis na kartę (WiFi wył.)", done_files + 1, n_files);
      snprintf(detail, sizeof(detail), "z%d · %lu/%lu MB · zapis %lu%%", files[cur].zoom, (unsigned long)((chunk_off + w_pos) >> 20), (unsigned long)(total >> 20),
               (unsigned long)(c_len ? (uint64_t)w_pos * 100 / c_len : 0));
      if (werr) {
        if (++w_tries > 5) {
          char why[100];
          if (werr == -1) snprintf(why, sizeof(why), "plik na karcie ma zły rozmiar");
          else snprintf(why, sizeof(why), "błąd zapisu na kartę: %d (%s) po %lu kB porcji", werr, werr > 0 ? strerror(werr) : "brak szczegółów", (unsigned long)(w_pos >> 10));
          fail(why);
          break;
        }
        if (werr == -1) { SD.remove(part); retryLater("plik na karcie ma zły rozmiar"); break; }
        SD.end(); delay(50); SD.begin(SD_CS, SPI, 20000000UL);         // spróbuj ponownie po ponownym otwarciu karty
        break;
      }
      if (w_pos < c_len) break;                        // ciąg dalszy w następnym wywołaniu
      // porcja zapisana
      if (chunk_off + c_len >= total) {
        char path[64]; snprintf(path, sizeof(path), DIR "/%s", files[cur].name);
        if (SD.exists(path)) SD.remove(path);
        SD.rename(part, path);
        changed = true;
        cur++;
      }
      wifiOn();                                         // następna porcja
      st = ST_CONNECT;
      break;
    }
  }
}

bool m9dlActive() { return !ota_mode && (st == ST_CONNECT || st == ST_LIST || st == ST_NEXT || st == ST_FETCH || st == ST_WIFI_OFF || st == ST_WRITE); }
bool m9otaActive() { return ota_mode && st != ST_IDLE && st != ST_ERROR && st != ST_OTA_DONE; }
bool m9dlChanged() { bool c = changed; changed = false; return c; }
bool m9dlFailed() { bool f = failed_flag; failed_flag = false; return f; }
bool m9dlFinished() { bool f = done_flag; done_flag = false; return f; }
const char* m9dlStatus() { return status; }
const char* m9dlDetail() { return detail; }
const char* m9dlDiag() { return diag; }
uint32_t m9dlServerTime() {
  uint32_t t = srv_time;
  if (!t) return 0;
  srv_time = 0;
  return t + (uint32_t)((millis() - srv_ms) / 1000);
}

#else
// atrapa dla symulatora
void m9dlStart(const char*, const char*, const char*, int) { }
void m9dlStop() { }
void m9dlLoop() { }
void m9otaCheck(const char*, const char*, const char*, const char*) { }
void m9otaInstall() { }
int m9otaState() { return 0; }
const char* m9otaVersion() { return ""; }
int m9otaPercent() { return 0; }
bool m9otaActive() { return false; }
bool m9dlActive() { return false; }
bool m9dlChanged() { return false; }
bool m9dlFailed() { return false; }
bool m9dlFinished() { return false; }
const char* m9dlStatus() { return ""; }
const char* m9dlDetail() { return ""; }
const char* m9dlDiag() { return ""; }
uint32_t m9dlServerTime() { return 0; }
#endif

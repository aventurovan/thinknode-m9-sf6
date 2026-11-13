#include "M9Download.h"
#if defined(ESP32)
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>

#define DIR        "/mapa"
#define MAX_FILES  80
#define BUF_SZ     16384
#define MAX_TRIES  25          // prób z rzędu bez żadnego postępu, zanim zgłosimy błąd

enum { ST_IDLE, ST_CONNECT, ST_LIST, ST_NEXT, ST_BODY, ST_DONE, ST_ERROR };

static uint8_t st = ST_IDLE;
static char base[160], status[64] = "", detail[96] = "";
static struct { char name[40]; uint32_t size; uint8_t zoom; } files[MAX_FILES];
static int n_files = 0, cur = -1, done_files = 0, retries = 0, max_zoom = 99;
static unsigned long t0 = 0, t_speed = 0, t_retry = 0;
static uint32_t got = 0, total = 0, speed_bytes = 0, speed_kbs = 0;
static HTTPClient* http = NULL;
static WiFiClient* client = NULL;
static File out;
static uint8_t* buf = NULL;
static bool changed = false;
static bool failed_flag = false;
static bool done_flag = false;
static int last_code = 0;      // ostatni kod odpowiedzi serwera (ujemny = błąd połączenia)

static void closeHttp() {
  if (http) { http->end(); delete http; http = NULL; }
  if (client) { delete client; client = NULL; }
}

static void fail(const char* why) {
  st = ST_ERROR;
  failed_flag = true;
  snprintf(status, sizeof(status), "błąd");
  snprintf(detail, sizeof(detail), "%s", why);
  if (out) out.close();
  closeHttp();
}

// Otwiera adres i zostawia otwarty strumień z treścią. Przekierowania (GitHub odsyła pliki wydań na inny serwer)
// obsługujemy sami, żeby nagłówek wznowienia „Range” na pewno trafił do serwera, który oddaje plik.
static bool openUrl(const char* url, uint32_t offset) {
  String u = url;
  for (int hop = 0; hop < 4; hop++) {
    closeHttp();
    if (u.startsWith("https://")) { WiFiClientSecure* c = new WiFiClientSecure(); c->setInsecure(); client = c; }
    else client = new WiFiClient();
    http = new HTTPClient();
    http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http->setTimeout(15000);
    http->setReuse(false);
    if (!http->begin(*client, u)) { last_code = -100; return false; }
    static const char* KEYS[] = { "Location" };
    http->collectHeaders(KEYS, 1);
    if (offset > 0) { char r[32]; snprintf(r, sizeof(r), "bytes=%lu-", (unsigned long)offset); http->addHeader("Range", r); }
    int code = http->GET();
    last_code = code;
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String loc = http->getLocation();
      if (loc.length() == 0) loc = http->header("Location");
      if (loc.length() == 0) return false;
      u = loc;
      continue;
    }
    if (offset > 0) return code == 206;               // 200 = serwer zignorował wznowienie
    return code == 200;
  }
  last_code = -101;                                   // za dużo przekierowań
  return false;
}

void m9dlStart(const char* ssid, const char* pass, const char* base_url, int max_zoom_) {
  m9dlStop();
  if (buf == NULL) buf = (uint8_t*)ps_malloc(BUF_SZ);
  if (buf == NULL) { fail("brak pamięci"); return; }
  snprintf(base, sizeof(base), "%s%s", base_url, base_url[strlen(base_url) - 1] == '/' ? "" : "/");
  n_files = 0; cur = -1; done_files = 0; retries = 0; t_retry = 0; last_code = 0;
  max_zoom = max_zoom_ > 0 ? max_zoom_ : 99;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                               // bez usypiania radia WiFi pobieranie jest wielokrotnie szybsze
  WiFi.begin(ssid, pass);
  st = ST_CONNECT; t0 = millis();
  snprintf(status, sizeof(status), "łączę z WiFi…");
  snprintf(detail, sizeof(detail), "%s", ssid);
}

void m9dlStop() {
  if (out) out.close();
  closeHttp();
  if (st != ST_IDLE) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
  st = ST_IDLE;
  status[0] = 0; detail[0] = 0;
}

// próba się nie udała: odczekaj i spróbuj jeszcze raz (pobrana część pliku zostaje na karcie)
static void retryLater(const char* what) {
  if (out) out.close();
  closeHttp();
  if (++retries > MAX_TRIES) {
    char why[80]; snprintf(why, sizeof(why), "%s, kod %d", what, last_code);
    fail(why);
    return;
  }
  snprintf(detail, sizeof(detail), "ponawiam (%d), kod %d", retries, last_code);
  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  cur--; st = ST_NEXT; t_retry = millis() + (retries < 5 ? 2000 : 6000);
}

static void nextFile() {
  for (;;) {
    cur++;
    if (cur >= n_files) {
      st = ST_DONE;
      done_flag = true;
      snprintf(status, sizeof(status), "gotowe");
      snprintf(detail, sizeof(detail), "pobrane pliki: %d", n_files);
      closeHttp();
      WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
      return;
    }
    char path[64]; snprintf(path, sizeof(path), DIR "/%s", files[cur].name);
    File f = SD.open(path);
    bool have = f && f.size() == files[cur].size;
    if (f) f.close();
    if (have) continue;
    break;
  }
  done_files = cur;
  char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
  if (!SD.exists(DIR)) SD.mkdir(DIR);
  uint32_t off = 0;
  File f = SD.open(part);
  if (f) { off = f.size(); f.close(); }
  if (off >= files[cur].size) { SD.remove(part); off = 0; }
  char url[220]; snprintf(url, sizeof(url), "%s%s", base, files[cur].name);
  if (!openUrl(url, off)) {
    if (off > 0 && last_code == 200) SD.remove(part);  // ten serwer nie umie wznawiać: plik pójdzie od początku
    retryLater("serwer nie oddaje pliku");
    return;
  }
  out = SD.open(part, off > 0 ? FILE_APPEND : FILE_WRITE);
  if (!out) { fail("nie mogę pisać na kartę"); return; }
  got = off; total = files[cur].size;
  t_speed = millis(); speed_bytes = 0;
  st = ST_BODY; t0 = millis();
}

void m9dlLoop() {
  unsigned long now = millis();
  switch (st) {
    case ST_CONNECT:
      if (WiFi.status() == WL_CONNECTED) { st = ST_LIST; snprintf(status, sizeof(status), "czytam listę plików…"); detail[0] = 0; }
      else if (now - t0 > 25000) {
        int ws = (int)WiFi.status();
        fail(ws == WL_NO_SSID_AVAIL ? "nie widzę takiej sieci WiFi (nazwa? 5 GHz?)" : ws == WL_CONNECT_FAILED ? "sieć odrzuciła połączenie (hasło?)" : "nie połączono z WiFi (nazwa, hasło?)");
      }
      break;
    case ST_LIST: {
      char url[220]; snprintf(url, sizeof(url), "%slista.txt", base);
      if (!openUrl(url, 0)) {
        if (++retries > 4) { char why[64]; snprintf(why, sizeof(why), "nie mogę pobrać listy plików, kod %d", last_code); fail(why); }
        break;
      }
      String body = http->getString();
      closeHttp();
      retries = 0;
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
      // od najmniejszego zoomu: ogólna mapa całego kraju jest na karcie po kilku minutach, szczegóły dochodzą później
      for (int i = 1; i < n_files; i++) {
        auto t = files[i];
        int k = i - 1;
        while (k >= 0 && files[k].zoom > t.zoom) { files[k + 1] = files[k]; k--; }
        files[k + 1] = t;
      }
      st = ST_NEXT;
      break;
    }
    case ST_NEXT:
      if (t_retry && (long)(now - t_retry) < 0) break;
      t_retry = 0;
      nextFile();
      break;
    case ST_BODY: {
      // czytamy przez ok. 150 ms na jedno wywołanie — szybkość nie zależy wtedy od tempa głównej pętli urządzenia
      WiFiClient* s = http->getStreamPtr();
      unsigned long t_in = millis();
      bool write_err = false, closed = false;
      while (s && got < total && millis() - t_in < 150) {
        int avail = s->available();
        if (avail <= 0) { if (!s->connected()) { closed = true; break; } delay(2); continue; }
        uint32_t want = total - got;
        int n = s->read(buf, (uint32_t)avail > BUF_SZ ? BUF_SZ : avail);
        if (n <= 0) { delay(2); continue; }
        if ((uint32_t)n > want) n = want;
        if (out.write(buf, n) != (size_t)n) { write_err = true; break; }
        got += n; speed_bytes += n; t0 = millis(); retries = 0;
      }
      now = millis();
      if (write_err) { fail("karta pełna albo błąd zapisu"); break; }
      if (now - t_speed >= 2000) { speed_kbs = (uint32_t)((uint64_t)speed_bytes * 1000 / 1024 / (now - t_speed)); speed_bytes = 0; t_speed = now; }
      snprintf(status, sizeof(status), "%d z %d · WiFi %d dBm · %u kB", done_files + 1, n_files, (int)WiFi.RSSI(), (unsigned)(ESP.getFreeHeap() / 1024));
      snprintf(detail, sizeof(detail), "z%d · %lu/%lu MB · %lu kB/s", files[cur].zoom, (unsigned long)(got >> 20), (unsigned long)(total >> 20), (unsigned long)speed_kbs);
      if (got >= total) {
        out.close();
        closeHttp();
        char part[72], path[64];
        snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name); snprintf(path, sizeof(path), DIR "/%s", files[cur].name);
        if (SD.exists(path)) SD.remove(path);
        SD.rename(part, path);
        retries = 0; changed = true;
        st = ST_NEXT;
      } else if (closed || now - t0 > 20000) {         // zerwane albo stoi: wznów od miejsca przerwania
        last_code = closed ? -200 : -201;
        retryLater("połączenie się zrywa");
      }
      break;
    }
  }
}

bool m9dlActive() { return st == ST_CONNECT || st == ST_LIST || st == ST_NEXT || st == ST_BODY; }
bool m9dlChanged() { bool c = changed; changed = false; return c; }
bool m9dlFailed() { bool f = failed_flag; failed_flag = false; return f; }
bool m9dlFinished() { bool f = done_flag; done_flag = false; return f; }
const char* m9dlStatus() { return status; }
const char* m9dlDetail() { return detail; }

#else
// atrapa dla symulatora
void m9dlStart(const char*, const char*, const char*, int) { }
void m9dlStop() { }
void m9dlLoop() { }
bool m9dlActive() { return false; }
bool m9dlChanged() { return false; }
bool m9dlFailed() { return false; }
bool m9dlFinished() { return false; }
const char* m9dlStatus() { return ""; }
const char* m9dlDetail() { return ""; }
#endif

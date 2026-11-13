#include "M9Download.h"
#if defined(ESP32)
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>

#define DIR        "/mapa"
#define MAX_FILES  80
#define RING_SZ    (256UL * 1024UL)   // bufor między siecią a kartą (PSRAM)
#define MAX_TRIES  25                 // prób z rzędu bez żadnego postępu, zanim zgłosimy błąd

// Podział pracy: połączenia z serwerem (które potrafią trwać kilkanaście sekund) robi osobne zadanie w tle
// i wkłada odebrane dane do bufora. Główna pętla urządzenia tylko przepisuje bufor na kartę SD — dzięki temu
// ekran i klawisze działają przez cały czas, a karta, radio i ekran (wspólna magistrala) są obsługiwane z jednego miejsca.

enum { ST_IDLE, ST_CONNECT, ST_LIST, ST_NEXT, ST_BODY, ST_DONE, ST_ERROR };
enum { N_IDLE, N_REQ, N_OPEN, N_STREAM, N_DONE, N_FAIL };

static uint8_t st = ST_IDLE;
static char base[160], status[64] = "", detail[96] = "";
static struct { char name[40]; uint32_t size; uint8_t zoom; } files[MAX_FILES];
static int n_files = 0, cur = -1, done_files = 0, retries = 0, max_zoom = 99;
static unsigned long t0 = 0, t_speed = 0, t_retry = 0;
static uint32_t got = 0, total = 0, speed_bytes = 0, speed_kbs = 0;
static File out;
static bool changed = false, failed_flag = false, done_flag = false, list_asked = false;

// wspólne z zadaniem w tle
static uint8_t* ring = NULL;
static volatile uint32_t r_head = 0, r_tail = 0;       // liczniki bajtów: zadanie zwiększa head, pętla główna tail
static volatile uint32_t session = 0;                  // zmiana numeru = zadanie ma się zakończyć
static volatile bool task_alive = false;
static volatile uint8_t net = N_IDLE;
static volatile int last_code = 0;                     // ostatni kod odpowiedzi serwera (ujemny = błąd połączenia)
static char req_url[220];
static volatile uint32_t req_off = 0, req_total = 0;
static volatile bool req_list = false;
static char list_body[4096];

// ---- zadanie w tle: tylko sieć ----
static HTTPClient* http = NULL;
static WiFiClient* client = NULL;

static void closeHttp() {
  if (http) { http->end(); delete http; http = NULL; }
  if (client) { delete client; client = NULL; }
}

// Otwiera adres i zostawia otwarty strumień z treścią. Przekierowania (GitHub odsyła pliki wydań na inny serwer)
// obsługujemy sami, żeby nagłówek wznowienia „Range” na pewno trafił do serwera, który oddaje plik.
static bool openUrl(const char* url, uint32_t offset) {
  String u = url;
  for (int hop = 0; hop < 4; hop++) {
    closeHttp();
    if (u.startsWith("https://")) { WiFiClientSecure* c = new WiFiClientSecure(); c->setInsecure(); c->setHandshakeTimeout(12); client = c; }
    else client = new WiFiClient();
    http = new HTTPClient();
    http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http->setConnectTimeout(8000);
    http->setTimeout(12000);
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

static void netTask(void* arg) {
  uint32_t my = (uint32_t)(uintptr_t)arg;
  while (session == my) {
    if (net != N_REQ) { vTaskDelay(10 / portTICK_PERIOD_MS); continue; }
    net = N_OPEN;
    bool ok = openUrl(req_url, req_off);
    if (session != my) break;
    if (!ok) { closeHttp(); net = N_FAIL; continue; }
    if (req_list) {
      String b = http->getString();
      strncpy(list_body, b.c_str(), sizeof(list_body) - 1); list_body[sizeof(list_body) - 1] = 0;
      closeHttp();
      net = N_DONE;
      continue;
    }
    net = N_STREAM;
    uint32_t left = req_total - req_off;
    unsigned long t_last = millis();
    bool bad = false;
    WiFiClient* s = http->getStreamPtr();
    while (left > 0 && session == my) {
      uint32_t used = r_head - r_tail;
      if (used >= RING_SZ) { t_last = millis(); vTaskDelay(2 / portTICK_PERIOD_MS); continue; }     // karta nie nadąża: czekamy na miejsce
      int av = s ? s->available() : 0;
      if (av <= 0) {
        if (!s || !s->connected()) { last_code = -200; bad = true; break; }      // serwer zamknął połączenie
        if (millis() - t_last > 20000) { last_code = -201; bad = true; break; }  // nic nie przychodzi od 20 s
        vTaskDelay(2 / portTICK_PERIOD_MS);
        continue;
      }
      uint32_t pos = r_head % RING_SZ, chunk = RING_SZ - used;
      if (chunk > RING_SZ - pos) chunk = RING_SZ - pos;
      if (chunk > (uint32_t)av) chunk = av;
      if (chunk > left) chunk = left;
      if (chunk > 8192) chunk = 8192;
      int n = s->read(ring + pos, chunk);
      if (n <= 0) { vTaskDelay(2 / portTICK_PERIOD_MS); continue; }
      r_head = r_head + n; left -= n; t_last = millis();
    }
    closeHttp();
    if (session != my) break;
    net = bad ? N_FAIL : N_DONE;
  }
  closeHttp();
  task_alive = false;
  vTaskDelete(NULL);
}

// ---- pętla główna ----

static void stopTask() { session = session + 1; net = N_IDLE; }

static void fail(const char* why) {
  st = ST_ERROR;
  failed_flag = true;
  snprintf(status, sizeof(status), "błąd");
  snprintf(detail, sizeof(detail), "%s", why);
  if (out) out.close();
  stopTask();
}

static void request(const char* url, uint32_t off, uint32_t tot, bool list) {
  r_head = 0; r_tail = 0;                              // zadanie w tym momencie nie czyta (stan inny niż N_OPEN / N_STREAM)
  strncpy(req_url, url, sizeof(req_url) - 1); req_url[sizeof(req_url) - 1] = 0;
  req_off = off; req_total = tot; req_list = list;
  t0 = millis();
  net = N_REQ;
}

void m9dlStart(const char* ssid, const char* pass, const char* base_url, int max_zoom_) {
  m9dlStop();
  if (ring == NULL) ring = (uint8_t*)ps_malloc(RING_SZ);
  if (ring == NULL) { fail("brak pamięci"); return; }
  for (int i = 0; i < 60 && task_alive; i++) delay(50); // poprzednie zadanie kończy połączenie
  if (task_alive) { fail("poprzednie połączenie jeszcze się zamyka"); return; }
  snprintf(base, sizeof(base), "%s%s", base_url, base_url[strlen(base_url) - 1] == '/' ? "" : "/");
  n_files = 0; cur = -1; done_files = 0; retries = 0; t_retry = 0; last_code = 0; list_asked = false;
  max_zoom = max_zoom_ > 0 ? max_zoom_ : 99;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  st = ST_CONNECT; t0 = millis();
  snprintf(status, sizeof(status), "łączę z WiFi…");
  snprintf(detail, sizeof(detail), "%s", ssid);
}

void m9dlStop() {
  if (out) out.close();
  stopTask();
  if (st != ST_IDLE) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
  st = ST_IDLE;
  status[0] = 0; detail[0] = 0;
}

// próba się nie udała: odczekaj i spróbuj jeszcze raz (pobrana część pliku zostaje na karcie)
static void retryLater(const char* what) {
  if (out) out.close();
  if (++retries > MAX_TRIES) {
    char why[80]; snprintf(why, sizeof(why), "%s, kod %d", what, (int)last_code);
    fail(why);
    return;
  }
  snprintf(detail, sizeof(detail), "ponawiam (%d), kod %d", retries, (int)last_code);
  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  t_retry = millis() + (retries < 5 ? 2000 : 6000);
}

static void nextFile() {
  for (;;) {
    cur++;
    if (cur >= n_files) {
      st = ST_DONE;
      done_flag = true;
      snprintf(status, sizeof(status), "gotowe");
      snprintf(detail, sizeof(detail), "pobrane pliki: %d", n_files);
      stopTask();
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
  out = SD.open(part, off > 0 ? FILE_APPEND : FILE_WRITE);
  if (!out) { fail("nie mogę pisać na kartę"); return; }
  got = off; total = files[cur].size;
  t_speed = millis(); speed_bytes = 0; speed_kbs = 0;
  char url[220]; snprintf(url, sizeof(url), "%s%s", base, files[cur].name);
  request(url, off, total, false);
  st = ST_BODY;
}

void m9dlLoop() {
  unsigned long now = millis();
  switch (st) {
    case ST_CONNECT:
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);                          // bez usypiania radia WiFi pobieranie jest wielokrotnie szybsze
        task_alive = true;
        if (xTaskCreatePinnedToCore(netTask, "m9dl", 16384, (void*)(uintptr_t)session, 1, NULL, 0) != pdPASS) { task_alive = false; fail("nie mogę uruchomić pobierania (pamięć)"); break; }
        st = ST_LIST; list_asked = false; t_retry = 0;
      } else if (now - t0 > 25000) {
        int ws = (int)WiFi.status();
        fail(ws == WL_NO_SSID_AVAIL ? "nie widzę takiej sieci WiFi (nazwa? 5 GHz?)" : ws == WL_CONNECT_FAILED ? "sieć odrzuciła połączenie (hasło?)" : "nie połączono z WiFi (nazwa, hasło?)");
      } else snprintf(status, sizeof(status), "łączę z WiFi… %lu s · %u kB", (now - t0) / 1000, (unsigned)(ESP.getFreeHeap() / 1024));
      break;
    case ST_LIST: {
      if (!list_asked) {
        if (t_retry && (long)(now - t_retry) < 0) break;
        t_retry = 0;
        char url[220]; snprintf(url, sizeof(url), "%slista.txt", base);
        request(url, 0, 0, true);
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
      // od najmniejszego zoomu: ogólna mapa całego kraju jest na karcie po kilku minutach, szczegóły dochodzą później
      for (int i = 1; i < n_files; i++) {
        auto t = files[i];
        int k = i - 1;
        while (k >= 0 && files[k].zoom > t.zoom) { files[k + 1] = files[k]; k--; }
        files[k + 1] = t;
      }
      detail[0] = 0;
      st = ST_NEXT;
      break;
    }
    case ST_NEXT:
      if (t_retry && (long)(now - t_retry) < 0) break;
      t_retry = 0;
      nextFile();
      break;
    case ST_BODY: {
      // przepisz z bufora na kartę (najwyżej 64 kB na jedno wywołanie, żeby nie blokować urządzenia)
      uint32_t budget = 64 * 1024;
      bool write_err = false;
      while (budget > 0) {
        uint32_t used = r_head - r_tail;
        if (used == 0) break;
        uint32_t pos = r_tail % RING_SZ, chunk = used;
        if (chunk > RING_SZ - pos) chunk = RING_SZ - pos;
        if (chunk > budget) chunk = budget;
        if (out.write(ring + pos, chunk) != chunk) { write_err = true; break; }
        r_tail = r_tail + chunk; got += chunk; speed_bytes += chunk; budget -= chunk; retries = 0;
      }
      if (write_err) { fail("karta pełna albo błąd zapisu"); break; }
      if (now - t_speed >= 2000) { speed_kbs = (uint32_t)((uint64_t)speed_bytes * 1000 / 1024 / (now - t_speed)); speed_bytes = 0; t_speed = now; }
      snprintf(status, sizeof(status), "%d z %d · WiFi %d dBm · %u kB", done_files + 1, n_files, (int)WiFi.RSSI(), (unsigned)(ESP.getFreeHeap() / 1024));
      if (net == N_REQ || net == N_OPEN) snprintf(detail, sizeof(detail), "z%d · łączę z serwerem… %lu s", files[cur].zoom, (now - t0) / 1000);
      else snprintf(detail, sizeof(detail), "z%d · %lu/%lu MB · %lu kB/s", files[cur].zoom, (unsigned long)(got >> 20), (unsigned long)(total >> 20), (unsigned long)speed_kbs);
      if ((net == N_DONE || net == N_FAIL) && r_head == r_tail) {       // zadanie skończyło i wszystko jest już na karcie
        bool ok = net == N_DONE && got >= total;
        if (net == N_FAIL && got == req_off && last_code == 200) {      // ten serwer nie umie wznawiać: plik pójdzie od początku
          out.close();
          char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
          SD.remove(part);
        }
        net = N_IDLE;
        if (ok) {
          out.close();
          char part[72], path[64];
          snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name); snprintf(path, sizeof(path), DIR "/%s", files[cur].name);
          if (SD.exists(path)) SD.remove(path);
          SD.rename(part, path);
          retries = 0; changed = true;
          st = ST_NEXT;
        } else {
          retryLater("połączenie się zrywa");
          if (st == ST_BODY) { cur--; st = ST_NEXT; }
        }
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

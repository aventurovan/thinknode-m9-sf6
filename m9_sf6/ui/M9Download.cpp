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

enum { ST_IDLE, ST_CONNECT, ST_LIST, ST_NEXT, ST_BODY, ST_DONE, ST_ERROR };

static uint8_t st = ST_IDLE;
static char base[160], status[64] = "", detail[96] = "";
static struct { char name[40]; uint32_t size; } files[MAX_FILES];
static int n_files = 0, cur = -1, done_files = 0, retries = 0;
static unsigned long t0 = 0, t_speed = 0;
static uint32_t got = 0, total = 0, speed_bytes = 0, speed_kbs = 0;
static HTTPClient* http = NULL;
static WiFiClient* client = NULL;
static File out;
static uint8_t* buf = NULL;
static bool changed = false;

static bool failed_flag = false;

static void fail(const char* why) {
  st = ST_ERROR;
  failed_flag = true;
  snprintf(status, sizeof(status), "błąd");
  snprintf(detail, sizeof(detail), "%s", why);
  if (out) out.close();
  if (http) { http->end(); delete http; http = NULL; }
  if (client) { delete client; client = NULL; }
}

static bool openUrl(const char* url, uint32_t offset) {
  if (http) { http->end(); delete http; http = NULL; }
  if (client) { delete client; client = NULL; }
  if (strncmp(url, "https://", 8) == 0) { WiFiClientSecure* c = new WiFiClientSecure(); c->setInsecure(); client = c; }
  else client = new WiFiClient();
  http = new HTTPClient();
  http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http->setTimeout(15000);
  if (!http->begin(*client, url)) return false;
  if (offset > 0) { char r[32]; snprintf(r, sizeof(r), "bytes=%lu-", (unsigned long)offset); http->addHeader("Range", r); }
  int code = http->GET();
  if (offset > 0 && code == 200) return false;        // serwer zignorował wznowienie: zaczniemy plik od zera
  return code == 200 || code == 206;
}

void m9dlStart(const char* ssid, const char* pass, const char* base_url) {
  m9dlStop();
  if (buf == NULL) buf = (uint8_t*)ps_malloc(BUF_SZ);
  if (buf == NULL) { fail("brak pamięci"); return; }
  snprintf(base, sizeof(base), "%s%s", base_url, base_url[strlen(base_url) - 1] == '/' ? "" : "/");
  n_files = 0; cur = -1; done_files = 0; retries = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  st = ST_CONNECT; t0 = millis();
  snprintf(status, sizeof(status), "łączę z WiFi…");
  snprintf(detail, sizeof(detail), "%s", ssid);
}

void m9dlStop() {
  if (out) out.close();
  if (http) { http->end(); delete http; http = NULL; }
  if (client) { delete client; client = NULL; }
  if (st != ST_IDLE) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
  st = ST_IDLE;
  status[0] = 0; detail[0] = 0;
}

static void nextFile() {
  for (;;) {
    cur++;
    if (cur >= n_files) {
      st = ST_DONE;
      snprintf(status, sizeof(status), "gotowe");
      snprintf(detail, sizeof(detail), "plików na karcie: %d", n_files);
      if (http) { http->end(); delete http; http = NULL; }
      if (client) { delete client; client = NULL; }
      WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
      return;
    }
    char path[64]; snprintf(path, sizeof(path), DIR "/%s", files[cur].name);
    File f = SD.open(path);
    bool have = f && f.size() == files[cur].size;
    if (f) f.close();
    if (have) { done_files++; continue; }
    break;
  }
  char part[72]; snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name);
  if (!SD.exists(DIR)) SD.mkdir(DIR);
  uint32_t off = 0;
  File f = SD.open(part);
  if (f) { off = f.size(); f.close(); }
  if (off > files[cur].size) { SD.remove(part); off = 0; }
  char url[220]; snprintf(url, sizeof(url), "%s%s", base, files[cur].name);
  snprintf(status, sizeof(status), "pobieram %d z %d", done_files + 1, n_files);
  if (!openUrl(url, off)) {
    if (off > 0) { SD.remove(part); off = 0; if (!openUrl(url, 0)) { if (++retries > 5) fail("serwer nie oddaje pliku"); else cur--; return; } }
    else { if (++retries > 5) fail("serwer nie oddaje pliku"); else cur--; return; }
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
        fail(ws == WL_NO_SSID_AVAIL ? "nie widzę takiej sieci WiFi (nazwa? sieć 5 GHz?)" : ws == WL_CONNECT_FAILED ? "sieć odrzuciła połączenie (hasło?)" : "nie połączono z WiFi (nazwa, hasło, zasięg?)");
      }
      break;
    case ST_LIST: {
      char url[220]; snprintf(url, sizeof(url), "%slista.txt", base);
      if (!openUrl(url, 0)) { fail("brak listy plików pod tym adresem"); break; }
      String body = http->getString();
      http->end();
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
        strncpy(files[n_files].name, line.substring(0, sp).c_str(), sizeof(files[0].name) - 1);
        files[n_files].name[sizeof(files[0].name) - 1] = 0;
        files[n_files].size = strtoul(line.substring(sp + 1).c_str(), NULL, 10);
        if (files[n_files].size > 0) n_files++;
      }
      if (n_files == 0) { fail("lista plików jest pusta"); break; }
      st = ST_NEXT;
      break;
    }
    case ST_NEXT: nextFile(); break;
    case ST_BODY: {
      WiFiClient* s = http->getStreamPtr();
      int avail = s ? s->available() : 0;
      if (avail > 0) {
        int n = s->readBytes(buf, avail > BUF_SZ ? BUF_SZ : avail);
        if (n > 0) {
          if (out.write(buf, n) != (size_t)n) { fail("karta pełna albo błąd zapisu"); break; }
          got += n; speed_bytes += n; t0 = now;
        }
      }
      if (now - t_speed >= 2000) { speed_kbs = speed_bytes / 1024 * 1000 / (now - t_speed); speed_bytes = 0; t_speed = now; }
      snprintf(detail, sizeof(detail), "%s · %lu z %lu MB · %lu kB/s", files[cur].name, (unsigned long)(got >> 20), (unsigned long)(total >> 20), (unsigned long)speed_kbs);
      if (got >= total) {
        out.close();
        char part[72], path[64];
        snprintf(part, sizeof(part), DIR "/%s.part", files[cur].name); snprintf(path, sizeof(path), DIR "/%s", files[cur].name);
        if (SD.exists(path)) SD.remove(path);
        SD.rename(part, path);
        done_files++; retries = 0; changed = true;
        st = ST_NEXT;
      } else if ((!http->connected() && avail <= 0) || now - t0 > 30000) {      // zerwane: wznów od miejsca przerwania
        out.close();
        if (++retries > 8) fail("połączenie się zrywa — spróbuj ponownie później");
        else { cur--; st = ST_NEXT; }
      }
      break;
    }
  }
}

bool m9dlActive() { return st == ST_CONNECT || st == ST_LIST || st == ST_NEXT || st == ST_BODY; }
bool m9dlChanged() { bool c = changed; changed = false; return c; }
bool m9dlFailed() { bool f = failed_flag; failed_flag = false; return f; }
const char* m9dlStatus() { return status; }
const char* m9dlDetail() { return detail; }

#else
// atrapa dla symulatora
void m9dlStart(const char*, const char*, const char*) { }
void m9dlStop() { }
void m9dlLoop() { }
bool m9dlActive() { return false; }
bool m9dlChanged() { return false; }
bool m9dlFailed() { return false; }
const char* m9dlStatus() { return ""; }
const char* m9dlDetail() { return ""; }
#endif

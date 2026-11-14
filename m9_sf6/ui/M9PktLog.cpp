#include "M9PktLog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef ESP32
  #include <esp_heap_caps.h>
#endif

static M9Pkt* buf = NULL;
static int head = 0, count = 0;
static uint32_t total = 0;

void m9logAdd(uint32_t ts, uint32_t ms, float snr, float rssi, const uint8_t* raw, int len) {
  if (len <= 0) return;
  if (buf == NULL) {
#ifdef ESP32
    buf = (M9Pkt*)heap_caps_calloc(M9LOG_MAX, sizeof(M9Pkt), MALLOC_CAP_SPIRAM);
#else
    buf = (M9Pkt*)calloc(M9LOG_MAX, sizeof(M9Pkt));
#endif
    if (buf == NULL) return;
  }
  M9Pkt& p = buf[head];
  p.seq = ++total; p.ts = ts; p.ms = ms;
  float s = snr * 4; p.snr_x4 = (int8_t)(s > 127 ? 127 : s < -128 ? -128 : s);
  p.rssi = (int16_t)rssi;
  if (len > M9LOG_RAW) len = M9LOG_RAW;
  p.len = (uint8_t)len; memcpy(p.raw, raw, len);
  head = (head + 1) % M9LOG_MAX;
  if (count < M9LOG_MAX) count++;
}

int m9logCount() { return count; }
uint32_t m9logTotal() { return total; }
const M9Pkt* m9logGet(int i) {
  if (buf == NULL || i < 0 || i >= count) return NULL;
  return &buf[(head - 1 - i + 2 * M9LOG_MAX) % M9LOG_MAX];
}

bool m9logParse(const M9Pkt& p, M9PktInfo& o) {
  memset(&o, 0, sizeof(o));
  int i = 0;
  if (p.len < 2) return false;
  uint8_t h = p.raw[i++];
  o.route = h & 3; o.type = (h >> 2) & 15;
  if (o.route == 0 || o.route == 3) i += 4;          // kody transportowe
  if (i >= p.len) return false;
  uint8_t pl = p.raw[i++];
  o.hash_size = (pl >> 6) + 1; o.hops = pl & 63;
  if (o.hash_size > 3) return false;
  int bl = o.hops * o.hash_size;
  if (i + bl > p.len) return false;
  o.path = &p.raw[i]; i += bl;
  o.payload = &p.raw[i]; o.payload_len = p.len - i;
  return o.payload_len >= 0;
}

const char* m9logTypeShort(uint8_t t) {
  static const char* const N[16] = { "ŻĄD", "ODP", "PRYW", "ACK", "ADV", "KANAŁ", "DANE", "LOGIN", "TRASA", "ŚLEDŹ", "CZĘŚĆ", "KONTR", "?", "?", "?", "WŁASNY" };
  return N[t & 15];
}
const char* m9logTypeLong(uint8_t t) {
  static const char* const N[16] = { "Żądanie (REQ)", "Odpowiedź (RESP)", "Wiadomość prywatna", "Potwierdzenie (ACK)", "Advert", "Wiadomość na kanale",
                                     "Dane na kanale", "Logowanie (ANON_REQ)", "Zwrot trasy (PATH)", "Śledzenie trasy (TRACE)", "Część większego pakietu",
                                     "Sterowanie / wykrywanie", "nieznany (0x0C)", "nieznany (0x0D)", "nieznany (0x0E)", "Własny (RAW)" };
  return N[t & 15];
}

void m9logPath(const M9PktInfo& in, char* d, int n, int maxIds) {
  d[0] = 0;
  int len = 0;
  for (int k = 0; k < in.hops; k++) {
    if (k == maxIds - 1 && in.hops > maxIds) {        // za długa: pierwsze, „…”, ostatnia
      len += snprintf(d + len, n - len, "…→");
      k = in.hops - 1;
    }
    const uint8_t* id = in.path + k * in.hash_size;
    char t[8];
    if (in.hash_size == 1) snprintf(t, sizeof(t), "%02X", id[0]);
    else if (in.hash_size == 2) snprintf(t, sizeof(t), "%02X%02X", id[0], id[1]);
    else snprintf(t, sizeof(t), "%02X%02X%02X", id[0], id[1], id[2]);
    len += snprintf(d + len, n - len, "%s%s", t, k < in.hops - 1 ? "→" : "");
    if (len >= n - 1) break;
  }
}

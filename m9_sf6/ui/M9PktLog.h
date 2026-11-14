#pragma once
// Log odebranych pakietów (bufor kołowy) i odczyt ich nagłówków — część przenośna (bez MeshCore).
#include <stdint.h>

#define M9LOG_MAX   200
#define M9LOG_RAW   255

struct M9Pkt {
  uint32_t seq;          // kolejny numer (rośnie)
  uint32_t ts;           // czas zegara urządzenia (s)
  uint32_t ms;           // millis() odbioru
  int8_t snr_x4; int16_t rssi;
  uint8_t len;
  uint8_t raw[M9LOG_RAW];
};

struct M9PktInfo {
  uint8_t route;         // 0 flood+transport, 1 flood, 2 direct, 3 direct+transport
  uint8_t type;          // PAYLOAD_TYPE_*
  uint8_t hash_size, hops;
  const uint8_t* path;
  const uint8_t* payload; int payload_len;
};

void m9logAdd(uint32_t ts, uint32_t ms, float snr, float rssi, const uint8_t* raw, int len);
int  m9logCount();                       // ile jest w buforze
const M9Pkt* m9logGet(int i);            // 0 = najnowszy
uint32_t m9logTotal();                   // ile odebrano od startu
bool m9logParse(const M9Pkt& p, M9PktInfo& out);
const char* m9logTypeShort(uint8_t type);    // krótka nazwa typu do listy (np. „ADV”)
const char* m9logTypeLong(uint8_t type);     // pełna nazwa do szczegółów
// ścieżka jako identyfikatory repeaterów, np. „1829→7915” (maxIds: ile pokazać, reszta jako „…”)
void m9logPath(const M9PktInfo& in, char* dest, int n, int maxIds);

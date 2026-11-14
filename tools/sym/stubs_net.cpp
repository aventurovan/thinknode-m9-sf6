// Atrapy WiFi (mapa, aktualizacja) i Bluetooth dla symulatora.
#include <stdint.h>
void m9dlStart(const char*, const char*, const char*, int) { }
void m9dlStop() { }
void m9dlLoop() { }
bool m9dlActive() { return false; }
bool m9dlFinished() { return false; }
bool m9dlFailed() { return false; }
bool m9dlChanged() { return false; }
const char* m9dlStatus() { return "gotowe"; }
const char* m9dlDetail() { return ""; }
const char* m9dlDiag() { return ""; }
uint32_t m9dlServerTime() { return 0; }
void m9otaCheck(const char*, const char*, const char*, const char*) { }
void m9otaInstall() { }
int m9otaState() { return 0; }
bool m9otaActive() { return false; }
const char* m9otaVersion() { return ""; }
int m9otaPercent() { return 0; }
int m9bleBondCount() { return 1; }
int m9bleClearBonds() { return 0; }
void m9bleShutdown() { }
unsigned m9FreeHeapKB() { return 30; }
volatile uint32_t m9ble_connects, m9ble_auth_ok, m9ble_auth_fail, m9ble_rx, m9ble_tx;
volatile uint8_t m9ble_fail_reason;

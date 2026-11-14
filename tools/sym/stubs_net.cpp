// Atrapy WiFi (mapa, aktualizacja) i Bluetooth dla symulatora.
#include <stdint.h>
#include "M9Download.h"
#include <string.h>
void m9dlStart(const M9WifiNet*, int, const char*, int) { }
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
void m9otaCheck(const M9WifiNet*, int, const char*, const char*) { }
// lista sieci: po dwóch odpytaniach „znajduje” kilka sieci
const char* sim_wifi_ap = "";
const char* m9wifiAp() { return sim_wifi_ap; }
static int sc_st = 0, sc_polls = 0;
static const struct { const char* s; int r, ch, open, n; } SC[6] = { { "Furgon", -47, 6, 0, 3 }, { "Warsztat-Goscie", -58, 11, 1, 1 }, { "iPhone (Paweł)", -63, 1, 0, 1 },
  { "UPC1234567", -71, 6, 0, 1 }, { "Orange_Swiatlowod_A1B0", -79, 11, 0, 2 }, { "NETIASPOT-2.4GHz-9F20", -86, 3, 0, 1 } };
void m9wifiScan() { sc_st = 1; sc_polls = 0; }
int m9wifiScanState() { if (sc_st == 1 && ++sc_polls > 2) sc_st = 2; return sc_st; }
int m9wifiScanCount() { return sc_st == 2 ? 6 : 0; }
bool m9wifiScanGet(int i, char* ssid, int* rssi, int* ch, bool* open, int* n) {
  if (sc_st != 2 || i < 0 || i >= 6) return false;
  if (ssid) strcpy(ssid, SC[i].s); if (rssi) *rssi = SC[i].r; if (ch) *ch = SC[i].ch; if (open) *open = SC[i].open; if (n) *n = SC[i].n;
  return true;
}
void m9wifiScanEnd() { sc_st = 0; }
void m9otaInstall() { }
int m9otaState() { return 0; }
bool m9otaActive() { return false; }
const char* m9otaVersion() { return ""; }
int m9otaPercent() { return 0; }
int sim_wifi = -1;
int m9wifiBars() { return sim_wifi; }
int m9bleBondCount() { return 1; }
int m9bleClearBonds() { return 0; }
void m9bleShutdown() { }
unsigned m9FreeHeapKB() { return 30; }
volatile uint32_t m9ble_connects, m9ble_auth_ok, m9ble_auth_fail, m9ble_rx, m9ble_tx;
volatile uint8_t m9ble_fail_reason;
int m9wifiRssi() { return sim_wifi >= 1 && sim_wifi <= 4 ? -62 : 0; }
// atrapy radia (widmo) i procesora
#include <math.h>
#include <stdlib.h>
static unsigned rs = 7; static float rnd() { rs = rs * 1103515245 + 12345; return ((rs >> 16) & 0x7FFF) / 32767.0f; }
int m9radioRssi() { return -116 + (int)(rnd() * 5); }
bool m9radioSweep(float f0, float step, int n, int8_t* out) {
  for (int i = 0; i < n; i++) {
    float f = f0 + step * (i + 0.5f), d = -117 + rnd() * 5;
    const float S[4][2] = { { 868.1f, -86 }, { 869.525f, -79 }, { 869.618f, -72 }, { 868.3f, -97 } };
    for (auto& x : S) { float e = (f - x[0]) / 0.08f; float v = x[1] - e * e * 9 + rnd() * 3; if (v > d) d = v; }
    out[i] = (int8_t)d;
  }
  return true;
}
bool m9radioListening() { return true; }
int m9radioNoiseFloor() { return -114; }
uint32_t m9radioAirtime(int len) { return 60 + len * 3; }
void m9cpuSave(bool) { }
int m9cpuMhz() { return 240; }

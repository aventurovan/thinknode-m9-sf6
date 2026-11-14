#pragma once
// Pobieranie plików mapy z internetu prosto na kartę SD: urządzenie łączy się z siecią WiFi (domową albo z telefonu),
// czyta listę plików spod podanego adresu i ściąga brakujące. Przerwane pobieranie jest wznawiane.
// Pod adresem bazowym musi leżeć plik lista.txt: w każdym wierszu „nazwa_pliku rozmiar_w_bajtach”.

#include <stdint.h>

// Zapisane sieci WiFi. Przed połączeniem urządzenie skanuje otoczenie i łączy się z najmocniejszym nadajnikiem (AP),
// który nadaje którąkolwiek z zapisanych sieci — ważne tam, gdzie kilka AP ma tę samą nazwę.
#define M9_WIFI_NETS 4
struct M9WifiNet { char ssid[33]; char pass[64]; };

void m9dlStart(const M9WifiNet* nets, int n_nets, const char* base_url, int max_zoom);   // max_zoom: 0 = wszystko
void m9dlStop();
void m9dlLoop();                 // wołać w pętli
bool m9dlActive();               // trwa łączenie albo pobieranie
bool m9dlFinished();             // true raz, gdy wszystkie pliki są pobrane
bool m9dlFailed();               // true raz po przejściu w stan błędu (opis w m9dlDetail)
bool m9dlChanged();              // true raz, gdy na karcie przybył plik (trzeba odświeżyć listę map)
const char* m9dlStatus();        // krótki stan, np. „pobieram 3 z 12”
const char* m9dlDetail();
const char* m9dlDiag();
uint32_t m9dlServerTime();

// Aktualizacja firmware przez WiFi z wydania „latest-build” (plik wersja.txt + plik „aktualizacja”).
void m9otaCheck(const M9WifiNet* nets, int n_nets, const char* base_url, const char* cur_ver);
void m9otaInstall();                    // po m9otaState() == 2 (albo 3, żeby wgrać tę samą wersję jeszcze raz)
int  m9otaState();                      // 0 nic, 1 sprawdzam, 2 jest nowsza, 3 aktualna, 4 pobieram, 5 gotowe — restart, 6 błąd
bool m9otaActive();
const char* m9otaVersion();             // wersja na serwerze
int  m9otaPercent();
int  m9wifiRssi();                     // siła sygnału WiFi (dBm), 0 = brak połączenia
int  m9wifiBars();                     // -1 WiFi wyłączone, 0 łączy albo szuka, 1..4 kreski zasięgu
const char* m9wifiAp();                // podgląd wybranego nadajnika: „Dom · kanał 6 · -48 dBm · 3 AP”; pusty, gdy nic nie łączymy

// Lista sieci w zasięgu (do wyboru na ekranie). Sieci o tej samej nazwie są jedną pozycją z najmocniejszym sygnałem.
void m9wifiScan();                     // włącza WiFi i zaczyna szukać (przerywa pobieranie, jeśli trwało)
int  m9wifiScanState();                // 0 nie szukano, 1 szukam, 2 gotowe, 3 nie udało się
int  m9wifiScanCount();
bool m9wifiScanGet(int i, char* ssid33, int* rssi, int* channel, bool* open, int* n_aps);   // od najmocniejszej
void m9wifiScanEnd();                  // czyści listę i wyłącza WiFi, jeśli nic nie pobieramy

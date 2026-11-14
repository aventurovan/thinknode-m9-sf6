#pragma once
// Pobieranie plików mapy z internetu prosto na kartę SD: urządzenie łączy się z siecią WiFi (domową albo z telefonu),
// czyta listę plików spod podanego adresu i ściąga brakujące. Przerwane pobieranie jest wznawiane.
// Pod adresem bazowym musi leżeć plik lista.txt: w każdym wierszu „nazwa_pliku rozmiar_w_bajtach”.

#include <stdint.h>

void m9dlStart(const char* ssid, const char* pass, const char* base_url, int max_zoom);   // max_zoom: 0 = wszystko
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
void m9otaCheck(const char* ssid, const char* pass, const char* base_url, const char* cur_ver);
void m9otaInstall();                    // po m9otaState() == 2 (albo 3, żeby wgrać tę samą wersję jeszcze raz)
int  m9otaState();                      // 0 nic, 1 sprawdzam, 2 jest nowsza, 3 aktualna, 4 pobieram, 5 gotowe — restart, 6 błąd
bool m9otaActive();
const char* m9otaVersion();             // wersja na serwerze
int  m9otaPercent();
int  m9wifiBars();                     // -1 WiFi wyłączone, 0 łączy, 1..4 kreski zasięgu, 5 własna sieć (wgrywanie z telefonu)       // czas UTC z ostatniej odpowiedzi serwera (nagłówek Date), poprawiony o czas od odczytu; 0 = brak. Zwraca raz.          // po błędzie: szczegóły w kilku wierszach (etap, kod, błąd szyfrowania, pamięć, WiFi)        // szczegóły, np. nazwa pliku i postęp

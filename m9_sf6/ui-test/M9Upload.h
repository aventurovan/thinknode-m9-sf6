#pragma once
// Wgrywanie plików mapy na kartę SD przez WiFi — karta w M9 jest wbudowana i nie da się jej wyjąć.
// Urządzenie tworzy własną sieć WiFi i stronę, na której wybiera się pliki .mcm z komputera.

#include <Arduino.h>

#define M9UP_SSID  "M9-MAPA"
#define M9UP_PASS  "mapa12345"

bool m9uploadStart();            // false = nie udało się uruchomić WiFi
void m9uploadStop();
void m9uploadLoop();             // wołać w pętli, gdy aktywne
bool m9uploadActive();
int  m9uploadClients();          // ilu klientów WiFi
const char* m9uploadFile();      // nazwa ostatnio wgrywanego pliku ("" = jeszcze nic)
uint32_t m9uploadBytes();        // ile bajtów tego pliku jest już na karcie
uint32_t m9uploadSpeed();        // kB/s (ostatni fragment)
bool m9uploadChanged();          // true raz po zakończeniu lub usunięciu pliku (trzeba odświeżyć listę map)

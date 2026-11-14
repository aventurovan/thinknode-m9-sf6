#pragma once
// Pomiary radia LR1110 dla ekranu Widmo i oszczędzanie energii procesora (część sprzętowa).
#include <stdint.h>

int  m9radioRssi();                                    // chwilowy poziom na naszej częstotliwości (dBm); 0 = radio zajęte
bool m9radioSweep(float f0, float step, int n, int8_t* out);   // przemiatanie pasma; false = radio odbiera albo nadaje
int  m9radioNoiseFloor();                              // szum wyliczany przez MeshCore (dBm)
uint32_t m9radioAirtime(int len);                      // czas w eterze pakietu o tej długości (ms)
void m9cpuSave(bool on);                               // true: procesor 80 MHz, false: 240 MHz
int  m9cpuMhz();

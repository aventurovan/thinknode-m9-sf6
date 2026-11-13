#pragma once
// Atrapa środowiska Arduino/ESP32 do uruchamiania interfejsu na komputerze (tools/sym).
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <math.h>
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1
extern unsigned long sim_millis;
inline unsigned long millis() { return sim_millis; }
inline void delay(int) { }
inline void pinMode(int, int) { }
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int, int) { }
inline void* ps_malloc(size_t n) { return malloc(n); }
inline void* ps_calloc(size_t a, size_t b) { return calloc(a, b); }
struct EspClass { uint32_t getFreeHeap() { return 107 * 1024; } uint32_t getFreePsram() { return 7900 * 1024; } };
extern EspClass ESP;
#ifndef __GLIBC_USE_LIB_EXT2
#endif
inline size_t sim_strlcat(char* d, const char* s, size_t n) { size_t l = strlen(d); if (l < n - 1) { strncat(d, s, n - 1 - l); } return l + strlen(s); }
#define strlcat sim_strlcat

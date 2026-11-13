#pragma once
#include <stdint.h>
#include <vector>
#define M9K_ENTER 0x0D
#define M9K_BKSP 0x08
#define M9K_BKSP_LONG 0x89
#define M9K_LEFT 0xB4
#define M9K_UP 0xB5
#define M9K_DOWN 0xB6
#define M9K_RIGHT 0xB7
#define M9K_MSG 0x81
#define M9K_HOME 0x82
#define M9K_F3 0x83
#define M9K_F4 0x84
#define M9K_MAP 0x85
#define M9K_BACK 0x86
#define M9K_CTRL 0x90
extern std::vector<uint8_t> sim_keys;
class M9Keyboard { public: void begin() { } bool isFound() { return true; } uint8_t addr() { return 0x6D; } void setBacklight(bool) { }
  uint8_t readKey() { if (sim_keys.empty()) return 0; uint8_t k = sim_keys.front(); sim_keys.erase(sim_keys.begin()); return k; } };

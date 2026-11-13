#pragma once
#define M9UP_SSID "M9-MAPA"
#define M9UP_PASS "mapa12345"
inline bool m9uploadStart() { return true; }
inline void m9uploadStop() { }
inline void m9uploadLoop() { }
inline bool m9uploadActive() { return false; }
inline int m9uploadClients() { return 0; }
inline const char* m9uploadFile() { return ""; }
inline unsigned m9uploadBytes() { return 0; }
inline unsigned m9uploadSpeed() { return 0; }
inline bool m9uploadChanged() { return false; }

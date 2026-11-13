# Symulator interfejsu

Uruchamia `m9_sf6/ui/UITask.cpp` i `UiView.cpp` na komputerze z atrapami sieci MeshCore i sprzętu.
Służy do sprawdzania ekranów bez wgrywania firmware.

```
g++ -std=c++17 -O1 -w -I tools/sym -I m9_sf6/ui -I m9_sf6 tools/sym/sym.cpp m9_sf6/ui/UITask.cpp m9_sf6/ui/UiView.cpp m9_sf6/ui/Gfx.cpp -o sym
./sym          # zapisuje zrzuty ekranów (.ppm) po zaplanowanych klawiszach
./sym fuzz 1   # 60 tys. losowych klawiszy (warto budować z -fsanitize=address,undefined)
```

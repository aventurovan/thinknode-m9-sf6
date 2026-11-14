# Makieta nowych ekranów

Rysuje projekty ekranów (Widmo, Log pakietów, Info o kanale) tym samym kodem co urządzenie
(Gfx, czcionka Inter, kolory, pasek stanu i podpowiedzi z `UiView.cpp`), z przykładowymi danymi.

    g++ -std=c++17 -O1 -w -I m9_sf6/ui tools/makieta/makieta.cpp m9_sf6/ui/Gfx.cpp -o makieta && ./makieta

Wynik: pliki `.ppm` 320×240 w bieżącym katalogu. `qr_demo.h` to przykładowy kod QR (wygenerowany w Pythonie, segno).

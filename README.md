# MC SF6 — ThinkNode M9

Firmware **companion MeshCore** dla **Elecrow ThinkNode M9** (ESP32-S3, LR1110, ekran 320×240, klawiatura QWERTY)
z polskim interfejsem „MC SF6” sieci **MeshCore Południe**.

Domyślny preset radia: **869,618 MHz · BW 62,5 kHz · SF6 · CR 4/5**.

## Stan prac

**Wersja testowa 0.5.0** — companion Bluetooth działa z aplikacją MeshCore (PIN **123456**), a na ekranie
zamiast interfejsu są **cztery strony testowe**:

1. **Klawiatura** — wykryta klawiatura, wersja kontrolera, ostatni klawisz z nazwą i historia 35 naciśnięć.
2. **Karta SD + GPS** — typ i rozmiar karty, prędkość, pliki mapy, stan GPS, bateria, pamięć.
3. **Mapa** — kolorowa mapa z karty SD (folder `/mapa`), wyśrodkowana na pozycji z GPS.
   Krzyżak przesuwa, `I` / `O` zmienia zoom, Enter wraca do pozycji GPS.
4. **Wgrywanie mapy** — karta w M9 jest wbudowana, więc pliki mapy wgrywa się przez WiFi:
   urządzenie tworzy sieć **M9-MAPA** (hasło `mapa12345`), a strona `http://192.168.4.1` przyjmuje pliki `.mcm`.

Następna strona: klawisz **Home**.

Klawiatura (protokół opisany i potwierdzony na sprzęcie w projekcie Launcher, `boards/elecrow-thinknode-m9/connections.md`):
kontroler pod adresem 0x6C (płytka v1.0) albo 0x6D (v1.1) na magistrali SDA 20 / SCL 21; rejestr 0x01 trzyma oczekujący klawisz
do odczytu, po odczycie zwraca 0x88; numer rejestru trzeba wysłać osobną transmisją (STOP) przed odczytem; linia KB_INT (pin 12)
pojawia się tylko dla niektórych klawiszy, więc rejestr jest odpytywany. Karta SD i kolorowy ekran działają na wspólnej magistrali SPI.

Mapa: narzędzia i instrukcja są w folderze [`mapa`](mapa/README.md).

## Pobieranie i wgrywanie

Pliki są w wydaniu **„Najnowszy build”** (zakładka *Releases*):

- `…-pelny.bin` — pełny obraz, do pierwszego wgrania (od adresu `0x0`), np. flasherem w przeglądarce,
- `…-aktualizacja.bin` — sama aplikacja, do późniejszych aktualizacji,
- `mapa-testowa.zip` — mapa testowa (siatka i miasta): rozpakuj na komputerze i wgraj pliki `.mcm` przez stronę 4 (WiFi).

## Budowanie

GitHub Actions pobiera MeshCore w przypiętej wersji (`MESHCORE_REF` w `.github/workflows/build.yml`),
dokłada folder `m9_sf6` jako przykład i dopisuje `platformio-m9.ini` do wariantu `thinknode_m9`.

## Licencja

Kod w tym repozytorium: MIT (jak MeshCore). Sterownik klawiatury jest napisany od zera.

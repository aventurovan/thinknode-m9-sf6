# MC SF6 — ThinkNode M9

Firmware **companion MeshCore** dla **Elecrow ThinkNode M9** (ESP32-S3, LR1110, ekran 320×240, klawiatura QWERTY)
z polskim interfejsem „MC SF6” sieci **MeshCore Południe**.

Domyślny preset radia: **869,618 MHz · BW 62,5 kHz · SF6 · CR 4/5**.

## Stan prac

**Wersja testowa 0.1.0** — companion Bluetooth działa z aplikacją MeshCore (PIN **123456**), a na ekranie
zamiast interfejsu jest **test klawiatury**:

- skan obu magistral I2C i wykryta klawiatura (adres, wersja sprzętu V1/V2),
- stan linii przerwania klawiatury i licznik naciśnięć,
- surowe rejestry klawiatury po każdym naciśnięciu i historia ostatnich 18 naciśnięć,
- napięcie baterii i wykrywanie zasilania zewnętrznego.

Na tej podstawie powstanie mapa klawiszy, a potem właściwy interfejs.

## Pobieranie i wgrywanie

Pliki są w wydaniu **„Najnowszy build”** (zakładka *Releases*):

- `…-pelny.bin` — pełny obraz, do pierwszego wgrania (od adresu `0x0`), np. flasherem w przeglądarce,
- `…-aktualizacja.bin` — sama aplikacja, do późniejszych aktualizacji.

## Budowanie

GitHub Actions pobiera MeshCore w przypiętej wersji (`MESHCORE_REF` w `.github/workflows/build.yml`),
dokłada folder `m9_sf6` jako przykład i dopisuje `platformio-m9.ini` do wariantu `thinknode_m9`.

## Licencja

Kod w tym repozytorium: MIT (jak MeshCore). Sterownik klawiatury jest napisany od zera.

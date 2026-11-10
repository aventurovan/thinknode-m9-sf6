# MC SF6 — ThinkNode M9

Firmware **companion MeshCore** dla **Elecrow ThinkNode M9** (ESP32-S3, LR1110, ekran 320×240, klawiatura QWERTY)
z polskim interfejsem „MC SF6” sieci **MeshCore Południe**.

Domyślny preset radia: **869,618 MHz · BW 62,5 kHz · SF6 · CR 4/5**.

## Stan prac

**Wersja testowa 0.2.0** — companion Bluetooth działa z aplikacją MeshCore (PIN **123456**), a na ekranie
zamiast interfejsu są **trzy strony testowe**:

1. **Klawiatura** — skan obu magistral I2C, wykryta klawiatura (adres, wersja V1/V2), linia przerwania,
   surowe rejestry po każdym naciśnięciu i historia 18 naciśnięć.
2. **Karta SD + GPS** — typ i rozmiar karty, prędkość zapisu i odczytu, pliki mapy, stan GPS
   (z próbą odwrotnej polaryzacji zasilania modułu), bateria, pamięć.
3. **Mapa** — kolorowa mapa z karty SD (folder `/mapa`), wyśrodkowana na pozycji z GPS.
   Krzyżak przesuwa, `I` / `O` zmienia zoom, Enter wraca do pozycji GPS.

Następna strona: klawisz **Menu** albo **ten sam klawisz 5 razy pod rząd** (działa, nawet jeśli kody klawiszy
okażą się inne niż zakładane).

Mapa: narzędzia i instrukcja są w folderze [`mapa`](mapa/README.md).

## Pobieranie i wgrywanie

Pliki są w wydaniu **„Najnowszy build”** (zakładka *Releases*):

- `…-pelny.bin` — pełny obraz, do pierwszego wgrania (od adresu `0x0`), np. flasherem w przeglądarce,
- `…-aktualizacja.bin` — sama aplikacja, do późniejszych aktualizacji,
- `mapa-testowa.zip` — mapa testowa na kartę SD (siatka i miasta), do sprawdzenia strony Mapa.

## Budowanie

GitHub Actions pobiera MeshCore w przypiętej wersji (`MESHCORE_REF` w `.github/workflows/build.yml`),
dokłada folder `m9_sf6` jako przykład i dopisuje `platformio-m9.ini` do wariantu `thinknode_m9`.

## Licencja

Kod w tym repozytorium: MIT (jak MeshCore). Sterownik klawiatury jest napisany od zera.

# MC SF6 — ThinkNode M9

Firmware **companion MeshCore** dla **Elecrow ThinkNode M9** (ESP32-S3, LR1110, ekran 320×240, klawiatura QWERTY)
z polskim interfejsem „MC SF6” sieci **MeshCore Południe**.

Domyślny preset radia: **869,618 MHz · BW 62,5 kHz · SF6 · CR 4/5**.

## Stan prac

**Wersja 0.9.1 — pierwszy etap właściwego interfejsu.** Companion Bluetooth dla aplikacji MeshCore,
a urządzenie ma własny kolorowy interfejs po polsku:

- **Start** — nowe wiadomości, ostatnio słyszane urządzenie, pozycja GPS, bateria i Bluetooth.
- **Wiadomości** — kanały i rozmowy prywatne (nieprzeczytane na górze), rozmowa z dymkami, pisanie z klawiatury.
- **Kontakty** — ulubione na górze, potem ostatnio słyszane; Enter otwiera rozmowę.
- **Repeatery** — lista z odległością (gdy jest GPS) i szczegóły repeatera.
- **Ustawienia** — radio (presety **SF6 — MeshCore Południe**: 869,618 · BW 62,5 · SF6 · CR 4/8 oraz **EU/UK Narrow**: 869,618 · BW 62,5 · SF8 · CR 4/8,
  ręczna zmiana częstotliwości, BW, SF i CR), advert, GPS, moc nadawania, Bluetooth, informacje.
- **Kanały** — na liście Wiadomości pozycja „+ Dodaj kanał” (kanał z hasztagiem), Del na kanale usuwa go.

Bluetooth: stały PIN **123456** (własny można ustawić w aplikacji MeshCore).

Klawisze: ◄ ► zmiana ekranu, ▲ ▼ lista, Enter / środek krzyżaka — wybór, **Wstecz** — krok wstecz, **Home** — Start,
**dymki** — Wiadomości, **wiadomość** — Kontakty, **mapa** — Repeatery. W rozmowie: Enter wysyła, **ctrl + litera** daje polską
literę (a→ą, c→ć, e→ę, l→ł, n→ń, o→ó, s→ś, z→ż, x→ź), **pinezka** wstawia pozycję, ▲ ▼ przewija.

Jeszcze nie ma (kolejne etapy): zapisu wiadomości po restarcie, zarządzania repeaterami, „Co tu słychać?”, „Usłyszanych”, mapy z karty SD.
Strony testowe (karta SD, GPS, mapa, wgrywanie mapy przez WiFi) zostały w kodzie jako osobny wariant `ThinkNode_M9_sf6_test`.

Klawiatura (protokół opisany i potwierdzony na sprzęcie w projekcie Launcher, `boards/elecrow-thinknode-m9/connections.md`):
kontroler pod adresem 0x6C (płytka v1.0) albo 0x6D (v1.1) na magistrali SDA 20 / SCL 21; rejestr 0x01 trzyma oczekujący klawisz
do odczytu, po odczycie zwraca 0x88; numer rejestru trzeba wysłać osobną transmisją (STOP) przed odczytem; linia KB_INT (pin 12)
pojawia się tylko dla niektórych klawiszy, więc rejestr jest odpytywany.

Czcionka ekranowa: Inter (SIL Open Font License 1.1), generowana skryptem `tools/gen_font.py`. Podgląd ekranów na komputerze: `tools/podglad`.

Mapa: narzędzia i instrukcja są w folderze [`mapa`](mapa/README.md).

## Pobieranie i wgrywanie

Pliki są w wydaniu **„Najnowszy build”** (zakładka *Releases*):

- `…-pelny.bin` — pełny obraz, do pierwszego wgrania (od adresu `0x0`), np. flasherem w przeglądarce,
- `…-aktualizacja.bin` — sama aplikacja, do późniejszych aktualizacji,
- `mapa-testowa.zip` — mapa testowa (siatka i miasta) do późniejszego etapu z mapą.

## Budowanie

GitHub Actions pobiera MeshCore w przypiętej wersji (`MESHCORE_REF` w `.github/workflows/build.yml`),
dokłada folder `m9_sf6` jako przykład i dopisuje `platformio-m9.ini` do wariantu `thinknode_m9`.

## Licencja

Kod w tym repozytorium: MIT (jak MeshCore). Sterownik klawiatury jest napisany od zera.

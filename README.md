# MC SF6 — ThinkNode M9

Firmware **companion MeshCore** dla **Elecrow ThinkNode M9** (ESP32-S3, LR1110, ekran 320×240, klawiatura QWERTY)
z polskim interfejsem „MC SF6” sieci **MeshCore Południe**.

Domyślny preset radia: **869,618 MHz · BW 62,5 kHz · SF6 · CR 4/5**.

## Stan prac

**Wersja 0.18.2.** Companion Bluetooth dla aplikacji MeshCore i MeshSpeed (PIN losowany przy każdym uruchomieniu i pokazywany na ekranie przy parowaniu; stały można ustawić w Ustawieniach) i własny kolorowy interfejs po polsku:

- **Start** — zegar i sześć kafelków (Wiadomości, Kontakty, Repeatery, Pozycja, Radio, Urządzenie); po kafelkach chodzi się strzałkami, Enter wchodzi.
- **Dolny pasek** — ikony: Start, Wiadomości, Kontakty, Repeatery, Ustawienia.
- **Wiadomości** — kanały i rozmowy prywatne (nieprzeczytane na górze), dodawanie i usuwanie kanałów z hasztagiem,
  rozmowa z dymkami, szybkie odpowiedzi, pozycja. Pod wysłaną wiadomością licznik powtórzeń (kanał) albo „dostarczono” (prywatna),
  pod odebraną liczba skoków i trasa z identyfikatorami repeaterów. Menu wiadomości: szczegóły (trasa z nazwami repeaterów, SNR, RSSI,
  kto powtórzył), odpowiedz, wyślij ponownie, kopiuj, usuń.
- **Kontakty** — wiersz filtrów, po którym chodzi się strzałkami (wszyscy / osoby / RPT / pokoje / czujniki / ulubione / słyszane), szukanie po nazwie,
  menu kontaktu: napisz, ulubiony, szczegóły, zarządzaj, usuń.
- **Repeatery** — lista wg ostatniego kontaktu albo odległości, „Co tu słychać?” (ping do sąsiadów z SNR w obie strony; wykryty repeater można otworzyć i dodać do kontaktów),
  zarządzanie: logowanie (hasła zapamiętywane), stan, sąsiedzi, akcje (advert, zegar, preset SF6, restart), konsola.
- **Ustawienia** — siatka ośmiu działów: Radio, Bluetooth, Pozycja i prywatność, Advert, Kontakty, Wiadomości, Ekran i dźwięk, System.
- **Zapis** — wiadomości (do 300), ustawienia ekranu, wyciszenia i hasła repeaterów zostają po restarcie. Kontaktów do 500.
- **Ustawienia** — każdą wartość wybiera się z listy (Enter otwiera listę); presety radia są pod pozycją „Preset”.
- **Dolny pasek** — ▼ z ostatniego wiersza lub dolnego rzędu kafelków schodzi na pasek, ◄ ► wybiera ikonę, Enter wchodzi, ▲ wraca.
- **Blokada klawiszy** (Ustawienia › Ekran i dźwięk) — włącza się z wygaszeniem ekranu; odblokowanie: Wstecz, potem Enter.
- **Wyciszanie rozmów** — Pinezka na liście Wiadomości otwiera opcje rozmowy (wycisz, oznacz jako przeczytane, usuń).
- **Bluetooth** — w ustawieniach liczniki połączeń, parowań i ramek do diagnozy (łatka `tools/patch_ble.py`).

### Klawisze

| Klawisz | Działanie |
|---|---|
| ◄ ► | poprzedni / następny ekran; w ustawieniach zmiana wartości |
| ▲ ▼ | lista; w rozmowie wybór wiadomości |
| Enter, środek krzyżaka | wybór; w rozmowie wysłanie (puste pole: szybkie odpowiedzi) |
| ► w rozmowie | menu zaznaczonej wiadomości |
| Wstecz | krok wstecz |
| Home / Dymki / Pinezka / Mapa | Start / Wiadomości / Repeatery / Mapa |
| Mała wiadomość pod Home („Menu”) | dodatkowe opcje na bieżącym ekranie: opcje rozmowy, menu kontaktu, sortowanie repeaterów, opcje wiadomości, zegar z GPS |
| litery na liście Kontakty | szukanie po nazwie (Del kasuje) |
| Del na kanale | usunięcie kanału |
| ctrl, potem litera | ą ć ę ł ń ó ś ż (ź pod X) |

- **Mapa** (klawisz Mapa) — mapa z karty SD z Twoją pozycją, repeaterami i kontaktami; strzałki przesuwają, I / O zmieniają zoom, Enter wraca do pozycji GPS,
  Menu otwiera pobieranie mapy z internetu, wgrywanie z telefonu i ustawienia WiFi. „Pokaż na mapie” jest w menu kontaktu.

- **Trasa wiadomości na mapie** — w menu wiadomości „Trasa na mapie”: odebrana — nadawca, kolejne repeatery i Ty; wysłana na kanale — repeatery, które ją powtórzyły.
- **Aktualizacja przez WiFi** — Ustawienia › System › „Aktualizacja przez WiFi”: urządzenie sprawdza wersję w wydaniu „Najnowszy build” i instaluje ją samo (plik `wersja.txt` z sumą MD5).
- **Mapa** — cała Polska w zoomach 6–13 (ok. 330 MB).

Jeszcze nie ma: SOS.

Symulator interfejsu na komputerze (ten sam kod ekranów, atrapy sieci i sprzętu): `tools/sym`.

Klawiatura (protokół opisany i potwierdzony na sprzęcie w projekcie Launcher, `boards/elecrow-thinknode-m9/connections.md`):
kontroler pod adresem 0x6C (płytka v1.0) albo 0x6D (v1.1) na magistrali SDA 20 / SCL 21; rejestr 0x01 trzyma oczekujący klawisz
do odczytu, po odczycie zwraca 0x88; numer rejestru trzeba wysłać osobną transmisją (STOP) przed odczytem; linia KB_INT (pin 12)
pojawia się tylko dla niektórych klawiszy, więc rejestr jest odpytywany.

Czcionka ekranowa: Inter (SIL Open Font License 1.1), generowana skryptem `tools/gen_font.py`; logo na ekranie startowym z ikony aplikacji MeshSpeed (`tools/gen_logo.py`).

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

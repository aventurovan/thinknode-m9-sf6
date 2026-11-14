# MeshSpeed — firmware dla ThinkNode M9

**MeshSpeed** to firmware **companion MeshCore** dla **Elecrow ThinkNode M9** (ESP32-S3, radio LR1110, kolorowy ekran 320×240, klawiatura QWERTY)
z polskim interfejsem, przygotowany dla sieci **MeshCore Południe**. Urządzenie działa samodzielnie: piszesz, czytasz, zarządzasz repeaterami
i oglądasz mapę bez telefonu. Przez Bluetooth łączy się z aplikacjami **MeshSpeed** i **MeshCore**.

Domyślny preset radia: **SF6 — 869,618 MHz · BW 62,5 kHz · SF6 · CR 4/8**. Do wyboru jest też **EU/UK Narrow (SF8)**.

Aktualna wersja: **0.20.0** (numer jest też na ekranie startowym i w Ustawieniach).

---

## Szybki start

### 1. Pobierz plik

Wejdź w zakładkę **Releases** i otwórz wydanie **„Najnowszy build”**:

| Plik | Do czego |
|---|---|
| `MC-SF6-ThinkNode-M9-<wersja>-pelny.bin` | pierwsze wgranie kablem USB (pełny obraz) |
| `MC-SF6-ThinkNode-M9-<wersja>-aktualizacja.bin` | sama aplikacja (aktualizacje kablem) |
| `wersja.txt` | podpisany opis wersji, z którego urządzenie sprawdza aktualizacje przez WiFi |
| `mapa-testowa.zip` | mała mapa testowa (siatka i miasta) |

### 2. Pierwsze wgranie (kabel USB)

1. Podłącz M9 kablem USB do komputera.
2. Otwórz flasher ESP32 w przeglądarce (Chrome lub Edge), np. <https://espressif.github.io/esptool-js/>, i połącz się z urządzeniem.
3. Wgraj plik **`…-pelny.bin` od adresu `0x0`**.
4. **Nie** używaj „Erase all / Erase flash” przy aktualizacji, bo skasujesz wiadomości, kontakty i ustawienia. Kasowanie ma sens tylko przy pierwszym wgraniu na urządzenie z innym firmware.

### 3. Pierwsze uruchomienie

- Ustaw nazwę urządzenia: **Ustawienia › System › Nazwa urządzenia**.
- Ustaw datę i godzinę: **Ustawienia › System › Data i godzina**. Zegar ustawi się też sam z GPS, z aplikacji albo przy aktualizacji przez WiFi.
- Sprawdź preset radia: **Ustawienia › Radio › Preset**.
- Wyślij advert: **Ustawienia › Advert**.

### 4. Połączenie z telefonem (Bluetooth)

1. W aplikacji **MeshSpeed** (albo MeshCore) wybierz urządzenie z listy Bluetooth.
2. Na ekranie M9 pojawi się **PIN** (losowy, inny przy każdym uruchomieniu). Wpisz go w telefonie.
3. Gdy parowanie nie działa: **Ustawienia › Bluetooth › Zapomnij wszystkie telefony**, a w telefonie usuń M9 z listy urządzeń Bluetooth i sparuj od nowa.

Stały PIN można ustawić w **Ustawieniach › Bluetooth**.

### 5. Kolejne aktualizacje (przez WiFi, bez komputera)

**Ustawienia › System › Aktualizacja przez WiFi**:

1. Wpisz sieć WiFi i hasło (domową albo hotspot z telefonu).
2. Urządzenie sprawdzi wersję w wydaniu „Najnowszy build”. Gdy jest nowsza, sprawdzi **podpis cyfrowy**, pobierze plik, porówna go z podpisem i samo się zrestartuje.

Aktualizacja bez poprawnego podpisu nie zostanie zainstalowana, a starszej wersji urządzenie nie wgra.

Na czas WiFi Bluetooth jest wyłączony (oba naraz nie mieszczą się w pamięci). Wraca po restarcie.

### 6. Mapa

Mapa całej Polski (zoom 6–13, ok. 330 MB) leży na wbudowanej karcie SD. Pobiera ją samo urządzenie:
klawisz **Mapa**, potem **Menu**, potem **„Pobierz z internetu”**. Szczegóły opisuje [`mapa/README.md`](mapa/README.md).

---

## Co potrafi

### Ekran Start

Duży zegar i sześć kafelków. Po kafelkach chodzi się strzałkami, a Enter otwiera wybrany:

| Kafelek | Pokazuje | Enter |
|---|---|---|
| Wiadomości | nowe wiadomości, ostatnia rozmowa | lista rozmów |
| Kontakty | liczba kontaktów, ilu słychać | lista kontaktów |
| Radio | preset (SF6 / SF8) i częstotliwość | ustawienia radia |
| Sieć | Bluetooth (nazwa połączonej aplikacji) i WiFi (dBm) | ustawienia Bluetooth |
| Widmo | szum na naszym kanale z wykresem, zajętość kanału | ekran Widmo |
| Bateria | procent, wykres napięcia z ostatnich godzin, czas pracy | szczegóły baterii |

Menu (mała wiadomość pod Home) na ekranie Start otwiera **Widmo** i **Log pakietów**.

### Wiadomości

- **Lista:** kanały i rozmowy prywatne, nieprzeczytane są na górze. Menu na liście: otwórz, info o kanale, wycisz, oznacz jako przeczytane, usuń, **dodaj kanał** (z hasztagiem, np. `#beskidy`).
- **Rozmowa:**
  - dymki, płynne przewijanie ▲▼ z suwakiem; po wejściu widok staje na pierwszej nieprzeczytanej;
  - pod wysłaną wiadomością na kanale licznik powtórzeń i identyfikator pierwszego repeatera, przy prywatnej „Doręczono”;
  - pod odebraną liczba hopów i trasa z identyfikatorami repeaterów (4 znaki).
- **Opcje wiadomości** (Menu albo ►): Odpowiedz @, Odpowiedz ack (z liczbą hopów), Szczegóły (trasa, SNR, RSSI, bajty), Trasa na mapie, Wyślij ponownie, Kopiuj, Usuń.
- **Info o kanale:** rodzaj kanału, hash, liczba wiadomości, średnia liczba hopów i najaktywniejsi z 24 h. Enter pokazuje **kod QR**, którym kolega doda kanał w aplikacji.
- **Szybkie odpowiedzi:** Enter na pustym polu, m.in. „Test” i pozycja.
- **Wyciszanie:** pojedyncze kanały i rozmowy.
- **Pamięć:** do 300 wiadomości, zostają po restarcie.

### Kontakty i repeatery

- **Kontakty** (do 500):
  - filtry w wierszu: wszyscy, osoby, RPT, pokoje, ulubione, słyszane; szukanie przez pisanie liter;
  - menu kontaktu: napisz, ulubiony, szczegóły, pokaż na mapie, zarządzaj, usuń;
  - czujniki nie są dodawane.
- **Repeatery** (klawisz Pinezka): lista według ostatniego kontaktu albo odległości.
  - **„Co tu słychać?”** wysyła ping do sąsiadów z SNR w obie strony. Wykryty repeater można otworzyć i dodać do kontaktów.
  - **Zarządzanie:** logowanie zawsze przez flood z resetem ścieżki, hasła są zapamiętywane. Do tego stan, sąsiedzi (kolejne porcje), akcje (advert, zegar, preset SF6, restart) i konsola.
- **Dodawanie z advertów:** wybór, jakie typy dodawać i z jak daleka (**Ustawienia › Kontakty**).

### Widmo i log pakietów

- **Widmo › Pasmo:** słupki poziomu w paśmie LoRa **867–870 MHz** (▲▼ przełącza na zbliżenie 869,3–869,9 MHz), szczyty i wodospad.
  - Nasza częstotliwość jest zaznaczona jako „SF6”. Enter wstrzymuje pomiar.
  - Na czas pomiaru odbiór pakietów MeshCore jest na chwilę przerywany, dlatego pomiar działa tylko przy otwartej karcie.
- **Widmo › Nasz kanał** (◄► przełącza karty):
  - szum, siła ostatniego pakietu i zajętość kanału z ostatniej minuty;
  - wykres z 10 minut: linia szumu i kropki odebranych pakietów;
  - nie przerywa odbioru.
- **Log pakietów:** wszystko, co odebrało radio: czas, typ (ADV, KANAŁ, PRYW, ACK, TRASA…), hopy, nadawca lub ścieżka, SNR, dBm.
  - Filtry ◄►: wszystkie, adverty, wiadomości, inne.
  - Enter otwiera szczegóły pakietu (◄► poprzedni / następny, Menu pokazuje nadawcę advertu na mapie).

### Mapa

Klawisz **Mapa** pokazuje mapę z karty SD z Twoją pozycją (GPS), repeaterami (pomarańczowe punkty z 4-znakowym identyfikatorem) i kontaktami.

- Strzałki przesuwają mapę, **I / O** zmieniają zoom, Enter wraca do pozycji GPS.
- „Trasa na mapie” z menu wiadomości rysuje drogę wiadomości przez repeatery.

### Oszczędzanie baterii

| Ustawienie | Gdzie | Domyślnie |
|---|---|---|
| Przyciemnianie ekranu (pierwszy klawisz tylko rozjaśnia) | Ekran i dźwięk › Przyciemnianie | po 10 s |
| Wygaszanie ekranu | Ekran i dźwięk › Wygaszanie | 1 min |
| Procesor 80 MHz po wygaszeniu ekranu | Ekran i dźwięk › Procesor po wygaszeniu | oszczędny |
| GPS szuka pozycji co 5 / 15 / 30 / 60 min | Pozycja › Oszczędzanie GPS | włączony stale |
| Bluetooth wyłącza się, gdy telefon się nie łączy | Bluetooth › Wyłącz bez połączenia | nigdy |

Kafelek Bateria pokazuje, jak spada napięcie i ile czasu zostało. Pierwsze oszacowanie pojawia się ok. 30 minut po włączeniu (pomiar co 7,5 min).

### Pozostałe

- **Ustawienia** w ośmiu działach: Radio, Bluetooth, Pozycja i prywatność, Advert, Kontakty, Wiadomości, Ekran i dźwięk, System. Każdą wartość wybiera się z listy.
- **Radio:** presety SF6 i EU/UK Narrow, własne parametry, moc nadawania, bajty trasy (1 / 2 / 3), limit nadawania.
- **Blokada ekranu:** włącza się z wygaszeniem ekranu. Do wyboru zwykła (Wstecz, potem Enter) albo z **PIN-em** (opis niżej).
- **Dźwięki:** wiadomości, adverty repeaterów (wszystkie albo tylko ulubione), wyciszenie całego urządzenia.
- **Jasność ekranu** i podświetlenie klawiatury.
- **Pasek stanu:** godzina, nieprzeczytane, GPS, Bluetooth, WiFi z zasięgiem, bateria.

### Bezpieczeństwo

- **Podpisane aktualizacje:** każdy build jest podpisywany kluczem Ed25519 trzymanym w sekretach repozytorium. Urządzenie ma wbudowany klucz publiczny i instaluje przez WiFi tylko podpisane pliki.
- **Blokada ekranu z PIN-em** (Ustawienia › Ekran i dźwięk › Blokada ekranu): 4–8 cyfr. Ekran blokuje się po wygaszeniu i po każdym włączeniu urządzenia. Po 5 złych próbach rośnie przerwa (od 30 s do 15 min). Zapomniany PIN usuwa tylko wgranie firmware kablem z kasowaniem pamięci.
- **Rozmowy prywatne należą do klucza kontaktu, nie do nazwy.** Drugi kontakt o tej samej nazwie dostaje osobną rozmowę z dopiskiem identyfikatora (np. „Marek ·A3F2”). Identyfikator rozmówcy jest w nagłówku rozmowy.
- **Hasła WiFi i repeaterów** są zapisywane zaszyfrowane kluczem wyprowadzonym z tożsamości urządzenia i nie są pokazywane na ekranie.

---

## Klawisze

| Klawisz | Działanie |
|---|---|
| ▲ ▼ ◄ ► | chodzenie po kafelkach i listach; w rozmowie ▲▼ przewija |
| Enter / środek krzyżaka | wybór; w rozmowie wysłanie (puste pole: szybkie odpowiedzi) |
| Wstecz | krok wstecz |
| Home | ekran Start |
| Dymki | Wiadomości |
| Pinezka | Repeatery |
| Mapa | Mapa |
| Mała wiadomość pod Home („Menu”) | opcje na bieżącym ekranie (rozmowy, wiadomości, kontaktu, mapy, Startu) |
| ▼ z dolnego rzędu | zejście na dolny pasek ikon (◄ ► wybór, ▲ powrót) |
| ctrl, potem litera | ą ć ę ł ń ó ś ż (ź pod X) |
| litery na liście Kontakty | szukanie po nazwie (Del kasuje) |

---

## Znane ograniczenia

- WiFi i Bluetooth nie działają jednocześnie. Po użyciu WiFi (mapa, aktualizacja) Bluetooth wraca dopiero po restarcie.
- Pomiar pasma w Widmie na chwilę przerywa odbiór pakietów, dlatego działa tylko przy otwartym ekranie.
- Nazwy telefonu przez Bluetooth nie da się odczytać. Kafelek Sieć pokazuje nazwę aplikacji (np. „MeshSpeed”).
- Blokada z PIN-em chroni ekran. Kto ma urządzenie w ręku i odczyta całą pamięć kablem, nadal może dostać się do danych (pamięć urządzenia nie jest szyfrowana).
- Jeszcze nie ma: SOS.

Błędy i pomysły zgłaszaj w zakładce **Issues** (najlepiej ze zdjęciem ekranu i numerem wersji).

---

## Dla programistów

- **Budowanie:** GitHub Actions (`.github/workflows/build.yml`) pobiera MeshCore w przypiętej wersji (`MESHCORE_REF`), dokłada folder `m9_sf6` jako przykład
  i dopisuje `platformio-m9.ini` do wariantu `thinknode_m9`. Każdy push do `main` publikuje wydanie „Najnowszy build”.
- **Podpis aktualizacji:** `tools/podpisz.py` tworzy `wersja.txt` (wersja, plik, rozmiar, MD5, SHA-256, podpis). Klucz prywatny jest w sekrecie `OTA_SIGNING_KEY`, publiczny w `m9_sf6/ui/OtaKey.h`. Własny fork potrzebuje własnej pary kluczy.
- **Kod:** `m9_sf6/ui` zawiera interfejs. `UITask` obsługuje dane i klawisze, `UiView` rysuje (przenośny), `M9Radio` odpowiada za widmo i procesor, `M9PktLog` za log pakietów, a `M9Download` za mapę i aktualizacje.
  W `m9_sf6/companion` jest MyMesh z funkcjami dla ekranu.
- **Symulator:** `tools/sym` uruchamia ten sam kod ekranów na komputerze z atrapami sieci i sprzętu, robi zrzuty ekranu i test losowych klawiszy.
- **Makiety:** `tools/makieta` rysuje projekty nowych ekranów kodem urządzenia.
- **Klawiatura:** kontroler pod adresem 0x6C (płytka v1.0) albo 0x6D (v1.1) na SDA 20 / SCL 21. Rejestr 0x01 trzyma oczekujący klawisz, a po odczycie zwraca 0x88.
  Numer rejestru trzeba wysłać osobną transmisją (STOP) przed odczytem. Protokół opisano w projekcie Launcher (`boards/elecrow-thinknode-m9/connections.md`).

## Licencje

- Kod w tym repozytorium: MIT (jak MeshCore). Sterownik klawiatury jest napisany od zera.
- Czcionka Inter: SIL Open Font License 1.1.
- Generator kodów QR: Project Nayuki, licencja MIT.
- Mapa: dane © autorzy OpenStreetMap (ODbL).

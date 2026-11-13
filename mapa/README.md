# Mapa na kartę SD

Urządzenie rysuje mapę z plików `.mcm` w folderze **`/mapa`** na karcie SD.

## Wgrywanie na kartę

Karta w M9 jest wbudowana, dlatego pliki wgrywa samo urządzenie. Na M9: klawisz **Mapa**, potem **Menu** (mała wiadomość pod Home).

**Z internetu, bez komputera i bez kopiowania przez telefon**

1. „Sieć WiFi” i „Hasło WiFi” — wpisz sieć domową albo hotspot z telefonu.
2. „Pobierz z internetu” — urządzenie łączy się z WiFi, czyta `lista.txt` spod „Adresu mapy” i pobiera brakujące pliki na kartę.
   Przerwane pobieranie wznawia się od miejsca przerwania po ponownym wybraniu tej pozycji.

Pod adresem mapy muszą leżeć pliki `.mcm` i plik `lista.txt` (w każdym wierszu: nazwa pliku, spacja, rozmiar w bajtach).

**Z telefonu**

1. „Wgraj z telefonu” — urządzenie tworzy sieć WiFi **M9-MAPA** (hasło **mapa12345**).
2. W telefonie połącz się z tą siecią i otwórz w przeglądarce **http://192.168.4.1**.
3. „Wybierz pliki” i wskaż pliki `.mcm` zapisane w telefonie. Nie zamykaj strony do końca wgrywania.

## Mapa testowa (bez danych OSM)

W wydaniu „Najnowszy build” jest `mapa-testowa.zip`: siatka z numerami kafelków i kilkanaście miast,
Polska w zoomach 6–9 i okolice Bielska, Katowic i Krakowa w zoomach 10–13. Służy do sprawdzenia ekranu, karty i GPS.
Rozpakuj ją i wgraj pliki `.mcm` jednym ze sposobów opisanych wyżej.

## Prawdziwa mapa — na własnym serwerze

Kafelków nie wolno masowo pobierać z serwerów OpenStreetMap. Robimy je sami z danych OSM
(licencja ODbL — „© autorzy OpenStreetMap”). Potrzebny jest komputer lub serwer z Dockerem i Pythonem 3,
ok. 8 GB RAM i ok. 30 GB wolnego dysku. Ta sama droga jest sprawdzana automatycznie na małym obszarze w GitHub Actions (`.github/workflows/mapa-test.yml`).

Pobierz ten folder (`mapa`) na serwer i wejdź do niego w terminalu.

**1. Dane wektorowe Polski** (pobiera wyciąg OSM i buduje `data/output.mbtiles`; kilkanaście–kilkadziesiąt minut):

```
docker run -e JAVA_TOOL_OPTIONS="-Xmx6g" -v "$(pwd)/data":/data ghcr.io/onthegomap/planetiler:latest --download --area=poland
```

**2. Styl i konfiguracja obok danych:**

```
mkdir -p data/styl
cp styl/ciemna.json data/styl/
cp config.json data/
```

**3. Serwer kafelków** (zostaw uruchomiony; podgląd mapy w przeglądarce: `http://ADRES-SERWERA:8080`):

```
docker run --rm -it -v "$(pwd)/data":/data -p 8080:8080 maptiler/tileserver-gl -p 8080 --config /data/config.json
```

Sprawdź w przeglądarce, czy styl „ciemna” pokazuje mapę z nazwami miast. Jeśli nazw nie ma, serwer nie znalazł
czcionki „Noto Sans Regular” — prześlij, co wypisał przy starcie.

**4. Pakowanie** (w drugim terminalu; kilka godzin, można przerwać i uruchomić ponownie — skończone zoomy są pomijane):

```
pip install pillow numpy
python3 pakuj_mape.py mapa --zrodlo "http://localhost:8080/styles/ciemna/256/{z}/{x}/{y}.png" --wyjscie karta/mapa
```

Najpierw warto zrobić próbę na małym obszarze (kilka minut):

```
python3 pakuj_mape.py mapa --zrodlo "http://localhost:8080/styles/ciemna/256/{z}/{x}/{y}.png" --obszar 18.9,49.6,19.3,49.9 --zoom 10-15 --wyjscie proba/mapa
python3 pakuj_mape.py podglad proba/mapa/z13_x4526.mcm 4529 2784 kafelek.png
```

**5. Karta:** wgraj pliki z `karta/mapa` przez WiFi (opis na górze). Pliki mają do ok. 1 GB każdy.

## Inne źródła kafelków

`--zrodlo` przyjmuje też folder (`kafelki/{z}/{x}/{y}.png`) i plik rastrowy `.mbtiles`.
Własny obszar i zoomy: `--obszar zachód,południe,wschód,północ --zoom 10-15`.

## Format

Opis plików `.mcm`: [FORMAT.md](FORMAT.md).

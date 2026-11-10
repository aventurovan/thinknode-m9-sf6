# Format mapy MCM (MC Mapa), wersja 1

Mapa na karcie SD leży w folderze `/mapa` jako kilka plików `.mcm`. Jeden plik to prostokąt kafelków
256×256 px jednego zoomu (kafelki w układzie XYZ / Web Mercator, jak w OpenStreetMap).
Dzięki temu na kartę kopiuje się kilkanaście dużych plików zamiast setek tysięcy małych.

Wszystkie liczby: młodszy bajt pierwszy (little-endian).

## Nagłówek (32 bajty)

| Bajt | Rozmiar | Znaczenie |
|---|---|---|
| 0 | 4 | znaki `MCM1` |
| 4 | 1 | zoom |
| 5 | 1 | rozmiar kafelka jako potęga dwójki (8 = 256 px) |
| 6 | 2 | rozmiar nagłówka (32) |
| 8 | 4 | `x0` — pierwsza kolumna kafelków |
| 12 | 4 | `y0` — pierwszy wiersz kafelków |
| 16 | 4 | `nx` — liczba kolumn |
| 20 | 4 | `ny` — liczba wierszy |
| 24 | 4 | liczba kafelków w pliku |
| 28 | 4 | zapas (0) |

## Indeks

Zaraz po nagłówku, po 8 bajtów na kafelek, **kolumnami**: wpis kafelka (x, y) ma numer `(x − x0) · ny + (y − y0)`.

| Bajty | Znaczenie |
|---|---|
| 0–4 | położenie danych kafelka w pliku (40 bitów) |
| 5–7 | długość danych (24 bity); 0 = kafelka nie ma |

Plik nie przekracza 4 GB (ograniczenie FAT32) — narzędzie dzieli zoom na kilka plików po ok. 3 GB.
Urządzenie czyta wszystkie pliki z `/mapa` i samo wybiera ten, w którym leży potrzebny kafelek.

## Kafelek

1. 1 bajt: liczba kolorów palety `n` (1–16),
2. `n` × 2 bajty: kolory RGB565,
3. serie pikseli, wierszami od lewego górnego rogu, razem dokładnie 65 536 pikseli. Każda seria zaczyna się bajtem:
   starsze 4 bity = numer koloru, młodsze 4 bity = `k`:
   - `k` = 0–13: seria ma `k + 1` pikseli,
   - `k` = 14: następny bajt `b`, seria ma `15 + b` pikseli,
   - `k` = 15: następne 2 bajty `w`, seria ma `271 + w` pikseli.

   Seria może przechodzić przez koniec wiersza.

Dekoder: `m9_sf6/ui-test/McmMap.cpp` (ten sam kod jest sprawdzany na komputerze testem `mapa/test/test_dekoder.cpp`).

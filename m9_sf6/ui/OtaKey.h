#pragma once
// Klucz publiczny (Ed25519), którym urządzenie sprawdza podpis pliku wersja.txt przed aktualizacją przez WiFi.
// Klucz prywatny jest tylko w sekretach repozytorium (OTA_SIGNING_KEY) i podpisuje każdy build w GitHub Actions.
// Same zera = klucz jeszcze nie ustawiony: aktualizacja przez WiFi jest wtedy wyłączona.
#define M9_OTA_PUBKEY_HEX "0000000000000000000000000000000000000000000000000000000000000000"
